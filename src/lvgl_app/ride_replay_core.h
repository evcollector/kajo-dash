#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ride_replay {
constexpr unsigned kBuckets = 128;
// Ride-log geometry, shared with the writer in ride_logger.cpp so the reader
// and the writer cannot drift apart. Version 3 inserted phase current after
// pack current; version 2 files are rejected outright, which needs no
// migration because no build that wrote them was ever released.
constexpr uint16_t kLogVersion = 3;
constexpr uint16_t kHeaderBytes = 32;
constexpr uint16_t kRecordBytes = 44;
// One read from the card is 46 whole records, about 2 KB. Every read is a round
// trip through the logger's writer task, so loading a ride and seeking within it
// cost what they cost in reads: a 27 minute ride at 10 Hz is 352 of them, against
// 740 when a read carried 22 records. The logger's transfer buffer is this size.
constexpr size_t kReadRecords = 46;
constexpr size_t kReadBytes = kRecordBytes * kReadRecords;
// Chartable fields, in the order the replay UI offers them. Sample and bucket
// masks use these indices, not the log's TelemetryField bits.
enum Field : uint8_t {
  kSpeed, kPower, kVoltage, kCurrent, kMotorCurrent, kBattery, kMotorTemp, kControllerTemp, kFields
};
// Eight fields is the ceiling for the uint8_t sample and bucket masks below.
constexpr uint8_t kAllFields = (1U << kFields) - 1;
// km/h, W, V, A (pack), A (phase), percent, degC, degC
struct Sample { uint32_t time = 0; float value[kFields] = {}; uint8_t mask = 0; };
// Buckets keep each interval's extremes rather than its mean: a mean pulls
// away from both ends, so a ride that stood still or hit its peak inside one
// bucket drew a trace that never reached the scale it was labelled with. The
// whole-ride extremes that set each scale live in the Overview.
struct Bucket {
  float low[kFields] = {}, high[kFields] = {};
  uint32_t firstRecord = UINT32_MAX;
  uint8_t mask = 0, gaps = 0;
};
// Growth of a running counter between two readings. The logger's trip counters
// are not reset per ride, so a ride's share is the difference; a drop means
// the counter was reset in between, and then only the value since counts.
inline uint32_t counterGrowth(uint32_t first, uint32_t last) { return last >= first ? last - first : last; }
// Whole-ride totals. Distance and energy sum the growth of the logged trip
// counters (skipping any reset), falling back to integrating the recorded
// speed and power when a ride has no counters.
struct Summary {
  float distanceKm = 0, netWh = 0, regenWh = 0;
  float first[kFields] = {}, last[kFields] = {};
  bool hasDistance = false, hasEnergy = false;
};
// Up to four charts; each slot names one Field.
constexpr unsigned kMaxCharts = 4;
struct Overview {
  Bucket buckets[kBuckets];
  float low[kFields] = {}, high[kFields] = {};
  uint8_t fields = 0;  // fields recorded at least once
  Summary summary;
  uint32_t duration = 0, records = 0, badRecords = 0;
  uint16_t periodMs = 200;
};
// A stretch of the ride at the chart's own resolution, for zoomed views. The
// overview's 128 buckets are too coarse once the chart shows a fraction of the
// ride, so the reader builds a window of columns, one per plot pixel. Each holds,
// per field, the lowest reading in its slice of time as a position 0..254 within
// the scale the chart draws against (kNoLevel: nothing recorded there). Column c
// is centred on start + c * (end - start) / (kWindowColumns - 1), which is the
// mapping the cursor uses. Where the recording is sparser than the columns, a
// reading is held across the columns between it and the next, as far as the
// recording's own gap limit allows: the chart then steps like the data does.
constexpr unsigned kWindowColumns = 288;
constexpr uint8_t kNoLevel = 255;
struct Window {
  uint32_t start = 0, end = 0;
  uint8_t level[kWindowColumns][kFields];
};
enum class Error : uint8_t { None, Read, Format, Empty, Timeline };
using Reader = bool (*)(void *, uint32_t, uint8_t *, size_t, size_t &, uint32_t &);
uint16_t crc16(const uint8_t *data, size_t size);
// Pure, bounded-memory parser. Reader is called only by the replay worker on
// hardware, never by LVGL. Header/record sizes and CRCs are checked explicitly.
class Core {
 public:
  Core() = default;
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;
  bool load(Reader reader, void *context, uint32_t rideId);
  bool sampleAt(uint32_t time, Sample &sample);
  // Builds a Window over [start, end] a few records at a time, so the task that
  // owns this Core can answer seeks between slices. `low` and `high` are the
  // scales the chart draws each field against. stepWindow returns true once the
  // window is complete or cannot be built (windowFailed); the columns are
  // allocated on the first window, so a ride that is never zoomed never pays
  // for them.
  bool beginWindow(uint32_t start, uint32_t end, const float low[kFields], const float high[kFields]);
  bool stepWindow(unsigned maxRecords);
  bool windowFailed() const { return buildFailed_; }
  const Window *window() const { return window_; }
  Overview overview;
  Error error = Error::None;
 private:
  // Trip counters: metres, and consumed/regenerated energy in 0.1 Wh.
  struct Trip { uint32_t meters = 0, consumedDeci = 0, regenDeci = 0; bool valid = false, energy = false; };
  bool bytes(uint32_t offset, uint8_t *out, size_t count);
  bool record(uint32_t index, Sample &sample, Trip *trip = nullptr);
  bool wrappedFirst(uint32_t index, const Sample &sample) const;
  Sample seekCandidate_;
  uint32_t seekIndex_ = 0, seekTime_ = 0;
  bool seekValid_ = false, seekFound_ = false;
  Reader reader_ = nullptr;
  void *context_ = nullptr;
  uint8_t cache_[kReadBytes] = {};  // 2024 bytes: 46 complete records
  uint32_t cacheOffset_ = UINT32_MAX, fileBytes_ = 0;
  size_t cacheSize_ = 0;
  // The window being built, and where the scan of the records has got to.
  struct Build {
    bool active = false;
    uint32_t index = 0, gapLimit = 1000;
    int holdColumns = 0;
    float low[kFields] = {}, high[kFields] = {};
    int hold[kFields] = {};            // column of the latest record that held the field
    uint8_t holdLevel[kFields] = {};   // and its value
  };
  uint8_t quantize(unsigned field, float value) const;
  void fillHold(unsigned field, int upTo);
  void finishWindow();
  Window *window_ = nullptr;
  Build build_;
  bool buildFailed_ = false;
};
}
