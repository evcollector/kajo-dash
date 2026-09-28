#include "controller_backends.h"

#ifndef CYD_LVGL_PREVIEW

#include <Arduino.h>

#include "config.h"
#include "fardriver_ble.h"
#include "vesc_ble.h"
#include "vesc_protocol.h"

namespace {

VescValues vescData = {};
HardwareSerial vescSerial(2);
Stream *vescTransport = nullptr;
bool vescUsingBle = false;
bool vescConnected = false;
uint32_t vescLastSuccessMs = 0;
uint32_t vescNextDisconnectedAttemptMs = 0;
uint32_t vescLastFwAttemptMs = 0;
uint8_t vescFwMajor = 0;
uint8_t vescFwMinor = 0;
long tripStartTachometerAbs = 0;
float tripStartDistanceM = 0.0F;
uint32_t vescRideStartedAtMs = 0;
bool countersFromSetup = false;

bool setupValuesSupported = false;
bool setupValuesProbed = false;
VescSetupValues liveSetup = {};
bool liveSetupValid = false;
uint8_t setupFailures = 0;

float farDriverTripKm = 0.0F;
uint32_t farDriverLastIntegrateMs = 0;
uint32_t farDriverRideStartedAtMs = 0;
bool farDriverAnnounced = false;

const uint32_t VESC_REPLY_TIMEOUT_MS = 100;

float wheelCircumferenceMeters() {
  return PI * wheelDiameterMm / 1000.0F;
}

float speedCalibration() {
  return constrain((int)speedCalibrationPercent, 50, 200) / 100.0F;
}

float tachometerToKm(long tachometer) {
  const float driveRatio = max(1, (int)vescDriveRatioHundredths) / 100.0F;
  const float polePairs = max<uint8_t>(1, vescMotorPolePairs);
  const float wheelTurns = fabsf((float)tachometer) / (6.0F * polePairs * driveRatio);
  return wheelTurns * wheelCircumferenceMeters() / 1000.0F;
}

void resetVescSession() {
  vescConnected = false;
  vescLastSuccessMs = 0;
  vescNextDisconnectedAttemptMs = 0;
  vescLastFwAttemptMs = 0;
  vescFwMajor = 0;
  vescFwMinor = 0;
  tripStartTachometerAbs = 0;
  tripStartDistanceM = 0.0F;
  vescRideStartedAtMs = 0;
  countersFromSetup = false;
  setupValuesSupported = false;
  setupValuesProbed = false;
  liveSetup = {};
  liveSetupValid = false;
  setupFailures = 0;
}

void vescSendPacket(const uint8_t *payload, uint8_t len) {
  uint8_t frame[16];
  const size_t frameLen = vescBuildFrame(payload, len, frame, sizeof(frame));
  if (frameLen && vescTransport) vescTransport->write(frame, frameLen);
}

bool vescReadByte(uint8_t &out, uint32_t deadline) {
  while ((int32_t)(millis() - deadline) < 0) {
    if (vescTransport && vescTransport->available()) {
      out = (uint8_t)vescTransport->read();
      return true;
    }
    vTaskDelay(1);
  }
  return false;
}

int vescReadPacket(uint8_t *payload, size_t capacity, uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  uint16_t length = 0;
  for (;;) {
    uint8_t b;
    if (!vescReadByte(b, deadline)) return -1;
    if (b == 2) {
      if (!vescReadByte(b, deadline)) return -1;
      length = b;
      break;
    }
  }
  if (length == 0 || length > capacity) return -1;
  for (uint16_t i = 0; i < length; i++)
    if (!vescReadByte(payload[i], deadline)) return -1;
  uint8_t crcHi, crcLo, stop;
  if (!vescReadByte(crcHi, deadline) || !vescReadByte(crcLo, deadline) || !vescReadByte(stop, deadline)) return -1;
  if (stop != 3) return -1;
  if (vescCrc16(payload, length) != (((uint16_t)crcHi << 8) | crcLo)) return -1;
  return (int)length;
}

// Sends one command and returns its reply payload length, or -1. The reply is
// left in `msg` for the caller to hand to the matching parser.
int vescRequest(uint8_t command, uint8_t *msg, size_t capacity) {
  if (!vescTransport) return -1;
  uint8_t payload[3];
  const size_t n = vescBuildCommandPayload(command, vescCanId, payload, sizeof(payload));
  if (n == 0) return -1;
  while (vescTransport->available()) vescTransport->read();
  vescSendPacket(payload, (uint8_t)n);
  return vescReadPacket(msg, capacity, VESC_REPLY_TIMEOUT_MS);
}

bool requestValues() {
  uint8_t msg[128];
  const int len = vescRequest(VESC_COMM_GET_VALUES, msg, sizeof(msg));
  if (len <= 0) return false;
  return vescParseValues(msg, (size_t)len, vescData);
}

bool requestFwVersion(uint8_t &major, uint8_t &minor) {
  uint8_t msg[128];
  const int len = vescRequest(VESC_COMM_FW_VERSION, msg, sizeof(msg));
  if (len <= 0) return false;
  return vescParseFwVersion(msg, (size_t)len, major, minor);
}

bool requestSetupValues(VescSetupValues &out) {
  uint8_t msg[128];
  const int len = vescRequest(VESC_COMM_GET_VALUES_SETUP, msg, sizeof(msg));
  if (len <= 0) return false;
  return vescParseSetupValues(msg, (size_t)len, out);
}

bool setupValuesAgreeWithPoll(const VescSetupValues &value) {
  if (!isfinite(value.speedMs) || !isfinite(value.voltage) || !isfinite(value.distanceAbsM)) return false;
  if (fabsf(value.voltage - vescData.inpVoltage) > 1.0F) return false;
  if (fabsf(value.tempFet - vescData.tempMosfet) > 3.0F) return false;
  if (fabsf(value.speedMs) > 120.0F || value.distanceAbsM < 0.0F) return false;
  return true;
}

void updateSetupValues() {
  if (setupValuesProbed && !setupValuesSupported) return;
  VescSetupValues setup = {};
  if (requestSetupValues(setup) && setupValuesAgreeWithPoll(setup)) {
    if (!setupValuesProbed) {
      tripStartDistanceM = setup.distanceAbsM;
      Serial.println("VESC setup values in use: speed and distance come from the controller");
    }
    liveSetup = setup;
    liveSetupValid = true;
    setupValuesSupported = true;
    setupFailures = 0;
  } else if (!setupValuesProbed) {
    Serial.println("VESC setup values unavailable: falling back to eRPM and wheel size");
  } else if (++setupFailures >= 10) {
    setupValuesSupported = false;
    liveSetupValid = false;
    Serial.println("VESC setup values stopped answering: back to eRPM and wheel size");
  }
  setupValuesProbed = true;
}

void updateFirmwareVersion(uint32_t now) {
  if (vescFwMajor != 0 || now - vescLastFwAttemptMs <= 5000) return;
  vescLastFwAttemptMs = now;
  uint8_t major = 0;
  uint8_t minor = 0;
  if (!requestFwVersion(major, minor)) return;
  vescFwMajor = major;
  vescFwMinor = minor;
  Serial.printf("VESC firmware %u.%02u\n", vescFwMajor, vescFwMinor);
}

void fillVescSample(ControllerSample &out, uint32_t now) {
  out = {};
  out.sampledAtMs = now;
  const float trim = speedCalibration();
  if (liveSetupValid) {
    out.values.speedKmh = max(0, (int)lroundf(fabsf(liveSetup.speedMs) * 3.6F * trim));
    out.values.odoKm = (int)lroundf(liveSetup.distanceAbsM / 1000.0F * trim);
    out.values.tripKm = max(0.0F, (liveSetup.distanceAbsM - tripStartDistanceM) / 1000.0F * trim);
  } else {
    const float driveRatio = max(1, (int)vescDriveRatioHundredths) / 100.0F;
    const float polePairs = max<uint8_t>(1, vescMotorPolePairs);
    const float mechanicalRpm = fabsf(vescData.rpm) / (polePairs * driveRatio);
    out.values.speedKmh = max(0, (int)lroundf(mechanicalRpm * wheelCircumferenceMeters() * 0.06F * trim));
    out.values.tripKm = tachometerToKm(vescData.tachometerAbs - tripStartTachometerAbs) * trim;
    out.values.odoKm = (int)lroundf(tachometerToKm(vescData.tachometerAbs) * trim);
  }
  out.values.voltage = vescData.inpVoltage;
  out.values.current = vescData.avgInputCurrent;
  // Phase current comes off the same packet; power stays pack-side, which is
  // what voltage x input current means.
  out.values.motorCurrent = vescData.avgMotorCurrent;
  out.values.watts = (int)lroundf(out.values.voltage * out.values.current);
  out.values.motorTemp = (int)lroundf(vescData.tempMotor);
  out.values.escTemp = (int)lroundf(vescData.tempMosfet);
  out.values.uptimeSeconds = vescRideStartedAtMs ? (now - vescRideStartedAtMs) / 1000UL : 0;
  out.values.avgSpeedKmh = out.values.uptimeSeconds > 0
                               ? out.values.tripKm * 3600.0F / out.values.uptimeSeconds
                               : 0.0F;
  out.values.batteryPercent = batterySocFromVoltage(out.values.voltage);
  out.available = TELEMETRY_FIELDS_DASHBOARD | TELEMETRY_FIELD_FAULT | TELEMETRY_FIELD_MOTOR_CURRENT;
  out.derived = TELEMETRY_FIELD_POWER | TELEMETRY_FIELD_AVG_SPEED |
                TELEMETRY_FIELD_UPTIME | TELEMETRY_FIELD_BATTERY_SOC;
  if (!liveSetupValid)
    out.derived |= TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_TRIP_DISTANCE | TELEMETRY_FIELD_ODOMETER;
  out.faultCode = (uint8_t)vescData.faultCode;
  out.firmwareMajor = vescFwMajor;
  out.firmwareMinor = vescFwMinor;
  out.firmwareKnown = vescFwMajor != 0;

  const float counterKm = liveSetupValid
                              ? liveSetup.distanceAbsM / 1000.0F * trim
                              : tachometerToKm(vescData.tachometerAbs) * trim;
  out.energy.available = true;
  out.energy.rebaseDistance = liveSetupValid != countersFromSetup;
  out.energy.ampHours = vescData.ampHours;
  out.energy.ampHoursCharged = vescData.ampHoursCharged;
  out.energy.wattHours = vescData.wattHours;
  out.energy.wattHoursCharged = vescData.wattHoursCharged;
  out.energy.odometerKm = counterKm;
  out.energy.voltage = vescData.inpVoltage;
  out.energy.current = vescData.avgInputCurrent;
  countersFromSetup = liveSetupValid;
}

}  // namespace

void vescUartBackendBegin() {
  resetVescSession();
  vescUsingBle = false;
  vescSerial.begin(vescUartBaud, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  vescTransport = &vescSerial;
}

void vescBleBackendBegin() {
  resetVescSession();
  vescUsingBle = true;
  vescBleStream.begin();
  vescTransport = &vescBleStream;
}

void vescBackendStop() {
  if (vescUsingBle) vescBleStream.disconnectDevice();
  else vescSerial.end();
  vescTransport = nullptr;
  resetVescSession();
}

ControllerPollResult vescBackendPoll(ControllerSample &out) {
  const uint32_t now = millis();
  // A fresh Bluetooth link means data is about to flow, so ask straight away
  // rather than waiting out the slow no-controller poll.
  const bool linkJustUp = vescUsingBle && vescBleStream.status().state == VESC_BLE_CONNECTED;
  if (!vescConnected && !linkJustUp && (int32_t)(now - vescNextDisconnectedAttemptMs) < 0)
    return CONTROLLER_POLL_NO_SAMPLE;
  if (!vescConnected) vescNextDisconnectedAttemptMs = now + VESC_DISCONNECTED_POLL_MS;

  if (!requestValues()) {
    if (vescConnected && now - vescLastSuccessMs > VESC_STALE_AFTER_MS) {
      vescConnected = false;
      setupValuesSupported = false;
      liveSetupValid = false;
      vescFwMajor = 0;
      vescFwMinor = 0;
      Serial.println("VESC telemetry disconnected");
    }
    return CONTROLLER_POLL_NO_SAMPLE;
  }

  if (!vescConnected) {
    tripStartTachometerAbs = vescData.tachometerAbs;
    vescRideStartedAtMs = now;
    setupValuesProbed = false;
    setupValuesSupported = false;
    liveSetupValid = false;
    setupFailures = 0;
    countersFromSetup = false;
    Serial.println("VESC telemetry connected");
  }
  vescConnected = true;
  vescLastSuccessMs = now;
  updateSetupValues();
  updateFirmwareVersion(now);
  fillVescSample(out, now);
  return CONTROLLER_POLL_UPDATED;
}

void farDriverBackendBegin() {
  farDriverTripKm = 0.0F;
  farDriverLastIntegrateMs = 0;
  farDriverRideStartedAtMs = 0;
  farDriverAnnounced = false;
  farDriverBleBegin();
}

void farDriverBackendStop() {
  farDriverBleDisconnect();
  farDriverLastIntegrateMs = 0;
  farDriverAnnounced = false;
}

ControllerPollResult farDriverBackendPoll(ControllerSample &out) {
  const FarDriverTelemetry telemetry = farDriverTelemetry();
  const uint32_t now = millis();
  const bool fresh = telemetry.updatedAtMs != 0 && now - telemetry.updatedAtMs <= VESC_STALE_AFTER_MS &&
                     telemetry.haveElectrical && telemetry.haveMotion;
  if (!fresh) {
    if (farDriverAnnounced && telemetry.updatedAtMs != 0 && now - telemetry.updatedAtMs > VESC_STALE_AFTER_MS) {
      farDriverAnnounced = false;
      farDriverLastIntegrateMs = 0;
      Serial.println("FarDriver telemetry lost");
    }
    return CONTROLLER_POLL_NO_SAMPLE;
  }

  if (!farDriverRideStartedAtMs) farDriverRideStartedAtMs = now;
  out = {};
  out.sampledAtMs = now;
  const float trim = speedCalibration();
  const float driveRatio = max(1, (int)vescDriveRatioHundredths) / 100.0F;
  const float polePairs = max<uint8_t>(1, vescMotorPolePairs);
  const float wheelRpm = fabsf((float)telemetry.rawRpm) * 4.0F / (polePairs * driveRatio);
  out.values.speedKmh = max(0, (int)lroundf(wheelRpm * wheelCircumferenceMeters() * 0.06F * trim));
  out.values.voltage = telemetry.voltage;
  out.values.current = telemetry.current;
  out.values.watts = (int)lroundf(telemetry.voltage * telemetry.current);
  // Work in progress: no phase current. The FarDriver app shows one, so the
  // frames very likely carry it, but the electrical frame this backend decodes
  // holds a single current figure that we read as the pack side. Which side
  // that figure really is has not been confirmed against a controller, and
  // guessing would corrupt power and the logged energy totals as well. Until
  // someone captures both readings side by side, motorCurrent stays unset and
  // TELEMETRY_FIELD_MOTOR_CURRENT stays clear, so the dashboard metric reads
  // "--" and replay marks the chart "Not recorded" rather than inventing a
  // trace. VESC reports both separately and does fill this in.
  out.values.motorTemp = telemetry.motorTemp;
  out.values.escTemp = telemetry.controllerTemp;
  out.values.batteryPercent = telemetry.socPercent >= 0
                                  ? telemetry.socPercent
                                  : batterySocFromVoltage(telemetry.voltage);
  if (farDriverLastIntegrateMs != 0)
    farDriverTripKm += out.values.speedKmh * ((now - farDriverLastIntegrateMs) / 3600000.0F);
  farDriverLastIntegrateMs = now;
  out.values.tripKm = farDriverTripKm;
  out.values.uptimeSeconds = (now - farDriverRideStartedAtMs) / 1000UL;
  out.values.avgSpeedKmh = out.values.uptimeSeconds > 0
                               ? out.values.tripKm * 3600.0F / out.values.uptimeSeconds
                               : 0.0F;
  out.available = TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER |
                  TELEMETRY_FIELD_VOLTAGE | TELEMETRY_FIELD_CURRENT |
                  TELEMETRY_FIELD_TRIP_DISTANCE | TELEMETRY_FIELD_AVG_SPEED |
                  TELEMETRY_FIELD_UPTIME | TELEMETRY_FIELD_BATTERY_SOC |
                  TELEMETRY_FIELD_RIDE_MODE;
  out.derived = TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER |
                TELEMETRY_FIELD_TRIP_DISTANCE | TELEMETRY_FIELD_AVG_SPEED |
                TELEMETRY_FIELD_UPTIME;
  if (telemetry.haveMotorTemp) out.available |= TELEMETRY_FIELD_MOTOR_TEMP;
  if (telemetry.haveControllerTemp) out.available |= TELEMETRY_FIELD_ESC_TEMP;
  if (!telemetry.haveSoc) out.derived |= TELEMETRY_FIELD_BATTERY_SOC;
  out.rideMode = telemetry.haveMotion ? telemetry.gear : 0;

  if (!farDriverAnnounced) {
    farDriverAnnounced = true;
    Serial.println("FarDriver telemetry connected");
  }
  return CONTROLLER_POLL_UPDATED;
}

#endif  // CYD_LVGL_PREVIEW
