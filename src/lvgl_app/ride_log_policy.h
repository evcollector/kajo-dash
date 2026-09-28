#pragma once
#include <stdint.h>

// When ride logging starts writing, pauses and finally closes a ride.
// Kept free of Arduino and FreeRTOS so the rule can be exercised on the host:
// ride_logger.cpp itself is unbuildable there, and three interacting timeouts
// are worth testing directly rather than only by riding a bike.
namespace ride_log {

// Movement has to hold this long before a ride is opened, so rolling the bike a
// step does not create a file.
constexpr uint32_t kAutoStartDelayMs = 2000;
// Standing still this long stops appending samples but keeps the ride open.
// Without it every ride ends with a tail of stationary records that inflates
// its duration and drags its average speed down. It is long enough to ride
// through an ordinary stop without leaving a gap in the trace.
constexpr uint32_t kAutoPauseDelayMs = 30000;
// Standing still this long closes the ride. Longer than the pause window, so a
// wait at the roadside resumes into the same ride instead of splitting it.
constexpr uint32_t kAutoStopDelayMs = 300000;
static_assert(kAutoPauseDelayMs < kAutoStopDelayMs, "a ride must pause before it closes");

struct AutoDecision {
  bool keepSession;   // hold the ride file open
  bool appendSample;  // write this reading to it
};

// `heldForMs` is how long the current moving/stopped state has lasted.
inline AutoDecision autoDecision(bool sessionOpen, bool moving, uint32_t heldForMs) {
  AutoDecision decision = {};
  decision.keepSession = sessionOpen || (moving && heldForMs >= kAutoStartDelayMs);
  if (sessionOpen && !moving && heldForMs >= kAutoStopDelayMs) decision.keepSession = false;
  decision.appendSample = decision.keepSession && !(!moving && heldForMs >= kAutoPauseDelayMs);
  return decision;
}

}  // namespace ride_log
