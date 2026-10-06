#include <cstring>
#include <iostream>
#include <vector>
#include "gauge_range_model.h"
#include "gauge_range_visual.h"
#include "Preferences.h"
#include "app_state.h"
#include "controller_manager.h"
#include "dashboards.h"
#include "host_runtime.h"
#include "screens.h"
#include "ui_common.h"
extern void previewSetMillis(uint32_t);
static void check(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
static void tap(int x, int y) {
  cyd::preview::setPointer(true,x,y); cyd::preview::advanceTime(30);
  cyd::preview::setPointer(false,x,y); cyd::preview::advanceTime(200);
}
int main() {
  // The ceiling is pushed up by a confirmed reading, to the exact peak, and never by one packet.
  auto fresh = [](int start) { GaugeRangeTracker t; t.ceiling = t.floorValue = start; return t; };
  GaugeRangeTracker range = fresh(30);
  uint32_t now = 1000;
  auto feed = [&](GaugeRangeTracker &t, float value, bool riding = true, uint32_t step = 100) {
    now += step; return t.observe(value, now, riding, 1000);
  };
  feed(range, 10); feed(range, 500); feed(range, 10);
  check(range.ceiling==30,"one spike moved the ceiling");
  feed(range, 29); feed(range, 30);
  check(range.ceiling==30,"reading at the ceiling pushed it");
  feed(range, 31);
  check(range.ceiling==30,"a single reading past the ceiling pushed it");
  feed(range, 31);
  check(range.ceiling==31,"ceiling did not follow the reading past it");
  feed(range, 33); feed(range, 35); feed(range, 35);
  check(range.ceiling==35,"ceiling did not follow the peak");
  for (int i=0;i<20;++i) feed(range, 20);
  check(range.ceiling==35,"ceiling changed after the peak with the window still full");
  feed(range, 35.2F); feed(range, 35.2F);
  check(range.ceiling==36,"fractional peak not rounded up to a whole unit");
  GaugeRangeTracker overrun = fresh(30);
  feed(overrun, 200); check(overrun.ceiling==30,"first overrun packet learned");
  feed(overrun, 200); check(overrun.ceiling==200,"confirmed overrun failed");
  GaugeRangeTracker duplicate = fresh(30);
  feed(duplicate, 200, true, 100); now -= 100; feed(duplicate, 200, true, 0);
  check(duplicate.ceiling==30,"duplicate packet learned");
  GaugeRangeTracker bad = fresh(30);
  feed(bad, 5000); feed(bad, 5000); feed(bad, -3); feed(bad, NAN);
  check(bad.ceiling==30,"implausible measurement learned");
  GaugeRangeTracker gap = fresh(30);
  feed(gap, 90); now += 5000; feed(gap, 90, true, 0);
  check(gap.ceiling==30,"reading across a telemetry gap confirmed itself");
  GaugeRangeTracker wrap = fresh(30);
  wrap.observe(80,0xffffffc0U,true,1000);
  check(wrap.observe(80,64,true,1000) && wrap.ceiling==80,"timestamp wrap failed");

  // The scale falls back once the peak ages out of 15 minutes of riding, and only while the
  // reading is low.
  GaugeRangeTracker aging = fresh(30);
  now = 1000; feed(aging, 80); feed(aging, 80);
  check(aging.ceiling==80,"peak not learned");
  for (int i=0; i<60*14; ++i) feed(aging, 5, true, 1000);
  check(aging.ceiling==80,"scale shrank inside the window");
  for (int i=0; i<60*2; ++i) feed(aging, 5, false, 1000);
  check(aging.ceiling==80,"standing still aged the window");
  for (int i=0; i<90; ++i) feed(aging, 25, true, 1000);
  check(aging.ceiling==80,"scale shrank while the reading was high");
  for (int i=0; i<5; ++i) feed(aging, 5, true, 1000);
  check(aging.ceiling==30,"scale did not shrink once the reading was low");
  GaugeRangeTracker hysteresis = fresh(30);
  now = 1000; feed(hysteresis, 33); feed(hysteresis, 33);
  for (int i=0; i<60*16; ++i) feed(hysteresis, 5, true, 1000);
  check(hysteresis.ceiling==33,"scale shrank by less than 15%");
  GaugeRangeTracker seeded = fresh(30);
  seeded.seed(80);
  check(seeded.ceiling==80,"seed ignored");
  now = 1000;
  for (int i=0; i<60*14; ++i) feed(seeded, 5, true, 1000);
  check(seeded.ceiling==80,"seed aged early");
  for (int i=0; i<60*2; ++i) feed(seeded, 5, true, 1000);
  check(seeded.ceiling==30,"seed never aged out");
  GaugeRangeTracker slow = fresh(40);
  slow.smoothMs = 3000; now = 1000;
  for (int i=0; i<20; ++i) feed(slow, 400);  // a 2 s launch transient
  check(slow.ceiling < 250,"slow quantity learned a short transient in full");  // about half of it by 2 s

  // The displayed ceiling follows a push at once and eases down when the learned one falls.
  GaugeRangeVisual visual;
  visual.reset(30);
  check(visual.advance(35,1000)==35,"displayed ceiling did not follow a push");
  check(visual.advance(36,1100)==36,"displayed ceiling lagged a second push");
  visual.reset(80);
  check(visual.advance(30,1000)==80,"shrink jumped at start");
  const int mid = visual.advance(30,2000);
  check(mid<80 && mid>30,"shrink did not ease");
  check(visual.advance(30,2500)<=mid,"shrink went backwards");
  check(visual.advance(30,3200)==30,"shrink did not settle");
  visual.reset(80);
  visual.advance(30,1000);
  const int partway = visual.advance(30,1800);
  check(visual.advance(partway+5,1900)==partway+5,"push did not cancel a shrink");
  visual.reset(80);
  visual.advance(30,0xffffff00U);
  check(visual.advance(30,0x00000900U)==30,"shrink failed across millis wrap");
  GaugeValueVisual needle;
  needle.reset(0,1000);
  const float firstMove = needle.advance(100,1100);
  check(firstMove>0 && firstMove<100,"indicator did not damp first sample");
  check(needle.advance(100,1200)>firstMove,"indicator did not keep moving");
  check(needle.advance(100,1900)==100,"indicator stayed stale after telemetry gap");
  check(needle.advance(-100,2000,true)==0,"regen transition showed old drive power");
  using namespace cyd::preview;
  initRuntime(); previewPreferencesConfigure(nullptr,true); loadAppSettings();
  automaticGaugeRanges=true; controllerType=CONTROLLER_VESC; controllerConnection=CONTROLLER_CONNECTION_UART;
  learnedGaugeSpeed[0]=80; saveAppSettings(); learnedGaugeSpeed[0]=30; loadAppSettings();
  check(speedScaleMaxKmh()==80,"learned speed not restored");
  controllerType=CONTROLLER_FARDRIVER; check(speedScaleMaxKmh()==30,"backend ranges leaked");
  setDemoPreview(true);
  for (int mode=0; mode<MODE_COUNT; ++mode) {
    resetAutomaticGaugeRanges();
    auto *old=lv_scr_act(); auto *screen=lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen); makePassive(screen); lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_scr_load(screen); lv_obj_del(old);
    DashboardValues v={}; v.speedKmh=80; v.watts=5000; v.current=100; v.motorCurrent=200;
    buildDashboardMode(screen,(DashboardMode)mode,v); previewFinishStartupSweep();
    updateDashboardMode((DashboardMode)mode,v,true); refreshNow();
    previewSetMillis(millis()+200);
    updateDashboardMode((DashboardMode)mode,v,true); lv_refr_now(display());
    check(speedScaleMaxKmh()>=80 && automaticGaugeMaximum(RANGE_POWER)>=5000,"theme failed to learn");
    std::vector<lv_color_t> incremental(framebuffer(),framebuffer()+320*240);
    refreshNow();
    if (std::memcmp(incremental.data(),framebuffer(),incremental.size()*sizeof(lv_color_t))) {
      std::cerr << "Range-change incremental rendering mismatch, theme " << mode << '\n'; return 1;
    }
    for (int frame=0; frame<6; ++frame) {
      previewSetMillis(millis()+150);
      updateDashboardMode((DashboardMode)mode,v,true); lv_refr_now(display());
      incremental.assign(framebuffer(),framebuffer()+320*240);
      refreshNow();
      if (std::memcmp(incremental.data(),framebuffer(),incremental.size()*sizeof(lv_color_t))) {
        std::cerr << "Range-transition incremental rendering mismatch, theme " << mode
                  << ", frame " << frame << '\n'; return 1;
      }
    }
  }
  setDemoPreview(false); check(speedScaleMaxKmh()==30,"preview affected live ranges");
  submenuType=SUBMENU_GAUGE_RANGES; uiShow(SCREEN_SUBMENU); advanceTime(300);
  tap(160,75); check(!automaticGaugeRanges,"manual menu toggle");
  topSpeedKmh=123; check(speedScaleMaxKmh()==123,"manual limit ignored");
  tap(160,75); check(automaticGaugeRanges,"auto menu toggle");
  learnedGaugeSpeed[2]=100; tap(160,135); check(speedScaleMaxKmh()==30,"reset menu failed");
  std::cout << "Gauge learning, visual transitions, all-theme redraws and menu passed\n";
}
