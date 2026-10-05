#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
extern void previewSetReplayWindowSlice(unsigned records);

// Zoom in the ride replay: the buttons, the animation, the windows the reader
// builds, and turning the page. Every step is also checked against a repaint
// from scratch, so no frame of the animation, and no page turn, leaves anything
// of the previous view on screen.
namespace {

using namespace cyd::preview;

constexpr uint32_t kScreenPixels = kDisplayWidth * kDisplayHeight;
constexpr int kZoomOutX = 19, kZoomInX = 301, kButtonY = 221;
constexpr int kBackX = 76, kForwardX = 244, kPlayX = 160;

bool ok = true;

void fail(const std::string &what) {
  std::cerr << "replay zoom test failed: " << what << '\n';
  ok = false;
}

uint32_t step(uint32_t ms) {
  FrameMetrics before, after;
  latestFrameMetrics(before);
  advanceTime(ms, ms);
  latestFrameMetrics(after);
  if (before.frameNumber == after.frameNumber) return 0;
  if (after.flushedPixels >= kScreenPixels) fail("a step repainted the whole screen in one frame");
  return after.flushedPixels;
}

void tap(int x, int y) {
  setPointer(true, x, y);
  advanceTime(30);
  setPointer(false, x, y);
  advanceTime(30);
}

void expectSamePicture(const std::string &what) {
  lv_refr_now(display());
  const std::vector<lv_color_t> partial(framebuffer(), framebuffer() + kScreenPixels);
  refreshNow();
  if (std::memcmp(partial.data(), framebuffer(), partial.size() * sizeof(lv_color_t)))
    fail("a partial repaint differs from a full one after " + what);
}

struct View {
  uint8_t zoom = 0;
  uint32_t start = 0, end = 0;
  bool detail = false;
};
View view() {
  View v;
  uiPreviewReplayView(v.zoom, v.start, v.end, v.detail);
  return v;
}
std::string show(const View &v) {
  return "zoom " + std::to_string(v.zoom) + " [" + std::to_string(v.start) + ", " + std::to_string(v.end) + "]" +
         (v.detail ? " with detail" : " without");
}

// The ink of a button's glyph: pure white pixels inside its outline. A dimmed
// button draws its glyph in the muted colour, so it has none.
int whitePixels(int x1, int x2) {
  int count = 0;
  for (int y = 208; y < 234; ++y)
    for (int x = x1; x <= x2; ++x) {
      lv_color32_t rgb;
      rgb.full = lv_color_to32(framebuffer()[y * kDisplayWidth + x]);
      if (rgb.ch.red > 0xE0 && rgb.ch.green > 0xE0 && rgb.ch.blue > 0xE0) ++count;
    }
  return count;
}
bool zoomInLit() { return whitePixels(286, 316) > 20; }
bool zoomOutLit() { return whitePixels(4, 34) > 20; }

// Lets an animation play out in 33 ms frames. Returns the views of its frames.
std::vector<View> play(const std::string &what, const char *captureDir = nullptr, const char *captureName = nullptr) {
  std::vector<View> frames;
  int quiet = 0;
  for (int i = 0; i < 40 && quiet < 3; ++i) {
    const View before = view();
    const uint32_t pixels = step(33);
    const View after = view();
    if (pixels) {
      frames.push_back(after);
      if (captureDir && captureName)
        capturePpm(std::filesystem::path(captureDir) / (std::string(captureName) + std::to_string(frames.size()) + ".ppm"));
      expectSamePicture(what + " frame " + std::to_string(frames.size()));
    }
    quiet = (before.start == after.start && before.end == after.end && pixels == 0) ? quiet + 1 : 0;
  }
  return frames;
}

}  // namespace

int main(int argc, char **argv) {
  const char *captureDir = argc > 1 ? argv[1] : nullptr;
  if (captureDir) std::filesystem::create_directories(captureDir);
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  previewSetInteractiveMode(true);
  loadAppSettings();
  previewRestoreRideLogs();
  previewSetCardState(true, false);

  uiPreviewRideReplay(504000, false);
  advanceTime(300);
  const uint32_t duration = rideReplayStatus().duration;
  if (duration < 2000000) {
    fail("the test ride did not load");
    return 1;
  }
  expectSamePicture("opening the ride");
  const std::vector<lv_color_t> wholeRide(framebuffer(), framebuffer() + kScreenPixels);

  // The whole ride: nothing to zoom out of, and something to zoom into.
  View v = view();
  if (v.zoom != 0 || v.start != 0 || v.end != duration || v.detail) fail("the ride did not open on the whole of itself: " + show(v));
  if (zoomOutLit() || !zoomInLit()) fail("the zoom buttons do not show where there is room to zoom");
  tap(kZoomOutX, kButtonY);
  if (!play("zooming out of the whole ride").empty()) fail("zoom out did something on the whole ride");

  // Zoom in once: the view narrows by a constant factor per frame, about the
  // cursor, which keeps its place on screen.
  tap(kZoomInX, kButtonY);
  const std::vector<View> in = play("zooming in", captureDir, "zoom_in_");
  if (in.size() < 4) fail("the zoom animation drew " + std::to_string(in.size()) + " frames");
  for (size_t i = 1; i < in.size(); ++i) {
    const uint32_t before = in[i - 1].end - in[i - 1].start, now = in[i].end - in[i].start;
    if (now > before) fail("zooming in widened the view");
  }
  v = view();
  if (v.zoom != 1 || v.end - v.start != duration / 2 || 504000 < v.start || 504000 > v.end) fail("one step in is not half the ride around the cursor: " + show(v));
  if (!v.detail) fail("the reader's window never replaced the stretched overview");
  const double fraction = (504000.0 - v.start) / (v.end - v.start);
  if (std::fabs(fraction - 504000.0 / duration) > 0.02) fail("the cursor moved on screen while zooming in");
  if (!zoomOutLit() || !zoomInLit()) fail("both zoom buttons should be lit one step in");

  // Zoom in to the end of the road: every step halves, the last one is refused.
  uint32_t narrowest = v.end - v.start;
  for (int i = 2; i <= 8; ++i) {
    tap(kZoomInX, kButtonY);
    const std::vector<View> frames = play("zooming in again");
    v = view();
    if (v.zoom != std::min(i, 6)) fail("zoom step " + std::to_string(i) + " ended at " + show(v));
    if (i <= 6 && frames.empty()) fail("a zoom step drew nothing");
    if (i > 6 && !frames.empty()) fail("zoom in past the limit did something");
    if (i <= 6 && (v.end - v.start) * 2 > narrowest + 2) fail("a zoom step did not halve the view");
    narrowest = v.end - v.start;
    if (v.start > 504000 || v.end < 504000) fail("the cursor left the view while zooming in: " + show(v));
  }
  if (zoomInLit() || !zoomOutLit()) fail("at the narrowest view zoom in should be dim and zoom out lit");
  if (narrowest < 28800) fail("the narrowest view is narrower than two columns a reading");

  // And all the way out again: exactly the picture the ride opened with.
  for (int i = 0; i < 7; ++i) {
    tap(kZoomOutX, kButtonY);
    const std::vector<View> frames = play("zooming out", i == 0 ? captureDir : nullptr, "zoom_out_");
    if (i == 6 && !frames.empty()) fail("zoom out past the whole ride did something");
  }
  v = view();
  if (v.zoom != 0 || v.start != 0 || v.end != duration) fail("zooming all the way out did not come back to the whole ride: " + show(v));
  expectSamePicture("zooming back out");
  if (std::memcmp(wholeRide.data(), framebuffer(), wholeRide.size() * sizeof(lv_color_t)))
    fail("the whole ride does not look as it did before zooming in and out");

  // Two quick taps are two steps: the second animation starts from the first one's frame.
  tap(kZoomInX, kButtonY);
  advanceTime(60);
  tap(kZoomInX, kButtonY);
  play("two quick zoom taps");
  v = view();
  if (v.zoom != 2 || v.end - v.start != duration / 4) fail("two quick taps did not make two steps: " + show(v));

  // The ends of the ride: the view stays inside it, and zooming out from there too.
  tap(kZoomOutX, kButtonY); play("back one step");
  tap(kZoomOutX, kButtonY); play("back to the whole");
  tap(28, 100);
  tap(kZoomInX, kButtonY); play("zooming in at the start");
  v = view();
  if (v.start != 0 || v.end != duration / 2) fail("zooming in at the very start moved the window off the ride: " + show(v));
  tap(kZoomOutX, kButtonY); play("out at the start");
  tap(316, 100);
  tap(kZoomInX, kButtonY); play("zooming in at the end");
  v = view();
  if (v.zoom != 1 || v.end != duration || v.start != duration - duration / 2) fail("zooming in at the very end moved the window off the ride: " + show(v));
  tap(kZoomOutX, kButtonY); play("out at the end");

  // Turning the page. Zoomed to 1/16 of the ride, play at 100x until the
  // cursor has gone through several windows; it is always on screen.
  uiPreviewRideReplay(400000, false);
  uiPreviewRideReplayZoom(4);
  advanceTime(300);
  v = view();
  const uint32_t pageSpan = v.end - v.start;
  if (!v.detail || v.zoom != 4) fail("zoom level 4 did not come with its window: " + show(v));
  tap(kPlayX, kButtonY);                          // play
  for (int i = 0; i < 5; ++i) tap(290, 22);       // 2x, 4x, 10x, 50x, 100x
  uint32_t pages = 0, lastStart = v.start;
  uint32_t coarse = 0;
  for (int tick = 0; tick < 120 && rideReplayStatus().position < duration - 20000; ++tick) {
    step(100);
    const View now = view();
    const uint32_t cursor = rideReplayStatus().position;
    if (cursor + 1000 < now.start || cursor > now.end + 1000) fail("the cursor ran off the view at " + std::to_string(cursor) + ": " + show(now));
    if (now.start != lastStart) {
      pages++;
      if (now.start <= lastStart) fail("a page turned backwards while playing forward");
      if (now.end - now.start != pageSpan) fail("a page changed the width of the view");
      lastStart = now.start;
      if (!now.detail) coarse++;
    }
    if (tick % 5 == 4) expectSamePicture("playing through pages, tick " + std::to_string(tick));
  }
  if (pages < 3) fail("playing at 100x turned only " + std::to_string(pages) + " pages");
  std::cout << "pages turned: " << pages << ", of which without their window ready: " << coarse << '\n';
  if (coarse > 0) fail("a page turned before its window was built, though the host builds it at once");

  // The same, with a reader that is slow: windows take many calls. The next
  // page is asked for early, so it should still be there in time at 50x.
  previewSetReplayWindowSlice(300);
  uiPreviewRideReplay(400000, false);
  uiPreviewRideReplayZoom(4);
  advanceTime(2000);
  tap(kPlayX, kButtonY);
  for (int i = 0; i < 4; ++i) tap(290, 22);       // 2x, 4x, 10x, 50x
  uint32_t slowPages = 0, slowCoarse = 0;
  lastStart = view().start;
  for (int tick = 0; tick < 150 && rideReplayStatus().position < duration - 20000; ++tick) {
    step(100);
    const View now = view();
    if (now.start != lastStart) {
      slowPages++;
      lastStart = now.start;
      if (!now.detail) slowCoarse++;
    }
    if (tick % 7 == 6) expectSamePicture("playing through pages with a slow reader, tick " + std::to_string(tick));
  }
  std::cout << "slow reader: pages " << slowPages << ", without their window ready: " << slowCoarse << '\n';
  if (slowPages < 3) fail("the slow reader test turned too few pages");
  if (slowCoarse > 0) fail("a page asked for early was still not ready when the cursor reached it");
  // Whatever arrives late arrives: a few more ticks later every view has its window.
  tap(kPlayX, kButtonY);   // pause
  advanceTime(3000);
  if (!view().detail) fail("a window that was late never arrived");

  // A reader so slow that the window is nowhere near ready when the page turns:
  // the overview stands in, and the window replaces it when it comes.
  previewSetReplayWindowSlice(10);
  uiPreviewRideReplay(400000, false);
  uiPreviewRideReplayZoom(4);
  expectSamePicture("zoom with a very slow reader");
  if (view().detail) fail("a window with a very slow reader should not have arrived at once");
  advanceTime(10000);
  if (!view().detail) fail("the very slow reader's window never arrived");
  expectSamePicture("the very slow window arriving");
  previewSetReplayWindowSlice(0);

  // Skips and taps that leave the view turn the page too, both ways.
  uiPreviewRideReplay(400000, false);
  uiPreviewRideReplayZoom(4);
  advanceTime(300);
  v = view();
  const uint32_t before = v.start;
  for (int i = 0; i < 12; ++i) tap(kForwardX, kButtonY);   // forward
  v = view();
  const uint32_t cursor = rideReplayStatus().position;
  if (v.start <= before || cursor < v.start || cursor > v.end) fail("skipping forward out of the view did not turn the page: " + show(v));
  expectSamePicture("skipping forward through pages");
  for (int i = 0; i < 30; ++i) tap(kBackX, kButtonY);      // back
  v = view();
  if (v.start >= before || rideReplayStatus().position < v.start || rideReplayStatus().position > v.end)
    fail("skipping back out of the view did not turn the page: " + show(v));
  expectSamePicture("skipping back through pages");
  advanceTime(300);
  if (!view().detail) fail("the page skipped back to never got its window");

  // A different layout while zoomed uses the same window.
  const uint8_t fields[4] = {ride_replay::kSpeed, ride_replay::kPower, ride_replay::kCurrent, ride_replay::kBattery};
  uiPreviewRideReplayLayout(4, fields);
  expectSamePicture("four charts while zoomed");
  uiPreviewRideReplayZoom(2);
  expectSamePicture("zoom level 2 with four charts");

  // Ticks that move only the cursor stay cheap when zoomed too.
  tap(kPlayX, kButtonY);
  uint64_t zoomedPixels = 0;
  for (int tick = 0; tick < 30; ++tick) zoomedPixels += step(100);
  tap(kPlayX, kButtonY);
  std::cout << "playing a zoomed view at 1x: " << zoomedPixels / 30.0 << " px per tick\n";
  if (zoomedPixels / 30.0 > 320 * 159 / 6.0) fail("playing a zoomed view repainted far more than the cursor");

  // The cost of the animation itself: a frame is the whole chart.
  uiPreviewRideReplay(1170000, false);
  uiPreviewRideReplayZoom(0);
  advanceTime(300);
  uint32_t frames = 0;
  uint64_t pixels = 0;
  tap(kZoomInX, kButtonY);
  for (int i = 0; i < 12; ++i) {
    const uint32_t flushed = step(33);
    if (flushed) { frames++; pixels += flushed; }
  }
  std::cout << "zoom animation: " << frames << " frames, " << (frames ? pixels / frames : 0)
            << " px per frame, " << pixels << " px in all\n";

  // Losing the card while zoomed: the chart becomes a message and the zoom buttons go quiet.
  uiPreviewRideReplay(400000, false);
  uiPreviewRideReplayZoom(3);
  advanceTime(300);
  previewSetCardState(false, false);
  advanceTime(300);
  expectSamePicture("removing the card while zoomed");
  if (zoomInLit() || zoomOutLit()) fail("the zoom buttons stayed lit with no ride to zoom");
  previewSetCardState(true, false);

  if (!ok) return 1;
  std::cout << "Replay zoom: buttons, animation, windows, page turns and repaints passed\n";
  return 0;
}
