#pragma once

#include <NimBLEDevice.h>

// NimBLEServer::start() rebuilds the GATT table the first time it runs and
// again whenever a service has been added. NimBLE only allows that while the
// host is completely idle: with a scan or connect attempt in flight, or any
// link still up, ble_gatts_add_svcs() returns BLE_HS_EBUSY and
// ble_svc_gap_init() asserts on it, which reboots the display.
//
// The controller workers scan and dial from their own tasks, and a disconnect
// only completes when the peer answers, so "released" is not yet "idle". Poll
// this before starting a server and wait until it holds.
inline bool bleGattRebuildAllowed() {
  if (!NimBLEDevice::isInitialized()) return true;
  if (ble_gap_disc_active() || ble_gap_conn_active()) return false;
  // A client being deleted keeps its slot until its link has really closed.
  if (NimBLEDevice::getCreatedClientCount() != 0) return false;
  NimBLEServer *existingServer = NimBLEDevice::getServer();
  return !existingServer || existingServer->getConnectedCount() == 0;
}
