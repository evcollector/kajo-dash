#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <lvgl.h>

namespace cyd::preview {

constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
constexpr int kDrawBufferLines = 26;
constexpr size_t kMaximumRecordedFlushes = 64;

struct FlushMetrics {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t pixels = 0;
};

struct FrameMetrics {
  uint64_t frameNumber = 0;
  uint32_t flushedPixels = 0;
  uint32_t flushCount = 0;
  uint32_t maximumFlushPixels = 0;
  uint16_t recordedFlushCount = 0;
  bool flushListTruncated = false;
  std::array<FlushMetrics, kMaximumRecordedFlushes> flushes = {};

  double hostHandlerMs = 0.0;
  double hostFlushSpanMs = 0.0;
  double spiTransferMs = 0.0;
  double dmaWaitMs = 0.0;
  double finalDmaTailMs = 0.0;
  double flushSetupMs = 0.0;
  double estimatedCydMs = 0.0;

  // Phase 3 starts with a transfer-dominated provisional model. A physical
  // CYD benchmark supplies the CPU/contention coefficients in Phase 4.
  bool modelCalibrated = false;
};

// Initializes LVGL with the same 26-line partial-buffer geometry as the CYD
// target and registers both the framebuffer display and pointer input drivers.
void initRuntime(uint32_t initialMillis = 3200);

lv_disp_t *display();
const lv_color_t *framebuffer();

void setPointer(bool pressed, int x, int y);

// Advances the deterministic Arduino/LVGL clock and services LVGL timers in
// bounded increments. No wall-clock sleeping happens here.
void advanceTime(uint32_t durationMs, uint32_t stepMs = 5);

void refreshNow();
bool capturePpm(const std::filesystem::path &path);

// Returns the newest completed LVGL refresh and its native workload plus the
// explicitly uncalibrated 40 MHz RGB565 transfer estimate.
bool latestFrameMetrics(FrameMetrics &metrics);

// The largest margin, in pixels, any label below `root` repaints around its box on a text
// change. Dashboards cap it at kLabelRepaintMargin (see tightenLabelRepaint).
int widestLabelRepaintMargin(lv_obj_t *root);

}  // namespace cyd::preview
