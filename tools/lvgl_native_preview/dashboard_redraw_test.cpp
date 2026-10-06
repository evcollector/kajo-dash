#include <cstring>
#include <iostream>
#include <vector>

#include "Preferences.h"
#include "app_state.h"
#include "dashboards.h"
#include "host_runtime.h"
#include "ui_common.h"
#include "src/draw/sw/lv_draw_sw_gradient.h"

// Exercise incremental damage against a full redraw of the same live objects.
// Pixel/flush counts describe SPI workload, not measured ESP32 frame time.

// What one 100 ms tick put on the display: the update, then LVGL's timers in 10 ms slices, so the
// needles, rings and bars that glide between readings move too. Counts add up over the tick's frames.
struct TickMetrics {
  bool changed = false;
  uint32_t pixels = 0;
  uint32_t flushes = 0;
  uint32_t largestFrame = 0;
};

static TickMetrics runTick(DashboardMode mode, const DashboardValues &values) {
  using namespace cyd::preview;
  TickMetrics tick;
  updateDashboardMode(mode, values, true);
  for (int slice = 0; slice < 10; ++slice) {
    FrameMetrics before, after;
    latestFrameMetrics(before);
    advanceTime(10);
    lv_refr_now(display());  // an animation step may have invalidated since the refresh timer last ran
    latestFrameMetrics(after);
    if (before.frameNumber == after.frameNumber) continue;
    tick.changed = true;
    tick.pixels += after.flushedPixels;
    tick.flushes += after.flushCount;
    tick.largestFrame = max(tick.largestFrame, after.flushedPixels);
  }
  return tick;
}

int main(int argc, char **argv) {
  using namespace cyd::preview;
  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  loadAppSettings();
  bool ok = true;
  std::cout << "mode,appearance,language,step,pixels,flushes,hash\n";
  // Every theme but Efficiency, whose plot scrolls on a clock and has its own test.
  for (DashboardMode mode : {MODE_HUD, MODE_GAUGE, MODE_SIMPLE, MODE_BARS, MODE_MOTOR_DATA, MODE_PIXEL_GAUGE,
                             MODE_LARGE_TILES, MODE_BIG_READOUT, MODE_REDLINE, MODE_TRACE, MODE_MINIMAL}) {
    for (bool light : {false, true}) {
      for (Language lang : {LANG_EN, LANG_FI, LANG_DE}) {
        language = lang;
        dashboardAppearanceMode = light ? DASH_APPEARANCE_LIGHT : DASH_APPEARANCE_DARK;
        dashboardMode = mode;
        lv_obj_t *old = lv_scr_act();
        lv_obj_t *screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(screen);
        makePassive(screen);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_scr_load(screen);
        lv_obj_del(old);
        DashboardValues values = makeDummyValues();
        buildDashboardMode(screen, mode, values);
        previewFinishStartupSweep();
        updateDashboardMode(mode, values, true);
        refreshNow();
        for (int step = 0; step < 103; ++step) {
          // Rising/falling meters, large jumps, zero, and sub-pixel power jitter.
          values.speedKmh = step < 60 ? (step <= 30 ? step * 4 : (60 - step) * 4) : 25;
          values.watts = step < 60 ? values.speedKmh * 100 : 1000 + step % 10;
          if (step >= 100) {
            values.speedKmh = step == 100 ? 999 : step == 101 ? 0 : 25;
            values.watts = step == 100 ? 100000 : step == 101 ? 0 : 1000;
          }
          // capturePpm/refreshNow intentionally invalidate the entire screen.
          // Service only accumulated damage when measuring a live update.
          const TickMetrics tick = runTick(mode, values);
          const bool changed = tick.changed;
          if (mode == MODE_GAUGE && changed && tick.largestFrame >= 320 * 240) {
            std::cerr << "Dual Gauge update fell back to a full-screen repaint\n";
            ok = false;
          }
          if (mode == MODE_HUD && step == 1 && tick.largestFrame >= 320 * 108) {
            std::cerr << "Small HUD update repainted at least the entire meter housing\n";
            ok = false;
          }
          std::vector<lv_color_t> incremental(framebuffer(), framebuffer() + 320 * 240);
          if (mode == MODE_HUD && light) {
            for (auto pixel : incremental) {
              lv_color32_t rgb;
              rgb.full = lv_color_to32(pixel);
              // RGB565 quantization gives neutral antialiased edges a few
              // levels of channel difference; actual accent hues must be gone.
              const int high = max<int>(rgb.ch.red, max<int>(rgb.ch.green, rgb.ch.blue));
              const int low = min<int>(rgb.ch.red, min<int>(rgb.ch.green, rgb.ch.blue));
              if (high - low > 8) {
                std::cerr << "Default light HUD contains a colored pixel\n";
                ok = false;
                break;
              }
            }
          }
          uint64_t hash = 14695981039346656037ULL;
          for (auto pixel : incremental) { hash ^= pixel.full; hash *= 1099511628211ULL; }
          std::cout << static_cast<int>(mode) << ',' << light << ',' << static_cast<int>(lang)
                    << ',' << step << ',' << (changed ? tick.pixels : 0)
                    << ',' << (changed ? tick.flushes : 0) << ',' << hash << '\n';
          if (argc > 1 && lang == LANG_EN && step == 7)
            capturePpm(std::filesystem::path(argv[1]) /
                       (std::to_string(mode) + (light ? "_light.ppm" : "_dark.ppm")));
          lv_obj_invalidate(screen);
          refreshNow();
          if (std::memcmp(incremental.data(), framebuffer(), incremental.size() * sizeof(lv_color_t))) {
            int differing = 0, worst = 0;
            for (int i = 0; i < 320 * 240; ++i) {
              const uint32_t a = lv_color_to32(incremental[i]), b = lv_color_to32(framebuffer()[i]);
              if (a == b) continue;
              ++differing;
              for (int shift : {0, 8, 16}) worst = max(worst, std::abs(int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF)));
            }
            // Segmented rings are arcs, and LVGL's arc mask antialiases a pixel or two differently
            // depending on where a repainted rectangle starts: two pixels, 9/255 at most, as in
            // cyd_seg_ring. A stale block would differ by 100 or more. Everything else must be exact.
            const bool arcNoise = mode == MODE_MOTOR_DATA && differing <= 16 && worst <= 24;
            if (!arcNoise) {
              std::cerr << "Incremental/full mismatch: mode=" << mode << " light=" << light << " lang=" << lang
                        << " step=" << step << " pixels=" << differing << " worst=" << worst << '\n';
              ok = false;
            }
          }
        }
        // Not idle yet: a glide under way (a one-reading step takes up to a second), and the 160 ms
        // damping of the meters that follow the live value, both go on while the readings repeat.
        for (int settle = 0; settle < 20; ++settle) runTick(mode, values);
        if (runTick(mode, values).changed) {
          std::cerr << "Unchanged dashboard values caused a repaint: mode=" << mode << '\n';
          ok = false;
        }
      }
    }
  }
  // Explicit accents must remain colored, including in the new light HUD.
  for (bool light : {false, true}) {
    language = LANG_EN;
    accentTheme = ACCENT_BLUE;
    dashboardAppearanceMode = light ? DASH_APPEARANCE_LIGHT : DASH_APPEARANCE_DARK;
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    makePassive(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_scr_load(screen);
    lv_obj_del(old);
    buildDashboardMode(screen, MODE_HUD, makeDummyValues());
    previewFinishStartupSweep();
    refreshNow();
    int bluePixels = 0;
    for (int i = 0; i < 320 * 240; ++i) {
      lv_color32_t rgb;
      rgb.full = lv_color_to32(framebuffer()[i]);
      if (rgb.ch.blue > rgb.ch.red + 40) ++bluePixels;
    }
    if (bluePixels < 100) {
      std::cerr << "Explicit blue HUD accent was lost\n";
      ok = false;
    }
    if (argc > 1)
      capturePpm(std::filesystem::path(argv[1]) / (light ? "blue_light.ppm" : "blue_dark.ppm"));
  }
  accentTheme = ACCENT_DEFAULT;
  // Compare gradient cache output to uncached rendering, including recolors
  // and equal-size opposing bell halves that can expose cache-key collisions.
  for (int variant = 0; variant < 8; ++variant) {
    dashboardGradientEnabled = true;
    dashboardGradientHorizontal = (variant & 1) != 0;
    dashboardGradientBell = (variant & 2) != 0;
    dashboardGradientReverse = (variant & 4) != 0;
    dashboardGradientPosition = 50;
    dashboardGradientTheme = variant % 2 ? ACCENT_BLUE : ACCENT_ORANGE;
    dashboardAppearanceMode = DASH_APPEARANCE_DARK;
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    makePassive(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_scr_load(screen);
    lv_obj_del(old);
    buildDashboardMode(screen, MODE_HUD, makeDummyValues());
    previewFinishStartupSweep();
    refreshNow();
    std::vector<lv_color_t> cached(framebuffer(), framebuffer() + 320 * 240);
    lv_gradient_set_cache_size(0);
    refreshNow();
    if (std::memcmp(cached.data(), framebuffer(), cached.size() * sizeof(lv_color_t))) {
      std::cerr << "Gradient cache changed pixels: variant=" << variant << '\n';
      ok = false;
    }
    lv_gradient_set_cache_size(LV_GRAD_CACHE_DEF_SIZE);
    refreshNow();
  }
  return ok ? 0 : 1;
}
