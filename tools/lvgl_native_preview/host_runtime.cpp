#include "host_runtime.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

extern void previewSetMillis(uint32_t value);
extern uint32_t previewMillisNow();

namespace cyd::preview {
namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr double kSpiClockHz = 40000000.0;
constexpr double kBitsPerPixel = 16.0;
// Address-window commands and host-side submission are not yet calibrated.
// Keep the provisional allowance small and label the resulting model clearly.
constexpr double kProvisionalFlushSetupMs = 0.04;

lv_color_t hostFramebuffer[kDisplayWidth * kDisplayHeight] = {};
lv_color_t hostDrawBuffer1[kDisplayWidth * kDrawBufferLines] = {};
lv_color_t hostDrawBuffer2[kDisplayWidth * kDrawBufferLines] = {};
lv_disp_t *hostDisplay = nullptr;
bool runtimeInitialized = false;

struct ActiveFrameMetrics {
  FrameMetrics metrics;
  SteadyClock::time_point firstFlushAt = {};
  double previousDmaTransferMs = 0.0;
};

ActiveFrameMetrics activeFrame;
FrameMetrics completedFrame;
bool completedFrameAvailable = false;

struct PointerState {
  bool pressed = false;
  lv_coord_t x = 0;
  lv_coord_t y = 0;
};

PointerState pointerState;

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
  const bool finalFlush = lv_disp_flush_is_last(driver);
  const auto flushStartedAt = SteadyClock::now();
  if (activeFrame.metrics.flushCount == 0) activeFrame.firstFlushAt = flushStartedAt;

  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);
  const uint32_t pixels = width * height;
  const double transferMs = static_cast<double>(pixels) * kBitsPerPixel / kSpiClockHz * 1000.0;
  // The target waits for the previous DMA at the start of every flush. Until
  // target CPU coefficients exist, the provisional model assumes the SPI path
  // is the limiting side of the double-buffered pipeline.
  activeFrame.metrics.dmaWaitMs += activeFrame.previousDmaTransferMs;
  activeFrame.previousDmaTransferMs = transferMs;
  activeFrame.metrics.flushedPixels += pixels;
  activeFrame.metrics.flushCount++;
  activeFrame.metrics.maximumFlushPixels = std::max(activeFrame.metrics.maximumFlushPixels, pixels);
  if (activeFrame.metrics.recordedFlushCount < kMaximumRecordedFlushes) {
    FlushMetrics &record = activeFrame.metrics.flushes[activeFrame.metrics.recordedFlushCount++];
    record.x = area->x1;
    record.y = area->y1;
    record.width = static_cast<uint16_t>(width);
    record.height = static_cast<uint16_t>(height);
    record.pixels = pixels;
  } else {
    activeFrame.metrics.flushListTruncated = true;
  }

  int source = 0;
  for (int y = area->y1; y <= area->y2; ++y) {
    for (int x = area->x1; x <= area->x2; ++x) {
      if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
        hostFramebuffer[y * kDisplayWidth + x] = colors[source];
      }
      ++source;
    }
  }
  lv_disp_flush_ready(driver);

  if (finalFlush) {
    activeFrame.metrics.frameNumber = completedFrame.frameNumber + 1;
    activeFrame.metrics.hostFlushSpanMs =
        std::chrono::duration<double, std::milli>(SteadyClock::now() - activeFrame.firstFlushAt).count();
    activeFrame.metrics.spiTransferMs =
        static_cast<double>(activeFrame.metrics.flushedPixels) * kBitsPerPixel / kSpiClockHz * 1000.0;
    activeFrame.metrics.finalDmaTailMs = activeFrame.previousDmaTransferMs;
    activeFrame.metrics.flushSetupMs =
        static_cast<double>(activeFrame.metrics.flushCount) * kProvisionalFlushSetupMs;
    activeFrame.metrics.estimatedCydMs = activeFrame.metrics.dmaWaitMs +
                                         activeFrame.metrics.finalDmaTailMs +
                                         activeFrame.metrics.flushSetupMs;
    activeFrame.metrics.modelCalibrated = false;
    completedFrame = activeFrame.metrics;
    completedFrameAvailable = true;
    activeFrame = {};
  }
}

void readPointer(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->state = pointerState.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = pointerState.x;
  data->point.y = pointerState.y;
}

}  // namespace

void initRuntime(uint32_t initialMillis) {
  if (runtimeInitialized) throw std::runtime_error("Native LVGL runtime was initialized more than once");

  previewSetMillis(initialMillis);
  lv_init();

  static lv_disp_draw_buf_t drawBuffer;
  lv_disp_draw_buf_init(&drawBuffer, hostDrawBuffer1, hostDrawBuffer2, kDisplayWidth * kDrawBufferLines);

  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = kDisplayWidth;
  displayDriver.ver_res = kDisplayHeight;
  displayDriver.draw_buf = &drawBuffer;
  displayDriver.flush_cb = flushDisplay;
  hostDisplay = lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readPointer;
  lv_indev_drv_register(&inputDriver);

  runtimeInitialized = true;
}

lv_disp_t *display() {
  return hostDisplay;
}

const lv_color_t *framebuffer() {
  return hostFramebuffer;
}

void setPointer(bool pressed, int x, int y) {
  pointerState.pressed = pressed;
  pointerState.x = static_cast<lv_coord_t>(std::clamp(x, 0, kDisplayWidth - 1));
  pointerState.y = static_cast<lv_coord_t>(std::clamp(y, 0, kDisplayHeight - 1));
}

void advanceTime(uint32_t durationMs, uint32_t stepMs) {
  if (!runtimeInitialized) throw std::runtime_error("Native LVGL runtime is not initialized");
  if (stepMs == 0) stepMs = 1;

  uint32_t remaining = durationMs;
  do {
    const uint32_t increment = remaining == 0 ? 0 : std::min(remaining, stepMs);
    previewSetMillis(previewMillisNow() + increment);
    const uint64_t frameBeforeHandler = completedFrame.frameNumber;
    const auto handlerStartedAt = SteadyClock::now();
    lv_timer_handler();
    const double handlerMs =
        std::chrono::duration<double, std::milli>(SteadyClock::now() - handlerStartedAt).count();
    if (completedFrame.frameNumber != frameBeforeHandler) completedFrame.hostHandlerMs = handlerMs;
    if (remaining == 0) break;
    remaining -= increment;
  } while (remaining > 0);
}

void refreshNow() {
  if (!hostDisplay) throw std::runtime_error("Native LVGL display is not initialized");
  lv_obj_invalidate(lv_scr_act());
  const uint64_t frameBeforeRefresh = completedFrame.frameNumber;
  const auto refreshStartedAt = SteadyClock::now();
  lv_refr_now(hostDisplay);
  if (completedFrame.frameNumber != frameBeforeRefresh) {
    completedFrame.hostHandlerMs =
        std::chrono::duration<double, std::milli>(SteadyClock::now() - refreshStartedAt).count();
  }
}

bool capturePpm(const std::filesystem::path &path) {
  refreshNow();
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());

  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << "P6\n" << kDisplayWidth << " " << kDisplayHeight << "\n255\n";
  for (const lv_color_t color : hostFramebuffer) {
    lv_color32_t converted;
    converted.full = lv_color_to32(color);
    const char rgb[3] = {static_cast<char>(converted.ch.red), static_cast<char>(converted.ch.green),
                         static_cast<char>(converted.ch.blue)};
    out.write(rgb, sizeof(rgb));
  }
  return static_cast<bool>(out);
}

bool latestFrameMetrics(FrameMetrics &metrics) {
  if (!completedFrameAvailable) return false;
  metrics = completedFrame;
  return true;
}

}  // namespace cyd::preview
