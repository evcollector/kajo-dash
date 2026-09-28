#pragma once

#include <Arduino.h>

constexpr uint8_t VESC_BLE_MAX_DEVICES = 12;

enum VescBleState : uint8_t { VESC_BLE_IDLE, VESC_BLE_SCANNING, VESC_BLE_CONNECTING, VESC_BLE_CONNECTED, VESC_BLE_ERROR };

struct VescBleDeviceInfo {
  char name[28];
  char address[20];
  int8_t rssi;
  uint8_t addressType;
  bool likelyVesc;
};

struct VescBleStatus {
  VescBleState state;
  VescBleDeviceInfo devices[VESC_BLE_MAX_DEVICES];
  uint8_t deviceCount;
  bool savedDevice;
  char savedAddress[20];
  char connectedAddress[20];
  char message[56];
  uint32_t revision;
};

#ifdef CYD_LVGL_PREVIEW
class VescBleStream {
#else
class VescBleStream : public Stream {
#endif
 public:
  void begin();
  void startScan();
  void connectDevice(uint8_t index);
  // Connect to the saved VESC by its stored address, whatever the device list
  // currently holds. Does nothing when no VESC is saved.
  void reconnectSaved();
  void disconnectDevice();
  // Hand the single NimBLE client slot back so the FarDriver backend can use
  // it. Asynchronous, like every other command here: poll radioReleased().
  void releaseRadio();
  bool radioReleased() const;
  void forgetDevice();
  // Clear the saved controller without starting BLE solely for the reset.
  // If the worker is active, any scan/connect work is cancelled and the live
  // link is disconnected asynchronously.
  void resetSettings();
  VescBleStatus status();

  int available()
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
  int read()
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
  int peek()
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
  void flush()
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
  size_t write(uint8_t value)
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
  size_t write(const uint8_t *buffer, size_t size)
#ifndef CYD_LVGL_PREVIEW
      override
#endif
      ;
#ifndef CYD_LVGL_PREVIEW
  using Print::write;
#endif
};

extern VescBleStream vescBleStream;
