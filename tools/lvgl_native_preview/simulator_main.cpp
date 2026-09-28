#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL_syswm.h>
#endif

#include <lvgl.h>

#include "Preferences.h"
#include "app_state.h"
#include "host_runtime.h"
#include "screens.h"
#include "windows_simulator_menu.h"

extern void previewSetInteractiveMode(bool enabled);

namespace fs = std::filesystem;

namespace {

enum class StartScreen { Auto, Dashboard, FirstBoot };
enum class PerformanceMode { Host, EstimatedCyd };

struct Options {
  bool headless = false;
  bool fixedTimestep = false;
  bool performanceOverlay = false;
  bool resetState = false;
  int scale = 3;
  uint32_t durationMs = 0;
  fs::path stateFile;
  fs::path screenshot;
  StartScreen startScreen = StartScreen::Auto;
  PerformanceMode performanceMode = PerformanceMode::Host;
};

bool parseUnsigned(const std::string &text, uint32_t &value) {
  try {
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(text, &consumed);
    if (consumed != text.size() || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

void printUsage() {
  std::cerr << "usage: cyd_simulator [--headless] [--fixed-timestep] [--scale=N] "
               "[--duration-ms=N] [--state-file=PATH] [--reset-state] "
               "[--start=auto|dashboard|first-boot] [--screenshot=PATH] "
               "[--performance=host|estimated-cyd] [--perf-overlay]\n";
}

bool applyPerformanceMode(const std::string &value, Options &options) {
  if (value == "host") {
    options.performanceMode = PerformanceMode::Host;
    return true;
  }
  if (value == "estimated-cyd") {
    options.performanceMode = PerformanceMode::EstimatedCyd;
    return true;
  }
  return false;
}

bool parseOptions(int argc, char **argv, Options &options) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    const auto nextValue = [&]() -> std::string {
      if (i + 1 >= argc) return {};
      return argv[++i];
    };
    if (argument == "--headless") {
      options.headless = true;
    } else if (argument == "--fixed-timestep") {
      options.fixedTimestep = true;
    } else if (argument == "--perf-overlay") {
      options.performanceOverlay = true;
    } else if (argument == "--reset-state") {
      options.resetState = true;
    } else if (argument.rfind("--scale=", 0) == 0) {
      uint32_t scale = 0;
      if (!parseUnsigned(argument.substr(8), scale) || scale < 1 || scale > 8) return false;
      options.scale = static_cast<int>(scale);
    } else if (argument == "--scale") {
      uint32_t scale = 0;
      if (!parseUnsigned(nextValue(), scale) || scale < 1 || scale > 8) return false;
      options.scale = static_cast<int>(scale);
    } else if (argument.rfind("--duration-ms=", 0) == 0) {
      if (!parseUnsigned(argument.substr(14), options.durationMs)) return false;
    } else if (argument == "--duration-ms") {
      if (!parseUnsigned(nextValue(), options.durationMs)) return false;
    } else if (argument.rfind("--state-file=", 0) == 0) {
      options.stateFile = fs::path(argument.substr(13));
    } else if (argument == "--state-file") {
      const std::string value = nextValue();
      if (value.empty()) return false;
      options.stateFile = fs::path(value);
    } else if (argument.rfind("--screenshot=", 0) == 0) {
      options.screenshot = fs::path(argument.substr(13));
    } else if (argument == "--screenshot") {
      const std::string value = nextValue();
      if (value.empty()) return false;
      options.screenshot = fs::path(value);
    } else if (argument.rfind("--performance=", 0) == 0) {
      if (!applyPerformanceMode(argument.substr(14), options)) return false;
    } else if (argument == "--performance") {
      if (!applyPerformanceMode(nextValue(), options)) return false;
    } else if (argument.rfind("--start=", 0) == 0) {
      const std::string value = argument.substr(8);
      if (value == "auto")
        options.startScreen = StartScreen::Auto;
      else if (value == "dashboard")
        options.startScreen = StartScreen::Dashboard;
      else if (value == "first-boot")
        options.startScreen = StartScreen::FirstBoot;
      else
        return false;
    } else if (argument == "--start") {
      const std::string value = nextValue();
      if (value == "auto")
        options.startScreen = StartScreen::Auto;
      else if (value == "dashboard")
        options.startScreen = StartScreen::Dashboard;
      else if (value == "first-boot")
        options.startScreen = StartScreen::FirstBoot;
      else
        return false;
    } else {
      return false;
    }
  }
  if (options.headless) options.fixedTimestep = true;
  return true;
}

void uiTimer(lv_timer_t *) {
  uiDashboardTick();
  uiAutoReturnTick();
  uiSensorTick();
}

void bootApplication(const Options &options) {
  previewPreferencesConfigure(options.stateFile.empty() ? nullptr : options.stateFile.string().c_str(),
                              options.resetState);
  previewSetInteractiveMode(true);
  loadAppSettings();
  loadDisplayPanelProfile();
  loadBatteryStats();

  if (options.startScreen == StartScreen::Dashboard) firstBootConfigured = true;
  if (options.startScreen == StartScreen::FirstBoot) firstBootConfigured = false;

  lv_timer_create(uiTimer, 100, nullptr);
  if (firstBootConfigured) {
    uiShow(SCREEN_DASHBOARD);
  } else {
    configStep = 0;
    uiShow(SCREEN_CONFIG);
  }
  cyd::preview::advanceTime(0);
  cyd::preview::refreshNow();
}

void updateTexture(SDL_Texture *texture, std::vector<uint32_t> &pixels) {
  const lv_color_t *source = cyd::preview::framebuffer();
  for (size_t i = 0; i < pixels.size(); ++i) {
    lv_color32_t converted;
    converted.full = lv_color_to32(source[i]);
    pixels[i] = 0xFF000000U | (static_cast<uint32_t>(converted.ch.red) << 16) |
                (static_cast<uint32_t>(converted.ch.green) << 8) | converted.ch.blue;
  }
  SDL_UpdateTexture(texture, nullptr, pixels.data(), cyd::preview::kDisplayWidth * sizeof(uint32_t));
}

void mapPointer(int logicalX, int logicalY, int &screenX, int &screenY) {
  // Once SDL_RenderSetLogicalSize() is active, SDL's renderer event watcher
  // has already converted mouse-event coordinates into the 320x240 logical
  // space. Calling SDL_RenderWindowToLogical() here would scale them twice.
  screenX = std::clamp(logicalX, 0, cyd::preview::kDisplayWidth - 1);
  screenY = std::clamp(logicalY, 0, cyd::preview::kDisplayHeight - 1);
}

cyd::preview::NativeMenuState menuState(const Options &options) {
  cyd::preview::NativeMenuState state;
  state.scale = options.scale;
  state.performanceOverlay = options.performanceOverlay;
  state.estimatedCydPerformance = options.performanceMode == PerformanceMode::EstimatedCyd;
  state.fixedTimestep = options.fixedTimestep;
  return state;
}

void updatePerformanceTitle(SDL_Window *window, const Options &options,
                            const cyd::preview::FrameMetrics *metrics) {
  if (!options.performanceOverlay) {
    SDL_SetWindowTitle(window, "KAJO-Dash Simulator");
    return;
  }
  if (!metrics) {
    SDL_SetWindowTitle(window, "KAJO-Dash Simulator | Performance: waiting for a completed frame");
    return;
  }

  const double pacedFrameMs = std::max(30.0, metrics->estimatedCydMs);
  std::ostringstream title;
  title << std::fixed << std::setprecision(2) << "KAJO-Dash Simulator | "
        << (options.performanceMode == PerformanceMode::EstimatedCyd ? "ESTIMATED CYD" : "HOST")
        << " | pace " << pacedFrameMs << " ms / " << 1000.0 / pacedFrameMs << " FPS"
        << " | model " << metrics->estimatedCydMs << " ms | SPI " << metrics->spiTransferMs << " ms | "
        << metrics->flushedPixels << " px / " << metrics->flushCount << " flushes | host "
        << metrics->hostHandlerMs << " ms | UNCALIBRATED";
  SDL_SetWindowTitle(window, title.str().c_str());
}

void printPerformanceSummary(const cyd::preview::FrameMetrics &metrics) {
  std::cout << std::fixed << std::setprecision(3) << "CYD performance frame=" << metrics.frameNumber
            << " pixels=" << metrics.flushedPixels << " flushes=" << metrics.flushCount
            << " max_flush_pixels=" << metrics.maximumFlushPixels << " host_handler_ms="
            << metrics.hostHandlerMs << " host_flush_span_ms=" << metrics.hostFlushSpanMs
            << " spi_40mhz_ms=" << metrics.spiTransferMs << " dma_wait_ms=" << metrics.dmaWaitMs
            << " final_dma_tail_ms=" << metrics.finalDmaTailMs
            << " flush_setup_ms=" << metrics.flushSetupMs
            << " estimated_cyd_ms=" << metrics.estimatedCydMs << " model=uncalibrated-spi-v1\n";
}

const char *startScreenArgument(StartScreen startScreen) {
  switch (startScreen) {
    case StartScreen::Dashboard: return "dashboard";
    case StartScreen::FirstBoot: return "first-boot";
    default: return "auto";
  }
}

#ifdef _WIN32
std::wstring quoteArgument(const std::wstring &argument) {
  std::wstring quoted = L"\"";
  for (const wchar_t character : argument) {
    if (character == L'\"') quoted += L'\\';
    quoted += character;
  }
  quoted += L'\"';
  return quoted;
}

bool launchRestart(const Options &options) {
  wchar_t executable[32768] = {};
  const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
  if (length == 0 || length >= std::size(executable)) return false;

  std::wstring command = quoteArgument(executable);
  command += L" --scale=" + std::to_wstring(options.scale);
  command += options.performanceMode == PerformanceMode::EstimatedCyd
                 ? L" --performance=estimated-cyd"
                 : L" --performance=host";
  command += L" --start=" + std::wstring(startScreenArgument(options.startScreen),
                                          startScreenArgument(options.startScreen) +
                                              std::char_traits<char>::length(startScreenArgument(options.startScreen)));
  if (options.fixedTimestep) command += L" --fixed-timestep";
  if (options.performanceOverlay) command += L" --perf-overlay";
  if (options.resetState) command += L" --reset-state";
  if (!options.stateFile.empty()) command += L" " + quoteArgument(L"--state-file=" + options.stateFile.wstring());

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                      nullptr, &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}
#else
bool launchRestart(const Options &) { return false; }
#endif

int runHeadless(const Options &options) {
  const uint32_t duration = options.durationMs ? options.durationMs : 1000;
  cyd::preview::advanceTime(duration);
  return 0;
}

struct InteractiveResult {
  int exitCode = 0;
  bool restartRequested = false;
  Options nextOptions;
};

InteractiveResult runInteractive(Options options) {
  InteractiveResult result;
  result.nextOptions = options;
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
    result.exitCode = 1;
    return result;
  }

  SDL_Window *window = SDL_CreateWindow(
      "KAJO-Dash Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      cyd::preview::kDisplayWidth * options.scale, cyd::preview::kDisplayHeight * options.scale,
      SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "SDL window creation failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    result.exitCode = 1;
    return result;
  }

#ifdef _WIN32
  // Embed the icon for Explorer and explicitly set both HWND sizes for the
  // running SDL window's title bar, taskbar and Alt-Tab entry.
  SDL_SysWMinfo windowInfo = {};
  SDL_VERSION(&windowInfo.version);
  if (SDL_GetWindowWMInfo(window, &windowInfo)) {
    const HINSTANCE instance = GetModuleHandle(nullptr);
    const HICON largeIcon = static_cast<HICON>(LoadImage(instance, MAKEINTRESOURCE(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    const HICON smallIcon = static_cast<HICON>(LoadImage(instance, MAKEINTRESOURCE(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (largeIcon) SendMessage(windowInfo.info.win.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    if (smallIcon) SendMessage(windowInfo.info.win.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
  }
#endif

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                       SDL_TEXTUREACCESS_STREAMING,
                                                       cyd::preview::kDisplayWidth,
                                                       cyd::preview::kDisplayHeight)
                                  : nullptr;
  if (!renderer || !texture) {
    std::cerr << "SDL renderer creation failed: " << SDL_GetError() << "\n";
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    result.exitCode = 1;
    return result;
  }
  SDL_RenderSetLogicalSize(renderer, cyd::preview::kDisplayWidth, cyd::preview::kDisplayHeight);
  SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

  cyd::preview::NativeSimulatorMenu nativeMenu;
  if (!nativeMenu.attach(window, menuState(options))) {
    std::cerr << "Native Windows simulator menu could not be attached\n";
  } else {
    // SetMenu changes the non-client frame. Reassert the requested 320x240
    // client-area multiple so the menu never steals pixels from the CYD view.
    SDL_SetWindowSize(window, cyd::preview::kDisplayWidth * options.scale,
                     cyd::preview::kDisplayHeight * options.scale);
  }

  std::vector<uint32_t> pixels(cyd::preview::kDisplayWidth * cyd::preview::kDisplayHeight);
  bool running = true;
  bool pressed = false;
  uint32_t elapsedTotal = 0;
  auto previous = std::chrono::steady_clock::now();
  uint64_t lastPacedFrame = 0;
  uint64_t lastPresentedMetricsFrame = 0;
  cyd::preview::FrameMetrics latestMetrics;
  bool metricsAvailable = cyd::preview::latestFrameMetrics(latestMetrics);
  if (metricsAvailable) lastPresentedMetricsFrame = latestMetrics.frameNumber;
  updatePerformanceTitle(window, options, metricsAvailable ? &latestMetrics : nullptr);

  const auto requestRestart = [&](StartScreen startScreen, bool resetState) {
    options.headless = false;
    options.durationMs = 0;
    options.screenshot.clear();
    options.startScreen = startScreen;
    options.resetState = resetState;
    result.restartRequested = true;
    running = false;
  };

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      cyd::preview::NativeMenuCommand menuCommand = cyd::preview::NativeMenuCommand::None;
      if (nativeMenu.handleEvent(event, menuCommand)) {
        const int commandValue = static_cast<int>(menuCommand);
        const int firstScale = static_cast<int>(cyd::preview::NativeMenuCommand::Scale1);
        const int lastScale = static_cast<int>(cyd::preview::NativeMenuCommand::Scale8);
        if (commandValue >= firstScale && commandValue <= lastScale) {
          options.scale = commandValue - firstScale + 1;
          SDL_SetWindowSize(window, cyd::preview::kDisplayWidth * options.scale,
                           cyd::preview::kDisplayHeight * options.scale);
        } else {
          switch (menuCommand) {
            case cyd::preview::NativeMenuCommand::SaveScreenshot: {
              const fs::path path = nativeMenu.chooseScreenshotPath();
              if (!path.empty()) {
                if (cyd::preview::capturePpm(path))
                  std::cout << "Saved simulator screenshot: " << fs::absolute(path).string() << "\n";
                else
                  std::cerr << "Could not save simulator screenshot: " << path.string() << "\n";
              }
              break;
            }
            case cyd::preview::NativeMenuCommand::SwitchStateProfile: {
              const fs::path path = nativeMenu.chooseStateProfilePath(options.stateFile);
              if (!path.empty()) {
                options.stateFile = fs::absolute(path);
                requestRestart(StartScreen::Auto, false);
              }
              break;
            }
            case cyd::preview::NativeMenuCommand::ResetStateAndRestart:
              requestRestart(StartScreen::Auto, true);
              break;
            case cyd::preview::NativeMenuCommand::RestartAuto:
              requestRestart(StartScreen::Auto, false);
              break;
            case cyd::preview::NativeMenuCommand::RestartDashboard:
              requestRestart(StartScreen::Dashboard, false);
              break;
            case cyd::preview::NativeMenuCommand::RestartFirstBoot:
              requestRestart(StartScreen::FirstBoot, false);
              break;
            case cyd::preview::NativeMenuCommand::Exit:
              running = false;
              break;
            case cyd::preview::NativeMenuCommand::TogglePerformanceOverlay:
              options.performanceOverlay = !options.performanceOverlay;
              break;
            case cyd::preview::NativeMenuCommand::PerformanceHost:
              options.performanceMode = PerformanceMode::Host;
              if (metricsAvailable) lastPacedFrame = latestMetrics.frameNumber;
              break;
            case cyd::preview::NativeMenuCommand::PerformanceEstimatedCyd:
              options.performanceMode = PerformanceMode::EstimatedCyd;
              if (metricsAvailable) lastPacedFrame = latestMetrics.frameNumber;
              break;
            case cyd::preview::NativeMenuCommand::ToggleFixedTimestep:
              options.fixedTimestep = !options.fixedTimestep;
              previous = std::chrono::steady_clock::now();
              break;
            default:
              break;
          }
        }
        nativeMenu.sync(menuState(options));
        updatePerformanceTitle(window, options, metricsAvailable ? &latestMetrics : nullptr);
      } else if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int x = 0, y = 0;
        mapPointer(event.button.x, event.button.y, x, y);
        pressed = true;
        cyd::preview::setPointer(true, x, y);
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        int x = 0, y = 0;
        mapPointer(event.button.x, event.button.y, x, y);
        pressed = false;
        cyd::preview::setPointer(false, x, y);
      } else if (event.type == SDL_MOUSEMOTION) {
        int x = 0, y = 0;
        mapPointer(event.motion.x, event.motion.y, x, y);
        cyd::preview::setPointer(pressed, x, y);
      }
    }

    if (!running) break;

    uint32_t elapsed = 5;
    if (!options.fixedTimestep) {
      const auto now = std::chrono::steady_clock::now();
      elapsed = static_cast<uint32_t>(
          std::clamp<std::chrono::milliseconds>(
              std::chrono::duration_cast<std::chrono::milliseconds>(now - previous),
              std::chrono::milliseconds(1), std::chrono::milliseconds(50))
              .count());
      previous = now;
    }
    const auto handlerStartedAt = std::chrono::steady_clock::now();
    cyd::preview::advanceTime(elapsed);
    const double hostWallMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - handlerStartedAt)
                                  .count();
    elapsedTotal += elapsed;

    bool pacedThisLoop = false;
    cyd::preview::FrameMetrics completedMetrics;
    if (cyd::preview::latestFrameMetrics(completedMetrics)) {
      latestMetrics = completedMetrics;
      metricsAvailable = true;
      if (completedMetrics.frameNumber != lastPresentedMetricsFrame) {
        lastPresentedMetricsFrame = completedMetrics.frameNumber;
        updatePerformanceTitle(window, options, &latestMetrics);
      }
      if (options.performanceMode == PerformanceMode::EstimatedCyd &&
          completedMetrics.frameNumber != lastPacedFrame) {
        const double targetFrameMs = std::max(30.0, completedMetrics.estimatedCydMs);
        const double remainingMs = targetFrameMs - hostWallMs;
        if (remainingMs > 0.5) SDL_Delay(static_cast<Uint32>(std::ceil(remainingMs)));
        lastPacedFrame = completedMetrics.frameNumber;
        pacedThisLoop = true;
      }
    }

    updateTexture(texture, pixels);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    if (options.durationMs && elapsedTotal >= options.durationMs) running = false;
    if (!options.fixedTimestep && !pacedThisLoop) SDL_Delay(5);
  }

  result.nextOptions = options;
  nativeMenu.detach();
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    Options options;
    if (!parseOptions(argc, argv, options)) {
      printUsage();
      return 2;
    }

    cyd::preview::initRuntime();
    bootApplication(options);
    // Reset is a one-shot boot action. Menu-triggered restarts must not keep
    // deleting the selected profile unless the user explicitly asks again.
    options.resetState = false;
    std::cout << "KAJO-Dash simulator ready: 320x240, screen=" << static_cast<int>(uiCurrentScreen())
              << (options.headless ? ", headless" : ", interactive") << "\n";

    int exitCode = 0;
    bool restartRequested = false;
    Options finalOptions = options;
    if (options.headless) {
      exitCode = runHeadless(options);
    } else {
      const InteractiveResult interactive = runInteractive(options);
      exitCode = interactive.exitCode;
      restartRequested = interactive.restartRequested;
      finalOptions = interactive.nextOptions;
    }

    if (!finalOptions.screenshot.empty() && !cyd::preview::capturePpm(finalOptions.screenshot)) {
      std::cerr << "Could not write screenshot: " << finalOptions.screenshot.string() << "\n";
      return 1;
    }
    cyd::preview::FrameMetrics metrics;
    if ((finalOptions.performanceOverlay || finalOptions.performanceMode == PerformanceMode::EstimatedCyd) &&
        cyd::preview::latestFrameMetrics(metrics)) {
      printPerformanceSummary(metrics);
    }
    if (restartRequested && !launchRestart(finalOptions)) {
      std::cerr << "Could not restart the simulator\n";
      return 1;
    }
    return exitCode;
  } catch (const std::exception &error) {
    std::cerr << "KAJO-Dash simulator failed: " << error.what() << "\n";
    return 1;
  }
}
