#pragma once

#include <filesystem>

#include <SDL.h>

namespace cyd::preview {

enum class NativeMenuCommand {
  None,
  SaveScreenshot,
  SwitchStateProfile,
  ResetStateAndRestart,
  RestartAuto,
  RestartDashboard,
  RestartFirstBoot,
  Exit,
  Scale1,
  Scale2,
  Scale3,
  Scale4,
  Scale5,
  Scale6,
  Scale7,
  Scale8,
  TogglePerformanceOverlay,
  PerformanceHost,
  PerformanceEstimatedCyd,
  ToggleFixedTimestep,
};

struct NativeMenuState {
  int scale = 3;
  bool performanceOverlay = false;
  bool estimatedCydPerformance = false;
  bool fixedTimestep = false;
};

class NativeSimulatorMenu {
 public:
  bool attach(SDL_Window *window, const NativeMenuState &state);
  void detach();
  bool handleEvent(const SDL_Event &event, NativeMenuCommand &command) const;
  void sync(const NativeMenuState &state) const;

  std::filesystem::path chooseScreenshotPath() const;
  std::filesystem::path chooseStateProfilePath(const std::filesystem::path &current) const;

 private:
  void *nativeWindow_ = nullptr;
  void *rootMenu_ = nullptr;
  void *scaleMenu_ = nullptr;
  void *viewMenu_ = nullptr;
  void *performanceMenu_ = nullptr;
  void *originalWindowProc_ = nullptr;
};

}  // namespace cyd::preview
