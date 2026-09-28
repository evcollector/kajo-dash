// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone lab firmware: TFT_eSPI only, no dashboard/LVGL application state.
// The FarDriver sender streams unprompted; a VESC answers requests, so this one
// is a responder and its controls are about what it answers, not how often.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <atomic>
#include "frames.h"

namespace {
constexpr char kService[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kReceive[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // receiver writes here
constexpr char kNotify[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";   // replies notify here

TFT_eSPI tft;
NimBLECharacteristic *output = nullptr;
NimBLEServer *server = nullptr;
std::atomic<bool> connected{false}, subscribed{false}, resetSession{false};
std::atomic<uint16_t> connectionHandle{0};
std::atomic<uint32_t> requests{0}, replies{0}, ignoredRequests{0}, badRequests{0};
bool responding = true, setupValues = true, inverted = false, redraw = true;
bool rideMode = true;
uint32_t rideMs = 0;
fake_vesc::Sample cycleSample = fake_vesc::kStationary;
fake_vesc::Totals totals = {};
float ampHours = 0, ampHoursCharged = 0, wattHours = 0, wattHoursCharged = 0, tachometer = 0, distanceMm = 0;
uint32_t lastPaintMs = 0;
char deviceName[30];
constexpr int kTouchCs = 33, kTouchClock = 25, kTouchMosi = 32, kTouchMiso = 39, kTouchIrq = 36;
uint16_t xMin = 3900, xMax = 250, yMin = 250, yMax = 3900;
uint8_t xAxis = 1, yAxis = 0;

// Same software-SPI XPT2046 readout and calibration convention as main_lvgl.cpp.
uint8_t transfer(uint8_t value) {
  uint8_t result = 0;
  for (int bit = 7; bit >= 0; bit--) {
    digitalWrite(kTouchMosi, (value >> bit) & 1);
    digitalWrite(kTouchClock, HIGH);
    result = (result << 1) | digitalRead(kTouchMiso);
    digitalWrite(kTouchClock, LOW);
  }
  return result;
}
uint16_t readAxis(uint8_t command) {
  transfer(command);
  const uint16_t high = transfer(0);
  return ((high << 8) | transfer(0)) >> 3;
}
bool readTouch(int &x, int &y) {
  if (digitalRead(kTouchIrq) != LOW) return false;
  digitalWrite(kTouchCs, LOW);
  const uint16_t raw[2] = {readAxis(0xD0), readAxis(0x90)};
  transfer(0);
  digitalWrite(kTouchCs, HIGH);
  if (raw[0] < 100 || raw[0] > 4000 || raw[1] < 100 || raw[1] > 4000) return false;
  x = constrain(map(raw[xAxis], xMin, xMax, 24, 295), 0L, 319L);
  y = constrain(map(raw[yAxis], yMin, yMax, 24, 215), 0L, 239L);
  return true;
}

void applyInversion() {
  tft.invertDisplay(inverted);
  if (inverted) {
    tft.writecommand(0x26);
    tft.writedata(0x02);
    delay(120);
  }
  tft.writecommand(0x26);
  tft.writedata(0x01);
  redraw = true;
}
void invertPanel() {
  inverted = !inverted;
  applyInversion();
  Preferences prefs;
  if (prefs.begin("vesc-test", false)) {
    prefs.putBool("invert", inverted);
    prefs.end();
  }
}
void button(int x, int y, const char *label) {
  tft.fillRoundRect(x, y, 150, 42, 5, TFT_DARKGREY);
  tft.drawRoundRect(x, y, 150, 42, 5, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString(label, x + 75, y + 13, 2);
}
void paint() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("FAKE VESC - TEST ONLY", 160, 4, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(deviceName, 160, 25, 2);
  tft.setTextColor(connected.load() ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString(!connected.load() ? "Advertising - select on receiver" :
      !subscribed.load() ? "Connected - awaiting subscription" :
      responding ? "CONNECTED / ANSWERING" : "CONNECTED / IGNORING", 160, 48, 2);
  char line[72];
  snprintf(line, sizeof(line), "req %lu | replies %lu | ignored %lu | bad %lu",
           (unsigned long)requests.load(), (unsigned long)replies.load(),
           (unsigned long)ignoredRequests.load(), (unsigned long)badRequests.load());
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(line, 160, 72, 2);
  snprintf(line, sizeof(line), "%u s | %.1f V | %.1f A | eRPM %ld", unsigned(rideMs / 1000),
           cycleSample.voltageDeci / 10.0, cycleSample.inputCurrentCenti / 100.0,
           (long)cycleSample.erpm);
  tft.drawCentreString(line, 160, 94, 2);
  button(6, 120, responding ? "STOP ANSWERING" : "START ANSWERING");
  button(164, 120, setupValues ? "SETUP VALS: ON" : "SETUP VALS: OFF");
  button(6, 170, "DISCONNECT");
  button(164, 170, "INVERT COLORS");
  tft.drawRoundRect(6, 215, 308, 24, 4, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(rideMode ? "MODE: RIDE SEQUENCE  (tap)" : "MODE: STATIONARY  (tap)", 160, 219, 2);
}
void resetTotals() {
  ampHours = ampHoursCharged = wattHours = wattHoursCharged = tachometer = distanceMm = 0;
  totals = {};
}
void toggleMode() {
  rideMode = !rideMode;
  rideMs = 0;
  cycleSample = fake_vesc::kStationary;
  resetTotals();
  redraw = true;
  Serial.println(rideMode ? "MODE RIDE SEQUENCE" : "MODE STATIONARY");
}
void toggleResponding() {
  responding = !responding;
  redraw = true;
  Serial.println(responding ? "ANSWERING REQUESTS" : "IGNORING REQUESTS");
}
void toggleSetupValues() {
  // COMM_GET_VALUES_SETUP is optional on real hardware, and the receiver falls
  // back to eRPM and wheel size when it goes unanswered. This is how to test
  // that fallback without finding an old VESC.
  setupValues = !setupValues;
  redraw = true;
  Serial.println(setupValues ? "SETUP VALUES ANSWERED" : "SETUP VALUES IGNORED");
}
void disconnectReceiver() {
  if (connected.load()) server->disconnect(connectionHandle.load());
  redraw = true;
}

// Replies are chunked to 20 bytes so they arrive whole at the smallest usable
// MTU; the receiver reassembles the byte stream, exactly like a UART.
void sendFrame(const uint8_t *frame, size_t length) {
  if (!output || !subscribed.load()) return;
  for (size_t sent = 0; sent < length;) {
    const size_t chunk = length - sent < 20 ? length - sent : 20;
    output->setValue(frame + sent, chunk);
    output->notify(connectionHandle.load());
    sent += chunk;
  }
  replies.fetch_add(1);
}

void answer(uint8_t command) {
  uint8_t frame[80];
  size_t length = 0;
  switch (command) {
    case fake_vesc::kCommandGetValues:
      length = fake_vesc::makeValuesFrame(frame, sizeof(frame), cycleSample, totals);
      break;
    case fake_vesc::kCommandGetValuesSetup:
      if (!setupValues) { ignoredRequests.fetch_add(1); return; }
      length = fake_vesc::makeSetupFrame(frame, sizeof(frame), cycleSample, totals);
      break;
    case fake_vesc::kCommandFwVersion:
      length = fake_vesc::makeFwVersionFrame(frame, sizeof(frame));
      break;
    default:
      // A real VESC also stays silent for commands it does not implement.
      ignoredRequests.fetch_add(1);
      return;
  }
  if (length) sendFrame(frame, length);
}

// The receiver writes one short request per poll, but a write can still be
// split, so requests are reassembled rather than assumed whole.
uint8_t requestBuffer[64];
size_t requestUsed = 0;

void consumeRequests() {
  for (;;) {
    size_t start = 0;
    while (start < requestUsed && requestBuffer[start] != 2) start++;
    if (start) {
      badRequests.fetch_add(1);
      memmove(requestBuffer, requestBuffer + start, requestUsed - start);
      requestUsed -= start;
    }
    if (requestUsed < 2) return;
    const size_t length = requestBuffer[1];
    const size_t total = length + 5;
    if (length == 0 || total > sizeof(requestBuffer)) {  // longer than any request of ours
      badRequests.fetch_add(1);
      memmove(requestBuffer, requestBuffer + 1, --requestUsed);
      continue;
    }
    if (requestUsed < total) return;  // wait for the rest
    const uint8_t *payload = requestBuffer + 2;
    const uint16_t crc = uint16_t(uint16_t(payload[length]) << 8) | payload[length + 1];
    const bool valid = payload[length + 2] == 3 && fake_vesc::crc16(payload, length) == crc;
    if (valid) {
      requests.fetch_add(1);
      // COMM_FORWARD_CAN wraps the real command: id, CAN id, command.
      const uint8_t command = payload[0] == fake_vesc::kCommandForwardCan && length >= 3 ? payload[2] : payload[0];
      if (responding) answer(command);
      else ignoredRequests.fetch_add(1);
    } else {
      badRequests.fetch_add(1);
    }
    memmove(requestBuffer, requestBuffer + total, requestUsed - total);
    requestUsed -= total;
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    connectionHandle.store(info.getConnHandle());
    subscribed.store(false);
    resetSession.store(true);
    connected.store(true);
    Serial.println("Receiver connected");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    connected.store(false);
    subscribed.store(false);
    resetSession.store(true);
    Serial.printf("Receiver disconnected: %d\n", reason);
  }
} serverCallbacks;

class NotifyCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t value) override {
    subscribed.store((value & 1) != 0);
    resetSession.store(true);
    Serial.printf("Notification subscription: %u\n", value);
  }
} notifyCallbacks;

class ReceiveCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    const std::string value = characteristic->getValue();
    if (value.size() > sizeof(requestBuffer)) { badRequests.fetch_add(1); return; }
    if (requestUsed + value.size() > sizeof(requestBuffer)) {
      badRequests.fetch_add(1);
      requestUsed = 0;  // nothing this long is a request of ours
    }
    memcpy(requestBuffer + requestUsed, value.data(), value.size());
    requestUsed += value.size();
    consumeRequests();
  }
} receiveCallbacks;
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  for (int pin : {4, 16, 17}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(kTouchCs, OUTPUT); digitalWrite(kTouchCs, HIGH);
  pinMode(kTouchClock, OUTPUT); digitalWrite(kTouchClock, LOW);
  pinMode(kTouchMosi, OUTPUT); pinMode(kTouchMiso, INPUT); pinMode(kTouchIrq, INPUT);
  tft.init();
  tft.setRotation(1);
  const uint32_t idD3 = tft.readcommand32(0xD3), id04 = tft.readcommand32(0x04);
  inverted = idD3 == 0 && id04 == 0;
  Preferences prefs;
  if (prefs.begin("vesc-test", true)) { inverted = prefs.getBool("invert", inverted); prefs.end(); }
  // Read existing dashboard calibration without changing dashboard settings.
  if (prefs.begin("touch", true)) {
    if (prefs.getBool("valid", false)) {
      const auto a = prefs.getUChar("xAxis", 1), b = prefs.getUChar("yAxis", 0);
      const auto x0 = prefs.getUShort("xMin", 3900), x1 = prefs.getUShort("xMax", 250);
      const auto y0 = prefs.getUShort("yMin", 250), y1 = prefs.getUShort("yMax", 3900);
      if (a < 2 && b < 2 && a != b && x0 <= 4095 && x1 <= 4095 && y0 <= 4095 && y1 <= 4095 &&
          abs(int(x1) - int(x0)) >= 1200 && abs(int(y1) - int(y0)) >= 900) {
        xAxis = a; yAxis = b; xMin = x0; xMax = x1; yMin = y0; yMax = y1;
      }
    }
    prefs.end();
  }
  applyInversion();
  snprintf(deviceName, sizeof(deviceName), "VESC TEST %04X", unsigned(ESP.getEfuseMac() & 0xFFFF));
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(247);  // larger is welcome; replies are chunked regardless
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  auto *service = server->createService(kService);
  output = service->createCharacteristic(kNotify, NIMBLE_PROPERTY::NOTIFY, 244);
  output->setCallbacks(&notifyCallbacks);
  auto *input = service->createCharacteristic(kReceive, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 244);
  input->setCallbacks(&receiveCallbacks);
  // NimBLE 2.x starts every service with the server.
  if (!server->start()) { Serial.println("ERROR: BLE server could not start"); return; }
  auto *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(deviceName);
  advertising->addServiceUUID(kService);
  advertising->enableScanResponse(true);
  if (!NimBLEDevice::startAdvertising()) Serial.println("ERROR: advertising could not start");
  Serial.printf("%s ready. Serial: s=answer/ignore v=setup values d=disconnect i=invert m=mode\n", deviceName);
}

void loop() {
  const uint32_t now = millis();
  static uint32_t lastLoopMs = now;
  const uint32_t elapsed = now - lastLoopMs;
  lastLoopMs = now;
  if (resetSession.exchange(false)) {
    requestUsed = 0;
    requests = 0; replies = 0; ignoredRequests = 0; badRequests = 0;
    rideMs = 0;
    resetTotals();
    redraw = true;
  }
  if (connected.load() && rideMode) rideMs = (rideMs + elapsed) % fake_vesc::kRideDurationMs;
  cycleSample = rideMode ? fake_vesc::rideSample(rideMs) : fake_vesc::kStationary;
  // Charge, energy, distance and the tachometer count up the way a real
  // controller's do, so the receiver's trip and battery maths see a plausible
  // series rather than a value that jumps backwards every ride cycle.
  const float seconds = elapsed / 1000.0F;
  const float amps = cycleSample.inputCurrentCenti / 100.0F;
  const float watts = amps * cycleSample.voltageDeci / 10.0F;
  if (amps >= 0) {
    ampHours += amps * seconds / 3600.0F;
    wattHours += watts * seconds / 3600.0F;
  } else {
    ampHoursCharged -= amps * seconds / 3600.0F;
    wattHoursCharged -= watts * seconds / 3600.0F;
  }
  tachometer += cycleSample.erpm / 60.0F * seconds * 6.0F;
  distanceMm += cycleSample.speedMmPerS * seconds;
  totals.ampHours1e4 = int32_t(ampHours * 1e4F);
  totals.ampHoursCharged1e4 = int32_t(ampHoursCharged * 1e4F);
  totals.wattHours1e4 = int32_t(wattHours * 1e4F);
  totals.wattHoursCharged1e4 = int32_t(wattHoursCharged * 1e4F);
  totals.tachometerAbs = int32_t(tachometer);
  totals.tripMm = int32_t(distanceMm);
  totals.odometerMm = int32_t(distanceMm);

  static bool wasTouched = false;
  static uint32_t releasedAt = 0;
  int x, y;
  if (readTouch(x, y)) {
    if (!wasTouched && now - releasedAt >= 100) {
      if (y >= 120 && y < 162) {
        if (x < 160) toggleResponding(); else toggleSetupValues();
      } else if (y >= 170 && y < 212) {
        if (x < 160) disconnectReceiver(); else invertPanel();
      } else if (y >= 215) {
        toggleMode();
      }
    }
    wasTouched = true;
  } else if (wasTouched) { wasTouched = false; releasedAt = now; }
  static uint32_t bootAt = 0;
  static bool bootHeld = false;
  if (digitalRead(0) == LOW && !bootHeld) { bootHeld = true; bootAt = now; }
  if (digitalRead(0) == HIGH && bootHeld) {
    bootHeld = false;
    if (now - bootAt >= 1200) invertPanel();
    else if (now - bootAt >= 40) toggleResponding();
  }
  while (Serial.available()) {
    switch (Serial.read()) {
      case 's': toggleResponding(); break;
      case 'v': toggleSetupValues(); break;
      case 'd': disconnectReceiver(); break;
      case 'i': invertPanel(); break;
      case 'm': toggleMode(); break;
    }
  }
  if (redraw || now - lastPaintMs >= 1000) {
    paint();
    redraw = false;
    lastPaintMs = now;
  }
  delay(2);
}
