#include "app_state.h"

#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"
#include "ride_log_policy.h"
#include "ride_replay_core.h"

namespace {

constexpr uint16_t kLogVersion = ride_replay::kLogVersion;
constexpr size_t kWriteBufferBytes = 1024;
constexpr uint64_t kMinimumFreeBytes = 512ULL * 1024ULL;
constexpr uint32_t kCardProbeIntervalMs = 1500;
constexpr uint32_t kMountRetryMs = 2500;
constexpr uint32_t kMaximumBufferedMs = 1500;
constexpr RideLoggingMode kDefaultLoggingMode = RIDE_LOG_OFF;
constexpr uint8_t kDefaultSampleHz = 5;
constexpr char kPendingRideWipeKey[] = "wipeRides";
// Header flag bits. The byte was reserved and written as zero, so an older file
// simply reads as a normal ride and no format version bump is needed.
constexpr uint8_t kRideFlagDemo = 1 << 0;

enum WriterMessageKind : uint8_t {
  WRITER_START,
  WRITER_RECORD,
  WRITER_STOP,
  WRITER_WAKE,
  WRITER_SCAN_CATALOG,
  WRITER_DELETE_ALL,
  WRITER_EXPORT,
  WRITER_DELETE_RIDE,  // payload.header.rideId names the ride
  WRITER_WIPE_CARD,    // every file on the card, not only ride logs
};

enum ExportRequestKind : uint8_t { EXPORT_NONE, EXPORT_SERIES, EXPORT_FILE };

struct ExportRequest {
  ExportRequestKind kind;
  uint32_t rideId;
  RideLogSeriesField field;
  uint32_t startSeconds;
  uint32_t endSeconds;
  uint8_t maximumPoints;
  RideLogSeriesPoint points[36];
  uint8_t pointCount;
  uint32_t offset;
  uint8_t buffer[1008];
  size_t capacity;
  size_t bytesRead;
  uint32_t fileBytes;
  bool success;
};

#pragma pack(push, 1)
struct RideFileHeader {
  char magic[4];             // "KAJL"
  uint16_t version;
  uint16_t headerBytes;
  uint16_t recordBytes;
  uint8_t sampleHz;
  uint8_t flags;  // see kRideFlag* below; zero in files written before it existed
  uint32_t rideId;
  uint32_t createdBootMs;
  uint8_t reserved[10];
  uint16_t crc;
};

struct RideRecordV3 {
  uint32_t elapsedMs;
  uint32_t rideUptimeSeconds;
  int32_t watts;
  uint32_t tripMeters;
  int32_t netWhDeci;
  uint32_t regenWhDeci;
  int16_t speedDeciKmh;
  uint16_t voltageCenti;
  int16_t currentDeci;       // pack side
  int16_t motorCurrentDeci;  // phase side, absent unless the backend reports it
  int16_t motorTempDeci;
  int16_t escTempDeci;
  uint8_t batteryPercent;
  uint8_t faultCode;
  uint32_t availableFields;
  uint16_t crc;
};

struct WriterMessage {
  uint8_t kind;
  union {
    RideFileHeader header;
    RideRecordV3 record;
  } payload;
};
#pragma pack(pop)

static_assert(sizeof(RideFileHeader) == ride_replay::kHeaderBytes, "Ride header format changed");
static_assert(sizeof(RideRecordV3) == ride_replay::kRecordBytes, "Ride record format changed");

SPIClass sdSpi(VSPI);
QueueHandle_t writerQueue = nullptr;
portMUX_TYPE loggerMux = portMUX_INITIALIZER_UNLOCKED;
RideLoggingStatus sharedStatus = {kDefaultLoggingMode, false, false, false, 5, 0, 0, 0, 0, 1, 0, 0};
RideLogCatalogStatus sharedCatalogStatus = {false, 0, 1};
RideLogSummary *sharedCatalog = nullptr;
Preferences loggerPrefs;
SemaphoreHandle_t exportMutex = nullptr;
SemaphoreHandle_t exportDone = nullptr;
ExportRequest exportRequest = {};

bool sessionOpen = false;
bool storageNeededByUi = false;
bool loggerSuspended = false;
bool deleteAllPending = false;
uint32_t sessionStartedMs = 0;
uint32_t lastSampleMs = 0;
uint32_t movingSinceMs = 0;
uint32_t stoppedSinceMs = 0;
alignas(4) uint8_t cardProbeBuffer[512];

void bumpStatusLocked() {
  sharedStatus.revision++;
  if (sharedStatus.revision == 0) sharedStatus.revision = 1;
}

bool storageIsNeeded() {
  bool needed;
  portENTER_CRITICAL(&loggerMux);
  needed = storageNeededByUi || deleteAllPending || (!loggerSuspended && sharedStatus.mode != RIDE_LOG_OFF) ||
           sharedStatus.recording;
  portEXIT_CRITICAL(&loggerMux);
  return needed;
}

void setCardChecking(bool checking) {
  portENTER_CRITICAL(&loggerMux);
  if (sharedStatus.cardChecking != checking) {
    sharedStatus.cardChecking = checking;
    bumpStatusLocked();
  }
  portEXIT_CRITICAL(&loggerMux);
}

uint16_t crc16(const uint8_t *data, size_t size) {
  uint16_t crc = 0xFFFF;
  while (size--) {
    crc ^= (uint16_t)*data++ << 8;
    for (uint8_t bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void updateStorageStatus() {
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  portENTER_CRITICAL(&loggerMux);
  if (sharedStatus.totalBytes != total || sharedStatus.usedBytes != used) {
    sharedStatus.totalBytes = total;
    sharedStatus.usedBytes = used;
    bumpStatusLocked();
  }
  portEXIT_CRITICAL(&loggerMux);
}

void scanRideCount() {
  uint32_t count = 0;
  File root = SD.open("/rides");
  if (root && root.isDirectory()) {
    File entry;
    while ((entry = root.openNextFile())) {
      if (!entry.isDirectory()) count++;
      entry.close();
    }
  }
  root.close();
  portENTER_CRITICAL(&loggerMux);
  if (sharedStatus.rideCount != count) {
    sharedStatus.rideCount = count;
    bumpStatusLocked();
  }
  portEXIT_CRITICAL(&loggerMux);
}

void insertCatalogNewestFirst(RideLogSummary *catalog, const RideLogSummary &summary, uint8_t &count) {
  uint8_t insertAt = 0;
  while (insertAt < count && catalog[insertAt].rideId > summary.rideId) insertAt++;
  if (insertAt >= RIDE_LOG_CATALOG_MAX) return;
  const uint8_t last = count < RIDE_LOG_CATALOG_MAX ? count : RIDE_LOG_CATALOG_MAX - 1;
  for (uint8_t i = last; i > insertAt; i--) catalog[i] = catalog[i - 1];
  catalog[insertAt] = summary;
  if (count < RIDE_LOG_CATALOG_MAX) count++;
}

void scanRideCatalog() {
  // Keep this workspace off the logger task's 4 KB stack. GCC inlines this
  // scanner into writerTask at -Os; a fixed local array therefore bloated the
  // task's permanent frame and left too little headroom for SD.begin/open.
  RideLogSummary *scanCatalog =
      static_cast<RideLogSummary *>(calloc(RIDE_LOG_CATALOG_MAX, sizeof(RideLogSummary)));
  if (!scanCatalog) {
    portENTER_CRITICAL(&loggerMux);
    sharedCatalogStatus.loading = false;
    sharedCatalogStatus.count = 0;
    sharedCatalogStatus.revision++;
    if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
    portEXIT_CRITICAL(&loggerMux);
    return;
  }
  uint8_t count = 0;
  File root = SD.open("/rides");
  if (root && root.isDirectory()) {
    File entry;
    while ((entry = root.openNextFile())) {
      if (!entry.isDirectory()) {
        const char *name = entry.name();
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        unsigned long fileRideId = 0;
        if (sscanf(base, "R%lu.cyd", &fileRideId) == 1) {
          RideLogSummary summary = {};
          summary.rideId = (uint32_t)fileRideId;
          summary.fileBytes = (uint32_t)entry.size();
          RideFileHeader header = {};
          entry.seek(0);
          const bool headerOk = entry.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                                memcmp(header.magic, "KAJL", 4) == 0 && header.version == kLogVersion &&
                                header.headerBytes == sizeof(RideFileHeader) &&
                                header.recordBytes == sizeof(RideRecordV3) &&
                                header.crc == crc16((const uint8_t *)&header, sizeof(header) - sizeof(header.crc));
          if (headerOk) {
            summary.rideId = header.rideId;
            summary.sampleHz = header.sampleHz;
            summary.demo = (header.flags & kRideFlagDemo) != 0;
            const size_t dataBytes = summary.fileBytes > header.headerBytes ? summary.fileBytes - header.headerBytes : 0;
            const size_t recordCount = dataBytes / header.recordBytes;
            if (recordCount > 0) {
              RideRecordV3 last = {};
              const size_t offset = header.headerBytes + (recordCount - 1) * header.recordBytes;
              entry.seek(offset);
              summary.valid = entry.read((uint8_t *)&last, sizeof(last)) == sizeof(last) &&
                              last.crc == crc16((const uint8_t *)&last, sizeof(last) - sizeof(last.crc));
              // The trip counters run across rides, so a ride's totals are
              // their growth from its first good record to its last; replay's
              // summary applies the same rule record by record.
              RideRecordV3 first = {};
              bool firstOk = false;
              for (size_t index = 0; summary.valid && !firstOk && index < min<size_t>(recordCount, 8); index++) {
                entry.seek(header.headerBytes + index * header.recordBytes);
                firstOk = entry.read((uint8_t *)&first, sizeof(first)) == sizeof(first) &&
                          first.crc == crc16((const uint8_t *)&first, sizeof(first) - sizeof(first.crc));
              }
              if (summary.valid) {
                if (!firstOk) first = RideRecordV3{};
                summary.durationSeconds = last.elapsedMs / 1000U;
                summary.distanceMeters = ride_replay::counterGrowth(first.tripMeters, last.tripMeters);
                // Net energy falls while regenerating; consumption (net + regen) only grows.
                auto consumed = [](const RideRecordV3 &r) {
                  return (uint32_t)constrain((int64_t)r.netWhDeci + r.regenWhDeci, (int64_t)0, (int64_t)UINT32_MAX);
                };
                const uint32_t regen = ride_replay::counterGrowth(first.regenWhDeci, last.regenWhDeci);
                summary.regenWhDeci = regen;
                summary.netWhDeci = (int32_t)((int64_t)ride_replay::counterGrowth(consumed(first), consumed(last)) - regen);
              }
            }
          }
          insertCatalogNewestFirst(scanCatalog, summary, count);
        }
      }
      entry.close();
    }
  }
  root.close();

  portENTER_CRITICAL(&loggerMux);
  if (sharedCatalog) memcpy(sharedCatalog, scanCatalog, count * sizeof(RideLogSummary));
  sharedCatalogStatus.count = sharedCatalog ? count : 0;
  sharedCatalogStatus.loading = false;
  sharedCatalogStatus.revision++;
  if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
  portEXIT_CRITICAL(&loggerMux);
  free(scanCatalog);
}

void removeOldestRideIfNeeded() {
  while (SD.totalBytes() > SD.usedBytes() && SD.totalBytes() - SD.usedBytes() < kMinimumFreeBytes) {
    File root = SD.open("/rides");
    uint32_t oldestId = UINT32_MAX;
    char oldestPath[32] = "";
    if (root && root.isDirectory()) {
      File entry;
      while ((entry = root.openNextFile())) {
        if (!entry.isDirectory()) {
          const char *name = entry.name();
          const char *base = strrchr(name, '/');
          base = base ? base + 1 : name;
          unsigned long id = 0;
          if (sscanf(base, "R%lu.cyd", &id) == 1 && id < oldestId) {
            oldestId = (uint32_t)id;
            snprintf(oldestPath, sizeof(oldestPath), "/rides/%s", base);
          }
        }
        entry.close();
      }
    }
    root.close();
    if (oldestPath[0] == '\0' || !SD.remove(oldestPath)) break;
  }
  scanRideCount();
  updateStorageStatus();
}

// Wiping a card can touch hundreds of files. Two constraints shape this:
//
//  - The writer task sits at the idle priority on core 0 and its only other
//    yield is the queue wait, so a walk that never blocks starves IDLE0 and the
//    task watchdog reboots the display mid-wipe.
//  - Its stack is 4 KB and already carries the 1 KB write buffer. Local arrays
//    here are inlined into writerTask's permanent frame at -Os and leave too
//    little headroom for SD.open/openNextFile, exactly as the comment in
//    scanRideCatalog() describes. The workspace therefore lives on the heap.
//
// Nothing is removed while its directory handle is open either, which deleting
// during iteration does not guarantee across FS backends.
constexpr uint8_t kWipeBatch = 8;
constexpr size_t kWipePathBytes = 64;
constexpr uint8_t kWipeMaxDirs = 6;

struct WipeWorkspace {
  char dirs[kWipeMaxDirs][kWipePathBytes];
  char names[kWipeBatch][kWipePathBytes];
  bool isDirectory[kWipeBatch];
  uint8_t dirCount;
};

// What the UI shows while a card wipe runs. Only a whole-card wipe reports:
// emptyTree() also serves the ride-only delete, which must not move it.
RideLogWipeStatus sharedWipe = {RideLogWipeStatus::Idle, 0, 0};

void setWipeState(RideLogWipeStatus::State state, uint32_t total = 0) {
  portENTER_CRITICAL(&loggerMux);
  sharedWipe.state = state;
  if (state == RideLogWipeStatus::Counting) sharedWipe.removed = 0;
  if (state == RideLogWipeStatus::Removing) sharedWipe.total = total;
  portEXIT_CRITICAL(&loggerMux);
}

void noteWipeRemoved() {
  portENTER_CRITICAL(&loggerMux);
  if (sharedWipe.state == RideLogWipeStatus::Removing) sharedWipe.removed++;
  portEXIT_CRITICAL(&loggerMux);
}

// Counts the files and subdirectories emptyTree(root) will remove, walking the
// same way and with the same directory limit so the two agree on what is in
// reach. The root itself is kept, so it is not counted.
uint32_t countTree(const char *root) {
  WipeWorkspace *work = static_cast<WipeWorkspace *>(calloc(1, sizeof(WipeWorkspace)));
  if (!work) return 0;
  snprintf(work->dirs[0], kWipePathBytes, "%s", root);
  work->dirCount = 1;
  uint32_t count = 0;
  for (uint8_t index = 0; index < work->dirCount; index++) {
    File dir = SD.open(work->dirs[index]);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    const bool atRoot = strcmp(work->dirs[index], "/") == 0;
    uint8_t sinceYield = 0;
    File entry;
    while ((entry = dir.openNextFile())) {
      if (!entry.isDirectory()) {
        count++;
      } else if (work->dirCount < kWipeMaxDirs) {
        const char *name = entry.name();
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        snprintf(work->dirs[work->dirCount++], kWipePathBytes, "%s%s%s", work->dirs[index], atRoot ? "" : "/",
                 base);
        count++;
      }
      entry.close();
      if (++sinceYield >= kWipeBatch) {
        sinceYield = 0;
        vTaskDelay(1);  // same watchdog reason as wipeDirectory()
      }
    }
    dir.close();
  }
  free(work);
  return count;
}

// Removes the files directly inside dirs[index], appending any subdirectory it
// meets for the caller to visit. Not recursive: the directory list is the work
// queue, so nothing nests on the stack.
bool wipeDirectory(WipeWorkspace &work, uint8_t index) {
  bool allRemoved = true;
  for (;;) {
    uint8_t found = 0;
    File dir = SD.open(work.dirs[index]);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return false;
    }
    const bool atRoot = strcmp(work.dirs[index], "/") == 0;
    File entry;
    while (found < kWipeBatch && (entry = dir.openNextFile())) {
      const char *name = entry.name();
      const char *base = strrchr(name, '/');
      base = base ? base + 1 : name;
      snprintf(work.names[found], kWipePathBytes, "%s%s%s", work.dirs[index], atRoot ? "" : "/", base);
      work.isDirectory[found] = entry.isDirectory();
      found++;
      entry.close();
    }
    dir.close();
    if (!found) break;
    uint8_t removed = 0;
    for (uint8_t i = 0; i < found; i++) {
      if (work.isDirectory[i]) {
        bool known = false;
        for (uint8_t d = 0; d < work.dirCount; d++)
          if (strcmp(work.dirs[d], work.names[i]) == 0) known = true;
        if (known) continue;
        if (work.dirCount < kWipeMaxDirs)
          snprintf(work.dirs[work.dirCount++], kWipePathBytes, "%s", work.names[i]);
        else
          allRemoved = false;  // deeper or wider than this walk covers
      } else if (SD.remove(work.names[i])) {
        removed++;
        noteWipeRemoved();
      } else {
        allRemoved = false;
      }
    }
    vTaskDelay(1);  // let IDLE0 run so the task watchdog stays fed
    // A pass that removed nothing is looking at the same subdirectories again.
    if (removed == 0) break;
  }
  return allRemoved;
}

void forgetAllRidesLocked() {
  if (sharedCatalog) memset(sharedCatalog, 0, RIDE_LOG_CATALOG_MAX * sizeof(RideLogSummary));
  sharedCatalogStatus.loading = false;
  sharedCatalogStatus.count = 0;
  sharedCatalogStatus.revision++;
  if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
}

// Empties `root` and removes every directory below it, keeping `root` itself.
bool emptyTree(const char *root) {
  WipeWorkspace *work = static_cast<WipeWorkspace *>(calloc(1, sizeof(WipeWorkspace)));
  if (!work) return false;
  snprintf(work->dirs[0], kWipePathBytes, "%s", root);
  work->dirCount = 1;
  bool allRemoved = true;
  // Breadth-first: wipeDirectory appends what it finds, so the index walk
  // reaches subdirectories without recursing.
  for (uint8_t index = 0; index < work->dirCount; index++)
    if (!wipeDirectory(*work, index)) allRemoved = false;
  // Children were appended after their parents, so reverse order empties a
  // directory before removing it. dirs[0] is the root and is never removed.
  while (work->dirCount > 1) {
    work->dirCount--;
    if (SD.rmdir(work->dirs[work->dirCount]))
      noteWipeRemoved();
    else
      allRemoved = false;
    vTaskDelay(1);
  }
  free(work);
  return allRemoved;
}

bool wipeCard() {
  // Count first so the UI can show real progress; on a card with a few
  // hundred files this pass takes a fraction of the removal itself.
  setWipeState(RideLogWipeStatus::Counting);
  setWipeState(RideLogWipeStatus::Removing, countTree("/"));
  const bool allRemoved = emptyTree("/");
  SD.mkdir("/rides");  // the writer expects it to exist for the next session
  scanRideCount();
  updateStorageStatus();
  portENTER_CRITICAL(&loggerMux);
  forgetAllRidesLocked();
  portEXIT_CRITICAL(&loggerMux);
  Preferences resetPreferences;
  resetPreferences.begin("logger", false);
  resetPreferences.putUInt("nextRide", 1);
  resetPreferences.end();
  // Reported last, so "done" on screen means the card is ready for new rides.
  setWipeState(allRemoved ? RideLogWipeStatus::Done : RideLogWipeStatus::Failed);
  return allRemoved;
}

bool deleteAllRideFiles() {
  // No directory is nothing to delete, which is a success: anything else would
  // leave the pending-wipe flag set and retry for ever.
  bool allRemoved = !SD.exists("/rides") || emptyTree("/rides");
  scanRideCount();
  updateStorageStatus();
  portENTER_CRITICAL(&loggerMux);
  if (allRemoved) {
    if (sharedCatalog) memset(sharedCatalog, 0, RIDE_LOG_CATALOG_MAX * sizeof(RideLogSummary));
    sharedCatalogStatus.loading = false;
    sharedCatalogStatus.count = 0;
    sharedCatalogStatus.revision++;
    if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
    deleteAllPending = false;
  }
  portEXIT_CRITICAL(&loggerMux);
  if (allRemoved) {
    Preferences resetPreferences;
    resetPreferences.begin("logger", false);
    resetPreferences.putUInt("nextRide", 1);
    resetPreferences.putBool(kPendingRideWipeKey, false);
    resetPreferences.end();
  }
  return allRemoved;
}

bool ridePath(uint32_t rideId, char *path, size_t size);

// Removes one finished ride. The ride being recorded is never deleted. The
// catalog entry goes at once, so an open ride list updates without a rescan.
void deleteRideFile(uint32_t rideId, bool sessionActive, const char *sessionPath) {
  char path[32];
  if (!ridePath(rideId, path, sizeof(path)) || (sessionActive && strcmp(path, sessionPath) == 0)) return;
  if (SD.exists(path) && !SD.remove(path)) return;
  portENTER_CRITICAL(&loggerMux);
  if (sharedCatalog) {
    uint8_t kept = 0;
    for (uint8_t index = 0; index < sharedCatalogStatus.count; index++)
      if (sharedCatalog[index].rideId != rideId) sharedCatalog[kept++] = sharedCatalog[index];
    if (kept != sharedCatalogStatus.count) {
      sharedCatalogStatus.count = kept;
      sharedCatalogStatus.revision++;
      if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
    }
  }
  portEXIT_CRITICAL(&loggerMux);
  scanRideCount();
  updateStorageStatus();
}

bool flushBuffered(const char *path, uint8_t *buffer, size_t &used) {
  if (used == 0) return true;
  File file = SD.open(path, FILE_APPEND);
  if (file) file.clearWriteError();
  bool ok = file && file.write(buffer, used) == used;
  if (ok) {
    file.flush();
    ok = file.getWriteError() == 0;
  }
  if (file) file.close();
  used = 0;
  return ok;
}

bool probeMountedCard() {
  return SD.readRAW(cardProbeBuffer, 0);
}

bool ridePath(uint32_t rideId, char *path, size_t size) {
  if (rideId == 0 || !path || size < 24) return false;
  snprintf(path, size, "/rides/R%08lu.cyd", (unsigned long)rideId);
  return true;
}

void processExportRequest(bool sessionActive, const char *sessionPath) {
  ExportRequest &request = exportRequest;
  request.success = false;
  request.pointCount = 0;
  request.bytesRead = 0;
  request.fileBytes = 0;
  char path[32];
  if (!ridePath(request.rideId, path, sizeof(path)) ||
      (sessionActive && strcmp(path, sessionPath) == 0)) {
    xSemaphoreGive(exportDone);
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    xSemaphoreGive(exportDone);
    return;
  }
  request.fileBytes = static_cast<uint32_t>(file.size());
  if (request.kind == EXPORT_FILE) {
    if (request.offset <= request.fileBytes && file.seek(request.offset)) {
      const size_t available = request.fileBytes - request.offset;
      request.bytesRead = file.read(request.buffer, min(request.capacity, available));
      request.success = request.bytesRead > 0 || request.offset == request.fileBytes;
    }
  } else if (request.kind == EXPORT_SERIES) {
    RideFileHeader header = {};
    const bool headerOk = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                          memcmp(header.magic, "KAJL", 4) == 0 && header.version == kLogVersion &&
                          header.headerBytes == sizeof(RideFileHeader) &&
                          header.recordBytes == sizeof(RideRecordV3) &&
                          header.crc == crc16((const uint8_t *)&header, sizeof(header) - sizeof(header.crc));
    if (headerOk && request.maximumPoints > 0 && request.endSeconds > request.startSeconds) {
      const uint32_t records = request.fileBytes > header.headerBytes
                                   ? (request.fileBytes - header.headerBytes) / header.recordBytes : 0;
      const uint32_t first = min(records, request.startSeconds * max<uint8_t>(1, header.sampleHz));
      const uint32_t last = min(records, request.endSeconds * max<uint8_t>(1, header.sampleHz) + 1);
      const uint32_t selected = last > first ? last - first : 0;
      const uint8_t buckets = min<uint32_t>(request.maximumPoints, selected);
      for (uint8_t bucket = 0; bucket < buckets; bucket++) {
        const uint32_t bucketFirst = first + (uint64_t)selected * bucket / buckets;
        const uint32_t bucketLast = first + (uint64_t)selected * (bucket + 1) / buckets;
        int64_t sum = 0;
        uint32_t count = 0;
        uint32_t elapsed = 0;
        for (uint32_t index = bucketFirst; index < bucketLast; index++) {
          RideRecordV3 record = {};
          if (!file.seek(header.headerBytes + index * header.recordBytes) ||
              file.read((uint8_t *)&record, sizeof(record)) != sizeof(record) ||
              record.crc != crc16((const uint8_t *)&record, sizeof(record) - sizeof(record.crc))) continue;
          sum += request.field == RIDE_SERIES_SPEED ? record.speedDeciKmh : record.watts;
          elapsed = record.elapsedMs / 1000U;
          count++;
        }
        if (count) {
          RideLogSeriesPoint &point = request.points[request.pointCount++];
          point.elapsedSeconds = static_cast<uint16_t>(min<uint32_t>(elapsed, UINT16_MAX));
          point.value = static_cast<int32_t>(sum / count);
        }
      }
      request.success = request.pointCount > 0;
    }
  }
  file.close();
  xSemaphoreGive(exportDone);
}

void publishCardState(bool ready, bool checking, bool ioFailure) {
  portENTER_CRITICAL(&loggerMux);
  const bool changed = sharedStatus.cardReady != ready || sharedStatus.cardChecking != checking;
  sharedStatus.cardReady = ready;
  sharedStatus.cardChecking = checking;
  if (!ready) {
    sharedStatus.recording = false;
    sharedStatus.usedBytes = 0;
    sharedStatus.totalBytes = 0;
    if (sharedCatalogStatus.loading || sharedCatalogStatus.count != 0) {
      sharedCatalogStatus.loading = false;
      sharedCatalogStatus.count = 0;
      sharedCatalogStatus.revision++;
      if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
    }
  }
  if (ioFailure) sharedStatus.ioErrors++;
  if (changed || ioFailure) bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
}

void abandonCard(size_t &buffered, bool ioFailure) {
  buffered = 0;
  SD.end();
  publishCardState(false, false, ioFailure);
  // Drain explicitly: resetting the queue silently loses an export request
  // and leaves its waiter (including a timed-out replay reader) stranded.
  WriterMessage discarded = {};
  while (writerQueue && xQueueReceive(writerQueue, &discarded, 0) == pdTRUE) {
    if (discarded.kind == WRITER_EXPORT) {
      exportRequest.success = false;
      xSemaphoreGive(exportDone);
    }
    // The clear-card dialog waits on this, so a dropped wipe must say so.
    if (discarded.kind == WRITER_WIPE_CARD) setWipeState(RideLogWipeStatus::Failed);
  }
}

void writerTask(void *) {
  sdSpi.begin(CYD_SD_SCLK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  bool mounted = false;
  bool wasNeeded = false;
  uint32_t nextMountAttemptMs = 0;
  uint32_t lastProbeMs = 0;
  uint32_t lastFlushMs = 0;
  bool sessionActive = false;
  char sessionPath[32] = "";
  uint8_t buffer[kWriteBufferBytes];
  size_t buffered = 0;
  WriterMessage message = {};
  bool deleteRequested = false;
  for (;;) {
    const bool received = xQueueReceive(writerQueue, &message, pdMS_TO_TICKS(250)) == pdTRUE;
    if (received && message.kind == WRITER_DELETE_ALL) deleteRequested = true;
    portENTER_CRITICAL(&loggerMux);
    if (deleteAllPending) deleteRequested = true;
    portEXIT_CRITICAL(&loggerMux);
    if (received && message.kind == WRITER_EXPORT && deleteRequested) {
      exportRequest.success = false;
      xSemaphoreGive(exportDone);
      message.kind = WRITER_WAKE;
    }
    // A pending ride-log delete takes the branch below and would drop the
    // wipe without a word; fail it visibly instead.
    if (received && message.kind == WRITER_WIPE_CARD && deleteRequested) {
      setWipeState(RideLogWipeStatus::Failed);
      message.kind = WRITER_WAKE;
    }
    const uint32_t now = millis();
    // A wipe is always serviced, even if the UI has already left the ride
    // list: skipping it here would lose the request.
    const bool needed = storageIsNeeded() || sessionActive || buffered > 0 ||
                        (received && (message.kind == WRITER_EXPORT || message.kind == WRITER_WIPE_CARD));

    if (!needed) {
      setCardChecking(false);
      wasNeeded = false;
      continue;  // Keep an existing mount warm, but perform no SD transactions.
    }

    if (!wasNeeded && mounted) {
      if (!probeMountedCard()) {
        abandonCard(buffered, false);
        sessionActive = false;
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now;
      } else {
        lastProbeMs = now;
      }
    }
    wasNeeded = true;

    if (!mounted) {
      if ((int32_t)(now - nextMountAttemptMs) >= 0) {
        SD.end();
        mounted = SD.begin(CYD_SD_CS_PIN, sdSpi, 20000000U);
        if (mounted) {
          SD.mkdir("/rides");
          publishCardState(true, false, false);
          scanRideCount();
          updateStorageStatus();
          lastProbeMs = now;
        } else {
          publishCardState(false, false, false);
          nextMountAttemptMs = now + kMountRetryMs;
        }
      }
      if (!mounted) {
        if (received && message.kind == WRITER_EXPORT) {
          exportRequest.success = false;
          xSemaphoreGive(exportDone);
        }
        if (received && message.kind == WRITER_WIPE_CARD) setWipeState(RideLogWipeStatus::Failed);
        continue;
      }
    }

    if (deleteRequested) {
      if (sessionActive && !flushBuffered(sessionPath, buffer, buffered)) {
        abandonCard(buffered, true);
        sessionActive = false;
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        continue;
      }
      sessionActive = false;
      sessionPath[0] = '\0';
      if (!deleteAllRideFiles()) {
        abandonCard(buffered, true);
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        deleteRequested = false;
        continue;
      }
      deleteRequested = false;
    } else if (received && message.kind == WRITER_START) {
      if (sessionActive) {
        if (!flushBuffered(sessionPath, buffer, buffered)) {
          abandonCard(buffered, true);
          sessionActive = false;
          sessionPath[0] = '\0';
          mounted = false;
          nextMountAttemptMs = now + kMountRetryMs;
          continue;
        }
        sessionActive = false;
      }
      removeOldestRideIfNeeded();
      snprintf(sessionPath, sizeof(sessionPath), "/rides/R%08lu.cyd",
               (unsigned long)message.payload.header.rideId);
      File headerFile = SD.open(sessionPath, FILE_WRITE);
      bool headerOk = headerFile &&
                      headerFile.write((const uint8_t *)&message.payload.header, sizeof(RideFileHeader)) ==
                          sizeof(RideFileHeader);
      if (headerOk) {
        headerFile.flush();
        headerOk = headerFile.getWriteError() == 0;
      }
      if (headerFile) headerFile.close();
      if (!headerOk) {
        abandonCard(buffered, true);
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        continue;
      }
      sessionActive = true;
      lastFlushMs = now;
    } else if (received && message.kind == WRITER_RECORD && sessionActive) {
      if (buffered + sizeof(RideRecordV3) > sizeof(buffer) &&
          !flushBuffered(sessionPath, buffer, buffered)) {
        abandonCard(buffered, true);
        sessionActive = false;
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        continue;
      }
      memcpy(buffer + buffered, &message.payload.record, sizeof(RideRecordV3));
      buffered += sizeof(RideRecordV3);
    } else if (received && message.kind == WRITER_STOP) {
      if (sessionActive) {
        if (!flushBuffered(sessionPath, buffer, buffered)) {
          abandonCard(buffered, true);
          sessionActive = false;
          sessionPath[0] = '\0';
          mounted = false;
          nextMountAttemptMs = now + kMountRetryMs;
          continue;
        }
        sessionActive = false;
        sessionPath[0] = '\0';
        scanRideCount();
        updateStorageStatus();
      }
    } else if (received && message.kind == WRITER_SCAN_CATALOG) {
      scanRideCatalog();
    } else if (received && message.kind == WRITER_EXPORT) {
      processExportRequest(sessionActive, sessionPath);
    } else if (received && message.kind == WRITER_DELETE_RIDE) {
      deleteRideFile(message.payload.header.rideId, sessionActive, sessionPath);
    } else if (received && message.kind == WRITER_WIPE_CARD) {
      // Close the open session first: the wipe removes its file too, so it
      // must not keep appending to a path that no longer exists.
      if (sessionActive) {
        flushBuffered(sessionPath, buffer, buffered);
        sessionActive = false;
        sessionPath[0] = '\0';
        portENTER_CRITICAL(&loggerMux);
        sharedStatus.recording = false;
        bumpStatusLocked();
        portEXIT_CRITICAL(&loggerMux);
      }
      buffered = 0;
      if (!wipeCard()) {
        abandonCard(buffered, true);
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        continue;
      }
    }

    if (sessionActive && buffered > 0 && now - lastFlushMs >= kMaximumBufferedMs) {
      if (!flushBuffered(sessionPath, buffer, buffered)) {
        abandonCard(buffered, true);
        sessionActive = false;
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now + kMountRetryMs;
        continue;
      }
      lastFlushMs = now;
    } else if (!sessionActive && now - lastProbeMs >= kCardProbeIntervalMs) {
      if (!probeMountedCard()) {
        abandonCard(buffered, false);
        sessionPath[0] = '\0';
        mounted = false;
        nextMountAttemptMs = now;
        continue;
      }
      setCardChecking(false);
      lastProbeMs = now;
    }
  }
}

void sendStop() {
  if (!sessionOpen || !writerQueue) return;
  WriterMessage message = {};
  message.kind = WRITER_STOP;
  xQueueSend(writerQueue, &message, pdMS_TO_TICKS(100));
  sessionOpen = false;
  lastSampleMs = 0;
  portENTER_CRITICAL(&loggerMux);
  sharedStatus.recording = false;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
  // A completed ride is a natural durable boundary for the much smaller
  // lifetime battery-history snapshot as well.
  batteryStatsCheckpoint();
}

bool startSession(uint32_t startedAtMs) {
  RideLoggingStatus status = rideLoggerStatus();
  if (!status.cardReady || !writerQueue) return false;
  loggerPrefs.begin("logger", false);
  const uint32_t rideId = loggerPrefs.getUInt("nextRide", 1);
  loggerPrefs.putUInt("nextRide", rideId + 1);
  loggerPrefs.end();

  WriterMessage message = {};
  message.kind = WRITER_START;
  RideFileHeader &header = message.payload.header;
  memcpy(header.magic, "KAJL", 4);
  header.version = kLogVersion;
  header.headerBytes = sizeof(RideFileHeader);
  header.recordBytes = sizeof(RideRecordV3);
  header.sampleHz = status.sampleHz;
  // Mark the file at creation: the ride list has no other way to tell a demo
  // recording from a real one once it is on the card.
  header.flags = dashboardDemoModeEnabled ? kRideFlagDemo : 0;
  header.rideId = rideId;
  header.createdBootMs = startedAtMs;
  header.crc = crc16((const uint8_t *)&header, sizeof(header) - sizeof(header.crc));
  if (xQueueSend(writerQueue, &message, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  sessionOpen = true;
  // rideLoggerSample captures `now` before starting the session and uses that
  // same value for the first record. Keeping both timestamps identical avoids
  // unsigned underflow (0xffffffff - a few ms) in that first record.
  sessionStartedMs = startedAtMs;
  lastSampleMs = 0;
  portENTER_CRITICAL(&loggerMux);
  sharedStatus.recording = true;
  sharedStatus.rideId = rideId;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
  return true;
}

void persistLoggerConfig() {
  const RideLoggingStatus status = rideLoggerStatus();
  loggerPrefs.begin("logger", false);
  loggerPrefs.putUChar("mode", (uint8_t)status.mode);
  loggerPrefs.putUChar("rate", status.sampleHz);
  loggerPrefs.end();
}

}  // namespace

void rideLoggerBegin() {
  loggerPrefs.begin("logger", true);
  // Any non-zero value, including the retired manual mode (2), means on.
  sharedStatus.mode = loggerPrefs.getUChar("mode", kDefaultLoggingMode) ? RIDE_LOG_ON : RIDE_LOG_OFF;
  const uint8_t savedRate = loggerPrefs.getUChar("rate", kDefaultSampleHz);
  sharedStatus.sampleHz =
      savedRate == 1 || savedRate == 2 || savedRate == 5 || savedRate == 10 ? savedRate : kDefaultSampleHz;
  const bool pendingRideWipe = loggerPrefs.getBool(kPendingRideWipeKey, false);
  loggerPrefs.end();
  portENTER_CRITICAL(&loggerMux);
  deleteAllPending = pendingRideWipe;
  portEXIT_CRITICAL(&loggerMux);
  if (!sharedCatalog)
    sharedCatalog = static_cast<RideLogSummary *>(calloc(RIDE_LOG_CATALOG_MAX, sizeof(RideLogSummary)));
  writerQueue = xQueueCreate(64, sizeof(WriterMessage));
  exportMutex = xSemaphoreCreateMutex();
  exportDone = xSemaphoreCreateBinary();
  if (writerQueue) xTaskCreatePinnedToCore(writerTask, "ride-logger", 4096, nullptr, 0, nullptr, 0);
}

RideLoggingStatus rideLoggerStatus() {
  RideLoggingStatus copy;
  portENTER_CRITICAL(&loggerMux);
  copy = sharedStatus;
  portEXIT_CRITICAL(&loggerMux);
  return copy;
}

void rideLoggerToggleEnabled() {
  portENTER_CRITICAL(&loggerMux);
  sharedStatus.mode = sharedStatus.mode == RIDE_LOG_OFF ? RIDE_LOG_ON : RIDE_LOG_OFF;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
  persistLoggerConfig();
}

void rideLoggerCycleRate() {
  portENTER_CRITICAL(&loggerMux);
  sharedStatus.sampleHz = sharedStatus.sampleHz == 1 ? 2 : sharedStatus.sampleHz == 2 ? 5 : sharedStatus.sampleHz == 5 ? 10 : 1;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
  persistLoggerConfig();
}

void rideLoggerSetSuspended(bool suspended) {
  portENTER_CRITICAL(&loggerMux);
  loggerSuspended = suspended;
  portEXIT_CRITICAL(&loggerMux);
  if (suspended) sendStop();
  if (writerQueue) {
    WriterMessage wake = {};
    wake.kind = WRITER_WAKE;
    xQueueSend(writerQueue, &wake, 0);
  }
}

namespace {
// Applies the start/pause/stop policy for one reading, opening or closing the
// ride file as it decides. Returns whether the reading should be appended.
bool advanceSession(const RideLoggingStatus &status, bool moving, uint32_t now) {
  // The writer can end a session after a card disappears. Mirror that failure
  // into the telemetry-side session state before deciding whether to queue more
  // records for a file that no longer exists.
  if (sessionOpen && !status.recording) {
    sessionOpen = false;
    lastSampleMs = 0;
  }
  if (moving) {
    if (movingSinceMs == 0) movingSinceMs = now;
    stoppedSinceMs = 0;
  } else {
    movingSinceMs = 0;
    if (stoppedSinceMs == 0) stoppedSinceMs = now;
  }

  portENTER_CRITICAL(&loggerMux);
  const bool suspended = loggerSuspended || deleteAllPending;
  portEXIT_CRITICAL(&loggerMux);
  // keepSession holds the file open; appendSample decides whether this reading
  // is written to it. They differ only while a ride is paused at a stop.
  bool keepSession = false;
  bool appendSample = false;
  if (!suspended && status.mode == RIDE_LOG_ON) {
    const uint32_t heldForMs = now - (moving ? movingSinceMs : stoppedSinceMs);
    const ride_log::AutoDecision decision = ride_log::autoDecision(sessionOpen, moving, heldForMs);
    keepSession = decision.keepSession;
    appendSample = decision.appendSample;
  }
  if (!keepSession) {
    sendStop();
    return false;
  }
  if (!sessionOpen && !startSession(now)) return false;
  // Moving again resumes into the same file. The paused stretch is simply
  // absent, which the replay parser already reads as a gap.
  return appendSample;
}
}  // namespace

void rideLoggerTelemetryLost() {
  // Only an open ride needs the policy to keep running. Without a controller
  // nothing would otherwise close it, and the writer never hands out the file
  // it is still writing, so replay would report the ride as unavailable.
  if (!sessionOpen || dashboardDemoModeEnabled) return;
  advanceSession(rideLoggerStatus(), false, millis());
}

void rideLoggerSample(const DashboardValues &values, const BatteryStats &stats, uint8_t faultCode,
                      uint32_t availableFields) {
  // Demo rides are recorded like real ones so logging and replay can be
  // exercised without a controller, but the two sources must never share a
  // file: close the session whenever it changes.
  static bool lastSampleWasDemo = false;
  if (lastSampleWasDemo != dashboardDemoModeEnabled) {
    lastSampleWasDemo = dashboardDemoModeEnabled;
    movingSinceMs = stoppedSinceMs = 0;
    sendStop();
    return;
  }
  const RideLoggingStatus status = rideLoggerStatus();
  const uint32_t now = millis();
  const bool moving = values.speedKmh >= 2 || abs(values.watts) >= 75;
  if (!advanceSession(status, moving, now)) return;
  const uint32_t intervalMs = 1000U / max<uint8_t>(1, status.sampleHz);
  if (lastSampleMs && now - lastSampleMs < intervalMs) return;
  lastSampleMs = now;

  WriterMessage message = {};
  message.kind = WRITER_RECORD;
  RideRecordV3 &record = message.payload.record;
  record.elapsedMs = now - sessionStartedMs;
  record.rideUptimeSeconds = values.uptimeSeconds;
  record.watts = values.watts;
  record.tripMeters = (uint32_t)max(0.0F, values.tripKm * 1000.0F);
  record.netWhDeci = (int32_t)lroundf(stats.tripWh * 10.0F);
  record.regenWhDeci = (uint32_t)max(0.0F, stats.tripRegenWh * 10.0F);
  record.speedDeciKmh = (int16_t)constrain(values.speedKmh * 10, -32768, 32767);
  record.voltageCenti = (uint16_t)constrain((int)lroundf(values.voltage * 100.0F), 0, 65535);
  record.currentDeci = (int16_t)constrain((int)lroundf(values.current * 10.0F), -32768, 32767);
  record.motorCurrentDeci = (int16_t)constrain((int)lroundf(values.motorCurrent * 10.0F), -32768, 32767);
  record.motorTempDeci = (int16_t)constrain(values.motorTemp * 10, -32768, 32767);
  record.escTempDeci = (int16_t)constrain(values.escTemp * 10, -32768, 32767);
  record.batteryPercent = (uint8_t)constrain(values.batteryPercent, 0, 100);
  record.faultCode = faultCode;
  record.availableFields = availableFields;
  record.crc = crc16((const uint8_t *)&record, sizeof(record) - sizeof(record.crc));
  if (xQueueSend(writerQueue, &message, 0) != pdTRUE) {
    portENTER_CRITICAL(&loggerMux);
    sharedStatus.droppedRecords++;
    bumpStatusLocked();
    portEXIT_CRITICAL(&loggerMux);
  }
}

void rideLoggerSetStorageNeeded(bool needed) {
  bool changed = false;
  portENTER_CRITICAL(&loggerMux);
  if (storageNeededByUi != needed) {
    storageNeededByUi = needed;
    // A mounted card does not need to flash through a synthetic "checking"
    // state whenever the menu opens. The writer still probes it immediately;
    // only an unmounted card needs the waiting UI while that happens.
    sharedStatus.cardChecking = needed && !sharedStatus.cardReady;
    bumpStatusLocked();
    changed = true;
  }
  portEXIT_CRITICAL(&loggerMux);
  if (changed && writerQueue) {
    WriterMessage wake = {};
    wake.kind = WRITER_WAKE;
    xQueueSend(writerQueue, &wake, 0);
  }
}

void rideLoggerResetSettings(bool deleteRideLogs) {
  sendStop();
  movingSinceMs = 0;
  stoppedSinceMs = 0;
  portENTER_CRITICAL(&loggerMux);
  sharedStatus.mode = kDefaultLoggingMode;
  sharedStatus.sampleHz = kDefaultSampleHz;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);

  Preferences resetPreferences;
  resetPreferences.begin("logger", false);
  resetPreferences.putUChar("mode", (uint8_t)kDefaultLoggingMode);
  resetPreferences.putUChar("rate", kDefaultSampleHz);
  resetPreferences.end();

  if (deleteRideLogs) rideLoggerDeleteAll();
}

void rideLoggerDeleteAll() {
  sendStop();
  // Persist the request before touching the card. If the card is missing, an
  // SD operation fails, or power is lost, rideLoggerBegin() restores the flag
  // and the writer completes the wipe when storage is available again.
  Preferences resetPreferences;
  resetPreferences.begin("logger", false);
  resetPreferences.putBool(kPendingRideWipeKey, true);
  resetPreferences.end();
  portENTER_CRITICAL(&loggerMux);
  deleteAllPending = true;
  bumpStatusLocked();
  portEXIT_CRITICAL(&loggerMux);
  if (!writerQueue) return;
  WriterMessage message = {};
  message.kind = WRITER_WAKE;
  // The pending flag is authoritative, so a full queue can only delay the
  // wake by at most the writer's normal 250 ms receive timeout.
  xQueueSend(writerQueue, &message, 0);
}

// Empties the whole card, not only the ride logs. Any open recording session
// is closed first by the writer.
bool rideLoggerWipeCard() {
  // Counting from the moment it is asked for, so the dialog never reads the
  // Done left over from an earlier wipe.
  setWipeState(RideLogWipeStatus::Counting);
  WriterMessage message = {};
  message.kind = WRITER_WIPE_CARD;
  const bool queued = writerQueue && xQueueSend(writerQueue, &message, pdMS_TO_TICKS(100)) == pdTRUE;
  if (!queued) setWipeState(RideLogWipeStatus::Failed);
  return queued;
}

RideLogWipeStatus rideLoggerWipeStatus() {
  portENTER_CRITICAL(&loggerMux);
  const RideLogWipeStatus copy = sharedWipe;
  portEXIT_CRITICAL(&loggerMux);
  return copy;
}

bool rideLoggerDeleteRide(uint32_t rideId) {
  if (!writerQueue || rideId == 0) return false;
  WriterMessage message = {};
  message.kind = WRITER_DELETE_RIDE;
  message.payload.header.rideId = rideId;
  return xQueueSend(writerQueue, &message, pdMS_TO_TICKS(100)) == pdTRUE;
}

void rideLoggerRequestCatalog() {
  portENTER_CRITICAL(&loggerMux);
  sharedCatalogStatus.loading = true;
  sharedCatalogStatus.revision++;
  if (sharedCatalogStatus.revision == 0) sharedCatalogStatus.revision = 1;
  portEXIT_CRITICAL(&loggerMux);
  if (writerQueue) {
    WriterMessage message = {};
    message.kind = WRITER_SCAN_CATALOG;
    if (xQueueSend(writerQueue, &message, 0) != pdTRUE) {
      portENTER_CRITICAL(&loggerMux);
      sharedCatalogStatus.loading = false;
      sharedCatalogStatus.revision++;
      portEXIT_CRITICAL(&loggerMux);
    }
  }
}

RideLogCatalogStatus rideLoggerCatalogStatus() {
  RideLogCatalogStatus copy;
  portENTER_CRITICAL(&loggerMux);
  copy = sharedCatalogStatus;
  portEXIT_CRITICAL(&loggerMux);
  return copy;
}

bool rideLoggerCatalogEntry(uint8_t index, RideLogSummary &entry) {
  bool validIndex;
  portENTER_CRITICAL(&loggerMux);
  validIndex = sharedCatalog && index < sharedCatalogStatus.count;
  if (validIndex) entry = sharedCatalog[index];
  portEXIT_CRITICAL(&loggerMux);
  return validIndex;
}

static bool exportOutstanding = false;
static bool runExportRequest(ExportRequest &request) {
  if (!writerQueue || !exportMutex || !exportDone ||
      xSemaphoreTake(exportMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
  if (exportOutstanding) {
    if (xSemaphoreTake(exportDone, pdMS_TO_TICKS(6000)) != pdTRUE) {
      xSemaphoreGive(exportMutex);
      return false;
    }
    exportOutstanding = false;
  }
  while (xSemaphoreTake(exportDone, 0) == pdTRUE) {}
  exportRequest = request;
  WriterMessage message = {};
  message.kind = WRITER_EXPORT;
  const bool queued = xQueueSend(writerQueue, &message, pdMS_TO_TICKS(100)) == pdTRUE;
  const bool completed = queued && xSemaphoreTake(exportDone, pdMS_TO_TICKS(6000)) == pdTRUE;
  exportOutstanding = queued && !completed;
  if (completed) request = exportRequest;
  xSemaphoreGive(exportMutex);
  return completed && request.success;
}

bool rideLoggerReadSeries(uint32_t rideId, RideLogSeriesField field, uint32_t startSeconds,
                          uint32_t endSeconds, uint8_t maximumPoints, RideLogSeriesPoint *points,
                          uint8_t &pointCount) {
  ExportRequest request = {};
  request.kind = EXPORT_SERIES;
  request.rideId = rideId;
  request.field = field;
  request.startSeconds = startSeconds;
  request.endSeconds = endSeconds;
  request.maximumPoints = maximumPoints;
  request.maximumPoints = min<uint8_t>(request.maximumPoints, 36);
  const bool ok = runExportRequest(request);
  pointCount = request.pointCount;
  if (ok && points && pointCount) memcpy(points, request.points, pointCount * sizeof(RideLogSeriesPoint));
  return ok;
}

bool rideLoggerReadFileChunk(uint32_t rideId, uint32_t offset, uint8_t *buffer, size_t capacity,
                             size_t &bytesRead, uint32_t &fileBytes) {
  ExportRequest request = {};
  request.kind = EXPORT_FILE;
  request.rideId = rideId;
  request.offset = offset;
  request.capacity = min<size_t>(capacity, sizeof(request.buffer));
  const bool ok = runExportRequest(request);
  bytesRead = request.bytesRead;
  fileBytes = request.fileBytes;
  if (ok && buffer && bytesRead) memcpy(buffer, request.buffer, bytesRead);
  return ok;
}
