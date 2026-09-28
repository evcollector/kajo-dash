// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone lab firmware: TFT_eSPI only, no dashboard/LVGL application state.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <atomic>
#include "frames.h"

namespace {
TFT_eSPI tft;
NimBLECharacteristic *output = nullptr;
NimBLEServer *server = nullptr;
std::atomic<bool> connected{false}, subscribed{false}, resetStream{false};
std::atomic<uint16_t> connectionHandle{0};
bool streaming = false, inverted = false, redraw = true;
bool rideMode = true;
uint32_t rideMs = 0;
fake_fardriver::Sample cycleSample = fake_fardriver::kStationary;
const uint16_t rates[] = {1, 10, 50};  // notifications/s, one 16-byte frame each
unsigned rateIndex = 1, sequence = 0;
uint32_t sent = 0, failed = 0, lastPacketMs = 0, lastPaintMs = 0;
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
  if (prefs.begin("fd-test", false)) {
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
  tft.drawCentreString("FAKE FARDRIVER - TEST ONLY", 160, 4, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(deviceName, 160, 25, 2);
  const bool ready = connected.load() && subscribed.load();
  tft.setTextColor(ready ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString(!connected.load() ? "Advertising - select on receiver" :
      !subscribed.load() ? "Connected - awaiting subscription" :
      streaming ? "CONNECTED / STREAMING" : "CONNECTED / SILENT", 160, 48, 2);
  char line[64];
  snprintf(line, sizeof(line), "%u packets/s | sent %lu | failed %lu", rates[rateIndex],
           (unsigned long)sent, (unsigned long)failed);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(line, 160, 72, 2);
  snprintf(line, sizeof(line), "%u s | %.1f V | %d W | RPM %d", unsigned(rideMs / 1000),
           cycleSample.voltageDeci / 10.0, int(cycleSample.voltageDeci * cycleSample.currentQuarters / 40),
           cycleSample.rawRpm);
  tft.drawCentreString(line, 160, 94, 2);
  button(6, 120, streaming ? "PAUSE PACKETS" : "START PACKETS");
  snprintf(line, sizeof(line), "RATE: %u /s", rates[rateIndex]);
  button(164, 120, line);
  button(6, 170, "DISCONNECT");
  button(164, 170, "INVERT COLORS");
  tft.drawRoundRect(6, 215, 308, 24, 4, TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(rideMode ? "MODE: RIDE SEQUENCE  (tap)" : "MODE: STATIONARY  (tap)", 160, 219, 2);
}
void toggleMode() {
  rideMode = !rideMode;
  rideMs = 0;
  sequence = 0;
  cycleSample = fake_fardriver::kStationary;
  redraw = true;
  Serial.println(rideMode ? "MODE RIDE SEQUENCE" : "MODE STATIONARY");
}
void toggleStream() {
  // Only arm after notification subscription, so every new connection is silent.
  streaming = !streaming && connected.load() && subscribed.load();
  lastPacketMs = millis();
  redraw = true;
  Serial.println(streaming ? "STREAM ON" : "STREAM OFF (or no subscriber)");
}
void disconnectReceiver() {
  streaming = false;
  if (connected.load()) server->disconnect(connectionHandle.load());
  redraw = true;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    connectionHandle.store(info.getConnHandle());
    subscribed.store(false);
    resetStream.store(true);
    connected.store(true);
    Serial.println("Receiver connected; silent until START");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    connected.store(false);
    subscribed.store(false);
    resetStream.store(true);
    Serial.printf("Receiver disconnected: %d\n", reason);
  }
} serverCallbacks;
class OutputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t value) override {
    subscribed.store((value & 1) != 0);
    resetStream.store(true);
    Serial.printf("Notification subscription: %u\n", value);
  }
} outputCallbacks;
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
  if (prefs.begin("fd-test", true)) { inverted = prefs.getBool("invert", inverted); prefs.end(); }
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
  snprintf(deviceName, sizeof(deviceName), "FarDriver TEST %04X", unsigned(ESP.getEfuseMac() & 0xFFFF));
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(23);  // 16-byte frames also fit the smallest normal BLE MTU
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  auto *service = server->createService("FFE0");
  output = service->createCharacteristic("FFEC", NIMBLE_PROPERTY::NOTIFY, 16);
  output->setCallbacks(&outputCallbacks);
  if (!server->start()) { Serial.println("ERROR: BLE server could not start"); return; }
  auto *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(deviceName);
  advertising->addServiceUUID("FFE0");
  advertising->enableScanResponse(true);
  if (!NimBLEDevice::startAdvertising()) Serial.println("ERROR: advertising could not start");
  Serial.printf("%s ready. Serial: s=start/pause r=rate d=disconnect i=invert m=mode\n", deviceName);
}

void loop() {
  const uint32_t now = millis();
  static uint32_t lastLoopMs = now;
  if (streaming && connected.load() && subscribed.load() && rideMode)
    rideMs = (rideMs + (now - lastLoopMs)) % fake_fardriver::kRideDurationMs;
  lastLoopMs = now;
  if (resetStream.exchange(false)) {
    streaming = false; sequence = 0; sent = 0; failed = 0; rideMs = 0;
    cycleSample = fake_fardriver::kStationary; redraw = true;
  }
  static bool wasTouched = false;
  static uint32_t releasedAt = 0;
  int x, y;
  if (readTouch(x, y)) {
    if (!wasTouched && now - releasedAt >= 100) {
      if (y >= 120 && y < 162) {
        if (x < 160) toggleStream(); else { rateIndex = (rateIndex + 1) % 3; redraw = true; }
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
    else if (now - bootAt >= 40) toggleStream();
  }
  while (Serial.available()) {
    switch (Serial.read()) {
      case 's': toggleStream(); break;
      case 'r': rateIndex = (rateIndex + 1) % 3; redraw = true; break;
      case 'd': disconnectReceiver(); break;
      case 'i': invertPanel(); break;
      case 'm': toggleMode(); break;
    }
  }
  if (streaming && connected.load() && subscribed.load() && now - lastPacketMs >= 1000U / rates[rateIndex]) {
    lastPacketMs = now;
    uint8_t frame[fake_fardriver::kFrameBytes];
    // Hold one sample across all four field groups, even at 1 packet/second.
    if (sequence % 4 == 0)
      cycleSample = rideMode ? fake_fardriver::rideSample(rideMs) : fake_fardriver::kStationary;
    fake_fardriver::makeFrame(sequence, frame, cycleSample);
    if (output->notify(frame, sizeof(frame))) { sent++; sequence++; } else failed++;
  }
  if (redraw || (streaming && now - lastPaintMs >= 1000)) {
    paint(); redraw = false; lastPaintMs = now;
  }
  delay(2);
}
