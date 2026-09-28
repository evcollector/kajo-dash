#pragma once
#include <stddef.h>

#include "app_state.h"

// Field ids. Vehicle Info, Controller Configuration and the speed screens all
// index into these; the SPEED_FIELD_* ids are passed around offset by
// SPEED_FIELD_BASE so one id space covers both enums.
enum VehicleField {
  VEHICLE_FIELD_OEM,
  VEHICLE_FIELD_NAME,
  VEHICLE_FIELD_MOTOR,
  VEHICLE_FIELD_CONTROLLER,
  VEHICLE_FIELD_BUILD_ID,
  VEHICLE_FIELD_BATTERY_S,
  VEHICLE_FIELD_BATTERY_AH,
  VEHICLE_FIELD_BATTERY_MAX_A,
  VEHICLE_FIELD_MOTOR_MAX_A,
  VEHICLE_FIELD_CONT_KW,
  VEHICLE_FIELD_PEAK_KW,
  VEHICLE_FIELD_WHEEL_MM,
  VEHICLE_FIELD_SPEED_SETUP,
  VEHICLE_FIELD_SETUP,
  VEHICLE_FIELD_VESC_BAUD,
  VEHICLE_FIELD_VESC_CAN_ID,
  VEHICLE_FIELD_MOTOR_POLE_PAIRS,
  VEHICLE_FIELD_DRIVE_RATIO,
  VEHICLE_FIELD_MODE_LABEL_1,
  VEHICLE_FIELD_MODE_LABEL_2,
  VEHICLE_FIELD_MODE_LABEL_3,
  VEHICLE_FIELD_COUNT
};

enum SpeedField {
  SPEED_FIELD_TOP_SPEED,
  SPEED_FIELD_MODE_1,
  SPEED_FIELD_MODE_2,
  SPEED_FIELD_MODE_3,
  SPEED_FIELD_POWER_CURVE,
  SPEED_FIELD_ACCEL_CURVE,
  SPEED_FIELD_COUNT
};

constexpr int SPEED_FIELD_BASE = VEHICLE_FIELD_COUNT;

// The vehicle/settings field model: the mapping from a VEHICLE_FIELD_* or
// SPEED_FIELD_* id to its title, hint, displayed value, editable text, and the
// clamping rules applied when a typed value is stored back.
//
// This is deliberately free of LVGL: it reads and writes the app_state globals
// and formats them as strings, and knows nothing about the widgets that show
// them. That is what lets tools/lvgl_native_preview/vehicle_fields_test.cpp
// exercise the parsing and clamping directly, without a display.
//
// Bounds live in exactly one place -- saveVehicleInputValue -- and the hints
// returned by vehicleFieldInputHint quote those same numbers to the rider, so
// the two are expected to stay in step.

// True when the field stores free text rather than a number. Drives whether
// the input screen shows the alphanumeric keyboard or the numeric keypad.
bool vehicleFieldIsText(int field);
bool vehicleFieldIsNumeric(int field);

// Localised tile title and the one-line explanation shown on the input screen.
const char *vehicleFieldTitle(int field);
const char *vehicleFieldInputHint(int field);

// Capacity of the backing buffer for a text field, used to cap input length.
// Returns 0 for numeric fields.
size_t vehicleFieldTextSize(int field);

// The value as shown on a menu tile, with its unit ("52 km/h", "20.0 Ah").
void vehicleFieldValue(int field, char *buffer, size_t size);

// The same value as the editable string seeded into the text area: no unit,
// and speeds already converted into the rider's display units.
void vehicleFieldEditText(int field, char *buffer, size_t size);

// Parses text back into the field, clamps it to the field's range and saves
// the vehicle profile. Speeds arrive in display units and are converted to
// km/h; mode limits are re-normalised against top speed afterwards.
void saveVehicleInputValue(int field, const char *text);
