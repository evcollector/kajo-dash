#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../../src/fardriver_test/frames.h"
#include "../../src/lvgl_app/fardriver_protocol.h"

namespace {

struct Receiver {
  FarDriverFrameStream stream;
  FarDriverTelemetry telemetry = {0, 0.0F, 0.0F, 0, 0, 0, 0, -1, false, false, false, false, false};
  unsigned frames = 0, decoded = 0;
};

void onFrame(const uint8_t *frame, void *context) {
  auto &receiver = *static_cast<Receiver *>(context);
  receiver.frames++;
  if (farDriverDecodeFrame(frame, receiver.telemetry)) receiver.decoded++;
}

bool near(float value, float expected) { return std::fabs(value - expected) < 0.001F; }

bool matches(const FarDriverTelemetry &t, const fake_fardriver::Sample &s) {
  return t.haveElectrical && t.haveMotion && t.haveMotorTemp && t.haveControllerTemp && t.haveSoc &&
         near(t.voltage, s.voltageDeci / 10.0F) && near(t.current, s.currentQuarters / 4.0F) &&
         t.rawRpm == s.rawRpm && t.gear == s.gear && t.motorTemp == s.motorC && t.controllerTemp == s.escC &&
         t.socPercent == s.soc;
}

}  // namespace

int main() {
  // Fixed wire expectations, independently calculated using the receiver's
  // frame IDs/address map and CRC convention. Check complete frames, including
  // zero-initialization when reusing a buffer and rotation across many cycles.
  const uint8_t expected[4][16] = {
      {0xAA, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xC2, 0x68},
      {0xAA, 0x81, 8, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2A, 0x8E},
      {0xAA, 0xB3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0x8F, 0xFB},
      {0xAA, 0xB5, 35, 0, 0, 75, 0, 0, 0, 0, 0, 0, 0, 0, 0x48, 0xD5},
  };
  uint8_t frame[16];
  memset(frame, 0xFF, sizeof(frame));
  for (unsigned i = 0; i < 100; i++) {
    fake_fardriver::makeFrame(i, frame);
    if (memcmp(frame, expected[i % 4], sizeof(frame)) != 0) {
      std::fprintf(stderr, "FarDriver sender frame mismatch at sequence %u\n", i);
      return 1;
    }
  }
  const auto peak = fake_fardriver::rideSample(25000);
  const auto regen = fake_fardriver::rideSample(50000);
  if (peak.rawRpm != 600 || peak.voltageDeci * peak.currentQuarters / 40 != 1960 ||
      regen.currentQuarters != -40 || regen.voltageDeci * regen.currentQuarters / 40 != -530 ||
      fake_fardriver::rideSample(60000).rawRpm != 0) {
    std::fprintf(stderr, "FarDriver sender ride profile changed\n");
    return 1;
  }

  // The firmware's own stream and decoder must recover every value the sender
  // emits across a whole ride cycle. Notifications are cut at sizes that never
  // line up with the 16-byte frame, and a line-noise prefix plus a stray magic
  // byte force the stream to resynchronise before the first frame.
  Receiver receiver;
  const uint8_t noise[] = {0x13, 0xAA, 0x00, 0x42};
  auto counts = receiver.stream.push(noise, sizeof(noise), onFrame, &receiver);
  uint32_t discarded = counts.discardedBytes, crcFailures = counts.crcFailures;
  static const size_t kChunks[] = {1, 7, 20, 5, 16, 3, 40};
  unsigned chunk = 0;
  for (uint32_t ms = 0; ms < 120000; ms += 250) {
    const auto sample = fake_fardriver::rideSample(ms);
    uint8_t cycle[4 * fake_fardriver::kFrameBytes];
    for (unsigned part = 0; part < 4; part++) {
      fake_fardriver::makeFrame(part, frame, sample);
      memcpy(cycle + part * fake_fardriver::kFrameBytes, frame, sizeof(frame));
    }
    for (size_t sent = 0; sent < sizeof(cycle);) {
      const size_t n = std::min(kChunks[chunk++ % (sizeof(kChunks) / sizeof(kChunks[0]))], sizeof(cycle) - sent);
      counts = receiver.stream.push(cycle + sent, n, onFrame, &receiver);
      discarded += counts.discardedBytes;
      crcFailures += counts.crcFailures;
      sent += n;
    }
    if (!matches(receiver.telemetry, sample)) {
      std::fprintf(stderr, "FarDriver firmware decoded the sender wrongly at %u ms\n", unsigned(ms));
      return 1;
    }
  }
  // The stray magic byte in the noise is the only window allowed to fail its CRC.
  if (discarded != sizeof(noise) || crcFailures != 1 || receiver.frames != 480 * 4 ||
      receiver.decoded != receiver.frames) {
    std::fprintf(stderr, "FarDriver stream lost frames: %u frames, %u decoded, %u bytes discarded, %u CRC failures\n",
                 receiver.frames, receiver.decoded, unsigned(discarded), unsigned(crcFailures));
    return 1;
  }
  std::puts("FarDriver sender: stationary fixtures and two ride cycles through the firmware decoder passed");
}
