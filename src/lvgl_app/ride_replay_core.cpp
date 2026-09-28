#include "ride_replay_core.h"
#include <algorithm>
#include <cstring>
namespace ride_replay {
static uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
static uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  while (n--) { c ^= uint16_t(*p++) << 8; for (int i=0;i<8;i++) c = c & 0x8000 ? (c<<1)^0x1021 : c<<1; }
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
bool Core::load(Reader reader, void *context, uint32_t rideId) {
  reader_=reader; context_=context; cacheOffset_=UINT32_MAX; fileBytes_=0; error=Error::None;
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
  // entire overview bucket on every frame. Large/backward seeks use the index.
  if(seekValid_ && time>=seekTime_ && time-seekTime_<=1000) {
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
}
