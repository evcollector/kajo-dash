#include "ride_log_policy.h"

#include <cstdlib>
#include <iostream>

using namespace ride_log;

static void check(bool ok, const char *message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

// Replays a ride as a sequence of moving/stopped stretches, exactly as
// rideLoggerSample does: the state's age resets whenever it flips.
struct Ride {
  bool sessionOpen = false;
  uint32_t heldForMs = 0;
  bool moving = false;
  uint32_t samples = 0, sessions = 0;

  void run(bool nowMoving, uint32_t forMs, uint32_t stepMs = 200) {
    if (nowMoving != moving) { moving = nowMoving; heldForMs = 0; }
    for (uint32_t elapsed = 0; elapsed < forMs; elapsed += stepMs) {
      const AutoDecision decision = autoDecision(sessionOpen, moving, heldForMs);
      if (!decision.keepSession) {
        sessionOpen = false;
      } else {
        if (!sessionOpen) { sessionOpen = true; sessions++; }
        if (decision.appendSample) samples++;
      }
      heldForMs += stepMs;
    }
  }
};

int main() {
  // Nudging the bike must not create a ride.
  {
    Ride r; r.run(true, kAutoStartDelayMs - 200); r.run(false, 10000);
    check(r.sessions == 0 && r.samples == 0, "movement shorter than the start delay opens no ride");
  }

  // A stop shorter than the pause window records straight through it, so an
  // ordinary traffic light leaves no gap in the trace.
  {
    Ride r; r.run(true, 60000);
    const uint32_t before = r.samples;
    r.run(false, kAutoPauseDelayMs - 200);
    check(r.sessions == 1 && r.samples > before, "a short stop keeps recording");
  }

  // A long stop stops writing but holds the ride open, and moving again
  // resumes into the same ride rather than starting a second one.
  {
    Ride r; r.run(true, 60000);
    r.run(false, kAutoStopDelayMs - 1000);
    const uint32_t atPause = r.samples;
    r.run(false, 800);
    check(r.samples == atPause, "a paused ride writes nothing while still");
    check(r.sessionOpen, "the ride stays open through a long stop");
    r.run(true, 10000);
    check(r.sessions == 1 && r.samples > atPause, "moving again resumes the same ride");
  }

  // The tail of stationary records is bounded by the pause window, not by the
  // much longer window that ends the ride.
  {
    Ride r; r.run(true, 60000);
    const uint32_t riding = r.samples;
    r.run(false, kAutoStopDelayMs + 10000);
    check(!r.sessionOpen, "standing still long enough closes the ride");
    const uint32_t tail = r.samples - riding;
    check(tail <= kAutoPauseDelayMs / 200 + 1, "the stationary tail is bounded by the pause window");
    check(tail * 200 < kAutoStopDelayMs / 2, "the tail is far shorter than the window that ends the ride");
  }

  // A second ride after a closed one is a new session.
  {
    Ride r; r.run(true, 10000); r.run(false, kAutoStopDelayMs + 1000); r.run(true, 10000);
    check(r.sessions == 2, "riding again after a closed ride starts a new one");
  }

  std::cout << "Auto ride logging start, pause, resume and stop windows passed\n";
}
