#pragma once
#include "ride_replay_core.h"
struct RideReplayStatus {
  enum State : uint8_t { Closed, Loading, Ready, Failed } state = Closed;
  uint32_t rideId = 0, position = 0, duration = 0, badRecords = 0;
  uint8_t progress = 0;
  ride_replay::Sample sample;
};
bool rideReplayOpen(uint32_t rideId);
void rideReplayClose();
void rideReplaySeek(uint32_t position);
RideReplayStatus rideReplayStatus();
bool rideReplayOverview(ride_replay::Overview &out);
