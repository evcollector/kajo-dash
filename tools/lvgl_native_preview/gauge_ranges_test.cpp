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
  GaugeRangeTracker range; range.ceiling=30;
  check(!range.observe(500,100,1000,1000), "one spike expanded range");
  range.observe(10,200,1000,1000);
  check(range.ceiling==30,"spike persisted");
  for (uint32_t now=300; now<1300; now+=100) check(!range.observe(28,now,1000,1000),"expanded too early");
  check(range.observe(28,1300,1000,1000) && range.ceiling==40,"sustained near-ceiling did not expand");
  for (int i=0;i<100;++i) range.observe(0,1400+i*100,1000,1000);
  check(range.ceiling==40,"range shrank while coasting");
  range.observe(200,12000,1000,1000); range.observe(200,12000,1000,1000);
  check(range.ceiling==40,"duplicate packet learned");
  check(range.observe(200,12100,1000,1000),"corroborated overrun failed");
  GaugeRangeTracker wrap; wrap.ceiling=30; wrap.observe(80,0xffffffc0U,1000,1000);
  check(wrap.observe(80,64,1000,1000),"timestamp wrap failed");
  GaugeRangeVisual visual;
  visual.reset(30);
  check(visual.advance(40,1000)==30,"visual scale jumped at start");
  check(visual.advance(40,1450)==35,"visual scale did not move gradually");
  check(visual.advance(40,1900)==40,"visual scale did not reach learned ceiling");
  visual.reset(1000);
  visual.advance(1500,2000);
  const int inFlight = visual.advance(1500,2300);
  check(inFlight>1000 && inFlight<1500,"power range jumped");
  check(visual.advance(2000,2300)==inFlight,"second expansion jumped");
  check(visual.advance(2000,3200)==2000,"second expansion did not settle");
  check(visual.advance(30,3300)==30,"reset did not snap down");
  visual.reset(30);
  visual.advance(40,0xffffff00U);
  check(visual.advance(40,0x00000284U)==40,"visual transition failed across millis wrap");
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
