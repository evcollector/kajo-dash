#include <cstring>
#include <iostream>
#include <vector>
#include "Preferences.h"
#include "app_state.h"
#include "dashboards.h"
#include "host_runtime.h"
#include "ui_common.h"

void previewResetEfficiencyHistory();
int previewEfficiencyNeedleRateX10();
int previewEfficiencyHistoryCount();

int main(int argc, char **argv) {
  using namespace cyd::preview;
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  loadAppSettings();
  uint32_t worst = 0, small = 0;
  for (bool light : {false, true}) {
    dashboardAppearanceMode = light ? DASH_APPEARANCE_LIGHT : DASH_APPEARANCE_DARK;
    dashboardMode = MODE_EFFICIENCY;
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    makePassive(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_scr_load(screen); lv_obj_del(old);
    DashboardValues v = makeDummyValues();
    v.speedKmh = 25; v.watts = 500;
    buildDashboardMode(screen, MODE_EFFICIENCY, v);
    if (light) previewResetEfficiencyHistory();
    previewFinishStartupSweep();
    updateDashboardMode(MODE_EFFICIENCY, v, true);
    refreshNow();
    if (widestLabelRepaintMargin(screen) > kLabelRepaintMargin) {
      std::cerr << "An Efficiency label repaints more than " << kLabelRepaintMargin << " px around itself\n";
      return 1;
    }
    // More than five minutes exercises scrolling, changing scale extrema,
    // flat histories, acceleration/deceleration and the arc's 360-degree seam.
    for (int step = 0; step < 3200; ++step) {
      advanceTime(100);
      v.speedKmh = step == 0 ? 26 : step < 800 ? (step / 3) % 130 : 40;
      v.watts = step < 1600 ? 300 + (step * 137) % 4000 : step < 2400 ? 800 : 0;
      FrameMetrics before, after;
      latestFrameMetrics(before);
      updateDashboardMode(MODE_EFFICIENCY, v, false);
      lv_refr_now(display());
      latestFrameMetrics(after);
      const uint32_t pixels = before.frameNumber == after.frameNumber ? 0 : after.flushedPixels;
      worst = max(worst, pixels);
      if (step == 0) {
        if (previewEfficiencyNeedleRateX10() >= 200 || previewEfficiencyNeedleRateX10() <= 115) {
          std::cerr << "Needle did not smooth the first 100 ms update\n"; return 1;
        }
        if (light && previewEfficiencyHistoryCount() != 0) {
          std::cerr << "Needle update added an early history sample\n"; return 1;
        }
        small = max(small, pixels);
        if (pixels >= 18000) { std::cerr << "Small update repainted too much: " << pixels << '\n'; return 1; }
        for (int i = 0; i < after.recordedFlushCount; ++i)
          if (after.flushes[i].y + after.flushes[i].height > 163 && after.flushes[i].y <= 197) {
            std::cerr << "Dial update damaged graph\n"; return 1;
          }
      }
      std::vector<lv_color_t> incremental(framebuffer(), framebuffer() + 320 * 240);
      lv_obj_invalidate(screen);
      lv_refr_now(display());
      if (memcmp(incremental.data(), framebuffer(), incremental.size() * sizeof(lv_color_t))) {
        std::cerr << "Efficiency incremental mismatch light=" << light << " step=" << step << '\n';
        if (argc > 1) capturePpm(std::filesystem::path(argv[1]) / "efficiency_mismatch.ppm");
        return 1;
      }
    }
    if (previewEfficiencyHistoryCount() != 300) {
      std::cerr << "Five-minute history did not fill and wrap\n"; return 1;
    }
    if (argc > 1) capturePpm(std::filesystem::path(argv[1]) / (light ? "efficiency_light.ppm" : "efficiency_dark.ppm"));
  }
  std::cout << "Efficiency: 6400 incremental frames match full redraws; small_max=" << small
            << " worst=" << worst << " pixels\n";
}
