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
enum class Error : uint8_t { None, Read, Format, Empty, Timeline };
using Reader = bool (*)(void *, uint32_t, uint8_t *, size_t, size_t &, uint32_t &);
uint16_t crc16(const uint8_t *data, size_t size);
// Pure, bounded-memory parser. Reader is called only by the replay worker on
// hardware, never by LVGL. Header/record sizes and CRCs are checked explicitly.
class Core {
 public:
  bool load(Reader reader, void *context, uint32_t rideId);
  bool sampleAt(uint32_t time, Sample &sample);
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
  uint8_t cache_[kRecordBytes * 23] = {};  // 1012 bytes: 23 complete records
  uint32_t cacheOffset_ = UINT32_MAX, fileBytes_ = 0;
  size_t cacheSize_ = 0;
};
}
