// Every dashboard's needle, ring or bar that glides (Motor Data, Dual Gauge, Redline, Ride Console,
// Minimal and Efficiency) moves from reading to reading instead of jumping a step at a time: the
// first reading is placed at once, a changed one is reached by a straight-line glide a quarter
// longer than the reading had been steady (at least 100 ms, at most 300 ms, or a second when the
// change is about one reading), and every frame of that glide repaints correctly. The same
// scenarios run on each theme's speed instrument.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "Preferences.h"
#include "app_state.h"
#include "controller_manager.h"
#include "dashboards.h"
#include "host_runtime.h"
#include "ui_common.h"

int previewGlidePosition(int instrument);
int previewGlideTarget(int instrument);

namespace {

using namespace cyd::preview;

bool ok = true;
// The theme under test, and the index of its speed glide (the order its theme adds them).
DashboardMode gMode = MODE_MOTOR_DATA;
int kSpeed = 1;
const char *gTheme = "";

void fail(const std::string &message) {
  std::cerr << gTheme << ": " << message << '\n';
  ok = false;
}

DashboardValues readings(int speed) {
  DashboardValues v = {};
  v.speedKmh = speed;
  v.watts = 900;
  v.voltage = 52.0F;
  v.current = 17.3F;
  v.motorCurrent = 40.0F;
  v.motorTemp = 42;
  v.escTemp = 38;
  v.tripKm = 12.5F;
  v.odoKm = 1250;
  v.avgSpeedKmh = 22.5F;
  v.uptimeSeconds = 600;
  v.batteryPercent = 75;
  v.dutyCycle = 0.4F;
  v.phaseVoltage = 8.5F;
  return v;
}

lv_obj_t *newScreen() {
  lv_obj_t *screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  makePassive(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  return screen;
}

// A Motor Data screen showing `speed`, settled (the startup sweep finished).
lv_obj_t *freshDashboard(int speed, bool finishSweep = true) {
  lv_obj_t *old = lv_scr_act();
  lv_obj_t *screen = newScreen();
  lv_scr_load(screen);
  lv_obj_del(old);
  setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL);
  buildDashboardMode(screen, gMode, readings(speed));
  if (finishSweep) previewFinishStartupSweep();
  updateDashboardMode(gMode, readings(speed), true);
  refreshNow();
  return screen;
}

struct Sample {
  int ms;
  int position;
};

struct Run {
  int clock = 0;
  std::vector<Sample> samples;
  // Frames since the last picture check: the partial repaint against a full one.
  long differingPixels = 0;
  int worstDelta = 0;
  uint32_t worstFramePixels = 0;
};
Run run;

// Advances 10 ms at a time, sampling the speed instrument and checking every frame's picture.
void watch(int duration, bool checkPictures = false) {
  for (int t = 0; t < duration; t += 10) {
    advanceTime(10);
    run.clock += 10;
    run.samples.push_back({run.clock, previewGlidePosition(kSpeed)});
    if (!checkPictures) continue;
    FrameMetrics before, after;
    latestFrameMetrics(before);
    lv_refr_now(display());
    latestFrameMetrics(after);
    if (before.frameNumber != after.frameNumber) run.worstFramePixels = std::max(run.worstFramePixels, after.flushedPixels);
    std::vector<lv_color_t> incremental(framebuffer(), framebuffer() + 320 * 240);
    refreshNow();
    for (int i = 0; i < 320 * 240; ++i) {
      if (incremental[i].full == framebuffer()[i].full) continue;
      ++run.differingPixels;
      const uint32_t a = lv_color_to32(incremental[i]), b = lv_color_to32(framebuffer()[i]);
      for (int shift : {0, 8, 16})
        run.worstDelta = std::max(run.worstDelta, std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)));
    }
  }
}

// Positions of the samples taken since `from` (an index into run.samples).
std::vector<int> positionsSince(size_t from) {
  std::vector<int> out;
  for (size_t i = from; i < run.samples.size(); ++i) out.push_back(run.samples[i].position);
  return out;
}

int largestMove(const std::vector<int> &positions) {
  int largest = 0;
  for (size_t i = 1; i < positions.size(); ++i) largest = std::max(largest, std::abs(positions[i] - positions[i - 1]));
  return largest;
}

// Milliseconds after sample `from` at which the position first equals `target`, or -1.
int reachedAfter(size_t from, int target) {
  for (size_t i = from; i < run.samples.size(); ++i)
    if (run.samples[i].position == target) return run.samples[i].ms - run.samples[from].ms;
  return -1;
}

bool monotonic(const std::vector<int> &positions, bool rising) {
  for (size_t i = 1; i < positions.size(); ++i)
    if (rising ? positions[i] < positions[i - 1] : positions[i] > positions[i - 1]) return false;
  return true;
}

}  // namespace

void runTheme(const char *name, DashboardMode mode, int speedIndex, bool unavailableDropsAtOnce) {
  gTheme = name;
  gMode = mode;
  kSpeed = speedIndex;
  run = Run();
  dashboardMode = mode;
  setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL);
  automaticGaugeRanges = false;
  topSpeedKmh = 60;

  // A. The first reading is placed where it belongs at once and nothing moves afterwards.
  freshDashboard(20);
  const int placed = previewGlidePosition(kSpeed);
  if (placed <= 0 || placed != previewGlideTarget(kSpeed)) fail("The first reading was not placed at once");
  size_t mark = run.samples.size();
  watch(400);
  if (largestMove(positionsSince(mark)) != 0) fail("The needle moved with no new reading");

  // B. After a long steady spell a 1 km/h change, so small a step, is a straight glide of a second, in many steps.
  watch(1000);
  updateDashboardMode(gMode, readings(21), true);
  const int step = previewGlideTarget(kSpeed) - placed;
  if (step < 40) fail("A 1 km/h change is under 40 of 4096 steps; the scale is too coarse");
  mark = run.samples.size();
  watch(1300, true);
  {
    const std::vector<int> glide = positionsSince(mark);
    const int target = previewGlideTarget(kSpeed);
    const int reached = reachedAfter(mark, target);
    if (!monotonic(glide, true)) fail("The glide was not monotonic");
    if (std::set<int>(glide.begin(), glide.end()).size() < 20) fail("The glide had fewer than 20 distinct positions");
    if (largestMove(glide) > step / 15) fail("A glide frame moved more than a fifteenth of the step");
    if (reached < 900 || reached > 1100) fail("A one-reading change took " + std::to_string(reached) + " ms, not about 1000");
    if (previewGlidePosition(kSpeed) != target) fail("The glide did not end on the target");
  }

  // C. A steady stream of 1 km/h steps, 100 ms apart, is one continuous motion that keeps up.
  mark = run.samples.size();
  for (int speed = 22; speed <= 33; ++speed) {
    updateDashboardMode(gMode, readings(speed), true);
    watch(100, true);
  }
  {
    const std::vector<int> stream = positionsSince(mark);
    if (!monotonic(stream, true)) fail("The needle went backwards during a steady acceleration");
    // From the second step on, no 50 ms stretch may pass without the needle moving.
    for (size_t i = 15; i + 5 < stream.size(); ++i)
      if (stream[i + 5] == stream[i]) {
        fail("The needle stood still for 50 ms during a steady acceleration at " + std::to_string(i * 10) + " ms");
        break;
      }
    mark = run.samples.size();
    watch(300, true);
    if (reachedAfter(mark, previewGlideTarget(kSpeed)) < 0 || reachedAfter(mark, previewGlideTarget(kSpeed)) > 200)
      fail("The needle did not catch up within 200 ms of the last reading");
  }

  // C2. A gentle ramp, one reading every 600 ms: the needle crawls instead of moving and stopping.
  watch(1500);
  mark = run.samples.size();
  for (int speed = 34; speed <= 39; ++speed) {
    updateDashboardMode(gMode, readings(speed), true);
    watch(600, true);
  }
  {
    const std::vector<int> ramp = positionsSince(mark);
    if (!monotonic(ramp, true)) fail("The needle went backwards during a gentle ramp");
    // From the second reading on, no 150 ms stretch may pass without the needle moving.
    for (size_t i = 65; i + 15 < ramp.size(); ++i)
      if (ramp[i + 15] == ramp[i]) {
        fail("The needle stood still for 150 ms during a gentle ramp at " + std::to_string(i * 10) + " ms");
        break;
      }
  }

  // D. A reading that arrives mid-glide takes over from where the needle is: no jump.
  watch(1000);
  const int before = previewGlidePosition(kSpeed);
  updateDashboardMode(gMode, readings(44), true);
  mark = run.samples.size();
  watch(100, true);
  updateDashboardMode(gMode, readings(49), true);
  watch(600, true);
  {
    const std::vector<int> positions = positionsSince(mark);
    if (!monotonic(positions, true)) fail("Retargeting mid-glide made the needle go backwards");
    // The new reading is reached in about 125 ms, four or five frames: fast, but never in one.
    if (largestMove(positions) > (previewGlideTarget(kSpeed) - before) / 3)
      fail("Retargeting mid-glide made the needle jump");
    if (positions.back() != previewGlideTarget(kSpeed)) fail("Retargeting mid-glide missed the new target");
  }

  // E. Turning round mid-glide goes back smoothly and never beyond where the needle had got to.
  watch(1000);
  const int low = previewGlidePosition(kSpeed);
  updateDashboardMode(gMode, readings(56), true);
  watch(120, true);
  const int turnedAt = previewGlidePosition(kSpeed);
  updateDashboardMode(gMode, readings(49), true);
  mark = run.samples.size();
  watch(500, true);
  {
    const std::vector<int> positions = positionsSince(mark);
    if (turnedAt <= low) fail("The needle had not started to rise before it turned round");
    if (*std::max_element(positions.begin(), positions.end()) > turnedAt + largestMove(positions) + 1)
      fail("The needle overshot when turning round");
    if (!monotonic(positions, false)) fail("The needle did not come back smoothly");
    if (positions.back() != low) fail("The needle did not return to its starting position");
  }

  // F. The same reading again does not restart or stretch the glide.
  watch(1000);
  updateDashboardMode(gMode, readings(30), true);
  mark = run.samples.size();
  for (int i = 0; i < 4; ++i) {
    watch(100);
    updateDashboardMode(gMode, readings(30), true);
  }
  watch(100);
  {
    const int reached = reachedAfter(mark, previewGlideTarget(kSpeed));
    if (reached < 240 || reached > 360) fail("Repeating a reading changed the glide's length to " + std::to_string(reached) + " ms");
  }

  // G. A reading the controller stops reporting is dropped at once; its return glides in.
  if (unavailableDropsAtOnce) {
  setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL & ~static_cast<uint32_t>(TELEMETRY_FIELD_SPEED));
  updateDashboardMode(gMode, readings(30), true);
  if (previewGlidePosition(kSpeed) != 0) fail("An unavailable reading was not cleared at once");
  setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL);
  updateDashboardMode(gMode, readings(30), true);
  if (previewGlidePosition(kSpeed) != 0) fail("A returning reading jumped instead of gliding");
  watch(60);
  if (previewGlidePosition(kSpeed) <= 0 || previewGlidePosition(kSpeed) >= previewGlideTarget(kSpeed))
    fail("A returning reading was not part-way after 60 ms");
  watch(500);
  if (previewGlidePosition(kSpeed) != previewGlideTarget(kSpeed)) fail("A returning reading did not arrive");
  }

  // H. The startup sweep drives the instruments itself and ends on the live reading.
  freshDashboard(25, false);
  watch(2500);
  if (previewGlidePosition(kSpeed) != previewGlideTarget(kSpeed) || previewGlideTarget(kSpeed) <= 0)
    fail("The startup sweep did not end on the live reading");

  // I. A replacement screen built before the old one goes is placed at once and glides on its own.
  updateDashboardMode(gMode, readings(31), true);
  watch(40);  // a glide is under way on the old screen
  {
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *screen = newScreen();
    buildDashboardMode(screen, gMode, readings(18));
    lv_scr_load(screen);
    lv_obj_del(old);
    previewFinishStartupSweep();
    updateDashboardMode(gMode, readings(18), true);
    refreshNow();
    if (previewGlidePosition(kSpeed) != previewGlideTarget(kSpeed)) fail("A replacement screen was not placed at once");
    const int settled = previewGlidePosition(kSpeed);
    mark = run.samples.size();
    watch(400);
    if (positionsSince(mark).front() != settled || largestMove(positionsSince(mark)) != 0)
      fail("The old screen's glide reached the replacement");
    updateDashboardMode(gMode, readings(19), true);
    watch(1100);  // a one-reading step may take up to a second
    if (previewGlidePosition(kSpeed) != previewGlideTarget(kSpeed) || previewGlideTarget(kSpeed) <= settled)
      fail("The replacement screen did not glide to a new reading");
  }

  // J. With automatic ranges a speed that outgrows the scale pushes the ceiling up with it: the
  // ring stays pegged while it rises, nothing moves when the scale follows, and the scale then
  // stays where the peak put it.
  automaticGaugeRanges = true;
  setDemoPreview(true);
  resetAutomaticGaugeRanges();
  freshDashboard(20);
  for (int speed = 20; speed <= 29; ++speed) {
    updateDashboardMode(gMode, readings(speed), true);
    watch(100);
  }
  mark = run.samples.size();
  for (int speed = 30; speed <= 36; ++speed) {
    updateDashboardMode(gMode, readings(speed), true);
    watch(100);
  }
  {
    const std::vector<int> climb = positionsSince(mark);
    // Skip the first reading at the old ceiling, which is still settling into the end of the dial.
    for (size_t i = 20; i < climb.size(); ++i)
      if (climb[i] != kGlideScale) { fail("The ring left the end of the dial while the speed pushed the scale"); break; }
  }
  for (int i = 0; i < 3; ++i) { updateDashboardMode(gMode, readings(36), true); watch(100); }
  updateDashboardMode(gMode, readings(20), true);
  watch(1200);
  {
    const int settled = previewGlidePosition(kSpeed);  // 20 of a 36 km/h scale
    if (settled < kGlideScale * 50 / 100 || settled > kGlideScale * 62 / 100)
      fail("The scale did not stay at the peak: the ring settled at " + std::to_string(settled));
  }
  automaticGaugeRanges = false;
  setDemoPreview(false);

  // Every frame of every glide above repainted like a full redraw, bar the odd antialiased pixel at the
  // edge of a repainted rectangle, and none was expensive.
  std::cout << gTheme << ": frames compared: " << run.differingPixels << " differing pixels, worst " << run.worstDelta
            << "/255; most pixels in a frame " << run.worstFramePixels << "\n";
  if (run.worstDelta > 24 || run.differingPixels > 64)
    fail("Glide frames drifted from full repaints: " + std::to_string(run.differingPixels) + " pixels, by up to " +
         std::to_string(run.worstDelta) + "/255");
  if (run.worstFramePixels >= 320 * 240) fail("A glide frame repainted the whole screen");

}

int main() {
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  loadAppSettings();
  language = LANG_EN;
  dashboardAppearanceMode = DASH_APPEARANCE_DARK;
  // A fixed 60 km/h scale, so a reading's place on the dial does not depend on the learned range.

  // Each theme's speed instrument is the glide at the index its theme adds it.
  runTheme("Motor Data", MODE_MOTOR_DATA, 1, true);
  runTheme("Dual Gauge", MODE_GAUGE, 0, false);
  runTheme("Redline", MODE_REDLINE, 0, false);
  runTheme("Ride Console", MODE_BIG_READOUT, 0, false);
  runTheme("Minimal", MODE_MINIMAL, 0, false);
  runTheme("Efficiency", MODE_EFFICIENCY, 0, false);

  std::cout << (ok ? "glide ok\n" : "glide FAILED\n");
  return ok ? 0 : 1;
}
