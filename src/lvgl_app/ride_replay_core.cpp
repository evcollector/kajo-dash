#include "ride_replay_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
namespace ride_replay {
static uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
static uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
// CRC-16/CCITT, a nibble at a time: a quarter of the work of the bitwise loop
// and 32 bytes of table. Every record is checked on every read, and building a
// zoomed window checks thousands.
uint16_t crc16(const uint8_t *p, size_t n) {
  static const uint16_t kNibble[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
                                       0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef};
  uint16_t c = 0xffff;
  while (n--) {
    c = uint16_t(c << 4) ^ kNibble[(c >> 12) ^ (*p >> 4)];
    c = uint16_t(c << 4) ^ kNibble[(c >> 12) ^ (*p++ & 0xf)];
  }
  return c;
}
bool Core::bytes(uint32_t offset, uint8_t *out, size_t count) {
  if (cacheOffset_ == UINT32_MAX || offset < cacheOffset_ || uint64_t(offset)+count > uint64_t(cacheOffset_)+cacheSize_) {
    cacheOffset_ = offset;
    cacheSize_ = 0;
    uint32_t size = 0;
    if (!reader_(context_, offset, cache_, sizeof(cache_), cacheSize_, size) ||
        (fileBytes_ && size != fileBytes_) || cacheSize_ < count) { error=Error::Read; return false; }
    fileBytes_ = size;
  }
  memcpy(out, cache_+offset-cacheOffset_, count);
  return true;
}
// Log availability bits (TelemetryField in controller_manager.h) per Field.
static constexpr uint32_t kLogBit[kFields]={1U<<0,1U<<1,1U<<2,1U<<3,1U<<24,1U<<10,1U<<4,1U<<5};
static constexpr uint32_t kLogTripBit=1U<<6, kLogEnergyBits=(1U<<11)|(1U<<12);
bool Core::record(uint32_t index, Sample &sample, Trip *trip) {
  uint8_t b[kRecordBytes];
  if (!bytes(kHeaderBytes+index*kRecordBytes,b,sizeof(b))) return false;
  if (u16(b+42)!=crc16(b,42)) return false;
  sample.time=u32(b);
  sample.value[kSpeed]=int16_t(u16(b+24))/10.0f;
  sample.value[kPower]=int32_t(u32(b+8));
  sample.value[kVoltage]=u16(b+26)/100.0f;
  sample.value[kCurrent]=int16_t(u16(b+28))/10.0f;
  sample.value[kMotorCurrent]=int16_t(u16(b+30))/10.0f;
  sample.value[kBattery]=b[36];
  sample.value[kMotorTemp]=int16_t(u16(b+32))/10.0f;
  sample.value[kControllerTemp]=int16_t(u16(b+34))/10.0f;
  const uint32_t available=u32(b+38);
  sample.mask=0;
  for (unsigned f=0;f<kFields;f++) if (available&kLogBit[f]) sample.mask|=1U<<f;
  if (trip) {
    trip->meters=u32(b+12); trip->valid=available&kLogTripBit;
    // Net energy falls during regeneration, so consumption is net + regen,
    // which only grows until the counters are reset.
    const int64_t consumed=int64_t(int32_t(u32(b+16)))+u32(b+20);
    trip->consumedDeci=uint32_t(std::max<int64_t>(0,std::min<int64_t>(consumed,UINT32_MAX)));
    trip->regenDeci=u32(b+20);
    trip->energy=(available&kLogEnergyBits)==kLogEnergyBits;
  }
  return true;
}
// Builds before the session clock fix stamped a ride's first record a few ms
// before the session started, which wrapped to just under 2^32. Only that
// record is forgiven, and it is skipped like a bad CRC; any other
// out-of-order timestamp still rejects the ride as a broken timeline.
bool Core::wrappedFirst(uint32_t index, const Sample &sample) const {
  // The known writer bug can only produce UINT32_MAX minus the few
  // milliseconds spent starting the session. Keep a generous one-second
  // window for slow SD cards, but do not reinterpret an ordinary backwards
  // first timestamp as this specific startup-wrap defect.
  static constexpr uint32_t kStartupWrapWindowMs = 1000;
  return index == 0 && sample.time >= UINT32_MAX - kStartupWrapWindowMs &&
         sample.time > overview.duration;
}
Core::~Core() { delete window_; }
bool Core::load(Reader reader, void *context, uint32_t rideId) {
  reader_=reader; context_=context; cacheOffset_=UINT32_MAX; fileBytes_=0; error=Error::None;
  build_.active=false; buildFailed_=false;
  // Do not create a full Overview temporary on the small ESP32 task stack.
  for(auto &bucket:overview.buckets) bucket=Bucket{};
  for(unsigned f=0;f<kFields;f++) overview.low[f]=overview.high[f]=0;
  overview.fields=0;
  overview.summary=Summary{};
  overview.duration=overview.records=overview.badRecords=0;
  overview.periodMs=200;
  seekValid_=seekFound_=false;
  uint8_t h[kHeaderBytes];
  if (!bytes(0,h,sizeof(h))) return false;
  if (memcmp(h,"KAJL",4) || u16(h+4)!=kLogVersion || u16(h+6)!=kHeaderBytes || u16(h+8)!=kRecordBytes ||
      !h[10] || h[10]>100 || u32(h+12)!=rideId || u16(h+30)!=crc16(h,30) ||
      fileBytes_<kHeaderBytes) { error=Error::Format; return false; }
  // Round down rather than demanding a whole number of records. Losing power
  // mid-append leaves a partial record on the card, and every complete record
  // before it is still intact and CRC-checked; the ride list, the ride-series
  // reader and the phone export all count the file this way too, so rejecting
  // it here would leave a ride that lists correctly but refuses to open.
  overview.records=(fileBytes_-kHeaderBytes)/kRecordBytes;
  overview.periodMs=1000/h[10];
  if (!overview.records) { error=Error::Empty; return false; }
  Sample sample;
  bool found=false;
  for (uint32_t i=overview.records;i>0;--i) {
    if (record(i-1,sample)) { overview.duration=sample.time; found=true; break; }
    if (error!=Error::None) return false;
  }
  if (!found) { error=Error::Empty; return false; }
  uint32_t previous=0;
  bool havePrevious=false, broken=false;
  const uint32_t gapLimit=std::max(1000U,unsigned(overview.periodMs)*3);
  Summary &summary=overview.summary;
  Sample before;
  Trip trip, tripBefore;
  float speedKm=0, tripKm=0, powerNetWh=0, powerRegenWh=0;
  uint64_t consumedDeci=0, regenDeci=0;
  bool sawTrip=false, sawSpeedStep=false, sawEnergyCounters=false, sawPowerStep=false;
  for (uint32_t i=0;i<overview.records;i++) {
    if (!record(i,sample,&trip) || wrappedFirst(i,sample)) {
      if (error!=Error::None) return false;
      overview.badRecords++; broken=true; continue;
    }
    if (havePrevious && sample.time<previous) { error=Error::Timeline; return false; }
    unsigned b=std::min<unsigned>(kBuckets-1,uint64_t(sample.time)*kBuckets/std::max(1U,overview.duration));
    Bucket &bucket=overview.buckets[b];
    if (bucket.firstRecord==UINT32_MAX) bucket.firstRecord=i;
    if (broken || (havePrevious && sample.time-previous>gapLimit)) bucket.gaps=kAllFields;
    for (unsigned f=0;f<kFields;f++) {
      uint8_t bit=1U<<f;
      if (!(sample.mask&bit)) { bucket.gaps|=bit; continue; }
      float v=sample.value[f];
      if (!(overview.fields&bit)) { overview.low[f]=overview.high[f]=v; summary.first[f]=v; }
      else { overview.low[f]=std::min(overview.low[f],v); overview.high[f]=std::max(overview.high[f],v); }
      summary.last[f]=v;
      overview.fields|=bit;
      if (!(bucket.mask&bit)) { bucket.low[f]=bucket.high[f]=v; }
      else { bucket.low[f]=std::min(bucket.low[f],v); bucket.high[f]=std::max(bucket.high[f],v); }
      bucket.mask|=bit;
    }
    if (havePrevious) {
      // Counters are cumulative, so their growth also covers any time gap. A
      // counter that drops was reset mid-ride and contributes nothing there.
      if (tripBefore.valid && trip.valid) {
        sawTrip=true;
        if (trip.meters>tripBefore.meters) tripKm+=(trip.meters-tripBefore.meters)/1000.0f;
      }
      if (tripBefore.energy && trip.energy) {
        sawEnergyCounters=true;
        if (trip.consumedDeci>=tripBefore.consumedDeci) consumedDeci+=trip.consumedDeci-tripBefore.consumedDeci;
        if (trip.regenDeci>=tripBefore.regenDeci) regenDeci+=trip.regenDeci-tripBefore.regenDeci;
      }
    }
    // The fallbacks integrate samples, so they never step across a time gap.
    if (havePrevious && sample.time-previous<=gapLimit) {
      const float hours=(sample.time-previous)/3600000.0f;
      if (before.mask&sample.mask&(1U<<kPower)) {
        const float watts=before.value[kPower];
        powerNetWh+=watts*hours;
        if (watts<0) powerRegenWh-=watts*hours;
        sawPowerStep=true;
      }
      if (before.mask&sample.mask&(1U<<kSpeed)) { speedKm+=std::max(0.0f,before.value[kSpeed])*hours; sawSpeedStep=true; }
    }
    broken=false; previous=sample.time; havePrevious=true; before=sample; tripBefore=trip;
  }
  summary.hasDistance=sawTrip || sawSpeedStep;
  summary.distanceKm=sawTrip?tripKm:speedKm;
  summary.hasEnergy=sawEnergyCounters || sawPowerStep;
  summary.regenWh=sawEnergyCounters?regenDeci/10.0f:powerRegenWh;
  summary.netWh=sawEnergyCounters?(int64_t(consumedDeci)-int64_t(regenDeci))/10.0f:powerNetWh;
  return true;
}
bool Core::sampleAt(uint32_t time, Sample &out) {
  out=Sample{}; out.time=time;
  if (error!=Error::None || !overview.records) return false;
  time=std::min(time,overview.duration);
  int bucket=std::min<unsigned>(kBuckets-1,uint64_t(time)*kBuckets/std::max(1U,overview.duration));
  // Start one bucket earlier so seeking into an empty interval can find the
  // preceding sample. Timestamps, not nominal sample frequency, drive seeks.
  if (bucket>0) bucket--;
  while (bucket>0 && overview.buckets[bucket].firstRecord==UINT32_MAX) bucket--;
  uint32_t first=overview.buckets[bucket].firstRecord;
  if (first==UINT32_MAX) first=0;
  Sample candidate;
  bool found=false;
  // Playback advances through the cached records instead of rereading an
  // entire overview bucket on every frame. Any forward seek that lands at or
  // past the previous scan resumes from where it stopped: at 100x a frame moves
  // about a hundred records, and none of them is read twice. Backward seeks, and
  // jumps that leave the previous scan behind, start from the bucket index.
  if(seekValid_ && time>=seekTime_ && seekIndex_>=first) {
    first=seekIndex_;candidate=seekCandidate_;found=seekFound_;
  }
  uint32_t i=first;
  for (;i<overview.records;i++) {
    Sample next;
    if (!record(i,next) || wrappedFirst(i,next)) {
      if (error!=Error::None) return false;
      candidate.mask=0; continue;
    }
    if (next.time>time) break;
    candidate=next; found=true;
  }
  seekValid_=true;seekTime_=time;seekIndex_=i;seekCandidate_=candidate;seekFound_=found;
  if (found) {
    out=candidate;
    if (time-candidate.time>std::max(1000U,unsigned(overview.periodMs)*3)) out.mask=0;
  }
  return true;
}
// ── Zoom windows ─────────────────────────────────────────────────────────────
static constexpr int kNever = -(1 << 28);  // no reading is held for this field
uint8_t Core::quantize(unsigned field, float value) const {
  const float range = build_.high[field] - build_.low[field];
  if (!(range > 0)) return 0;
  const float position = (value - build_.low[field]) / range;
  return uint8_t(lroundf(254.0f * std::min(1.0f, std::max(0.0f, position))));
}
// Columns after the held reading's own, up to `upTo`, take its value if they
// have none: the signal did not change until the next reading arrived. The run
// stops at the recording's gap limit, so a real hole in the ride stays a hole.
void Core::fillHold(unsigned field, int upTo) {
  const int held = build_.hold[field];
  if (held == kNever) return;
  const int last = std::min(upTo, held + build_.holdColumns);
  for (int c = std::max(0, held + 1); c <= last; c++) {
    uint8_t &cell = window_->level[c][field];
    if (cell == kNoLevel) cell = build_.holdLevel[field];
  }
}
void Core::finishWindow() {
  for (unsigned f = 0; f < kFields; f++) fillHold(f, int(kWindowColumns) - 1);
  build_.active = false;
}
bool Core::beginWindow(uint32_t start, uint32_t end, const float low[kFields], const float high[kFields]) {
  build_.active = false;
  buildFailed_ = false;
  if (error != Error::None || !overview.records || end <= start) return false;
  if (!window_) window_ = new (std::nothrow) Window;
  if (!window_) return false;
  window_->start = start;
  window_->end = end;
  memset(window_->level, kNoLevel, sizeof(window_->level));
  build_.gapLimit = std::max(1000U, unsigned(overview.periodMs) * 3);
  build_.holdColumns = int(uint64_t(build_.gapLimit) * (kWindowColumns - 1) / (end - start));
  for (unsigned f = 0; f < kFields; f++) {
    build_.low[f] = low[f];
    build_.high[f] = high[f];
    build_.hold[f] = kNever;
    build_.holdLevel[f] = kNoLevel;
  }
  // Begin a gap limit early: readings that old still hold into the first columns.
  const uint32_t from = start > build_.gapLimit ? start - build_.gapLimit : 0;
  unsigned bucket = std::min<unsigned>(kBuckets - 1, uint64_t(from) * kBuckets / std::max(1U, overview.duration));
  while (bucket > 0 && overview.buckets[bucket].firstRecord == UINT32_MAX) bucket--;
  build_.index = overview.buckets[bucket].firstRecord == UINT32_MAX ? 0 : overview.buckets[bucket].firstRecord;
  build_.active = true;
  return true;
}
bool Core::stepWindow(unsigned maxRecords) {
  if (!build_.active) return true;
  const int64_t span = int64_t(window_->end) - int64_t(window_->start);
  for (unsigned n = 0; n < maxRecords; n++) {
    if (build_.index >= overview.records) { finishWindow(); return true; }
    const uint32_t index = build_.index++;
    Sample s;
    if (!record(index, s) || wrappedFirst(index, s)) {
      if (error != Error::None) { buildFailed_ = true; build_.active = false; return true; }
      // A reading that cannot be trusted ends any run of held values.
      for (unsigned f = 0; f < kFields; f++) build_.hold[f] = kNever;
      continue;
    }
    const int64_t scaled = (int64_t(s.time) - int64_t(window_->start)) * (int64_t(kWindowColumns) - 1) + span / 2;
    const int64_t column = scaled >= 0 ? scaled / span : -((-scaled + span - 1) / span);  // floor
    if (column > int64_t(kWindowColumns) - 1) { finishWindow(); return true; }  // time order: nothing later is inside
    for (unsigned f = 0; f < kFields; f++) {
      if (!(s.mask & (1U << f))) { build_.hold[f] = kNever; continue; }
      const uint8_t level = quantize(f, s.value[f]);
      if (column >= 0) {
        fillHold(f, int(column) - 1);
        uint8_t &cell = window_->level[column][f];
        if (cell == kNoLevel || level < cell) cell = level;
      }
      build_.hold[f] = int(std::max<int64_t>(column, -(1 << 20)));
      build_.holdLevel[f] = level;
    }
  }
  return false;
}
}
