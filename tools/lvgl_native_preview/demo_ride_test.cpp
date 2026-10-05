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
  check(close(demoRideSeconds(),15) && close(makeDummyValues().watts, 1500, 120), "1x clock / peak");
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
    // Power steps at keyframes, so integrate it at tenth-second midpoints.
    const auto mid=demoRideAt((i-.5F)*.1F,false);
    wh+=mid.watts*.1/3600;
    regen+=std::max(0.0F,-mid.watts)*.1/3600;
    prev=r;
  }
  check(close(prev.km,(float)km,.002F) && close(prev.wh,(float)wh,.1F), "integrated ride disagrees with readouts");
  demoTimeScale=60; advance(40000);
  auto stats=makeDemoBatteryStats();
  check(close(stats.tripRegenWh,(float)regen,.1F), "regen integration disagrees");
  check(close(stats.learnedCapacityAh,10.5F), "wrong battery capacity");
  // Running the pack empty must start a new ride rather than freeze.
  float last=demoRideSeconds(), lastWh=0, lastKm=0, lastAvg=0; bool wrapped=false;
  for (int i=0;i<200 && !wrapped;++i) {
    advance(1000);
    const float now=demoRideSeconds();
    if (now<last) { wrapped=true; break; }
    last=now; lastWh=makeDemoBatteryStats().tripWh;
    lastKm=makeDummyValues().tripKm; lastAvg=makeDummyValues().avgSpeedKmh;
  }
  check(wrapped, "empty ride did not restart");
  check(lastWh > 400 && lastWh <= 505.06F, "wrong usable demo energy");
  check(lastAvg > 20 && lastAvg < 29, "average speed outside intended range");
  const auto fresh=makeDummyValues();
  check(demoRideSeconds()<61 && fresh.tripKm<lastKm && fresh.batteryPercent>=95 && makeDemoBatteryStats().tripWh<lastWh, "restarted ride kept old trip");
  const float end=last;
  restartDemoRide();
  check(demoRideSeconds()==0 && makeDemoBatteryStats().tripWh==0 && makeDummyValues().batteryPercent==100, "restart failed");
  // Power follows speed: drive power while cruising, regen only while slowing.
  check(demoRideAt(25,false).watts>300 && demoRideAt(25,false).watts<600, "cruise power");
  check(demoRideAt(110,false).watts<0 && demoRideAt(117,false).watts>0, "regen / slow-roll power");
  // Launches from rest once, then never stops: every later moment stays 5-10 km/h or faster.
  check(demoRideAt(0,false).speedKmh==0 && demoRideAt(10,false).speedKmh>5, "ride does not launch from rest");
  float slowest=1000;
  for (float t=16; t<demoRideAt(1e6F,false).seconds; t+=.5F) {
    const float v=demoRideAt(t,false).speedKmh; slowest=std::min(slowest,v);
  }
  check(slowest>=5 && slowest<=10, "ride stops or dips outside 5-10 km/h");
  check(demoRidePeak().watts>=demoRideAt(15,false).watts, "peak below lap peak");
  // Later laps differ from the first, but the same moment always reads the same.
  check(std::fabs(demoRideAt(135,false).speedKmh-demoRideAt(15,false).speedKmh)>.05F, "laps identical");
  check(demoRideAt(135,false).speedKmh<=demoRidePeak().speedKmh, "lap above speed peak");
  check(demoRideAt(500).km==demoRideAt(500).km && demoRideAt(500,false).watts==demoRideAt(500).watts, "ride not deterministic");
  // Jitter perturbs live readings between ticks but never a stopped bike.
  setDemoMode(false); setDemoMode(true); demoTimeScale=1;
  int distinct=0, prevWatts=-1;
  for (int i=0;i<300;++i) { advance(100); if (demoRideSeconds()>=20 && demoRideSeconds()<30) {
    const int w=makeDummyValues().watts; if (w!=prevWatts) ++distinct; prevWatts=w; } }
  check(distinct>20, "no sensor jitter");
  setDemoMode(false); setDemoMode(true);
  check(makeDummyValues().watts==0 && makeDummyValues().speedKmh==0, "stopped bike jittered");
  demoTimeScale=60;  // the menu test below cycles from the fastest rate
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
            << end/60 << " min, " << lastKm << " km, " << lastAvg << " km/h\n";
}
