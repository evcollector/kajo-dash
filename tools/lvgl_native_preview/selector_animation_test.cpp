#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "Preferences.h"
#include "app_state.h"
#include "dashboards.h"
#include "host_runtime.h"
#include "screens.h"
#include "ui_common.h"
#include "ui_style.h"

extern void previewSetInteractiveMode(bool interactive);
extern int previewGlidePosition(int index);
extern int previewGlideTarget(int index);
using namespace cyd::preview;

static bool require(bool condition, const char *message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

static void openPreview(DashboardMode mode) {
  uiPreviewSetDashboardMode(mode);
  uiPreviewSetSubmenu(SUBMENU_DASH_UI, 0, false);
  uiPreviewSetColorPalette(true);
  uiPreviewSetColorPalette(false);
  uiShow(SCREEN_SUBMENU);
  refreshNow();
}

static void toggleControls() {
  lv_event_send(lv_scr_act(), LV_EVENT_CLICKED, nullptr);
}

int main() {
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  previewSetInteractiveMode(true);
  loadAppSettings();
  language = LANG_EN;
  for (int mode = 0; mode < 12; ++mode) {
    // Render the thumbnail fixture through the ordinary dashboard path as the
    // reference; selector chrome is hidden before comparing every pixel.
    setDemoPreview(false);
    resetAutomaticGaugeRanges();
    uiPreviewSetDashboardMode(static_cast<DashboardMode>(mode));
    if (mode == MODE_TRACE) previewSeedTrace();
    uiShow(SCREEN_DASHBOARD);
    previewFinishStartupSweep();
    updateDashboardMode(static_cast<DashboardMode>(mode), makeThumbnailDashboardValues(), true);
    if (mode == MODE_EFFICIENCY) previewSeedEfficiency();
    refreshNow();
    const std::vector<lv_color_t> reference(framebuffer(), framebuffer() + 320 * 240);
    capturePpm(".pio/selector-review/thumbnail-reference.ppm");
    openPreview(static_cast<DashboardMode>(mode));
    const std::vector<lv_color_t> frozen(framebuffer(), framebuffer() + 320 * 240);
    for (int i = 0; i < 12; ++i) { advanceTime(100); uiDashboardTick(); }
    if (!require(demoPreviewIsFrozen(), "Opening preview did not freeze its thumbnail fixture") ||
        !require(makeDummyValues().speedKmh == 25 && makeDummyValues().watts == 1000 &&
                 makeDummyValues().batteryPercent == 75 && makeDummyValues().uptimeSeconds == 600,
                 "Frozen readings diverged from thumbnail data") ||
        !require(std::equal(frozen.begin(), frozen.end(), framebuffer(),
                           [](lv_color_t a, lv_color_t b) { return a.full == b.full; }),
                 "Frozen theme changed while its controls were visible")) return 1;
    toggleControls();
    advanceTime(cyd_ui::kMotionMs + 550);
    if (!require(demoPreviewIsFrozen(), "Demo ride started before the clear-view delay")) return 1;
    refreshNow();
    if (!require(std::equal(reference.begin(), reference.end(), framebuffer(),
                           [](lv_color_t a, lv_color_t b) { return a.full == b.full; }),
                 "Frozen selector did not match thumbnail dashboard state")) {
      std::cerr << "mode=" << mode << std::endl;
      capturePpm(".pio/selector-review/frozen-mismatch.ppm");
      return 1;
    }
    // No self-test: the demo ride is released after the delay and the instruments glide into it.
    const int speedIndex = mode == MODE_MOTOR_DATA ? 1 : 0;
    const bool glides = mode == MODE_GAUGE || mode == MODE_MOTOR_DATA || mode == MODE_BIG_READOUT ||
                        mode == MODE_REDLINE || mode == MODE_MINIMAL || mode == MODE_EFFICIENCY;
    int position = glides ? previewGlidePosition(speedIndex) : 0;
    int largestStep = 0;
    bool released = false;
    for (int t = 0; t < 400; t += 10) {
      advanceTime(10);
      uiDashboardTick();
      if (!demoPreviewIsFrozen()) released = true;
      if (glides) {
        largestStep = std::max(largestStep, std::abs(previewGlidePosition(speedIndex) - position));
        position = previewGlidePosition(speedIndex);
      }
    }
    if (!require(released, "Demo ride was not released after the clear-view delay")) return 1;
    if (!require(largestStep <= 600, "Instrument jumped when the demo ride took over")) {
      std::cerr << "mode=" << mode << " step=" << largestStep << std::endl;
      return 1;
    }
    if (glides && !require(previewGlideTarget(speedIndex) != 25 * kGlideScale / 30 || mode == MODE_EFFICIENCY ||
                               previewGlidePosition(speedIndex) != 25 * kGlideScale / 30,
                           "Instrument ignored the demo ride")) return 1;
    // Showing the controls again must not freeze the live preview back to the thumbnail.
    toggleControls();
    advanceTime(cyd_ui::kMotionMs + 700);
    if (!require(!demoPreviewIsFrozen(), "Returning controls froze the demo ride again")) return 1;
  }
  openPreview(MODE_MOTOR_DATA);
  toggleControls();
  advanceTime(cyd_ui::kMotionMs + 100);
  toggleControls();
  advanceTime(850);
  if (!require(demoPreviewIsFrozen(), "Returning controls did not cancel the pending demo release")) return 1;
  toggleControls();
  advanceTime(200);
  uiShow(SCREEN_MENU);
  advanceTime(850);  // a pending release must not reach the deleted screen

  // Normal reading glides also freeze, preserve their elapsed easing state,
  // resume towards their target, and cannot outlive replacement widgets.
  openPreview(MODE_MOTOR_DATA);
  DashboardValues v = makeDummyValues();
  v.speedKmh = 5;
  updateDashboardMode(MODE_MOTOR_DATA, v, true);
  advanceTime(1100);
  v.speedKmh = 25;
  updateDashboardMode(MODE_MOTOR_DATA, v, true);
  advanceTime(40);
  int position = previewGlidePosition(1);
  pauseDashboardAnimations(160);
  advanceTime(100);
  if (!require(previewGlidePosition(1) == position, "Live glide moved while suspended")) return 1;
  pauseDashboardAnimations(160);
  advanceTime(150);
  if (!require(previewGlidePosition(1) == position, "Repeated slides did not extend suspension")) return 1;
  advanceTime(15);
  if (!require(previewGlidePosition(1) >= position, "Resuming jumped backwards")) return 1;
  advanceTime(1100);
  if (!require(previewGlidePosition(1) == previewGlideTarget(1), "Suspended glide did not reach its target")) return 1;
  v.speedKmh = 10;
  updateDashboardMode(MODE_MOTOR_DATA, v, true);
  advanceTime(30);
  pauseDashboardAnimations(160);
  openPreview(MODE_MOTOR_DATA);
  position = previewGlidePosition(1);
  advanceTime(500);
  if (!require(previewGlidePosition(1) == position, "Suspended animation reached replacement widgets")) return 1;
  // A tap on a live, moving dashboard: the press stops every repaint at once, the slide that
  // follows runs on the faster frame clock, and nothing but the controls changes while it runs.
  setDemoPreview(false);
  demoTimeScale = 60;  // fast enough that any update that did run would visibly change the dials
  setDemoMode(true);
  uiPreviewSetDashboardMode(MODE_MOTOR_DATA);
  uiShow(SCREEN_DASHBOARD);
  for (int i = 0; i < 20; ++i) { advanceTime(100); uiDashboardTick(); }
  // Control: with nothing held the instruments really are moving from tick to tick.
  const auto bandHash = [] {
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 56; y < 196; ++y)
      for (int x = 0; x < 320; ++x) { hash ^= framebuffer()[y * 320 + x].full; hash *= 1099511628211ULL; }
    return hash;
  };
  const uint64_t liveBefore = bandHash();
  for (int i = 0; i < 5; ++i) { advanceTime(100); uiDashboardTick(); }
  advanceTime(100);
  if (!require(bandHash() != liveBefore, "The live dashboard was not changing, so the freeze test proves nothing")) return 1;
  const uint32_t regularPeriod = currentFramePeriod();
  setPointer(true, 160, 120);
  advanceTime(20);
  if (!require(uiUpdatesHeld(), "A press did not stop the live updates")) return 1;
  advanceTime(60);  // anything already pending has been drawn
  const std::vector<lv_color_t> before(framebuffer(), framebuffer() + 320 * 240);
  setPointer(false, 160, 120);
  int frames = 0, boosted = 0;
  for (int t = 0; t < 150; t += 10) {
    FrameMetrics a, b;
    latestFrameMetrics(a);
    advanceTime(10);
    if (t % 100 == 0) uiDashboardTick();  // the tick that would redraw the dials
    latestFrameMetrics(b);
    if (a.frameNumber != b.frameNumber) ++frames;
    if (t == 50) boosted = static_cast<int>(currentFramePeriod());
  }
  bool dialsStill = true;  // the band between the two controls holds every instrument
  for (int y = 56; y < 196 && dialsStill; ++y)
    for (int x = 0; x < 320; ++x)
      if (before[y * 320 + x].full != framebuffer()[y * 320 + x].full) { dialsStill = false; break; }
  if (!require(dialsStill, "Dashboard instruments changed while the controls slid in") ||
      !require(frames >= 7, "The slide ran below 7 frames in its 140 ms") ||
      !require(boosted == static_cast<int>(cyd_ui::kSlideFramePeriodMs), "The slide did not get the faster frame clock")) {
    std::cerr << "frames=" << frames << " period=" << boosted << std::endl;
    return 1;
  }
  advanceTime(400);
  if (!require(currentFramePeriod() == regularPeriod, "The frame clock stayed boosted after the slide")) return 1;
  setDemoMode(false);
  demoTimeScale = 1;

  std::cout << "selector animation timing ok\n";
  return 0;
}
