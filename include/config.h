#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

constexpr int TFT_BACKLIGHT_PIN = 21;

// The version being worked towards: the next release, not the last one. The
// release builder (option 5 in kajo.bat) releases whatever these say, records
// it in RELEASES.md, and then moves them on to the next version. Edit them by
// hand to choose a different next version.
//
// Monotonic release number covered by the signed OTA manifest. The display
// asks for confirmation before installing a lower one.
#define CYD_FIRMWARE_VERSION_CODE 2UL
// Human-readable name. The numeric code above stays the monotonic value OTA
// compares for rollback; this is display only.
#define CYD_FIRMWARE_VERSION_NAME "0.02"

// What the display shows as its version, on the boot splash and in Settings.
// Test packages from the release builder are built with
// CYD_FIRMWARE_TEST_BUILD (the kajo_test_package environment), so a board
// running one says so; they carry the same version code as the release they
// lead up to.
#ifdef CYD_FIRMWARE_TEST_BUILD
#define CYD_FIRMWARE_VERSION_LABEL CYD_FIRMWARE_VERSION_NAME "-test"
#else
#define CYD_FIRMWARE_VERSION_LABEL CYD_FIRMWARE_VERSION_NAME
#endif

// VESC UART on the CN1 connector (GND / IO22 / IO27 / 3V3): one JST cable
// carries the whole link. GPIO16/17 must stay free — on the CYD they are
// hard-wired to the RGB LED's green/blue legs and not broken out anywhere.
constexpr int VESC_RX_PIN = 27;  // CN1: VESC TX -> IO27
constexpr int VESC_TX_PIN = 22;  // CN1: VESC RX <- IO22
constexpr unsigned long VESC_BAUD = 115200;

// Drivetrain conversion for VESC electrical RPM/tachometer values. Set the
// pole-pair count to half the motor magnet count. DRIVE_RATIO is motor turns
// per wheel turn (1.0 for a hub motor).
constexpr uint8_t VESC_MOTOR_POLE_PAIRS = 7;
constexpr float VESC_DRIVE_RATIO = 1.0F;
constexpr uint32_t VESC_CONNECTED_POLL_MS = 100;
constexpr uint32_t VESC_DISCONNECTED_POLL_MS = 500;
constexpr uint32_t VESC_STALE_AFTER_MS = 1500;

// Print one compact LVGL workload line over USB serial at this interval.
// Disable after performance tuning if serial diagnostics are not wanted.
constexpr bool LVGL_PERFORMANCE_LOG_ENABLED = true;
constexpr uint32_t LVGL_PERFORMANCE_LOG_INTERVAL_MS = 5000;

// ESP32-2432S028R / CYD LED1 RGB LED pins (fixed by the board, active-low).
constexpr bool CYD_RGB_LED_ENABLED = true;
constexpr int CYD_RGB_LED_RED_PIN = 4;
constexpr int CYD_RGB_LED_GREEN_PIN = 16;
constexpr int CYD_RGB_LED_BLUE_PIN = 17;

// Front LDR light sensor (fixed by the board; input-only ADC1 pin, read
// with 0 dB attenuation). Raw reads are HIGH in the dark and LOW in bright
// light; bright conditions bottom out near 0 because of the ESP32 ADC's
// low-end dead zone. Tune the endpoints with the live reading shown in the
// DISPLAY submenu (cover the sensor -> DARK_RAW, dim room -> BRIGHT_RAW).
constexpr int CYD_LDR_PIN = 34;
constexpr int CYD_LDR_DARK_RAW = 900;    // raw fully covered  -> min brightness
constexpr int CYD_LDR_BRIGHT_RAW = 100;  // raw in normal light -> max brightness

// CYD_* names throughout this firmware describe the ESP32-2432S028R "Cheap
// Yellow Display" board -- its pinout, its panel variants, its icon and layout
// assets. They are deliberately not branded: the product is KAJO-Dash, the
// board it runs on is a CYD, and collapsing the two would lose the distinction
// a reader needs when wiring or picking a panel profile.

// On-board peripherals. The SD socket owns hardware VSPI; the low-bandwidth
// resistive touch controller is read with software SPI on its fixed pins.
constexpr int CYD_SD_CS_PIN = 5;
constexpr int CYD_SD_SCLK_PIN = 18;
constexpr int CYD_SD_MISO_PIN = 19;
constexpr int CYD_SD_MOSI_PIN = 23;
constexpr int CYD_TOUCH_CS_PIN = 33;
constexpr int CYD_TOUCH_IRQ_PIN = 36;
constexpr int CYD_TOUCH_SCLK_PIN = 25;
constexpr int CYD_TOUCH_MISO_PIN = 39;
constexpr int CYD_TOUCH_MOSI_PIN = 32;

// Recovery gesture. Press and hold anywhere on the dashboard, or hold the
// on-board BOOT button, to reach touch calibration when a bad calibration has
// locked the settings menu out of reach. Nothing is drawn during the silent
// phase so incidental contact -- rain, a sleeve, a thumb parked at a stop --
// never flashes anything at the rider; the progress ring then owns the rest of
// the window and releasing during it aborts.
//
// GPIO0 is the BOOT button, otherwise unused by this firmware, and it is what
// keeps a recovery path alive when the touch panel itself is the fault: the
// dashboard still renders, it just cannot be tapped. It is a strapping pin, so
// holding it through a power-up enters the ESP32 serial bootloader instead of
// running this firmware at all -- the boot-time half of the gesture is
// therefore reached by holding the panel, and BOOT only serves the gesture
// once the dashboard is up.
constexpr int CYD_BOOT_BUTTON_PIN = 0;
constexpr uint32_t RECOVERY_HOLD_SILENT_MS = 2000;
constexpr uint32_t RECOVERY_HOLD_TOTAL_MS = 5000;

#endif // CONFIG_H
