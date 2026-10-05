#pragma once
#include "ride_replay_core.h"
struct RideReplayStatus {
  enum State : uint8_t { Closed, Loading, Ready, Failed } state = Closed;
  uint32_t rideId = 0, position = 0, duration = 0, badRecords = 0;
  uint8_t progress = 0;
  uint32_t windowId = 0;  // the newest zoom window the reader has finished building
  ride_replay::Sample sample;
};
bool rideReplayOpen(uint32_t rideId);
void rideReplayClose();
void rideReplaySeek(uint32_t position);
RideReplayStatus rideReplayStatus();
bool rideReplayOverview(ride_replay::Overview &out);
// Zoom windows: the chart's columns for a stretch of the ride, built on the
// reader's task a few reads at a time while it keeps answering seeks. One window
// is wanted at a time and a newer request replaces an older one, finished or
// not; an empty range cancels. The id returned (0 when nothing can be built)
// appears in RideReplayStatus::windowId when that window is done, and
// rideReplayWindow then copies it out. Until it appears the overview is all
// there is.
uint32_t rideReplayRequestWindow(uint32_t start, uint32_t end, const float low[ride_replay::kFields],
                                 const float high[ride_replay::kFields]);
bool rideReplayWindow(ride_replay::Window &out);
