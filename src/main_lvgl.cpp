// SPDX-License-Identifier: GPL-3.0-or-later
//
// ESP32 CYD VESC Telemetry Display
// Copyright (C) 2026 villevu
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
// License in LICENSE, or <https://www.gnu.org/licenses/>, for details.
//
// LVGL variant of the CYD VESC telemetry firmware. Same features and screens
// as src/main.cpp, rendered with LVGL widgets instead of direct TFT_eSPI
// drawing. Built by the kajo environment (scripts\upload_firmware_usb.bat).
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <lvgl.h>


#include "config.h"
#include "kajo_splash_generated.h"
#include "lvgl_app/app_state.h"
#include "lvgl_app/controller_manager.h"
#include "lvgl_app/display_panel_tuning.h"
#include "lvgl_app/firmware_update.h"
#include "lvgl_app/firmware_update_ble.h"
#include "lvgl_app/companion_ble.h"
#include "lvgl_app/screens.h"

// ── Hardware ──────────────────────────────────────────────────────────────────

// The panel's own reset is wired to the board reset (TFT_RST=-1), so every
// power-up and reboot leaves it undriven, and an undriven normally-white TN
// panel is white for as long as the backlight is lit. The ROM and second-stage
// bootloaders run before a single instruction of ours, so that opening white is
// not something firmware can prevent.
//
// Keep that white stable during panel bring-up, then deliberately fade the
// backlight down, paint the black KAJO artwork, and fade it up. The artwork
// stays black for both dashboard appearances. The final handoff uses the same
// fade-through-darkness technique so partial drawing is never visible.
//
// ESP-IDF runs C++ global constructors before app_main(), so this claims the
// pin before initArduino() and well before setup(), making the backlight state
// deterministic from the earliest point any of our code can run.
struct BacklightOnFromBoot {
  BacklightOnFromBoot() {
    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, TFT_BACKLIGHT_ON);
  }
};
static BacklightOnFromBoot backlightOnFromBoot;

static TFT_eSPI tft;

// When the boot splash went up, or 0 once something else has taken the screen.
// Declared here because the recovery and calibration flows, which clear it,
// are defined above the splash itself.
static uint32_t splashShownAtMs = 0;
static Preferences touchPrefs;
static Preferences displayHardwarePrefs;
static uint32_t displayIdD3 = 0;
static uint32_t displayId04 = 0;
static bool displayPanelProfileStored = false;
static bool displayPanelTuningStored = false;

struct TS_Point {
  int16_t x;
  int16_t y;
  int16_t z;
  TS_Point(int16_t px = 0, int16_t py = 0, int16_t pz = 0) : x(px), y(py), z(pz) {}
};

// The CYD routes TFT, touch and SD to three different pin groups but classic
// ESP32 exposes only two application SPI peripherals. Keep hardware SPI for
// the high-bandwidth TFT and SD; touch is tiny and only read while IRQ is low.
class SoftXpt2046 {
 public:
  void begin() {
    pinMode(CYD_TOUCH_CS_PIN, OUTPUT);
    pinMode(CYD_TOUCH_SCLK_PIN, OUTPUT);
    pinMode(CYD_TOUCH_MOSI_PIN, OUTPUT);
    pinMode(CYD_TOUCH_MISO_PIN, INPUT);
    pinMode(CYD_TOUCH_IRQ_PIN, INPUT_PULLUP);
    digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
    digitalWrite(CYD_TOUCH_SCLK_PIN, LOW);
  }

  bool touched() const { return digitalRead(CYD_TOUCH_IRQ_PIN) == LOW; }

  TS_Point getPoint() {
    if (!touched()) return TS_Point();
    digitalWrite(CYD_TOUCH_CS_PIN, LOW);
    transfer8(0xD0);  // X position, 12-bit differential conversion
    const uint16_t x = transfer16() >> 3;
    transfer8(0x90);  // Y position
    const uint16_t y = transfer16() >> 3;
    transfer8(0x00);  // power down and re-enable PENIRQ
    digitalWrite(CYD_TOUCH_CS_PIN, HIGH);
    return TS_Point(x, y, 1);
  }

 private:
  static uint8_t transfer8(uint8_t out) {
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(CYD_TOUCH_MOSI_PIN, (out >> bit) & 1);
      digitalWrite(CYD_TOUCH_SCLK_PIN, HIGH);
      in = (uint8_t)((in << 1) | digitalRead(CYD_TOUCH_MISO_PIN));
      digitalWrite(CYD_TOUCH_SCLK_PIN, LOW);
    }
    return in;
  }

  static uint16_t transfer16() {
    return (uint16_t)transfer8(0) << 8 | transfer8(0);
  }
};

static SoftXpt2046 touch;

#define SCREEN_W 320
#define SCREEN_H 240
#define BUF_LINES 26

static lv_disp_draw_buf_t draw_buf;
// double buffer: LVGL renders into one half while DMA streams the other
static lv_color_t lvgl_buf1[SCREEN_W * BUF_LINES];
static lv_color_t lvgl_buf2[SCREEN_W * BUF_LINES];
static bool lvglStarted = false;  // calibration runs on boot before lv_init()

static uint32_t perfWindowStartedMs = 0;
static uint32_t perfHandlerCount = 0;
static uint64_t perfHandlerTotalUs = 0;
static uint32_t perfHandlerMaxUs = 0;
static uint32_t perfFlushCount = 0;
static uint64_t perfFlushPixels = 0;
static uint64_t perfFlushWaitSubmitUs = 0;

constexpr uint8_t BACKLIGHT_PWM_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_PWM_FREQ = 5000;
constexpr uint8_t BACKLIGHT_PWM_BITS = 8;
constexpr uint8_t RGB_LED_RED_PWM_CHANNEL = 1;
constexpr uint8_t RGB_LED_GREEN_PWM_CHANNEL = 2;
constexpr uint8_t RGB_LED_BLUE_PWM_CHANNEL = 3;
constexpr uint32_t RGB_LED_PWM_FREQ = 5000;
constexpr uint8_t RGB_LED_PWM_BITS = 8;

constexpr bool rgbLedPinAvailable(int pin) {
  return pin >= 0 && pin != TFT_BACKLIGHT_PIN && pin != VESC_RX_PIN && pin != VESC_TX_PIN;
}

constexpr bool RGB_LED_RED_AVAILABLE = CYD_RGB_LED_ENABLED && rgbLedPinAvailable(CYD_RGB_LED_RED_PIN);
constexpr bool RGB_LED_GREEN_AVAILABLE = CYD_RGB_LED_ENABLED && rgbLedPinAvailable(CYD_RGB_LED_GREEN_PIN);
constexpr bool RGB_LED_BLUE_AVAILABLE = CYD_RGB_LED_ENABLED && rgbLedPinAvailable(CYD_RGB_LED_BLUE_PIN);

static void writeBacklightPercent(uint8_t pct) {
  uint8_t duty = map(pct, 0, 100, 0, 255);
#if TFT_BACKLIGHT_ON == LOW
  duty = 255 - duty;
#endif
  ledcWrite(BACKLIGHT_PWM_CHANNEL, duty);
}

static uint8_t configuredBrightnessPercent() {
  return constrain(displayBrightnessPercent, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
}

void applyDisplayBrightness() {
  writeBacklightPercent(configuredBrightnessPercent());
}

static uint8_t activeLowLedDuty(uint8_t value) {
  return 255 - value;
}

static void setupRgbLedChannel(uint8_t channel, int pin) {
  ledcSetup(channel, RGB_LED_PWM_FREQ, RGB_LED_PWM_BITS);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 255);  // active-low off
}

static void setupThemeRgbLed() {
  if (!CYD_RGB_LED_ENABLED) return;
  if (RGB_LED_RED_AVAILABLE) setupRgbLedChannel(RGB_LED_RED_PWM_CHANNEL, CYD_RGB_LED_RED_PIN);
  if (RGB_LED_GREEN_AVAILABLE) setupRgbLedChannel(RGB_LED_GREEN_PWM_CHANNEL, CYD_RGB_LED_GREEN_PIN);
  if (RGB_LED_BLUE_AVAILABLE) setupRgbLedChannel(RGB_LED_BLUE_PWM_CHANNEL, CYD_RGB_LED_BLUE_PIN);
  if (!RGB_LED_GREEN_AVAILABLE || !RGB_LED_BLUE_AVAILABLE) {
    Serial.println("CYD RGB LED: green/blue channels unavailable because they conflict with configured VESC UART pins");
  }
}

static void applyThemeRgbLed() {
  if (!CYD_RGB_LED_ENABLED) return;
  if (!themeLedEnabled) {
    if (RGB_LED_RED_AVAILABLE) ledcWrite(RGB_LED_RED_PWM_CHANNEL, 255);  // active-low off
    if (RGB_LED_GREEN_AVAILABLE) ledcWrite(RGB_LED_GREEN_PWM_CHANNEL, 255);
    if (RGB_LED_BLUE_AVAILABLE) ledcWrite(RGB_LED_BLUE_PWM_CHANNEL, 255);
    return;
  }
  const uint16_t color = accentColor565();
  const uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
  const uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
  const uint8_t b = (color & 0x1F) * 255 / 31;
  if (RGB_LED_RED_AVAILABLE) ledcWrite(RGB_LED_RED_PWM_CHANNEL, activeLowLedDuty(r));
  if (RGB_LED_GREEN_AVAILABLE) ledcWrite(RGB_LED_GREEN_PWM_CHANNEL, activeLowLedDuty(g));
  if (RGB_LED_BLUE_AVAILABLE) ledcWrite(RGB_LED_BLUE_PWM_CHANNEL, activeLowLedDuty(b));
}

static void setupBacklight() {
  ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(TFT_BACKLIGHT_PIN, BACKLIGHT_PWM_CHANNEL);
  applyDisplayBrightness();
}

// ── LDR auto brightness ───────────────────────────────────────────────────────
// The front photoresistor reads HIGH in the dark. Readings are smoothed with
// an EMA and the applied brightness slews a couple percent per tick so the
// backlight never visibly jumps.

static float ldrFiltered = -1.0F;
static int autoBrightnessApplied = -1;

int lightSensorRaw() {
  return ldrFiltered < 0 ? 0 : (int)ldrFiltered;
}

int lightSensorTargetPct() {
  const int pct = map(lightSensorRaw(), lightSensorDarkRaw, lightSensorBrightRaw, 15, 100);
  return constrain(pct, 10, 100);
}

static void serviceAutoBrightness() {
  const int raw = analogRead(CYD_LDR_PIN);
  ldrFiltered = ldrFiltered < 0 ? raw : ldrFiltered + (raw - ldrFiltered) * 0.1F;

  if (!autoBrightnessEnabled) {
    autoBrightnessApplied = -1;  // manual setting owns the backlight
    return;
  }
  const int target = lightSensorTargetPct();
  if (autoBrightnessApplied < 0) autoBrightnessApplied = displayBrightnessPercent;
  if (autoBrightnessApplied < target) {
    autoBrightnessApplied = min(target, autoBrightnessApplied + 2);
  } else if (autoBrightnessApplied > target) {
    autoBrightnessApplied = max(target, autoBrightnessApplied - 2);
  }
  writeBacklightPercent(autoBrightnessApplied);
}


// lv_conf.h sets LV_COLOR_16_SWAP=1, so bytes are already in ILI9341 order
// and DMA sends the buffer unmodified (setSwapBytes(false) in setup). The
// SPI bus is held permanently (startWrite in setup) — the display owns HSPI
// alone; touch lives on VSPI. Calibration draws before LVGL starts, so it
// briefly releases and re-claims the bus.
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  const uint32_t startedUs = micros();
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.dmaWait();  // previous transfer must finish before touching the window
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixelsDMA((uint16_t *)&px->full, w * h);
  if (LVGL_PERFORMANCE_LOG_ENABLED) {
    perfFlushCount++;
    perfFlushPixels += w * h;
    perfFlushWaitSubmitUs += micros() - startedUs;
  }
  // safe immediately: LVGL renders into the other buffer while DMA streams
  lv_disp_flush_ready(drv);
}

static void servicePerformanceLog() {
  if (!LVGL_PERFORMANCE_LOG_ENABLED) return;
  const uint32_t now = millis();
  if (perfWindowStartedMs == 0) perfWindowStartedMs = now;
  const uint32_t windowMs = now - perfWindowStartedMs;
  if (windowMs < LVGL_PERFORMANCE_LOG_INTERVAL_MS) return;

  const uint32_t handlerAvgUs = perfHandlerCount ? perfHandlerTotalUs / perfHandlerCount : 0;
  const uint32_t flushAvgUs = perfFlushCount ? perfFlushWaitSubmitUs / perfFlushCount : 0;
  const uint32_t flushesPerSecond = windowMs ? (uint64_t)perfFlushCount * 1000 / windowMs : 0;
  const uint32_t pixelsPerSecond = windowMs ? perfFlushPixels * 1000 / windowMs : 0;
  Serial.printf("LVGL perf screen=%u mode=%u handler_us(avg/max)=%u/%u flush_s=%u pixels_s=%u flush_wait_us=%u\n",
                (unsigned)uiCurrentScreen(), (unsigned)dashboardMode, handlerAvgUs, perfHandlerMaxUs,
                flushesPerSecond, pixelsPerSecond, flushAvgUs);

  perfWindowStartedMs = now;
  perfHandlerCount = 0;
  perfHandlerTotalUs = 0;
  perfHandlerMaxUs = 0;
  perfFlushCount = 0;
  perfFlushPixels = 0;
  perfFlushWaitSubmitUs = 0;
}

// ── Touch calibration (identical math to src/main.cpp) ────────────────────────

struct TouchCalibration {
  uint16_t xMin;
  uint16_t xMax;
  uint16_t yMin;
  uint16_t yMax;
  uint8_t xAxis;
  uint8_t yAxis;
  bool valid;
};

static TouchCalibration touchCal = {3900, 250, 250, 3900, 1, 0, false};
static const TouchCalibration defaultTouchCal = {3900, 250, 250, 3900, 1, 0, false};

static uint16_t rawAxisValue(const TS_Point &point, uint8_t axis) {
  return axis == 0 ? point.x : point.y;
}

static void writeDisplayRegister(uint8_t command, const uint8_t *data, uint8_t length) {
  tft.writecommand(command);
  for (uint8_t i = 0; i < length; i++) tft.writedata(data[i]);
}

// Writes the gamma and VCOM registers of the selected panel tuning
// (display_panel_tuning.h), never inversion or memory access control. The
// caller owns the SPI transaction. This runs on every change and again on
// every boot, after init.
static void writeDisplayPanelTuning() {
  // TFT_eSPI ILI9341_2_DRIVER init tables: what the init sequence sends.
  static const uint8_t kIli2Positive[15] = {0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9,
                                            0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t kIli2Negative[15] = {0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56,
                                            0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F};
  static const uint8_t kIli2Vcom[2] = {0x30, 0x30};
  // TFT_eSPI ILI9341_DRIVER (original) init tables.
  static const uint8_t kIliPositive[15] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                                           0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
  static const uint8_t kIliNegative[15] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                                           0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
  // TFT_eSPI ST7789_DRIVER init tables (14 parameters, opposite order).
  static const uint8_t kStPositive[14] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
                                          0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
  static const uint8_t kStNegative[14] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
                                          0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};

  const uint8_t preset = displayPanelTuning < PANEL_TUNING_COUNT ? displayPanelTuning : PANEL_TUNING_BASELINE;

  uint8_t curve = 0x01;
  if (preset == PANEL_TUNING_CURVE_2) curve = 0x02;
  else if (preset == PANEL_TUNING_CURVE_4) curve = 0x04;
  writeDisplayRegister(0x26, &curve, 1);
  if (preset == PANEL_TUNING_ILI9341_CLASSIC) {
    writeDisplayRegister(0xE0, kIliPositive, sizeof(kIliPositive));
    writeDisplayRegister(0xE1, kIliNegative, sizeof(kIliNegative));
  } else if (preset >= PANEL_TUNING_ST7789) {
    writeDisplayRegister(0xE0, kStPositive, sizeof(kStPositive));
    writeDisplayRegister(0xE1, kStNegative, sizeof(kStNegative));
  } else {
    writeDisplayRegister(0xE0, kIli2Positive, sizeof(kIli2Positive));
    writeDisplayRegister(0xE1, kIli2Negative, sizeof(kIli2Negative));
  }
  if (preset == PANEL_TUNING_ST7789_VCOM) {
    const uint8_t noOffset = 0x20;  // ST7789 VCMOFSET: 0x20 is 0 V
    writeDisplayRegister(0xC5, &noOffset, 1);
  } else {
    writeDisplayRegister(0xC5, kIli2Vcom, sizeof(kIli2Vcom));
  }
  Serial.printf("CYD panel tuning: %s\n", displayPanelTuningName(preset));
}

// The original single-USB CYD and several later dual-USB batches expose the
// same pins and framebuffer protocol, but the panel glass expects opposite
// display inversion. Some alternate panels also need their built-in gamma
// curve re-selected after inversion to avoid the harsh, posterized look.
void applyDisplayPanelProfile() {
  if (lvglStarted) {
    tft.dmaWait();
    tft.endWrite();
  }

  const bool alternate = displayPanelProfile == DISPLAY_PANEL_ALTERNATE;
  tft.invertDisplay(alternate);
  if (alternate) {
    tft.writecommand(0x26);  // ILI9341-compatible GAMMASET
    tft.writedata(0x02);
    delay(120);
  }
  writeDisplayPanelTuning();  // selects the tuning's gamma curve last

  if (lvglStarted) tft.startWrite();
  Serial.printf("CYD panel profile: %s\n", alternate ? "alternate/inverted" : "standard");
}

void applyDisplayPanelTuning() {
  if (lvglStarted) {
    tft.dmaWait();
    tft.endWrite();
  }
  writeDisplayPanelTuning();
  if (lvglStarted) tft.startWrite();
}

void saveDisplayPanelTuning() {
  displayHardwarePrefs.begin("display-hw", false);
  displayHardwarePrefs.putUChar("tuning", displayPanelTuning);
  displayHardwarePrefs.end();
  displayPanelTuningStored = true;
}

void loadDisplayPanelProfile() {
  displayHardwarePrefs.begin("display-hw", true);
  displayPanelProfileStored = displayHardwarePrefs.isKey("panel");
  const uint8_t saved = displayHardwarePrefs.getUChar("panel", DISPLAY_PANEL_STANDARD);
  displayPanelTuningStored = displayHardwarePrefs.isKey("tuning");
  const uint8_t tuning = displayHardwarePrefs.getUChar("tuning", PANEL_TUNING_COUNT);
  displayHardwarePrefs.end();
  displayPanelProfile = saved < DISPLAY_PANEL_COUNT ? static_cast<DisplayPanelProfile>(saved)
                                                     : DISPLAY_PANEL_STANDARD;
  displayPanelTuning = tuning < PANEL_TUNING_COUNT ? tuning : defaultDisplayPanelTuning(displayPanelProfile);
}

void saveDisplayPanelProfile() {
  displayHardwarePrefs.begin("display-hw", false);
  displayHardwarePrefs.putUChar("panel", static_cast<uint8_t>(displayPanelProfile));
  displayHardwarePrefs.end();
  displayPanelProfileStored = true;
}

void autoDetectDisplayPanelProfile(bool force) {
  if (displayPanelProfileStored && !force) return;

  // Genuine ILI9341 CYDs return an ID ending in 9341. The dual-USB Rv3/ST7789
  // board has no working display readback on this bus and returns zero for
  // both paths. Keep STANDARD as the conservative fallback for unknown IDs;
  // Developer Options always permits a manual override.
  if ((displayIdD3 & 0xFFFFU) == 0x9341U)
    displayPanelProfile = DISPLAY_PANEL_STANDARD;
  else if (displayIdD3 == 0 && displayId04 == 0)
    displayPanelProfile = DISPLAY_PANEL_ALTERNATE;
  else
    displayPanelProfile = DISPLAY_PANEL_STANDARD;
  // Each detected model starts from its own tuning; a forced re-detect (a
  // settings reset) also replaces a previous manual tuning choice.
  if (force || !displayPanelTuningStored) {
    displayPanelTuning = defaultDisplayPanelTuning(displayPanelProfile);
    if (force) saveDisplayPanelTuning();
  }

  saveDisplayPanelProfile();
  if (force) applyDisplayPanelProfile();
  Serial.printf("CYD panel auto-detected: %s (D3 %08lX, 04 %08lX)\n",
                displayPanelProfile == DISPLAY_PANEL_ALTERNATE ? "alternate/ST7789" : "standard/ILI9341",
                (unsigned long)displayIdD3, (unsigned long)displayId04);
}

void displayModuleInfo(char *buffer, size_t size) {
  snprintf(buffer, size, "%s rev %u | %u MB flash", ESP.getChipModel(), ESP.getChipRevision(),
           ESP.getFlashChipSize() / (1024U * 1024U));
}

void displayControllerInfo(char *buffer, size_t size) {
  snprintf(buffer, size, "D3 %08lX | 04 %08lX", (unsigned long)displayIdD3, (unsigned long)displayId04);
}

static void probeDisplayController() {
  // Read both standard identification paths. Genuine ILI9341 normally ends
  // D3 with 9341; ST7789 commonly reports 85/52 through RDDID. Clone panels
  // may return zeros or mirror the genuine ID, so this remains diagnostic and
  // deliberately does not override the user's persistent profile.
  displayIdD3 = tft.readcommand32(0xD3);
  displayId04 = tft.readcommand32(0x04);
  char module[48];
  char panel[48];
  displayModuleInfo(module, sizeof(module));
  displayControllerInfo(panel, sizeof(panel));
  Serial.printf("CYD hardware: %s | TFT %s\n", module, panel);
}

static bool touchCalibrationPlausible(const TouchCalibration &cal) {
  if (!cal.valid || cal.xAxis > 1 || cal.yAxis > 1 || cal.xAxis == cal.yAxis) return false;
  const int32_t xSpan = abs((int32_t)cal.xMax - cal.xMin);
  const int32_t ySpan = abs((int32_t)cal.yMax - cal.yMin);
  if (xSpan < 1200 || ySpan < 900) return false;
  return cal.xMin <= 4095 && cal.xMax <= 4095 && cal.yMin <= 4095 && cal.yMax <= 4095;
}

static int mapCalibrated(uint16_t value, uint16_t inA, uint16_t inB, int outA, int outB) {
  if (inA == inB) {
    return outA;
  }
  // The saved raw endpoints were measured at inset calibration targets, not at
  // the physical glass edges. Preserve those points as linear anchors and let
  // values beyond them extrapolate; touchpad_read performs the only clamp at
  // the real 320x240 display bounds. Clamping here created dead 24 px bands.
  return map(value, inA, inB, outA, outB);
}

static void saveTouchCalibration() {
  touchPrefs.begin("touch", false);
  touchPrefs.putUShort("xMin", touchCal.xMin);
  touchPrefs.putUShort("xMax", touchCal.xMax);
  touchPrefs.putUShort("yMin", touchCal.yMin);
  touchPrefs.putUShort("yMax", touchCal.yMax);
  touchPrefs.putUChar("xAxis", touchCal.xAxis);
  touchPrefs.putUChar("yAxis", touchCal.yAxis);
  touchPrefs.putBool("valid", true);
  touchPrefs.end();
}

static void loadTouchCalibration() {
  touchPrefs.begin("touch", true);
  touchCal.xMin = touchPrefs.getUShort("xMin", touchCal.xMin);
  touchCal.xMax = touchPrefs.getUShort("xMax", touchCal.xMax);
  touchCal.yMin = touchPrefs.getUShort("yMin", touchCal.yMin);
  touchCal.yMax = touchPrefs.getUShort("yMax", touchCal.yMax);
  touchCal.xAxis = touchPrefs.getUChar("xAxis", touchCal.xAxis);
  touchCal.yAxis = touchPrefs.getUChar("yAxis", touchCal.yAxis);
  touchCal.valid = touchPrefs.getBool("valid", false);
  touchPrefs.end();
  if (!touchCalibrationPlausible(touchCal)) {
    Serial.println("Saved touch calibration is missing or implausible; using factory mapping until calibrated");
    touchCal = defaultTouchCal;
  }
}

// The calibration flow paints with TFT_eSPI directly (LVGL is idle while this
// blocking loop runs); the caller repaints the LVGL screen afterwards.
//
// Every string drawn this way is ASCII only, and deliberately so. TFT_eSPI's
// built-in face here is Font 2, whose glyph table is widtbl_f16[96] -- exactly
// 0x20..0x7F, with no accented characters at any index. The Rajdhani faces that
// carry the European set belong to LVGL, which is not running yet.
//
// That is why these calls stop at English and Finnish, and why the Finnish is
// spelled without its umlauts ("NAYTTOJARJESTELMA", "nayton"). Adding German,
// French or Spanish here would not translate the screen, it would render the
// accented letters as whatever byte happens to sit past the end of the table.
// If these screens ever need the other four languages, the font has to change
// first -- either a Latin-1 bitmap face for TFT_eSPI, or moving the recovery
// flows onto LVGL, which would cost them their independence from it.
static void drawCalibrationTarget(int x, int y, const char *label) {
  tft.fillScreen(TFT_BLACK);
  // Keep all guidance in the open centre between the four corner targets. The
  // earlier LVGL port retained only the crosshairs and accidentally dropped
  // the expected/raw coordinate feedback from the original calibration flow.
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(txt("Touch calibration", "Kosketuskalibrointi"), 160, 82, 2);
  tft.drawString(txt("Tap the crosshair", "Kosketa ristikkoa"), 160, 105, 2);
  tft.setTextColor(accentColor565(), TFT_BLACK);
  tft.drawString(label, 160, 128, 2);

  char expected[48];
  snprintf(expected, sizeof(expected), "Expected: x:%03d y:%03d", x, y);
  tft.setTextColor(0xBDF7, TFT_BLACK);
  tft.drawString(expected, 160, 151, 1);
  tft.drawString("Recorded: waiting...", 160, 168, 1);

  tft.drawCircle(x, y, 13, accentColor565());
  tft.drawCircle(x, y, 6, TFT_WHITE);
  tft.drawFastHLine(x - 18, y, 36, TFT_WHITE);
  tft.drawFastVLine(x, y - 18, 36, TFT_WHITE);
}

static bool readRawTouchPointUntil(uint32_t timeoutMs, TS_Point &result) {
  const uint32_t started = millis();
  while (!touch.touched()) {
    if (millis() - started >= timeoutMs) return false;
    delay(10);
  }

  uint32_t sumX = 0;
  uint32_t sumY = 0;
  uint8_t samples = 0;
  while (touch.touched() && samples < 12) {
    const TS_Point raw = touch.getPoint();
    sumX += raw.x;
    sumY += raw.y;
    samples++;
    delay(20);
  }
  const uint32_t releaseStarted = millis();
  while (touch.touched() && millis() - releaseStarted < 1500) delay(10);
  delay(100);
  if (samples == 0) return false;
  result = TS_Point(sumX / samples, sumY / samples, 0);
  return true;
}

static void mapTouchPoint(const TouchCalibration &cal, const TS_Point &raw, int &x, int &y) {
  x = constrain(mapCalibrated(rawAxisValue(raw, cal.xAxis), cal.xMin, cal.xMax, TOUCH_X_MIN, TOUCH_X_MAX),
                0, SCREEN_W - 1);
  y = constrain(mapCalibrated(rawAxisValue(raw, cal.yAxis), cal.yMin, cal.yMax, 24, 215), 0,
                SCREEN_H - 1);
}

static void showRecordedCalibrationPoint(TS_Point raw) {
  char recorded[48];
  snprintf(recorded, sizeof(recorded), "Recorded: raw x:%04d y:%04d", raw.x, raw.y);
  tft.fillRect(45, 160, 230, 17, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(recorded, 160, 168, 1);
  delay(500);
}

static bool calibrationCornersPlausible(const TouchCalibration &candidate, const TS_Point &topLeft,
                                        const TS_Point &topRight, const TS_Point &bottomRight,
                                        const TS_Point &bottomLeft) {
  if (!touchCalibrationPlausible(candidate)) return false;
  const int32_t xSpan = abs((int32_t)candidate.xMax - candidate.xMin);
  const int32_t ySpan = abs((int32_t)candidate.yMax - candidate.yMin);
  const int32_t leftError = abs((int32_t)rawAxisValue(topLeft, candidate.xAxis) -
                                rawAxisValue(bottomLeft, candidate.xAxis));
  const int32_t rightError = abs((int32_t)rawAxisValue(topRight, candidate.xAxis) -
                                 rawAxisValue(bottomRight, candidate.xAxis));
  const int32_t topError = abs((int32_t)rawAxisValue(topLeft, candidate.yAxis) -
                               rawAxisValue(topRight, candidate.yAxis));
  const int32_t bottomError = abs((int32_t)rawAxisValue(bottomLeft, candidate.yAxis) -
                                  rawAxisValue(bottomRight, candidate.yAxis));
  return leftError <= xSpan / 3 && rightError <= xSpan / 3 && topError <= ySpan / 3 &&
         bottomError <= ySpan / 3;
}

static bool verifyTouchCalibration(const TouchCalibration &candidate) {
  static const int verifyX[] = {160, 62, 258};
  static const int verifyY[] = {120, 178, 62};
  for (uint8_t i = 0; i < 3; i++) {
    char step[20];
    snprintf(step, sizeof(step), "VERIFY %u / 3", i + 1);
    drawCalibrationTarget(verifyX[i], verifyY[i], step);
    TS_Point raw(0, 0, 0);
    if (!readRawTouchPointUntil(8000, raw)) return false;
    int mappedX = 0;
    int mappedY = 0;
    mapTouchPoint(candidate, raw, mappedX, mappedY);
    char recorded[52];
    snprintf(recorded, sizeof(recorded), "Recorded: x:%03d y:%03d", mappedX, mappedY);
    tft.fillRect(45, 160, 230, 17, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(recorded, 160, 168, 1);
    delay(350);
    if (abs(mappedX - verifyX[i]) > 32 || abs(mappedY - verifyY[i]) > 32) return false;
  }
  return true;
}

bool runTouchCalibration() {
  splashShownAtMs = 0;  // the splash is gone; nothing left to hold on screen
  if (lvglStarted) {
    // let the in-flight DMA finish and release the held bus before drawing
    // with blocking TFT calls
    tft.dmaWait();
    tft.endWrite();
  }
  const int left = 24;
  const int right = 295;
  const int top = 24;
  const int bottom = 215;

  const TouchCalibration previous = touchCal;
  auto finishCalibrationDrawing = []() {
    tft.fillScreen(TFT_BLACK);
    if (lvglStarted) {
      tft.startWrite();
      lv_obj_invalidate(lv_scr_act());
    }
  };
  auto calibrationFailed = [&](const char *reason) {
    touchCal = previous;
    Serial.printf("Touch calibration rejected: %s\n", reason);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFBE0, TFT_BLACK);
    tft.drawString(txt("Calibration rejected", "Kalibrointi hylatty"), 160, 105, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(previous.valid ? txt("Previous calibration restored", "Edellinen palautettu")
                                  : txt("Factory mapping restored", "Oletusarvot palautettu"),
                   160, 132, 1);
    delay(1200);
    finishCalibrationDrawing();
  };

  drawCalibrationTarget(left, top, "1 / 4");
  TS_Point topLeft(0, 0, 0);
  if (!readRawTouchPointUntil(12000, topLeft)) {
    calibrationFailed("top-left timed out");
    return false;
  }
  showRecordedCalibrationPoint(topLeft);
  drawCalibrationTarget(right, top, "2 / 4");
  TS_Point topRight(0, 0, 0);
  if (!readRawTouchPointUntil(12000, topRight)) {
    calibrationFailed("top-right timed out");
    return false;
  }
  showRecordedCalibrationPoint(topRight);
  drawCalibrationTarget(right, bottom, "3 / 4");
  TS_Point bottomRight(0, 0, 0);
  if (!readRawTouchPointUntil(12000, bottomRight)) {
    calibrationFailed("bottom-right timed out");
    return false;
  }
  showRecordedCalibrationPoint(bottomRight);
  drawCalibrationTarget(left, bottom, "4 / 4");
  TS_Point bottomLeft(0, 0, 0);
  if (!readRawTouchPointUntil(12000, bottomLeft)) {
    calibrationFailed("bottom-left timed out");
    return false;
  }
  showRecordedCalibrationPoint(bottomLeft);

  const uint16_t rawXHorizontal = (abs(topRight.x - topLeft.x) + abs(bottomRight.x - bottomLeft.x)) / 2;
  const uint16_t rawXVertical = (abs(bottomLeft.x - topLeft.x) + abs(bottomRight.x - topRight.x)) / 2;
  const uint16_t rawYHorizontal = (abs(topRight.y - topLeft.y) + abs(bottomRight.y - bottomLeft.y)) / 2;
  const uint16_t rawYVertical = (abs(bottomLeft.y - topLeft.y) + abs(bottomRight.y - topRight.y)) / 2;

  TouchCalibration candidate = defaultTouchCal;
  candidate.xAxis = rawXHorizontal >= rawYHorizontal ? 0 : 1;
  candidate.yAxis = candidate.xAxis == 0 ? 1 : 0;

  if (candidate.xAxis == 0) {
    candidate.xMin = (topLeft.x + bottomLeft.x) / 2;
    candidate.xMax = (topRight.x + bottomRight.x) / 2;
  } else {
    candidate.xMin = (topLeft.y + bottomLeft.y) / 2;
    candidate.xMax = (topRight.y + bottomRight.y) / 2;
  }

  if (candidate.yAxis == 0) {
    candidate.yMin = (topLeft.x + topRight.x) / 2;
    candidate.yMax = (bottomLeft.x + bottomRight.x) / 2;
  } else {
    candidate.yMin = (topLeft.y + topRight.y) / 2;
    candidate.yMax = (bottomLeft.y + bottomRight.y) / 2;
  }
  candidate.valid = true;

  const uint16_t horizontalMotion = candidate.xAxis == 0 ? rawXHorizontal : rawYHorizontal;
  const uint16_t horizontalLeak = candidate.xAxis == 0 ? rawYHorizontal : rawXHorizontal;
  const uint16_t verticalMotion = candidate.yAxis == 0 ? rawXVertical : rawYVertical;
  const uint16_t verticalLeak = candidate.yAxis == 0 ? rawYVertical : rawXVertical;
  if (horizontalMotion < horizontalLeak * 2 || verticalMotion < verticalLeak * 2 ||
      !calibrationCornersPlausible(candidate, topLeft, topRight, bottomRight, bottomLeft)) {
    calibrationFailed("corner samples are inconsistent");
    return false;
  }
  if (!verifyTouchCalibration(candidate)) {
    calibrationFailed("verification target missed or timed out");
    return false;
  }

  touchCal = candidate;
  touchCal.valid = true;
  saveTouchCalibration();

  Serial.println("Touch calibration saved:");
  Serial.printf("xAxis=%u, xMin=%u, xMax=%u, yAxis=%u, yMin=%u, yMax=%u\n", touchCal.xAxis, touchCal.xMin,
                touchCal.xMax, touchCal.yAxis, touchCal.yMin, touchCal.yMax);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(accentColor565(), TFT_BLACK);
  tft.drawString(txt("Calibration saved", "Kalibrointi tallennettu"), 160, 120, 2);
  delay(800);
  finishCalibrationDrawing();
  return true;
}

// ── LVGL touch input ──────────────────────────────────────────────────────────

// Raw contact, bypassing both LVGL and the calibration mapping. The recovery
// gesture has to work when the stored calibration is exactly what is broken,
// so it may not travel through widget hit-testing or a coordinate transform.
// The BOOT button is the second path, and it is the one that survives a failed
// touch panel. It cannot open the boot-time flow below -- GPIO0 is a strapping
// pin, so holding it across a power-up lands in the serial bootloader instead
// of here -- but from the dashboard it works like any other contact.
bool recoveryInputHeld() {
  return touch.touched() || digitalRead(CYD_BOOT_BUTTON_PIN) == LOW;
}

// The splash used to read the controller before LVGL owned it, and the
// recovery gesture still does. Require a short, clean release at every handoff
// so neither a resting finger nor the input driver's initial state can
// synthesize a tap on whatever screen appears next.
static bool waitingForTouchRelease = true;
static uint8_t touchReleasePolls = 0;

void touchInputArmAfterRecovery() {
  waitingForTouchRelease = true;
  touchReleasePolls = 0;
}

static void touchpad_read(lv_indev_drv_t *, lv_indev_data_t *data) {
  // keep the previous point on release so LVGL evaluates the tap where it began
  static lv_coord_t lastX = 0;
  static lv_coord_t lastY = 0;

  // average a burst of samples: single XPT2046 reads jitter enough to drag a
  // press off the button, which makes LVGL drop the tap
  uint32_t sumX = 0;
  uint32_t sumY = 0;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (!touch.touched()) break;
    TS_Point raw = touch.getPoint();
    sumX += raw.x;
    sumY += raw.y;
    samples++;
  }
  if (waitingForTouchRelease) {
    if (samples == 0) {
      if (touchReleasePolls < 3) touchReleasePolls++;
      if (touchReleasePolls >= 3) waitingForTouchRelease = false;
    } else {
      touchReleasePolls = 0;
    }
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = lastX;
    data->point.y = lastY;
    return;
  }
  // debounce release: the XPT2046 pressure reading can flicker "not touched"
  // for a single poll mid-press, which would split one tap into two
  static uint8_t releasedPolls = 3;
  if (samples == 0) {
    if (releasedPolls < 3) releasedPolls++;
    data->state = releasedPolls >= 3 ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
    data->point.x = lastX;
    data->point.y = lastY;
    return;
  }
  releasedPolls = 0;
  const uint16_t rawX = sumX / samples;
  const uint16_t rawY = sumY / samples;
  const uint16_t rawForX = touchCal.xAxis == 0 ? rawX : rawY;
  const uint16_t rawForY = touchCal.yAxis == 0 ? rawX : rawY;
  lastX = constrain(mapCalibrated(rawForX, touchCal.xMin, touchCal.xMax, TOUCH_X_MIN, TOUCH_X_MAX), 0,
                    SCREEN_W - 1);
  lastY = constrain(mapCalibrated(rawForY, touchCal.yMin, touchCal.yMax, 24, 215), 0, SCREEN_H - 1);
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = LV_INDEV_STATE_PRESSED;
}

// ── Boot splash ───────────────────────────────────────────────────────────────
//
// SVG artwork is compiled offline into solid triangles. No SVG parser, heap
// allocation or full-screen sprite is needed before LVGL initializes.
// Keep the caption as real text so release versions never require retracing.
constexpr uint32_t SPLASH_MIN_MS = 700;

static void drawSplash() {
  tft.fillScreen(TFT_BLACK);
  for (const auto &triangle : kajo_splash::triangles) {
    tft.fillTriangle(triangle.x0, triangle.y0, triangle.x1, triangle.y1,
                     triangle.x2, triangle.y2, triangle.color);
  }
  char line[48];
  snprintf(line, sizeof(line), "Dash Firmware v%s", CYD_FIRMWARE_VERSION_LABEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Font 2 is already present for calibration/recovery. No new font payload.
  tft.drawString(line, 160, 220, 2);
}

static void rampBacklight(int fromPct, int toPct, uint32_t durationMs) {
  const int steps = 28;
  for (int i = 1; i <= steps; i++) {
    writeBacklightPercent(fromPct + (toPct - fromPct) * i / steps);
    delay(durationMs / steps);
  }
}

// Swap what the panel shows without ever showing the swap: dip the backlight,
// redraw behind it, bring it back.
static void crossFadeTo(void (*paint)(), uint32_t outMs, uint32_t inMs) {
  const int target = configuredBrightnessPercent();
  rampBacklight(target, 0, outMs);
  writeBacklightPercent(0);
  paint();
  rampBacklight(0, target, inMs);
  applyDisplayBrightness();
}

static void paintFirstUiFrame() {
  lv_refr_now(NULL);
  tft.dmaWait();  // the final flush is still streaming when lv_refr_now returns
}

// Finish the black artwork's minimum visible hold, then reveal the first UI
// frame. Recovery/calibration clear splashShownAtMs and bypass that hold.
static void fadeSplashIntoUi() {
  if (splashShownAtMs) {
    while (millis() - splashShownAtMs < SPLASH_MIN_MS) delay(10);
  }
  crossFadeTo(paintFirstUiFrame, 140, 180);
}

// ── Boot recovery hold ────────────────────────────────────────────────────────
//
// The everyday way into touch calibration is a press-and-hold on the
// dashboard, handled by screens.cpp. This is the boot-time half of the same
// gesture, and it exists for the one case the dashboard cannot cover: a stored
// setting or a controller backend that makes boot hang or crash. That never
// reaches a dashboard, so a recovery path living there would be unreachable.
// Running here -- before controllerManagerBegin(), rideLoggerBegin() and
// loadBatteryStats() -- keeps a door open into a board that cannot finish
// booting, and costs nothing on a normal power-up because the whole flow is
// skipped unless something is already being held.
//
// The gesture advertises nothing: the boot splash stays up untouched until the
// hold has lasted long enough to be unambiguous, and only then does this take
// the screen over.
//
// Every string drawn here is ASCII only, and deliberately so -- see the note
// above drawCalibrationTarget. The fully localized copy now lives on the
// dashboard side, which has LVGL and the Rajdhani faces.

constexpr int kRecoveryArcCx = 160;
constexpr int kRecoveryArcCy = 96;
constexpr int kRecoveryArcR = 30;
constexpr int kRecoveryArcIr = 23;

static void drawRecoveryPrompt(const char *heading, const char *action, const char *escape) {
  splashShownAtMs = 0;  // the splash is gone; nothing left to hold on screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(heading, 160, 152, 2);
  tft.setTextColor(0xBDF7, TFT_BLACK);
  tft.drawString(action, 160, 180, 1);
  tft.drawString(escape, 160, 197, 1);
  tft.drawArc(kRecoveryArcCx, kRecoveryArcCy, kRecoveryArcR, kRecoveryArcIr, 0, 360, 0x4208, TFT_BLACK);
}

// Paints only the newly filled wedge. Repainting the whole ring on every degree
// pushed a few thousand pixels down the bus eight times a second and flickered;
// the empty track is drawn once by drawRecoveryPrompt. Anti-aliasing is off for
// the segments so consecutive wedges butt together without seams. Passing 0 as
// the target restores the empty track.
static void drawRecoveryArcSegment(int fromDegrees, int toDegrees) {
  if (toDegrees <= 0) {
    tft.drawArc(kRecoveryArcCx, kRecoveryArcCy, kRecoveryArcR, kRecoveryArcIr, 0, 360, 0x4208,
                TFT_BLACK);
    return;
  }
  const int start = max(fromDegrees, 0);
  const int end = min(toDegrees, 360);
  if (end <= start) return;
  tft.drawArc(kRecoveryArcCx, kRecoveryArcCy, kRecoveryArcR, kRecoveryArcIr, start, end,
              accentColor565(), TFT_BLACK, false);
}

// Entry gesture, held on the panel from power-on. Silent for the first phase so
// a finger resting on the glass shows nothing at all; the ring then owns the
// rest of the window and releasing during it aborts with the screen left as it
// was found.
static bool runBootRecoveryHold() {
  if (!recoveryInputHeld()) return false;

  const uint32_t started = millis();
  bool prompted = false;
  int lastDegrees = -1;
  while (recoveryInputHeld()) {
    const uint32_t held = millis() - started;
    if (held >= RECOVERY_HOLD_TOTAL_MS) {
      drawRecoveryArcSegment(lastDegrees, 360);
      return true;
    }
    if (held >= RECOVERY_HOLD_SILENT_MS) {
      if (!prompted) {
        prompted = true;
        lastDegrees = 0;
        drawRecoveryPrompt(txt("DISPLAY RECOVERY", "NAYTON PALAUTUS"),
                           txt("Keep holding to calibrate touch", "Pida pohjassa: kosketuskalibrointi"),
                           txt("Release to cancel", "Vapauta peruaksesi"));
      }
      constexpr uint32_t span = RECOVERY_HOLD_TOTAL_MS - RECOVERY_HOLD_SILENT_MS;
      const int degrees = (int)((held - RECOVERY_HOLD_SILENT_MS) * 360UL / span);
      if (degrees != lastDegrees) {
        drawRecoveryArcSegment(lastDegrees, degrees);
        lastDegrees = degrees;
      }
    }
    delay(8);
  }

  if (prompted) tft.fillScreen(TFT_BLACK);
  return false;
}

// Offered only after a boot-hold recovery, and only here rather than through
// the dashboard's LVGL dialog: if a stored setting is what stops this board
// from booting, the reset has to land before the subsystems that read it
// start. Confirmed by holding rather than tapping, because the touch mapping
// is not to be trusted at this point in the flow.
static bool bootConfirmSettingsReset() {
  constexpr uint32_t kConfirmHoldMs = 3000;
  constexpr uint32_t kIdleTimeoutMs = 8000;
  drawRecoveryPrompt(txt("RESET SETTINGS TOO?", "PALAUTETAANKO ASETUKSET?"),
                     txt("Hold to clear all settings", "Pida pohjassa: tyhjenna asetukset"),
                     txt("Wait to keep them", "Odota: sailyta asetukset"));

  uint32_t idleSince = millis();
  uint32_t holdStarted = 0;
  int lastDegrees = 0;
  while (millis() - idleSince < kIdleTimeoutMs) {
    if (!recoveryInputHeld()) {
      if (holdStarted) {
        holdStarted = 0;
        lastDegrees = 0;
        drawRecoveryArcSegment(0, 0);  // releasing empties the ring again
      }
      delay(8);
      continue;
    }
    idleSince = millis();  // an active hold must never time out underneath itself
    if (!holdStarted) holdStarted = millis();
    const uint32_t held = millis() - holdStarted;
    if (held >= kConfirmHoldMs) {
      drawRecoveryArcSegment(lastDegrees, 360);
      delay(200);
      return true;
    }
    const int degrees = (int)(held * 360UL / kConfirmHoldMs);
    if (degrees != lastDegrees) {
      drawRecoveryArcSegment(lastDegrees, degrees);
      lastDegrees = degrees;
    }
    delay(8);
  }
  return false;
}

// Shared tail of both recovery paths. Waiting for a clean release first is not
// optional: readRawTouchPointUntil() samples as soon as the panel reports
// contact, so the finger that opened recovery would otherwise be recorded as
// calibration corner one.
bool runRecoveryCalibration() {
  while (recoveryInputHeld()) delay(8);
  delay(120);
  const bool calibrated = runTouchCalibration();
  touchInputArmAfterRecovery();
  return calibrated;
}

// ── Entry points ──────────────────────────────────────────────────────────────

static uint32_t controllerRestartAtMs = 0;

static void dashboardTimerCb(lv_timer_t *) {
  if (controllerRestartAtMs && static_cast<int32_t>(millis() - controllerRestartAtMs) >= 0) {
    ESP.restart();
    return;
  }
  applyThemeRgbLed();
  serviceAutoBrightness();
  if (dashboardDemoModeEnabled) {
    // The telemetry task skips the logger while demo mode is on, so feed it
    // from here instead. This is the same snapshot the dashboard draws, so a
    // replayed demo ride matches what was on screen. 100 ms ticks cover every
    // sample rate the logger offers.
    const ControllerSnapshot demo = controllerSnapshot();
    rideLoggerSample(demo.values, demo.battery, demo.faultCode, demo.available);
  }
  uiDashboardTick();
  uiAutoReturnTick();
  uiSensorTick();
}

void requestControllerRestart() {
  // Restart on the next LVGL timer pass, after the current button callback
  // returns. No intermediate screen or status notice is shown.
  batteryStatsCheckpoint();
  controllerRestartAtMs = millis();
}

void setup() {
  // The backlight is already lit and the panel is already white:
  // backlightOnFromBoot claimed the pin from the global constructor pass,
  // before the Arduino core started. Preserve white through panel bring-up,
  // then fade into the artwork before starting the remaining subsystems.
  Serial.begin(115200);

  // VESC communication settings must be available before the UART starts.
  loadAppSettings();
  loadDisplayPanelProfile();

  tft.init();
  setupBacklight();  // hand TFT_BL to the PWM channel; brightness is unchanged
  // Blank the panel for the rest of the bring-up. A blanked panel is undriven,
  // which is the same white it has been showing since power-on, so nothing
  // changes on screen -- but it hides the undefined GRAM that tft.init() just
  // made visible with its closing DISPON, and it hides the inversion and gamma
  // changes applyDisplayPanelProfile() is about to make. The display comes back
  // only once white is genuinely in GRAM, which is a transition to itself.
  tft.writecommand(TFT_DISPOFF);
  tft.setRotation(1);
  probeDisplayController();
  autoDetectDisplayPanelProfile(false);
  applyDisplayPanelProfile();
  tft.fillScreen(TFT_WHITE);
  tft.writecommand(TFT_DISPON);
  crossFadeTo(drawSplash, 150, 250);
  splashShownAtMs = millis();

  touch.begin();
  pinMode(CYD_BOOT_BUTTON_PIN, INPUT_PULLUP);
  loadTouchCalibration();
  // Boot recovery runs its calibration and its settings reset here, ahead of
  // every subsystem that reads persisted state, so a board that cannot finish
  // booting can still be rescued. The dashboard hosts the same gesture for the
  // ordinary case, where LVGL is up and the dialog can be localized.
  if (runBootRecoveryHold()) {
    runRecoveryCalibration();
    if (bootConfirmSettingsReset()) resetAppSettings(false);
    tft.fillScreen(TFT_BLACK);
  }

  rideLoggerBegin();
  // Load lifetime energy/wear before the manager publishes its first coherent
  // controller snapshot. Boot recovery may have reset the selected backend, so
  // selection and transport startup deliberately happen here, after recovery.
  loadBatteryStats();
  controllerManagerBegin();
  setupThemeRgbLed();
  applyThemeRgbLed();
  // LDR: 0 dB attenuation (~1.1 V full scale). The divider only swings a
  // couple hundred millivolts, so the default 11 dB range wasted 4x of the
  // resolution and pushed bright readings into the ADC's ~100 mV dead zone.
  analogSetPinAttenuation(CYD_LDR_PIN, ADC_0db);
  // only calibrate when nothing is saved yet; the settings menu has a touch
  // calibration option in the Display submenu, and a press-and-hold anywhere
  // on the dashboard reaches the same flow when that menu is out of reach
  if (!touchCal.valid) {
    runTouchCalibration();
    touchInputArmAfterRecovery();
  }

  tft.initDMA();
  tft.setSwapBytes(false);
  tft.startWrite();  // hold the bus: display is alone on HSPI

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, lvgl_buf2, SCREEN_W * BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);
  lvglStarted = true;

  lv_timer_create(dashboardTimerCb, 100, NULL);

  // Boot always lands on the dashboard. The first-boot configurator runs only
  // when no configuration has been stored yet: a freshly flashed board, or one
  // whose settings were reset (both leave "configured" false in NVS).
  if (!firstBootConfigured) {
    configStep = 0;
    uiShow(SCREEN_CONFIG);
  } else {
    uiShow(SCREEN_DASHBOARD);
  }
  fadeSplashIntoUi();
  // The Arduino core is configured for OTA rollback, but its default weak
  // hook confirms a new image before setup(). Our override keeps it pending
  // until setup has completed and the LVGL loop has remained healthy.
  firmwareUpdateBootValidationBegin();

  Serial.println("LVGL firmware ready");
}

void loop() {
  const uint32_t handlerStartedUs = micros();
  lv_timer_handler();
  const uint32_t handlerUs = micros() - handlerStartedUs;
  if (LVGL_PERFORMANCE_LOG_ENABLED) {
    perfHandlerCount++;
    perfHandlerTotalUs += handlerUs;
    perfHandlerMaxUs = max(perfHandlerMaxUs, handlerUs);
  }
  servicePerformanceLog();
  firmwareUpdateBleService();
  companionBleService();
  firmwareUpdateBootValidationService();
  delay(5);
}
