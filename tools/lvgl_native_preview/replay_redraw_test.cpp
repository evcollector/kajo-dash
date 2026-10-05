#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Preferences.h"
#include "app_state.h"
#include "host_runtime.h"
#include "ride_replay.h"
#include "screens.h"

extern void previewSetCardState(bool ready, bool checking);
extern void previewSetInteractiveMode(bool enabled);
extern void previewRestoreRideLogs();

// A replay tick repaints the cursor, its dots and its bubbles, not the chart.
// This drives the real screen through taps, drags and playback, and after every
// step checks two things: that the partial repaints left exactly the picture a
// repaint from scratch draws, and what they cost in flushed pixels. Pixel counts
// describe SPI workload on the 40 MHz panel link, not measured ESP32 frame time.
namespace {

using namespace cyd::preview;

constexpr uint32_t kScreenPixels = kDisplayWidth * kDisplayHeight;
constexpr uint32_t kChartPixels = 320 * 159;  // what every tick used to repaint

bool ok = true;

void fail(const std::string &what) {
  std::cerr << "replay redraw test failed: " << what << '\n';
  ok = false;
}

// One clock step, and the pixels of the frame LVGL rendered in it (none if it
// had nothing to draw). A single handler pass renders at most one frame, so
// no step can hide another's cost.
uint32_t step(uint32_t ms, const std::string &what) {
  FrameMetrics before, after;
  latestFrameMetrics(before);
  advanceTime(ms, ms);
  latestFrameMetrics(after);
  if (before.frameNumber == after.frameNumber) return 0;
  if (after.flushedPixels >= kScreenPixels) fail(what + " repainted the whole screen in one frame");
  return after.flushedPixels;
}

struct Cost {
  uint64_t pixels = 0;
  uint32_t worst = 0, frames = 0, steps = 0;
  void add(uint32_t flushed) {
    pixels += flushed;
    worst = std::max(worst, flushed);
    if (flushed) frames++;
    steps++;
  }
  double average() const { return steps ? double(pixels) / steps : 0.0; }
};

// Renders whatever is still dirty, then repaints the whole screen from scratch.
// The partial repaints must have left the same picture.
void expectSamePicture(const std::string &what) {
  lv_refr_now(display());
  const std::vector<lv_color_t> partial(framebuffer(), framebuffer() + kScreenPixels);
  refreshNow();
  if (std::memcmp(partial.data(), framebuffer(), partial.size() * sizeof(lv_color_t)))
    fail("a partial repaint differs from a full one after " + what);
}

// A tap the way a finger lands: press, release, settle. Returns its cost.
uint32_t tapCost(int x, int y, const std::string &what) {
  setPointer(true, x, y);
  uint32_t flushed = step(30, what);
  setPointer(false, x, y);
  flushed += step(30, what);
  flushed += step(30, what);
  return flushed;
}

void tap(int x, int y) {
  setPointer(true, x, y);
  advanceTime(30);
  setPointer(false, x, y);
  advanceTime(30);
}

lv_obj_t *findLabel(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && strcmp(lv_label_get_text(object), text) == 0) return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); i++)
    if (lv_obj_t *found = findLabel(lv_obj_get_child(object, i), text)) return found;
  return nullptr;
}

bool tapLabel(const char *text) {
  lv_obj_t *label = findLabel(lv_scr_act(), text);
  if (!label) return false;
  lv_area_t area;
  lv_obj_get_coords(label, &area);
  tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
  advanceTime(100);
  return true;
}

// Plays on in 100 ms ticks, as the replay timer runs, comparing every few.
Cost run(const std::string &what, int ticks, int compareEvery = 4) {
  Cost cost;
  for (int tick = 1; tick <= ticks; ++tick) {
    cost.add(step(100, what));
    if (tick % compareEvery == 0) expectSamePicture(what + " tick " + std::to_string(tick));
  }
  expectSamePicture(what);
  return cost;
}

void expectBelow(const Cost &cost, double averageBudget, const std::string &what) {
  if (cost.average() > averageBudget)
    fail(what + " averaged " + std::to_string(cost.average()) + " px per step, over the budget of " +
         std::to_string(averageBudget));
}

}  // namespace

int main() {
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  previewSetInteractiveMode(true);
  loadAppSettings();
  previewRestoreRideLogs();
  previewSetCardState(true, false);

  uiPreviewRideReplay(504000, false);
  advanceTime(300);
  if (rideReplayStatus().state != RideReplayStatus::Ready) {
    fail("the ride did not load");
    return 1;
  }
  expectSamePicture("opening the ride");

  // Paused and untouched, the replay paints nothing at all.
  const Cost idle = run("an idle replay", 20, 10);
  if (idle.pixels != 0) fail("a paused replay repainted " + std::to_string(idle.pixels) + " px");

  // Taps anywhere in the chart: the cursor jumps, and its bubbles with it, from
  // right of the quarter-way point to left of it and back.
  Cost taps;
  for (int y : {60, 110, 160})
    for (int x : {28, 40, 95, 99, 100, 101, 160, 170, 230, 300, 316}) {
      taps.add(tapCost(x, y, "a tap"));
      expectSamePicture("a tap at " + std::to_string(x) + "," + std::to_string(y));
    }
  expectBelow(taps, kChartPixels / 2.0, "tapping the chart");

  // Dragging the cursor across the plot, and back, in 40 ms moves.
  Cost drag;
  setPointer(true, 40, 100);
  advanceTime(40);
  for (int pass = 0; pass < 2; ++pass)
    for (int move = 0; move <= 28; ++move) {
      const int x = pass == 0 ? 40 + move * 9 : 292 - move * 9;
      setPointer(true, x, 100);
      drag.add(step(40, "a drag"));
      expectSamePicture("a drag to " + std::to_string(x));
    }
  setPointer(false, 40, 100);
  advanceTime(60);
  expectSamePicture("releasing the drag");
  expectBelow(drag, kChartPixels / 2.0, "dragging the cursor");

  // The skip buttons, including three quick taps that widen the step.
  for (int i = 0; i < 3; ++i) {
    tap(272, 221);
    expectSamePicture("skip forward");
  }
  tap(45, 221);
  expectSamePicture("skip back");
  tap(45, 221);
  tap(45, 221);
  tap(45, 221);
  expectSamePicture("widened skip back");
  advanceTime(2100);
  expectSamePicture("the skip narrowing again");

  // Playback at every rate, from near the start so the ride outlasts them all.
  tap(60, 110);
  tap(160, 221);  // play
  Cost byRate[6];
  static const char *const kRates[6] = {"1x", "2x", "4x", "10x", "50x", "100x"};
  for (int i = 0; i < 6; ++i) {
    byRate[i] = run(std::string("playback at ") + kRates[i], 40, 3);
    std::cout << "playback at " << kRates[i] << ": " << byRate[i].average() << " px per tick, worst "
              << byRate[i].worst << ", " << byRate[i].frames << " repainting ticks of " << byRate[i].steps << '\n';
    if (i < 5) tap(290, 22);  // next rate
  }
  expectBelow(byRate[0], kChartPixels / 10.0, "playback at 1x");
  expectBelow(byRate[3], kChartPixels / 3.0, "playback at 10x");
  expectBelow(byRate[5], kChartPixels / 2.0, "playback at 100x");
  tap(290, 22);   // back to 1x
  tap(160, 221);  // pause

  // The gap in the voltage trace (20 s without it) and the regeneration dip.
  tap(170, 110);
  expectSamePicture("seeking into the voltage gap");
  tap(146, 110);
  expectSamePicture("seeking to regeneration");

  // Both ends of the ride, and playing from the end, which restarts it.
  tap(28, 100);
  expectSamePicture("seeking to the start");
  tap(316, 100);
  expectSamePicture("seeking to the end");
  tap(160, 221);
  run("restarting from the end", 12, 3);
  tap(160, 221);

  // A new layout repaints the whole chart; the cursor must come back on it.
  tap(247, 22);
  advanceTime(100);
  expectSamePicture("opening the chart picker");
  for (const char *count : {"1", "2", "4", "3"}) {
    if (!tapLabel(count)) fail(std::string("no chart count button ") + count);
    expectSamePicture(std::string("showing ") + count + " charts");
  }
  if (tapLabel("Speed")) {
    expectSamePicture("opening a chart's field list");
    if (!tapLabel("Battery")) fail("no Battery field");
    expectSamePicture("choosing Battery");
  } else {
    fail("no Speed chart in the picker");
  }
  if (!tapLabel("DONE")) fail("no Done button");
  expectSamePicture("closing the chart picker");
  tap(160, 221);  // play with the new layout
  run("playing the new layout", 20, 4);
  tap(160, 221);

  // Four charts are 39 px bands: bubbles and dots sit close to their edges.
  tap(247, 22);
  advanceTime(100);
  if (!tapLabel("4")) fail("no four-chart button");
  tapLabel("DONE");
  tap(100, 100);
  tap(160, 221);
  run("playing four charts", 20, 4);
  tap(160, 221);
  tap(300, 100);
  expectSamePicture("four charts near the end");

  // The summary opens over the chart and closes again.
  tap(155, 22);
  advanceTime(300);
  expectSamePicture("opening the summary");
  tap(268, 29);
  advanceTime(100);
  expectSamePicture("closing the summary");

  // Losing the card turns the chart into a message, a full repaint.
  previewSetCardState(false, false);
  advanceTime(300);
  expectSamePicture("removing the card");

  if (!ok) return 1;
  std::cout << "Replay partial repaints match full repaints; px per step: idle " << idle.average() << ", taps "
            << taps.average() << ", drag " << drag.average() << ", playing 1x " << byRate[0].average()
            << " and 100x " << byRate[5].average() << " (a full chart is " << kChartPixels << ")\n";
  return 0;
}
