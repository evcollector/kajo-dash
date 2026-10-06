#include <cstdint>
#include <iostream>

#include "Preferences.h"
#include "app_state.h"
#include "controller_manager.h"
#include "host_runtime.h"
#include "firmware_update_ble.h"
#include "screens.h"
#include "ride_replay.h"
extern void previewSetCardState(bool ready, bool checking);
extern void previewRestoreRideLogs();
extern void previewSetLoggingState(RideLoggingMode mode, bool recording);
extern void previewSetTelemetryLink(TelemetryLink state);
extern void previewSetWipeStatus(RideLogWipeStatus::State state, uint32_t removed, uint32_t total, bool pinned);
#include "vehicle_fields.h"

extern void previewSetInteractiveMode(bool enabled);
extern void previewSetLightSensor(int raw, int targetPercent);
extern void previewSetFirmwareUpdateActive(bool active);
extern void previewFarDriverReceivePacket();
extern void previewSetFarDriverConnected(bool connected);
extern void previewSetFirmwareUpdateState(FirmwareUpdateBleState state, bool configured, uint32_t received,
                                          uint32_t total, const char *message);

namespace {

bool hasLabel(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && strcmp(lv_label_get_text(object), text) == 0) return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); i++)
    if (hasLabel(lv_obj_get_child(object, i), text)) return true;
  return false;
}

lv_obj_t *findLabel(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && strcmp(lv_label_get_text(object), text) == 0) return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); i++)
    if (lv_obj_t *found = findLabel(lv_obj_get_child(object, i), text)) return found;
  return nullptr;
}

// Vertical centre of a label, or -1 when it is not on screen.
int labelY(const char *text) {
  lv_obj_t *label = findLabel(lv_scr_act(), text);
  if (!label) return -1;
  lv_area_t area; lv_obj_get_coords(label, &area);
  return (area.y1 + area.y2) / 2;
}

void tap(int x, int y) {
  cyd::preview::setPointer(true, x, y);
  cyd::preview::advanceTime(30);
  cyd::preview::setPointer(false, x, y);
  cyd::preview::advanceTime(30);
}

// Taps the centre of the control showing this label, then lets deferred
// deletions and rebuilds settle. Returns false when the label is not shown.
bool tapLabel(const char *text) {
  lv_obj_t *label = findLabel(lv_scr_act(), text);
  if (!label) return false;
  lv_area_t area; lv_obj_get_coords(label, &area);
  tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
  cyd::preview::advanceTime(100);
  return true;
}

bool expectScreen(ScreenMode expected, const char *stage) {
  if (uiCurrentScreen() == expected) return true;
  std::cerr << "runtime input test failed after " << stage << ": expected screen "
            << static_cast<int>(expected) << ", got " << static_cast<int>(uiCurrentScreen()) << '\n';
  return false;
}

bool expect(bool condition, const char *message) {
  if (condition) return true;
  std::cerr << "runtime input test failed: " << message << '\n';
  return false;
}

}  // namespace

int main() {
  cyd::preview::initRuntime();
  previewPreferencesConfigure(nullptr, true);
  previewSetInteractiveMode(true);
  loadAppSettings();

  // Backend ids are persisted, so registry order must not influence selection.
  ControllerType selectedType = CONTROLLER_VESC;
  ControllerConnection selectedConnection = CONTROLLER_CONNECTION_UART;
  controllerSelectionForId(CONTROLLER_ID_FARDRIVER_BLE, selectedType, selectedConnection);
  if (!expect(selectedType == CONTROLLER_FARDRIVER && selectedConnection == CONTROLLER_CONNECTION_BLE,
              "stable FarDriver backend id did not restore its selection") ||
      !expect(controllerBackendIdFor(selectedType, selectedConnection) == CONTROLLER_ID_FARDRIVER_BLE,
              "controller selection did not round-trip through the stable id"))
    return 1;
  controllerType = CONTROLLER_FARDRIVER;
  controllerConnection = CONTROLLER_CONNECTION_BLE;
  saveAppSettings();
  controllerType = CONTROLLER_VESC;
  controllerConnection = CONTROLLER_CONNECTION_UART;
  loadAppSettings();
  if (!expect(controllerType == CONTROLLER_FARDRIVER && controllerConnection == CONTROLLER_CONNECTION_BLE,
              "stable backend id did not survive an application-settings reload"))
    return 1;
  controllerType = CONTROLLER_VESC;
  controllerConnection = CONTROLLER_CONNECTION_UART;
  previewSetControllerTelemetryFields(TELEMETRY_FIELDS_ALL & ~TELEMETRY_FIELD_ODOMETER,
                                      TELEMETRY_FIELDS_ALL & ~TELEMETRY_FIELD_ODOMETER);
  if (!expect(!telemetryHas(controllerSnapshot().available, TELEMETRY_FIELD_ODOMETER),
              "snapshot did not preserve an unavailable field"))
    return 1;
  previewSetControllerTelemetryFields(TELEMETRY_FIELDS_ALL, TELEMETRY_FIELDS_ALL);
  firstBootConfigured = true;
  uiShow(SCREEN_DASHBOARD);
  cyd::preview::advanceTime(20);
  if (!expectScreen(SCREEN_DASHBOARD, "boot")) return 1;

  // A dashboard tap reveals the real animated settings control. The second
  // tap activates that LVGL button; no preview setter skips the navigation.
  tap(160, 120);
  cyd::preview::advanceTime(200);
  tap(274, 22);
  cyd::preview::advanceTime(200);
  if (!expectScreen(SCREEN_MENU, "dashboard and settings taps")) return 1;

  // Basic users enter Information and tap Bluetooth Link, which is the one
  // door to the phone: the companion session and firmware updates both live
  // behind it. There is no separate update tile any more.
  tap(160, 202);
  cyd::preview::advanceTime(200);
  if (!expectScreen(SCREEN_SUBMENU, "information tile")) return 1;
  previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_READY, true, 0, 0, "Ready to enter update mode");
  tap(264, 190);
  cyd::preview::advanceTime(200);
  if (!expectScreen(SCREEN_COMPANION_MODE, "bluetooth link tile")) return 1;

  // The phone, not the rider, starts an update: it asks over the companion
  // link, companionBleService() starts update mode, and the Bluetooth Link
  // screen hands the display over on its next tick. previewSetFirmwareUpdate-
  // Active stands in for that request.
  // advanceTime only pumps lv_timer_handler; the 100 ms firmware tick that
  // notices the handoff is driven from the main loop, so run it by hand the
  // way simulator_main.cpp does.
  previewSetFirmwareUpdateActive(true);
  uiDashboardTick();
  cyd::preview::advanceTime(200);
  if (!expectScreen(SCREEN_FIRMWARE_UPDATE, "phone-requested update handoff")) return 1;
  if (firmwareUpdateBleStatus().state != FIRMWARE_UPDATE_BLE_PREPARING) {
    std::cerr << "basic update screen did not automatically start BLE receive mode\n";
    return 1;
  }

  cyd::preview::FrameMetrics metrics;
  if (!cyd::preview::latestFrameMetrics(metrics) || metrics.frameNumber == 0 || metrics.flushedPixels == 0 ||
      metrics.flushCount == 0 || metrics.spiTransferMs <= 0.0 || metrics.estimatedCydMs <= 0.0) {
    std::cerr << "runtime input test failed: completed-frame performance metrics were missing\n";
    return 1;
  }

  // Simulate continuous FarDriver packet updates while discovery is visible.
  // Deleting its controls resets touch input and can swallow Continue presses.
  previewSetFirmwareUpdateActive(false);
  uiPreviewSetControllerSetup(2, CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
  uiShow(SCREEN_SUBMENU);
  uiDashboardTick();
  cyd::preview::advanceTime(200);
  bool discoveryDeleted = false;
  lv_obj_add_event_cb(lv_obj_get_child(lv_scr_act(), 0), [](lv_event_t *event) {
    *static_cast<bool *>(lv_event_get_user_data(event)) = true;
  }, LV_EVENT_DELETE, &discoveryDeleted);
  for (int i = 0; i < 100; i++) {
    previewFarDriverReceivePacket();
    uiDashboardTick();
    cyd::preview::advanceTime(100);
    if (!expect(!discoveryDeleted, "FarDriver packet rebuilt discovery and interrupted input")) return 1;
  }
  tap(220, 216);
  cyd::preview::advanceTime(200);
  if (!expect(hasLabel(lv_scr_act(), "APPLY CONTROLLER SETUP?"), "Continue did not open setup confirmation"))
    return 1;
  uiPreviewSetControllerSetup(2, CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
  uiShow(SCREEN_SUBMENU);
  previewSetFarDriverConnected(false);
  uiDashboardTick();
  cyd::preview::advanceTime(200);
  if (!expect(!hasLabel(lv_scr_act(), "CONTINUE >"), "Disconnect left Continue available")) return 1;
  previewSetFarDriverConnected(true);
  uiDashboardTick();
  cyd::preview::advanceTime(200);
  if (!expect(hasLabel(lv_scr_act(), "CONTINUE >"), "Reconnect did not restore Continue")) return 1;

  // Applying the setup ends the wizard: it says what happened and hands the
  // display back, rather than dropping the rider in the settings menu with no
  // sign that anything took.
  uiPreviewSetControllerSetup(3, CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
  uiShow(SCREEN_SUBMENU);
  cyd::preview::advanceTime(500);
  if (!expect(tapLabel("APPLY"), "setup confirmation has no Apply")) return 1;
  cyd::preview::advanceTime(500);
  if (!expectScreen(SCREEN_DASHBOARD, "applying the controller setup") ||
      !expect(hasLabel(lv_scr_act(), "CONTROLLER SETUP SAVED"), "Apply gave no confirmation"))
    return 1;
  cyd::preview::advanceTime(3000);  // let the toast retire before the next step

  // A normal reset clears controller pairings and logging configuration while
  // preserving the ride sequence/files and battery history.
  Preferences logger;
  logger.begin("logger", false);
  logger.putUChar("mode", (uint8_t)RIDE_LOG_ON);
  logger.putUChar("rate", 10);
  logger.putUInt("nextRide", 42);
  logger.end();
  Preferences vescPair;
  vescPair.begin("vescBle", false);
  vescPair.putString("addr", "C4:7C:8D:21:40:9A");
  vescPair.end();
  Preferences farDriverPair;
  farDriverPair.begin("fardriver", false);
  farDriverPair.putString("addr", "A4:C1:38:72:19:EF");
  farDriverPair.end();
  Preferences battery;
  battery.begin("batt", false);
  battery.putUInt("reset-test", 73);
  battery.end();

  resetAppSettings(false);
  logger.begin("logger", true);
  if (!expect(logger.getUChar("mode", 0xFF) == RIDE_LOG_OFF, "normal reset did not switch logging off") ||
      !expect(logger.getUChar("rate", 0) == 5, "normal reset did not restore the logging rate") ||
      !expect(logger.getUInt("nextRide", 0) == 42, "normal reset changed the preserved ride sequence"))
    return 1;
  logger.end();
  vescPair.begin("vescBle", true);
  if (!expect(!vescPair.isKey("addr"), "normal reset kept the VESC BLE pairing")) return 1;
  vescPair.end();
  farDriverPair.begin("fardriver", true);
  if (!expect(!farDriverPair.isKey("addr"), "normal reset kept the FarDriver BLE pairing")) return 1;
  farDriverPair.end();
  battery.begin("batt", true);
  if (!expect(battery.getUInt("reset-test", 0) == 73, "normal reset cleared battery history")) return 1;
  battery.end();

  // A full reset adds ride-log/sequence and battery-history removal.
  logger.begin("logger", false);
  logger.putUChar("mode", (uint8_t)RIDE_LOG_ON);
  logger.putUChar("rate", 10);
  logger.putUInt("nextRide", 99);
  logger.end();
  battery.begin("batt", false);
  battery.putUInt("reset-test", 99);
  battery.end();
  resetAppSettings(true);
  logger.begin("logger", true);
  if (!expect(logger.getUChar("mode", 0xFF) == RIDE_LOG_OFF, "full reset did not switch logging off") ||
      !expect(logger.getUChar("rate", 0) == 5, "full reset did not restore the logging rate") ||
      !expect(logger.getUInt("nextRide", 99) == 1, "full reset did not reset the ride sequence"))
    return 1;
  logger.end();
  battery.begin("batt", true);
  if (!expect(!battery.isKey("reset-test"), "full reset kept battery history")) return 1;
  battery.end();

  // Exercise the actual Back/Next and editor return paths in the vehicle menus.
  language = LANG_EN;
  controllerType = CONTROLLER_FARDRIVER;
  controllerConnection = CONTROLLER_CONNECTION_BLE;
  uiPreviewSetSubmenu(SUBMENU_CONTROLLER_CONFIG, 0, false);
  uiShow(SCREEN_SUBMENU);
  tap(240, 90);
  if (!expect(submenuType == SUBMENU_SPEED, "Ride Modes tile did not open modes")) return 1;
  tap(275, 22);
  if (!expect(hasLabel(lv_scr_act(), "Low gear label"), "Next did not open gear labels") ||
      !expect(!hasLabel(lv_scr_act(), "NEXT >"), "final mode page still has Next")) return 1;
  tap(70, 90);
  if (!expectScreen(SCREEN_TEXT_INPUT, "mode label editor")) return 1;
  tap(40, 22);
  if (!expectScreen(SCREEN_SUBMENU, "cancel label editor") ||
      !expect(hasLabel(lv_scr_act(), "Low gear label"), "editor lost mode page")) return 1;
  tap(40, 22);
  if (!expect(hasLabel(lv_scr_act(), "REPORTED GEAR"), "Back skipped previous mode page")) return 1;
  tap(40, 22);
  if (!expect(submenuType == SUBMENU_CONTROLLER_CONFIG, "Back did not return to configuration")) return 1;

  controllerType = CONTROLLER_VESC;
  uiPreviewSetSubmenu(SUBMENU_SPEED, 0, false);
  uiShow(SCREEN_SUBMENU);
  if (!expect(!hasLabel(lv_scr_act(), "NEXT >"), "VESC offered unsupported gear labels")) return 1;
  uiPreviewSetSubmenu(SUBMENU_CONNECTION, 0, false);
  controllerConnection = CONTROLLER_CONNECTION_UART;
  uiShow(SCREEN_SUBMENU);
  if (!expect(hasLabel(lv_scr_act(), vehicleFieldTitle(VEHICLE_FIELD_VESC_BAUD)), "UART baud missing from Connection")) return 1;
  controllerConnection = CONTROLLER_CONNECTION_BLE;
  uiShow(SCREEN_SUBMENU);
  if (!expect(!hasLabel(lv_scr_act(), vehicleFieldTitle(VEHICLE_FIELD_VESC_BAUD)), "BLE displayed a UART baud editor")) return 1;

  uiPreviewSetSubmenu(SUBMENU_BATTERY, 0, false);
  uiShow(SCREEN_SUBMENU);
  tap(275, 22);
  if (!expect(hasLabel(lv_scr_act(), "PACK SETUP"), "battery Next did not open pack setup")) return 1;
  tap(40, 22);
  if (!expect(hasLabel(lv_scr_act(), "CHARGE"), "battery Back did not restore statistics")) return 1;

  saveVehicleInputValue(VEHICLE_FIELD_MODE_LABEL_1, "Eco");
  saveVehicleInputValue(VEHICLE_FIELD_MODE_LABEL_2, "Drive");
  memset(rideModeLabels, 0, sizeof(rideModeLabels));
  loadAppSettings();
  if (!expect(strcmp(rideModeName(1), "Eco") == 0 && strcmp(rideModeName(2), "Drive") == 0,
              "gear aliases did not survive settings reload") ||
      !expect(strcmp(rideModeName(0), "-") == 0 && strcmp(rideModeName(4), "-") == 0,
              "unknown gear gained a misleading label")) return 1;
  saveVehicleInputValue(VEHICLE_FIELD_MODE_LABEL_1, "");
  if (!expect(strcmp(rideModeName(1), "LOW") == 0, "blank alias did not restore default")) return 1;

  // Exercise replay through real LVGL hit testing, including asynchronous
  // screen teardown. The generated V2 fixture uses the production parser.
  previewRestoreRideLogs();
  previewSetCardState(true,false);
  uiShow(SCREEN_RIDE_LOGS);
  tap(140,72);
  if(!expectScreen(SCREEN_RIDE_REPLAY,"ride selection") ||
     !expect(rideReplayStatus().state==RideReplayStatus::Ready,"ride did not load paused")) return 1;
  DashboardValues liveBefore={},liveAfter={};
  getLiveDashboardValues(liveBefore);
  const BatteryStats batteryBefore=batteryStatsLive();
  const auto logBefore=rideLoggerStatus();
  tap(171,110);
  const auto halfway=rideReplayStatus().position;
  if(!expect(halfway>1300000 && halfway<1500000,"chart tap did not seek"))return 1;
  tap(45,221);
  if(!expect(rideReplayStatus().position==halfway-10000,"minus 10 seconds"))return 1;
  tap(272,221);
  if(!expect(rideReplayStatus().position==halfway,"plus 10 seconds"))return 1;
  tap(290,22);
  if(!expect(hasLabel(lv_scr_act(),"2x"),"playback rate control"))return 1;
  tap(160,221);
  cyd::preview::advanceTime(1000);
  auto afterPlay=rideReplayStatus().position;
  if(!expect(afterPlay>=halfway+1800 && afterPlay<=halfway+2300,"2x playback clock"))return 1;
  tap(160,221);
  cyd::preview::advanceTime(200);
  auto paused=rideReplayStatus().position;
  cyd::preview::advanceTime(500);
  if(!expect(rideReplayStatus().position==paused,"pause did not stop"))return 1;
  // Three quick taps on one skip button widen the step to 30 s until the
  // buttons rest for two seconds; slow taps never widen it.
  tap(272,221);tap(272,221);tap(272,221);
  if(!expect(rideReplayStatus().position==paused+30000 && hasLabel(lv_scr_act(),"+30 s") &&
             hasLabel(lv_scr_act(),"-30 s"),"third quick tap did not widen the skip"))return 1;
  tap(272,221);
  if(!expect(rideReplayStatus().position==paused+60000,"widened skip did not step 30 seconds"))return 1;
  tap(45,221);
  if(!expect(rideReplayStatus().position==paused+30000,"widened skip did not apply to minus"))return 1;
  cyd::preview::advanceTime(2100);
  if(!expect(hasLabel(lv_scr_act(),"+10 s") && hasLabel(lv_scr_act(),"-10 s"),"skip did not narrow after resting"))return 1;
  tap(45,221);cyd::preview::advanceTime(800);tap(45,221);cyd::preview::advanceTime(800);tap(45,221);
  if(!expect(rideReplayStatus().position==paused && hasLabel(lv_scr_act(),"-10 s"),"slow taps widened the skip"))return 1;
  for(const char *rate:{"4x","10x","50x","100x","1x","2x"}) {
    tap(290,22);
    if(!expect(hasLabel(lv_scr_act(),rate),"playback rate cycle"))return 1;
  }
  tap(160,221);cyd::preview::advanceTime(300);
  const auto beforeSkip=rideReplayStatus().position;
  tap(272,221);cyd::preview::advanceTime(500);
  if(!expect(rideReplayStatus().position>beforeSkip+10500,"skip paused playback"))return 1;
  tap(160,221);cyd::preview::advanceTime(200);
  // Chart picker: the charts are listed top to bottom in chart order; each
  // opens a field list, where a field another chart shows swaps the two. The
  // count cycles 1-4, and Done stores the layout for later rides.
  tap(247,22);cyd::preview::advanceTime(100);
  if(!expect(hasLabel(lv_scr_act(),"Number of charts:") && hasLabel(lv_scr_act(),"3") &&
             labelY("Speed")<labelY("Power") && labelY("Power")<labelY("Voltage") && labelY("Current")<0,
             "chart picker lists the charts in order"))return 1;
  const int threeHigh=labelY("Power")-labelY("Speed");
  if(!expect(tapLabel("Speed") && hasLabel(lv_scr_act(),"CHART 1") && hasLabel(lv_scr_act(),"Battery"),"a chart opens its field list"))return 1;
  if(!expect(tapLabel("Voltage") && hasLabel(lv_scr_act(),"Number of charts:") &&
             labelY("Voltage")<labelY("Power") && labelY("Power")<labelY("Speed"),"picking a shown field swaps charts"))return 1;
  // Phase current is a chartable field of its own, separate from pack current.
  if(!expect(tapLabel("Speed") && hasLabel(lv_scr_act(),"CHART 3") && hasLabel(lv_scr_act(),"Phase amps"),
             "the field list offers phase amps"))return 1;
  if(!expect(tapLabel("Phase amps") && hasLabel(lv_scr_act(),"Number of charts:") &&
             labelY("Phase amps")>labelY("Power") && !hasLabel(lv_scr_act(),"Speed"),
             "a chart can show phase amps"))return 1;
  if(!expect(hasLabel(lv_scr_act(),"1") && hasLabel(lv_scr_act(),"2") && hasLabel(lv_scr_act(),"4"),"every count is shown"))return 1;
  if(!expect(tapLabel("4") && labelY("Current")>labelY("Phase amps") && labelY("Power")-labelY("Voltage")<threeHigh,
             "four charts share the stack"))return 1;
  if(!expect(tapLabel("RESET") && labelY("Speed")<labelY("Power") && labelY("Power")<labelY("Voltage") && labelY("Current")<0,
             "Reset restores the default charts"))return 1;
  if(!expect(tapLabel("1") && labelY("Speed")>0 && labelY("Power")<0,"one chart"))return 1;
  tapLabel("DONE");
  {
    Preferences stored; uint8_t fields[4]={};
    stored.begin("replay",true);
    const uint8_t count=stored.getUChar("count",0); stored.getBytes("fields",fields,sizeof(fields));
    stored.end();
    if(!expect(count==1 && fields[0]==ride_replay::kSpeed && fields[2]==ride_replay::kVoltage && fields[3]==ride_replay::kCurrent &&
               !hasLabel(lv_scr_act(),"Number of charts:"),"Done stores the layout and closes the picker"))return 1;
  }
  // Summary: the title opens it, Done closes it.
  tap(155,22);cyd::preview::advanceTime(100);
  if(!expect(hasLabel(lv_scr_act(),"Duration") && hasLabel(lv_scr_act(),"47:23") &&
             hasLabel(lv_scr_act(),"Peak current") && hasLabel(lv_scr_act(),"Peak phase"),
             "title opens the ride summary"))return 1;
  tap(268,29);cyd::preview::advanceTime(100);
  if(!expect(!hasLabel(lv_scr_act(),"Duration"),"summary Done closes it"))return 1;
  cyd::preview::setPointer(true,100,155);cyd::preview::advanceTime(40);
  cyd::preview::setPointer(true,240,155);cyd::preview::advanceTime(40);
  cyd::preview::setPointer(false,240,155);cyd::preview::advanceTime(40);
  if(!expect(rideReplayStatus().position>2000000,"drag cursor"))return 1;
  getLiveDashboardValues(liveAfter);
  if(!expect(liveBefore.speedKmh==liveAfter.speedKmh && liveBefore.tripKm==liveAfter.tripKm &&
             batteryBefore.tripWh==batteryStatsLive().tripWh && batteryBefore.lifetimeKm==batteryStatsLive().lifetimeKm &&
             logBefore.recording==rideLoggerStatus().recording,"replay changed live telemetry or logging"))return 1;
  tap(315,100);tap(160,221);cyd::preview::advanceTime(1000);
  if(!expect(rideReplayStatus().position<2500,"play at end should restart"))return 1;
  // Seeking moves the moment being watched; only the play button stops playback.
  tap(100,155);cyd::preview::advanceTime(60);
  {
    const uint32_t seeked=rideReplayStatus().position;
    cyd::preview::advanceTime(1000);
    if(!expect(rideReplayStatus().position>seeked+500,"tapping the chart stopped playback"))return 1;
  }
  tap(160,221);cyd::preview::advanceTime(100);
  previewSetCardState(false,false);cyd::preview::advanceTime(200);
  if(!expect(rideReplayStatus().state==RideReplayStatus::Closed,"removed card left reader open"))return 1;
  tap(40,22);cyd::preview::advanceTime(500);
  if(!expectScreen(SCREEN_RIDE_LOGS,"replay Back"))return 1;
  // Logging cannot be switched on without a ready card; it says why instead.
  // Switching off never needs the card.
  const auto showLoggingMenu=[](){uiPreviewSetSubmenu(SUBMENU_LOGGING,0,false);uiShow(SCREEN_SUBMENU);
                                   cyd::preview::advanceTime(500);};
  previewSetLoggingState(RIDE_LOG_OFF,false);showLoggingMenu();
  if(!expect(tapLabel("DATA LOGGING") && rideLoggerStatus().mode==RIDE_LOG_OFF &&
             hasLabel(lv_scr_act(),"SD CARD NOT READY"),"enabling logging without a card"))return 1;
  previewSetLoggingState(RIDE_LOG_ON,false);showLoggingMenu();
  if(!expect(tapLabel("DATA LOGGING") && rideLoggerStatus().mode==RIDE_LOG_OFF,
             "disabling logging without a card"))return 1;
  previewSetCardState(true,false);showLoggingMenu();
  if(!expect(tapLabel("DATA LOGGING") && rideLoggerStatus().mode==RIDE_LOG_ON,
             "enabling logging with a ready card"))return 1;
  previewSetCardState(false,false);
  // The ride still being written is listed but cannot be opened: the writer
  // never hands out that file. Once it closes, the same row opens replay.
  previewSetCardState(true,false);previewSetLoggingState(RIDE_LOG_ON,true);uiShow(SCREEN_RIDE_LOGS);
  if(!expect(hasLabel(lv_scr_act(),"RIDE 12 | RECORDING"),"recording ride is marked in the list"))return 1;
  tap(140,72);cyd::preview::advanceTime(500);
  if(!expectScreen(SCREEN_RIDE_LOGS,"tapping the recording ride") ||
     !expect(rideReplayStatus().state==RideReplayStatus::Closed,"recording ride must not open replay"))return 1;
  previewSetLoggingState(RIDE_LOG_ON,false);
  previewSetCardState(true,false);uiShow(SCREEN_RIDE_LOGS);tap(140,72);
  if(!expect(rideReplayStatus().state==RideReplayStatus::Ready,"reopen after card replacement"))return 1;
  tap(40,22);cyd::preview::advanceTime(500);
  if(!expect(rideReplayStatus().state==RideReplayStatus::Closed,"Back did not release replay reader"))return 1;
  // Delete from the summary: Cancel returns to it; Delete removes the ride
  // and returns to the list, which no longer shows it.
  uiShow(SCREEN_RIDE_LOGS);tap(140,72);tap(155,22);cyd::preview::advanceTime(100);
  if(!expect(tapLabel("DELETE RIDE") && hasLabel(lv_scr_act(),"DELETE RIDE 12?"),"summary opens the delete confirmation"))return 1;
  if(!expect(tapLabel("CANCEL") && hasLabel(lv_scr_act(),"Duration"),"Cancel returns to the summary"))return 1;
  tapLabel("DELETE RIDE");tapLabel("DELETE");cyd::preview::advanceTime(500);
  {
    RideLogSummary first={};
    if(!expect(uiCurrentScreen()==SCREEN_RIDE_LOGS && rideReplayStatus().state==RideReplayStatus::Closed &&
               rideLoggerCatalogStatus().count==4 && rideLoggerCatalogEntry(0,first) && first.rideId==11,
               "Delete removes the ride and returns to the list"))return 1;
  }
  previewRestoreRideLogs();

  // Clear SD card: the confirmation must be cancellable, and confirming has to
  // close any open replay reader before the wipe removes the file underneath
  // it, then rebuild the list. Same screen-rebuild-from-a-popup shape as the
  // delete above, which is where a deferred delete previously crashed.
  uiShow(SCREEN_RIDE_LOGS);cyd::preview::advanceTime(100);
  if(!expect(tapLabel("CLEAR SD") && hasLabel(lv_scr_act(),"CLEAR THE SD CARD?"),
             "Clear SD opens its confirmation"))return 1;
  if(!expect(tapLabel("CANCEL") && !hasLabel(lv_scr_act(),"CLEAR THE SD CARD?") &&
             rideLoggerCatalogStatus().count==5,"Cancel left the card alone"))return 1;
  uiShow(SCREEN_RIDE_LOGS);tap(140,72);cyd::preview::advanceTime(200);
  if(!expect(rideReplayStatus().state==RideReplayStatus::Ready,"replay did not open before the wipe"))return 1;
  tap(40,22);cyd::preview::advanceTime(500);
  // Confirming keeps the dialog up with progress, says the card is clear only
  // once the wipe has finished, and then returns to the emptied list by itself.
  tapLabel("CLEAR SD");tapLabel("CLEAR");cyd::preview::advanceTime(300);
  if(!expect(hasLabel(lv_scr_act(),"CLEARING THE SD CARD") && rideReplayStatus().state==RideReplayStatus::Closed,
             "Clear SD did not show its progress"))return 1;
  if(!expect(!hasLabel(lv_scr_act(),"SD CARD CLEARED"),
             "the outcome flashed past before the progress could be seen"))return 1;
  cyd::preview::advanceTime(900);
  if(!expect(hasLabel(lv_scr_act(),"SD CARD CLEARED") && hasLabel(lv_scr_act(),"6 / 6 removed"),
             "a finished wipe did not say so"))return 1;
  cyd::preview::advanceTime(2000);
  if(!expect(uiCurrentScreen()==SCREEN_RIDE_LOGS && !hasLabel(lv_scr_act(),"SD CARD CLEARED") &&
             rideLoggerCatalogStatus().count==0 && hasLabel(lv_scr_act(),"NO SAVED RIDES"),
             "Clear SD did not return to the emptied list"))return 1;
  previewRestoreRideLogs();

  // A wipe that could not remove everything waits for OK instead of leaving.
  uiShow(SCREEN_RIDE_LOGS);cyd::preview::advanceTime(100);
  previewSetWipeStatus(RideLogWipeStatus::Failed,139,142,true);
  tapLabel("CLEAR SD");tapLabel("CLEAR");cyd::preview::advanceTime(4000);
  if(!expect(hasLabel(lv_scr_act(),"COULD NOT CLEAR THE CARD") && tapLabel("OK"),
             "a failed wipe did not wait for OK"))return 1;
  cyd::preview::advanceTime(300);
  if(!expect(uiCurrentScreen()==SCREEN_RIDE_LOGS && !hasLabel(lv_scr_act(),"COULD NOT CLEAR THE CARD"),
             "OK did not return to the list"))return 1;
  previewSetWipeStatus(RideLogWipeStatus::Idle,0,0,false);
  previewRestoreRideLogs();
  uiShow(SCREEN_RIDE_LOGS);cyd::preview::advanceTime(100);

  // Overlays parented to a screen must not outlive it. Both of these are
  // created over whatever screen is loaded, and both refuse to open while they
  // believe they are already open, so a pointer left dangling by a screen
  // rebuild silently disables them for the rest of the session -- and gets the
  // freed object deleted a second time on the next dashboard tick.
  uiShow(SCREEN_DASHBOARD);
  uiPreviewShowRecoveryHold(60);
  if(!expect(hasLabel(lv_scr_act(),"KEEP HOLDING"),"recovery hold overlay did not open"))return 1;
  uiShow(SCREEN_MENU);cyd::preview::advanceTime(200);
  if(!expect(!hasLabel(lv_scr_act(),"KEEP HOLDING"),"the overlay survived the screen change"))return 1;
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);
  uiPreviewShowRecoveryHold(60);
  if(!expect(hasLabel(lv_scr_act(),"KEEP HOLDING"),"recovery hold overlay stopped opening after a screen change"))return 1;
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);

  uiPreviewShowDeveloperPrompt();
  if(!expect(hasLabel(lv_scr_act(),"CANCEL"),"developer prompt did not open"))return 1;
  uiShow(SCREEN_MENU);cyd::preview::advanceTime(200);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);
  uiPreviewShowDeveloperPrompt();
  if(!expect(hasLabel(lv_scr_act(),"CANCEL"),"developer prompt stopped opening after a screen change"))return 1;
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);

  // Settings opens on its display page for every user, with Next to a second
  // page holding Vehicle Config, plus Developer Options only while enabled.
  // Back walks the same way out: vehicle config, second page, first page, home.
  for (uint8_t category : {1, 0}) {
    const bool developer = category == 0;
    uiPreviewSetSettingsCategory(category);
    uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);
    tap(160,120);cyd::preview::advanceTime(200);tap(274,22);cyd::preview::advanceTime(200);
    if(!expectScreen(SCREEN_MENU,"settings from the dashboard") ||
       !expect(hasLabel(lv_scr_act(),"THEME") && hasLabel(lv_scr_act(),"1/2"),
               "settings did not open on the display page"))return 1;
    tap(274,22);cyd::preview::advanceTime(200);
    if(!expect(hasLabel(lv_scr_act(),"2/2") && hasLabel(lv_scr_act(),"VEHICLE CONFIGURATION"),
               "Next did not reach the second page"))return 1;
    if(!expect(hasLabel(lv_scr_act(),"DEVELOPER OPTIONS")==developer,
               "Developer Options must show on the second page exactly when enabled"))return 1;
    if(!expect(tapLabel("VEHICLE CONFIGURATION") && hasLabel(lv_scr_act(),"CONNECTION"),
               "Vehicle Config did not open from the second page"))return 1;
    tap(40,22);cyd::preview::advanceTime(200);
    if(!expect(hasLabel(lv_scr_act(),"2/2"),"Vehicle Config did not return to the second page"))return 1;
    tap(40,22);cyd::preview::advanceTime(200);
    if(!expect(hasLabel(lv_scr_act(),"1/2"),"the second page did not return to the first"))return 1;
    tap(40,22);cyd::preview::advanceTime(200);
    if(!expectScreen(SCREEN_DASHBOARD,"Back from the first settings page"))return 1;
  }
  uiPreviewSetSettingsCategory(1);

  // With a Bluetooth controller selected and none paired, the home screen
  // alert is itself the shortcut into controller setup.
  if(!expect(!pinEnabled,"the alert shortcut test expects no PIN"))return 1;
  uiPreviewSetControllerBackend(CONTROLLER_VESC,CONTROLLER_CONNECTION_BLE);
  previewSetTelemetryLink(LINK_WAITING);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);
  if(!expect(hasLabel(lv_scr_act(),"NO CONTROLLER PAIRED"),"unpaired alert did not show at once"))return 1;
  tapLabel("NO CONTROLLER PAIRED");cyd::preview::advanceTime(200);
  if(!expectScreen(SCREEN_SUBMENU,"tapping the unpaired alert") ||
     !expect(hasLabel(lv_scr_act(),"CONTROLLER SETUP"),"the alert did not open controller setup"))return 1;
  previewSetTelemetryLink(LINK_LIVE);
  uiPreviewSetControllerBackend(CONTROLLER_VESC,CONTROLLER_CONNECTION_UART);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);

  // After a reset the default wired VESC has nothing to pair, but nothing has
  // connected either: the dashboard offers setup straight away rather than
  // waiting. The first real data is remembered, and from then on a dropout is
  // an ordinary lost link.
  controllerEverConnected = false;
  previewSetTelemetryLink(LINK_WAITING);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);uiDashboardTick();
  if(!expect(hasLabel(lv_scr_act(),"NO CONTROLLER CONNECTED YET"),
             "a display that never saw a controller did not offer setup at once"))return 1;
  previewSetTelemetryLink(LINK_LIVE);cyd::preview::advanceTime(200);uiDashboardTick();
  if(!expect(controllerEverConnected,"the first live data was not remembered"))return 1;
  previewSetTelemetryLink(LINK_LOST);uiDashboardTick();cyd::preview::advanceTime(6000);uiDashboardTick();
  if(!expect(!hasLabel(lv_scr_act(),"NO CONTROLLER CONNECTED YET") && hasLabel(lv_scr_act(),"VESC LINK LOST"),
             "a dropout after the first connection must read as a lost link"))return 1;
  previewSetTelemetryLink(LINK_LIVE);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);

  // Entering the dashboard shows the live readings at once: no startup sweep plays, and an
  // automatic light/dark flip restyles the same dashboard without animating either.
  dashboardAppearanceMode = DASH_APPEARANCE_AUTO;
  previewSetLightSensor(800, 20);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(20);
  if(!expect(lv_anim_count_running() == 0,"entering the dashboard started an animation"))return 1;
  previewSetLightSensor(3500, 90);uiDashboardTick();cyd::preview::advanceTime(20);
  if(!expect(dashboardLightModeActive(),"the automatic light switch did not take effect") ||
     !expect(lv_anim_count_running() == 0,"the automatic light switch started an animation"))return 1;
  dashboardAppearanceMode = DASH_APPEARANCE_DARK;
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(2000);

  // The Connection page's Bluetooth switch belongs to Bluetooth controllers
  // only, and a switched-off radio is named on the dashboard at once.
  uiPreviewSetSubmenu(SUBMENU_CONNECTION);
  uiShow(SCREEN_SUBMENU);cyd::preview::advanceTime(100);
  if(!expect(!hasLabel(lv_scr_act(),"BLUETOOTH"),"a wired controller offered the Bluetooth switch"))return 1;
  uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER,CONTROLLER_CONNECTION_BLE);
  uiShow(SCREEN_SUBMENU);cyd::preview::advanceTime(100);
  if(!expect(tapLabel("BLUETOOTH") && !bluetoothEnabled,"the Bluetooth switch did not turn Bluetooth off"))return 1;
  previewSetTelemetryLink(LINK_LOST);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);uiDashboardTick();
  if(!expect(hasLabel(lv_scr_act(),"BLUETOOTH IS OFF"),"the dashboard did not say Bluetooth is off"))return 1;
  uiPreviewSetSubmenu(SUBMENU_CONNECTION);
  uiShow(SCREEN_SUBMENU);cyd::preview::advanceTime(100);
  if(!expect(tapLabel("BLUETOOTH") && bluetoothEnabled,"the Bluetooth switch did not turn Bluetooth back on"))return 1;
  previewSetTelemetryLink(LINK_LIVE);
  uiPreviewSetControllerBackend(CONTROLLER_VESC,CONTROLLER_CONNECTION_UART);
  uiShow(SCREEN_DASHBOARD);cyd::preview::advanceTime(200);

  std::cout << "shared pointer path opened settings and automatic user update mode; frame "
            << metrics.frameNumber << " used " << metrics.flushCount << " flushes and "
            << metrics.flushedPixels << " pixels\n";
  return 0;
}
