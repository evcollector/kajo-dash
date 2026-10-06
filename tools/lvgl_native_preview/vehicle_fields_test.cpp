// Tests for the vehicle/settings field model.
//
// These values are the ones a rider types on a numeric keypad, so the parsing
// and clamping is the whole contract: a typed string goes in, a clamped global
// comes out, and the tile has to render it back. None of that needs a display,
// which is why this links vehicle_fields.cpp and app_logic.cpp on their own
// rather than going through the screen stack.
//
// The bounds asserted here are the ones vehicleFieldInputHint() quotes to the
// rider ("Series cell count, 4-32 S", "UART speed, 9600-921600 baud"). If a
// range moves, the hint text has to move with it.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Preferences.h"
#include "app_state.h"
#include "vehicle_fields.h"

// preview_stubs.cpp: decides whether the controller is reporting its own speed,
// which is what gives VEHICLE_FIELD_WHEEL_MM its two meanings.
void previewSetTelemetryLink(TelemetryLink state);

namespace {

int failures = 0;

void expect(bool ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

// Types `text` into `field` and reports the resulting global through `actual`,
// which must be bound by reference: the value is read after the save, not
// captured before it.
template <typename T>
void expectSaved(int field, const char *text, const T &actual, long wanted, const char *what) {
  saveVehicleInputValue(field, text);
  if ((long)actual != wanted) {
    printf("FAIL: %s (typed %s, got %ld, want %ld)\n", what, text, (long)actual, wanted);
    failures++;
  }
}

void expectText(const char *actual, const char *wanted, const char *what) {
  if (strcmp(actual, wanted) != 0) {
    printf("FAIL: %s (got [%s], want [%s])\n", what, actual, wanted);
    failures++;
  }
}

void expectValue(int field, const char *wanted, const char *what) {
  char buffer[64];
  vehicleFieldValue(field, buffer, sizeof(buffer));
  expectText(buffer, wanted, what);
}

void expectEditText(int field, const char *wanted, const char *what) {
  char buffer[64];
  vehicleFieldEditText(field, buffer, sizeof(buffer));
  expectText(buffer, wanted, what);
}

// -- Clamping ---------------------------------------------------------------
// Each field is pushed below its minimum, set to both ends of its range, and
// pushed above its maximum. A range that quietly widens shows up here.

void testNumericClamps() {
  expectSaved(VEHICLE_FIELD_BATTERY_S, "3", batterySeriesCount, 4, "battery series below minimum");
  expectSaved(VEHICLE_FIELD_BATTERY_S, "4", batterySeriesCount, 4, "battery series at minimum");
  expectSaved(VEHICLE_FIELD_BATTERY_S, "32", batterySeriesCount, 32, "battery series at maximum");
  expectSaved(VEHICLE_FIELD_BATTERY_S, "99", batterySeriesCount, 32, "battery series above maximum");

  expectSaved(VEHICLE_FIELD_BATTERY_AH, "0.5", batteryCapacityDeciAh, 10, "battery Ah below minimum");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "1.0", batteryCapacityDeciAh, 10, "battery Ah at minimum");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "99.9", batteryCapacityDeciAh, 999, "battery Ah at maximum");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "150", batteryCapacityDeciAh, 999, "battery Ah above maximum");

  expectSaved(VEHICLE_FIELD_BATTERY_MAX_A, "0", batteryMaxAmps, 1, "battery amps below minimum");
  expectSaved(VEHICLE_FIELD_BATTERY_MAX_A, "500", batteryMaxAmps, 500, "battery amps at maximum");
  expectSaved(VEHICLE_FIELD_BATTERY_MAX_A, "9999", batteryMaxAmps, 500, "battery amps above maximum");

  expectSaved(VEHICLE_FIELD_MOTOR_MAX_A, "0", motorMaxAmps, 1, "motor amps below minimum");
  expectSaved(VEHICLE_FIELD_MOTOR_MAX_A, "500", motorMaxAmps, 500, "motor amps at maximum");
  expectSaved(VEHICLE_FIELD_MOTOR_MAX_A, "9999", motorMaxAmps, 500, "motor amps above maximum");

  expectSaved(VEHICLE_FIELD_CONT_KW, "0.0", continuousPowerDeciKw, 1, "continuous kW below minimum");
  expectSaved(VEHICLE_FIELD_CONT_KW, "50.0", continuousPowerDeciKw, 500, "continuous kW at maximum");
  expectSaved(VEHICLE_FIELD_CONT_KW, "99.9", continuousPowerDeciKw, 500, "continuous kW above maximum");

  expectSaved(VEHICLE_FIELD_PEAK_KW, "0.0", peakPowerDeciKw, 1, "peak kW below minimum");
  expectSaved(VEHICLE_FIELD_PEAK_KW, "50.0", peakPowerDeciKw, 500, "peak kW at maximum");

  // One slot, two meanings. With no live controller it is a wheel diameter in
  // millimetres; with one reporting its own speed it becomes a correction
  // percentage, and the ranges are completely different.
  previewSetTelemetryLink(LINK_LOST);
  expect(!telemetrySpeedFromController(), "a lost link means the slot is a wheel diameter");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "100", wheelDiameterMm, 300, "wheel diameter below minimum");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "700", wheelDiameterMm, 700, "wheel diameter mid-range");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "2000", wheelDiameterMm, 1000, "wheel diameter above maximum");

  previewSetTelemetryLink(LINK_LIVE);
  expect(telemetrySpeedFromController(), "a live link means the slot is a speed calibration");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "10", speedCalibrationPercent, 50, "speed calibration below minimum");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "103", speedCalibrationPercent, 103, "speed calibration mid-range");
  expectSaved(VEHICLE_FIELD_WHEEL_MM, "400", speedCalibrationPercent, 200, "speed calibration above maximum");
  expect(wheelDiameterMm == 1000, "editing the calibration leaves the wheel diameter alone");
  previewSetTelemetryLink(LINK_LOST);

  expectSaved(VEHICLE_FIELD_VESC_BAUD, "1200", vescUartBaud, 9600, "UART baud below minimum");
  expectSaved(VEHICLE_FIELD_VESC_BAUD, "115200", vescUartBaud, 115200, "UART baud mid-range");
  expectSaved(VEHICLE_FIELD_VESC_BAUD, "999999", vescUartBaud, 921600, "UART baud above maximum");

  expectSaved(VEHICLE_FIELD_VESC_CAN_ID, "0", vescCanId, 0, "CAN id at minimum");
  expectSaved(VEHICLE_FIELD_VESC_CAN_ID, "254", vescCanId, 254, "CAN id at maximum");
  expectSaved(VEHICLE_FIELD_VESC_CAN_ID, "300", vescCanId, 254, "CAN id above maximum");

  expectSaved(VEHICLE_FIELD_MOTOR_POLE_PAIRS, "0", vescMotorPolePairs, 1, "pole pairs below minimum");
  expectSaved(VEHICLE_FIELD_MOTOR_POLE_PAIRS, "64", vescMotorPolePairs, 64, "pole pairs at maximum");
  expectSaved(VEHICLE_FIELD_MOTOR_POLE_PAIRS, "100", vescMotorPolePairs, 64, "pole pairs above maximum");

  expectSaved(VEHICLE_FIELD_DRIVE_RATIO, "0.05", vescDriveRatioHundredths, 10, "drive ratio below minimum");
  expectSaved(VEHICLE_FIELD_DRIVE_RATIO, "2.50", vescDriveRatioHundredths, 250, "drive ratio mid-range");
  expectSaved(VEHICLE_FIELD_DRIVE_RATIO, "200", vescDriveRatioHundredths, 10000, "drive ratio above maximum");

  expectSaved(SPEED_FIELD_BASE + SPEED_FIELD_POWER_CURVE, "150", speedPowerCurvePercent, 100,
              "power curve above maximum");
  expectSaved(SPEED_FIELD_BASE + SPEED_FIELD_ACCEL_CURVE, "150", speedAccelCurvePercent, 100,
              "accel curve above maximum");
}

// -- Fixed-point parsing ----------------------------------------------------
// parseDeciValue multiplies by ten and rounds; the tenth has to survive, and a
// value with no decimal point must not be read as tenths.

void testDeciParsing() {
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "20.4", batteryCapacityDeciAh, 204, "Ah keeps its tenth");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "20.6", batteryCapacityDeciAh, 206, "Ah keeps a rounded-up tenth");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "20", batteryCapacityDeciAh, 200, "whole Ah is not read as tenths");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "20.44", batteryCapacityDeciAh, 204, "extra digits round down");
  expectSaved(VEHICLE_FIELD_BATTERY_AH, "20.46", batteryCapacityDeciAh, 205, "extra digits round up");
  expectSaved(VEHICLE_FIELD_DRIVE_RATIO, "3.75", vescDriveRatioHundredths, 375, "drive ratio keeps hundredths");
}

// -- Speed round-trip -------------------------------------------------------
// A speed leaves storage in km/h, is shown in the rider's units, and is typed
// back in those units. The conversion out and the conversion in have to agree,
// in every unit mode, or a setting drifts a little every time it is opened.

void testSpeedRoundTrip() {
  const UnitMode modes[] = {UNITS_METRIC, UNITS_IMPERIAL, UNITS_NAUTICAL, UNITS_MACH};
  const char *modeNames[] = {"metric", "imperial", "nautical", "mach"};
  const uint32_t speeds[] = {25, 45, 100, 180};

  for (int m = 0; m < 4; m++) {
    unitMode = modes[m];
    for (uint32_t original : speeds) {
      topSpeedKmh = 250;  // headroom, so normalisation never clips the sample
      speedMode1Kmh = original;

      char edit[32];
      vehicleFieldEditText(SPEED_FIELD_BASE + SPEED_FIELD_MODE_1, edit, sizeof(edit));
      saveVehicleInputValue(SPEED_FIELD_BASE + SPEED_FIELD_MODE_1, edit);

      // The displayed value is rounded to whole units (three decimals for
      // Mach), so one km/h of slack is the honest tolerance here.
      const long drift = (long)speedMode1Kmh - (long)original;
      if (drift < -1 || drift > 1) {
        printf("FAIL: %s speed round-trip (%u km/h shown as %s, came back as %u)\n", modeNames[m],
               (unsigned)original, edit, (unsigned)speedMode1Kmh);
        failures++;
      }
    }
  }
  unitMode = UNITS_METRIC;
}

// -- Invariants -------------------------------------------------------------
// Mode limits are meaningless above the top speed, so lowering the top speed
// has to pull them down with it.

void testSpeedNormalisation() {
  unitMode = UNITS_METRIC;
  topSpeedKmh = 100;
  speedMode1Kmh = 60;
  speedMode2Kmh = 80;
  speedMode3Kmh = 100;

  saveVehicleInputValue(SPEED_FIELD_BASE + SPEED_FIELD_TOP_SPEED, "50");
  expect(topSpeedKmh == 50, "top speed stored");
  expect(speedMode1Kmh == 50, "mode 1 pulled down to the new top speed");
  expect(speedMode2Kmh == 50, "mode 2 pulled down to the new top speed");
  expect(speedMode3Kmh == 50, "mode 3 pulled down to the new top speed");

  // A mode typed above the top speed is clamped on the way in.
  saveVehicleInputValue(SPEED_FIELD_BASE + SPEED_FIELD_MODE_2, "90");
  expect(speedMode2Kmh <= topSpeedKmh, "mode 2 cannot be typed above the top speed");
}

// -- Text fields ------------------------------------------------------------
// The backing buffers are small and fixed; an over-long paste must truncate
// rather than run off the end of one.

void testTextFields() {
  const char *tooLong = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  // 40 chars

  saveVehicleInputValue(VEHICLE_FIELD_OEM, tooLong);
  expect(strlen(oemName) == vehicleFieldTextSize(VEHICLE_FIELD_OEM) - 1, "OEM name truncated to its buffer");

  saveVehicleInputValue(VEHICLE_FIELD_NAME, tooLong);
  expect(strlen(vehicleName) == vehicleFieldTextSize(VEHICLE_FIELD_NAME) - 1,
         "vehicle name truncated to its buffer");

  saveVehicleInputValue(VEHICLE_FIELD_OEM, "Sur-Ron");
  expectText(oemName, "Sur-Ron", "OEM name stored verbatim");
  expectValue(VEHICLE_FIELD_OEM, "Sur-Ron", "OEM tile shows the stored name");

  saveVehicleInputValue(VEHICLE_FIELD_OEM, "");
  expectValue(VEHICLE_FIELD_OEM, "-", "empty text field shows a dash, not blank");
}

// -- Rendering --------------------------------------------------------------

void testFormatting() {
  unitMode = UNITS_METRIC;

  batteryCapacityDeciAh = 205;
  expectValue(VEHICLE_FIELD_BATTERY_AH, "20.5 Ah", "battery capacity tile");
  expectEditText(VEHICLE_FIELD_BATTERY_AH, "20.5", "battery capacity edit text drops the unit");

  batterySeriesCount = 20;
  expectValue(VEHICLE_FIELD_BATTERY_S, "20S", "battery series tile");

  continuousPowerDeciKw = 72;
  expectValue(VEHICLE_FIELD_CONT_KW, "7.2 kW", "continuous power tile");

  vescDriveRatioHundredths = 250;
  expectValue(VEHICLE_FIELD_DRIVE_RATIO, "2.50 : 1", "drive ratio tile");
  expectEditText(VEHICLE_FIELD_DRIVE_RATIO, "2.50", "drive ratio edit text");

  vescCanId = 0;
  expectValue(VEHICLE_FIELD_VESC_CAN_ID, "0 (local)", "CAN id 0 is labelled local");
  vescCanId = 7;
  expectValue(VEHICLE_FIELD_VESC_CAN_ID, "7", "non-zero CAN id is bare");

  previewSetTelemetryLink(LINK_LOST);
  wheelDiameterMm = 480;
  expectValue(VEHICLE_FIELD_WHEEL_MM, "480 mm", "wheel diameter tile");

  // The same slot renders as a multiplier once the controller reports speed.
  previewSetTelemetryLink(LINK_LIVE);
  speedCalibrationPercent = 103;
  expectValue(VEHICLE_FIELD_WHEEL_MM, "1.03x", "speed calibration tile");
  expectEditText(VEHICLE_FIELD_WHEEL_MM, "103", "speed calibration is typed as a percent");
  previewSetTelemetryLink(LINK_LOST);

  topSpeedKmh = 250;
  speedMode1Kmh = 52;
  expectValue(SPEED_FIELD_BASE + SPEED_FIELD_MODE_1, "52 KM/H", "speed tile carries its unit");
  expectEditText(SPEED_FIELD_BASE + SPEED_FIELD_MODE_1, "52", "speed edit text drops the unit");

  unitMode = UNITS_IMPERIAL;
  expectValue(SPEED_FIELD_BASE + SPEED_FIELD_MODE_1, "32 MPH", "speed tile converts to the rider's units");
  unitMode = UNITS_METRIC;

  speedPowerCurvePercent = 65;
  expectValue(SPEED_FIELD_BASE + SPEED_FIELD_POWER_CURVE, "65%", "power curve tile");
}

// -- Field metadata ---------------------------------------------------------
// Classification drives which keyboard the input screen puts up, and the title
// and hint are what the rider sees above it. Checked across every field rather
// than by restating the table for a few of them.

void testFieldMetadata() {
  // No field is both kinds, and every editable one has a title and a hint.
  for (int field = 0; field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT; field++) {
    if (vehicleFieldIsText(field) && vehicleFieldIsNumeric(field)) {
      printf("FAIL: field %d is classified as both text and numeric\n", field);
      failures++;
    }
    if (!vehicleFieldIsText(field) && !vehicleFieldIsNumeric(field)) continue;
    if (vehicleFieldTitle(field)[0] == 0) {
      printf("FAIL: field %d has no title\n", field);
      failures++;
    }
    if (vehicleFieldInputHint(field)[0] == 0) {
      printf("FAIL: field %d has no input hint\n", field);
      failures++;
    }
  }
}

// -- Battery chemistry --------------------------------------------------------
// The chemistry picks the open-circuit curve, so the same resting voltage has to
// read very differently on a LiFePO4 pack than on a Li-ion one.

void testBatteryChemistry() {
  batterySeriesCount = 10;
  setBatteryChemistry(BATTERY_LIION);
  expectValue(VEHICLE_FIELD_BATTERY_CHEMISTRY, "Li-ion", "chemistry tile shows Li-ion");
  expectValue(VEHICLE_FIELD_CELL_MIN_V, "3.20 V", "Li-ion default minimum");
  expectValue(VEHICLE_FIELD_CELL_NOMINAL_V, "3.60 V", "Li-ion default nominal");
  expectValue(VEHICLE_FIELD_CELL_MAX_V, "4.18 V", "Li-ion default maximum");
  expect(batterySocFromVoltage(10 * 3.80F) == 60, "Li-ion 3.80 V per cell is 60%");
  expect(batterySocFromVoltage(10 * 4.30F) == 100, "Li-ion above the curve clamps to 100%");
  expect(batterySocFromVoltage(10 * 3.00F) == 0, "Li-ion below the curve clamps to 0%");
  expect(fabsf(batteryCellFullVolts() - 4.18F) < 0.001F, "Li-ion full cell voltage keeps the old 4.18 V scale");

  setBatteryChemistry(BATTERY_LIFEPO4);
  expectValue(VEHICLE_FIELD_BATTERY_CHEMISTRY, "LiFePO4", "chemistry tile shows LiFePO4");
  expectValue(VEHICLE_FIELD_CELL_NOMINAL_V, "3.20 V", "LiFePO4 default nominal");
  expectValue(VEHICLE_FIELD_CELL_MAX_V, "3.65 V", "LiFePO4 default maximum");
  expect(batterySocFromVoltage(10 * 3.65F) == 100, "LiFePO4 default maximum reads 100%");
  expect(batterySocFromVoltage(10 * 3.60F) < 100, "LiFePO4 3.60 V per cell is below full charge");
  expect(batterySocFromVoltage(10 * 3.80F) == 100, "3.80 V per cell is a full LiFePO4 cell");
  expect(batterySocFromVoltage(10 * 3.26F) == 50, "LiFePO4 3.26 V per cell is half full");
  expect(batterySocFromVoltage(10 * 2.80F) == 0, "LiFePO4 below the curve is empty");
  expect(fabsf(batteryCellFullVolts() - 3.65F) < 0.001F, "LiFePO4 full cell voltage");

  // Editing the window stretches the curve: 100 % is now the rider's maximum.
  saveVehicleInputValue(VEHICLE_FIELD_CELL_MAX_V, "3.30");
  expect(batteryCellMaxMv == 3300, "typed maximum stored in millivolts");
  expect(batterySocFromVoltage(10 * 3.30F) == 100, "a pack at the rider's maximum reads 100%");
  expect(batterySocFromVoltage(10 * 3.40F) == 100, "above the rider's maximum stays 100%");
  expect(batterySocFromVoltage(10 * 3.10F) < 100, "below the rider's maximum is not full");

  // The window keeps its order and the nominal voltage stays inside it.
  saveVehicleInputValue(VEHICLE_FIELD_CELL_MIN_V, "3.25");
  expect(batteryCellMaxMv - batteryCellMinMv >= 300, "minimum pushed past the maximum keeps a window");
  expect(batteryCellNominalMv > batteryCellMinMv && batteryCellNominalMv < batteryCellMaxMv,
         "nominal stays between minimum and maximum");
  saveVehicleInputValue(VEHICLE_FIELD_CELL_MIN_V, "0.5");
  expect(batteryCellMinMv == 2000, "minimum clamps at 2.00 V");
  saveVehicleInputValue(VEHICLE_FIELD_CELL_MAX_V, "9");
  expect(batteryCellMaxMv == 4500, "maximum clamps at 4.50 V");
  saveVehicleInputValue(VEHICLE_FIELD_CELL_NOMINAL_V, "3.333");
  expect(batteryCellNominalMv == 3330, "nominal keeps its tenth of a volt of precision");
  expectEditText(VEHICLE_FIELD_CELL_NOMINAL_V, "3.33", "nominal edit text");

  // Choosing the same chemistry again restores its defaults.
  setBatteryChemistry(BATTERY_LIFEPO4);
  expect(batteryCellMinMv == 2900 && batteryCellNominalMv == 3200 && batteryCellMaxMv == 3650,
         "reselecting a chemistry restores its default voltages");

  // Chemistry and voltages are stored with the vehicle profile.
  setBatteryChemistry(BATTERY_LIPO);
  saveVehicleInputValue(VEHICLE_FIELD_CELL_MAX_V, "4.10");
  batteryChemistry = BATTERY_LIION;
  batteryCellMaxMv = 4180;
  loadAppSettings();
  expect(batteryChemistry == BATTERY_LIPO, "chemistry did not survive a settings reload");
  expect(batteryCellMaxMv == 4100, "tweaked maximum did not survive a settings reload");

  // An unknown chemistry, or a stored window that lost its order, falls back.
  batteryChemistry = 200;
  batteryCellMinMv = 4000;
  batteryCellMaxMv = 3000;
  saveVehicleProfile();
  loadAppSettings();
  expect(batteryChemistry == BATTERY_LIION, "an unknown stored chemistry should fall back to Li-ion");
  expect(batteryCellMaxMv - batteryCellMinMv >= 300, "a disordered stored window should fall back to the defaults");

  setBatteryChemistry(BATTERY_LIION);
  batterySeriesCount = 20;
}

}  // namespace

int main() {
  // Keep the settings in memory: these tests are about parsing, not storage.
  previewPreferencesConfigure("", true);

  testNumericClamps();
  testDeciParsing();
  testSpeedRoundTrip();
  testSpeedNormalisation();
  testTextFields();
  testFormatting();
  testFieldMetadata();
  testBatteryChemistry();

  if (failures == 0) printf("vehicle field model: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
