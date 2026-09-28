#include <cmath>
#include <iostream>
#include "app_state.h"
#include "controller_manager.h"
#include "host_runtime.h"
#include "Preferences.h"
#include "screens.h"
extern void previewSetInteractiveMode(bool);
extern void previewSetMillis(uint32_t);
static void check(bool ok, const char *message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static bool close(float a, float b, float tolerance = 0.01F) { return std::fabs(a-b) < tolerance; }
static void advance(uint32_t ms) { previewSetMillis(millis() + ms); serviceDemoMode(); }
static void tap(int x, int y) {
  cyd::preview::setPointer(true,x,y); cyd::preview::advanceTime(30);
  cyd::preview::setPointer(false,x,y); cyd::preview::advanceTime(200);
}
int main() {
  cyd::preview::initRuntime();
  previewPreferencesConfigure(nullptr, true);
  previewSetInteractiveMode(true);
  loadAppSettings();
  demoTimeScale = 1;
  setDemoMode(true);
  const auto full = makeDummyValues();
  check(full.batteryPercent == 100 && close(full.voltage,54.6F) && full.tripKm == 0, "full pack initial state");
  advance(15000);
  check(close(demoRideSeconds(),15) && makeDummyValues().watts == 1500, "1x clock / peak");
  const auto before = makeDummyValues();
  for (int i=0;i<1000;++i) { makeDummyValues(); makeDemoBatteryStats(); controllerSnapshot(); }
  check(close(demoRideSeconds(),15) && makeDummyValues().tripKm == before.tripKm, "reads advanced clock");
  cycleDemoTimeScale(); advance(1000);
  check(demoTimeScale == 5 && close(demoRideSeconds(),20), "rate switch applied retroactively");
  setDemoPreview(true); advance(1000);
  check(close(demoRideSeconds(),5) && controllerSnapshot().values.uptimeSeconds == 25, "preview mixed with dashboard");
  setDemoPreview(false);
  check(close(demoRideSeconds(),25), "preview replaced dashboard session");
  setDemoMode(false); advance(5000);
  check(close(demoRideSeconds(),25), "disabled demo advances");
  setDemoMode(true);
  check(demoRideSeconds()==0, "enable should start fresh ride");
  // Independent numerical integration of instantaneous output checks the trip integrals.
  double km=0, wh=0, regen=0;
  auto prev=demoRideAt(0);
  for (int i=1;i<=24000;++i) {
    auto r=demoRideAt(i * .1F);
    km+=(prev.speedKmh+r.speedKmh)*.05/3600;
    wh+=(prev.watts+r.watts)*.05/3600;
    regen+=(std::max(0.0F,-prev.watts)+std::max(0.0F,-r.watts))*.05/3600;
    prev=r;
  }
  check(close(prev.km,(float)km,.002F) && close(prev.wh,(float)wh,.01F), "integrated ride disagrees with readouts");
  demoTimeScale=60; advance(40000);
  auto stats=makeDemoBatteryStats();
  check(close(stats.tripRegenWh,(float)regen,.01F), "regen integration disagrees");
  check(close(stats.learnedCapacityAh,10.5F), "wrong battery capacity");
  advance(200000);
  auto empty=makeDummyValues(); stats=makeDemoBatteryStats();
  check(empty.speedKmh==0 && empty.watts==0 && empty.batteryPercent==0 && stats.rangeKm==0, "empty pack did not stop");
  check(close(stats.tripWh,505.05F,.01F), "wrong usable demo energy");
  check(empty.avgSpeedKmh > 24 && empty.avgSpeedKmh < 28, "average speed outside intended range");
  const float end=demoRideSeconds(); advance(200000);
  check(demoRideSeconds()==end && makeDummyValues().tripKm==empty.tripKm, "empty ride looped");
  restartDemoRide();
  check(demoRideSeconds()==0 && makeDemoBatteryStats().tripWh==0 && makeDummyValues().batteryPercent==100, "restart failed");
  // Exercise actual menu hit targets and persistence.
  submenuType=SUBMENU_DEMO; uiShow(SCREEN_SUBMENU); cyd::preview::advanceTime(300);
  tap(160,135); check(demoTimeScale==1,"time compression menu tap");
  saveAppSettings(); demoTimeScale=30; loadAppSettings(); check(demoTimeScale==1,"compression not persisted");
  tap(160,75); check(!dashboardDemoModeEnabled,"demo toggle menu tap");
  tap(160,75); advance(20000); tap(160,195);
  check(demoRideSeconds()==0,"restart menu tap");
  setDemoMode(false); setDemoPreview(true);
  uiShow(SCREEN_DASHBOARD);
  check(!demoModeIsActive(), "leaving preview leaked demo state");
  setDemoMode(true); demoTimeScale=1;
  for (int i=0;i<10000;++i) advance(100);
  check(close(demoRideSeconds(),1000), "small timer steps drifted");
  setDemoMode(false); previewSetMillis(0xffffff00U); setDemoMode(true); advance(1000);
  check(close(demoRideSeconds(),1), "millis wrap broke demo clock");
  std::cout << "Demo full-to-empty, clocks, integration, preview isolation and menu passed. Ride "
            << end/60 << " min, " << empty.tripKm << " km, " << empty.avgSpeedKmh << " km/h\n";
}
