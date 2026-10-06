// The segmented ring (makeSegRing): the rule that sizes its blocks, and that the blocks
// a new value invalidates are exactly the ones that change on screen.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Preferences.h"
#include "app_state.h"
#include "host_runtime.h"
#include "ui_common.h"

namespace {

bool ok = true;
const double kPi = 3.14159265358979323846;

void fail(const std::string &message) {
  std::cerr << message << '\n';
  ok = false;
}

// The rule is meant to give every ring the same block and gap widths in pixels
// at its centre line, whatever its radius, in whole degrees.
void testGeometry() {
  for (int thickness = 6; thickness <= 8; ++thickness) {
    for (int radius = 28; radius <= 70; ++radius) {
      const SegRingGeometry g = segRingGeometry(radius, thickness, 260);
      const double toPx = kPi / 180.0 * (radius - thickness / 2.0);
      const int gapDeg = g.pitchDeg - g.blockDeg;
      const std::string where = " (radius " + std::to_string(radius) + ", thickness " + std::to_string(thickness) + ")";
      if (g.blockDeg < 1 || gapDeg < 1) fail("A block or gap is under a degree" + where);
      if (g.count < 2) fail("Fewer than two blocks" + where);
      if (g.extentDeg != g.count * g.pitchDeg - gapDeg) fail("Extent does not add up" + where);
      if (g.extentDeg > 260) fail("Ring is wider than its sweep" + where);
      if ((g.count + 1) * g.pitchDeg - gapDeg <= 260) fail("Another block would have fitted" + where);
      if (std::abs(g.startDeg + g.extentDeg / 2.0 - 270.0) > 0.5) fail("Ring is not centred on 12 o'clock" + where);
      if (g.pitchDeg * toPx < 4.4 || g.pitchDeg * toPx > 6.6) fail("Block pitch is out of range" + where);
      if (g.blockDeg * toPx < 2.6 || g.blockDeg * toPx > 3.9) fail("Block width is out of range" + where);
      if (gapDeg * toPx < 1.5 || gapDeg * toPx > 3.0) fail("Gap is out of range" + where);
    }
  }
  // The thickness rule: a ring grows with its dial, never shrinks as the dial grows, and the
  // spacing stays the bar meters' whatever thickness it hands the geometry.
  if (segRingThickness(34) != 7 || segRingThickness(48) != 9 || segRingThickness(59) != 11)
    fail("Thickness is not 7, 9 and 11 px at radius 34, 48 and 59");
  for (int radius = 20; radius < 90; ++radius) {
    if (segRingThickness(radius + 1) < segRingThickness(radius)) fail("Thickness shrinks as the radius grows");
    if (segRingThickness(radius) < 4) fail("Thickness under 4 px");
  }
  for (int radius = 28; radius <= 70; ++radius) {
    const int thickness = segRingThickness(radius);
    const SegRingGeometry g = segRingGeometry(radius, thickness, 240);
    const double toPx = kPi / 180.0 * (radius - thickness / 2.0);
    const int gapDeg = g.pitchDeg - g.blockDeg;
    const std::string where = " (radius " + std::to_string(radius) + ", thickness " + std::to_string(thickness) + ")";
    if (g.pitchDeg * toPx < 4.4 || g.pitchDeg * toPx > 6.6) fail("Ruled block pitch is out of range" + where);
    if (g.blockDeg * toPx < 2.6 || g.blockDeg * toPx > 3.9) fail("Ruled block width is out of range" + where);
    if (gapDeg * toPx < 1.5 || gapDeg * toPx > 3.0) fail("Ruled gap is out of range" + where);
    if (g.extentDeg > 240 || std::abs(g.startDeg + g.extentDeg / 2.0 - 270.0) > 0.5) fail("Ruled ring is off its sweep" + where);
  }
  for (int sweep : {180, 220, 240, 300}) {
    const SegRingGeometry g = segRingGeometry(40, 7, sweep);
    if (g.extentDeg > sweep || g.count < 2) fail("Wrong block count for a " + std::to_string(sweep) + " degree sweep");
    if (std::abs(g.startDeg + g.extentDeg / 2.0 - 270.0) > 0.5) fail("Sweep " + std::to_string(sweep) + " is not centred");
  }
}

struct Spec {
  int cx, cy, radius;
  lv_color_t lit;
};

uint32_t rgb(lv_color_t color) { return lv_color_to32(color) & 0xFFFFFF; }

}  // namespace

int main() {
  using namespace cyd::preview;
  testGeometry();

  initRuntime();
  previewPreferencesConfigure(nullptr, true);
  loadAppSettings();
  lv_obj_t *old = lv_scr_act();
  lv_obj_t *screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  makePassive(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_scr_load(screen);
  lv_obj_del(old);

  // Four rings laid out like Motor Data's, so the big ones overlap each other's squares.
  const Spec specs[4] = {{44, 79, 34, lv_color_make(0, 255, 0)},
                         {160, 78, 59, lv_color_make(0, 255, 255)},
                         {160, 170, 48, lv_color_make(255, 160, 0)},
                         {276, 169, 34, lv_color_make(255, 0, 255)}};
  static SegRingWidget rings[4];
  lv_color_t unlit[4];
  for (int i = 0; i < 4; ++i) {
    unlit[i] = lv_color_mix(specs[i].lit, lv_color_black(), 38);
    makeSegRing(rings[i], screen, specs[i].cx, specs[i].cy, specs[i].radius, segRingThickness(specs[i].radius), 240,
                specs[i].lit, unlit[i]);
    // The dual gauge's longer ticks on every ring, and the dark disc inside two of them.
    setSegRingMajors(rings[i], 3, 5);
    if (i % 2 == 1) setSegRingFill(rings[i], lv_color_mix(specs[i].lit, lv_color_black(), 14), 206);
  }
  refreshNow();

  // 1. Nothing lit, then every kind of change: single steps both ways, jumps across
  // the whole ring, the extremes, and the same value again.
  std::vector<int> values;
  for (int v = 0; v <= 100; ++v) values.push_back(v);
  for (int v = 100; v >= 0; --v) values.push_back(v);
  uint32_t seed = 12345;
  for (int i = 0; i < 400; ++i) {
    seed = seed * 1664525u + 1013904223u;
    values.push_back(static_cast<int>((seed >> 16) % 101));
  }
  for (int v : {0, 100, 100, 0, 0, 50, 50}) values.push_back(v);
  int checked = 0, repaints = 0, differingSteps = 0, worstDelta = 0, worstStep = -1;
  long differingPixels = 0;
  uint64_t pictureHash = 14695981039346656037ULL;
  for (size_t step = 0; step < values.size(); ++step) {
    const int ring = static_cast<int>(step % 4);
    // A ring that has not had a value yet shows no lit blocks, the same as zero.
    const int shownBefore = std::max<int>(rings[ring].litBlocks, 0);
    FrameMetrics before, after;
    latestFrameMetrics(before);
    setSegRingValue(rings[ring], values[step], 100);
    lv_refr_now(display());
    latestFrameMetrics(after);
    const bool repainted = before.frameNumber != after.frameNumber;
    if (repainted) ++repaints;
    if (!repainted && rings[ring].litBlocks != shownBefore)
      fail("A changed value repainted nothing at step " + std::to_string(step));
    if (repainted && rings[ring].litBlocks == shownBefore)
      fail("An unchanged value repainted at step " + std::to_string(step));
    std::vector<lv_color_t> incremental(framebuffer(), framebuffer() + 320 * 240);
    refreshNow();  // invalidates the whole screen: the reference picture
    for (int i = 0; i < 320 * 240; ++i) {
      pictureHash ^= framebuffer()[i].full;
      pictureHash *= 1099511628211ULL;
    }
    int different = 0;
    for (int i = 0; i < 320 * 240; ++i) {
      if (incremental[i].full == framebuffer()[i].full) continue;
      ++different;
      const uint32_t a = rgb(incremental[i]), b = rgb(framebuffer()[i]);
      for (int shift : {0, 8, 16}) {
        const int delta = std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF));
        if (delta > worstDelta) {
          worstDelta = delta;
          worstStep = static_cast<int>(step);
        }
      }
    }
    differingPixels += different;
    if (different) ++differingSteps;
    ++checked;
  }
  std::cout << "checked " << checked << " steps, " << repaints << " repaints; " << differingSteps
            << " steps left a partial repaint differing from a full one in " << differingPixels
            << " pixels, by at most " << worstDelta << "/255 (step " << worstStep << ")\n";
  std::cout << "picture hash " << std::hex << pictureHash << std::dec << '\n';

  // A partial repaint and a full one agree to the pixel, but for one place: LVGL's angle
  // mask antialiases a pixel or two at the very edge of a repainted rectangle a few percent
  // differently. Allow that and nothing like a stale or missing block (a hundred levels).
  if (worstDelta > 24 || differingPixels > 16)
    fail("Partial repaints drifted from full ones: " + std::to_string(differingPixels) + " pixels, by up to " +
         std::to_string(worstDelta) + "/255");

  // 2. Every block is the right colour where it should be, including those that straddle 3 o'clock.
  const int finalValues[4] = {37, 100, 0, 63};
  for (int i = 0; i < 4; ++i) setSegRingValue(rings[i], finalValues[i], 100);
  refreshNow();
  for (int r = 0; r < 4; ++r) {
    const SegRingWidget &ring = rings[r];
    const int lit = (finalValues[r] * ring.count + 50) / 100;
    if (ring.litBlocks != lit) fail("Ring " + std::to_string(r) + " has the wrong number of lit blocks");
    for (int b = 0; b < ring.count; ++b) {
      const double mid = (ring.startDeg + b * ring.pitchDeg + ring.blockDeg / 2.0) * kPi / 180.0;
      const double centre = ring.radius - ring.thickness / 2.0;
      const int x = static_cast<int>(std::lround(ring.cx + std::cos(mid) * centre));
      const int y = static_cast<int>(std::lround(ring.cy + std::sin(mid) * centre));
      const uint32_t want = rgb(b < lit ? specs[r].lit : unlit[r]);
      // A block is only about 3 px wide, so its centre pixel may be an antialiased edge: take the best of the
      // 3x3 around it (the gaps are wider than that, so a neighbouring block cannot answer for this one).
      uint32_t got = rgb(framebuffer()[y * 320 + x]);
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (rgb(framebuffer()[(y + dy) * 320 + x + dx]) == want) got = want;
      if (want != got)
        fail("Ring " + std::to_string(r) + " block " + std::to_string(b) + " is " + std::to_string(got) +
             ", expected " + std::to_string(want));
    }
  }

  // 3. The opening at the bottom, and everything off the rings, stays background.
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 320; ++x) {
      if (rgb(framebuffer()[y * 320 + x]) == 0) continue;
      bool onARing = false;
      for (int r = 0; r < 4 && !onARing; ++r) {
        const SegRingWidget &ring = rings[r];
        const double dx = x - ring.cx, dy = y - ring.cy, distance = std::hypot(dx, dy);
        if (ring.hasFill && distance <= ring.radius + 1) {
          onARing = true;
          continue;
        }
        if (distance < ring.radius - ring.thickness - ring.majorExtra - 2 || distance > ring.radius + 2) continue;
        double angle = std::atan2(dy, dx) * 180.0 / kPi;
        if (angle < 0) angle += 360.0;
        if (angle < ring.startDeg - 4) angle += 360.0;  // the ring runs past 3 o'clock
        onARing = angle >= ring.startDeg - 4 && angle <= ring.startDeg + ring.extentDeg + 4;
      }
      if (!onARing) {
        fail("Stray pixel at (" + std::to_string(x) + "," + std::to_string(y) + ")");
        y = 240;
        break;
      }
    }
  }

  std::cout << (ok ? "segmented ring ok\n" : "segmented ring FAILED\n");
  return ok ? 0 : 1;
}
