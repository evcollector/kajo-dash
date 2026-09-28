#include "windows_simulator_menu.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

#include <SDL_syswm.h>

namespace cyd::preview {
namespace {

constexpr wchar_t kMenuOwnerProperty[] = L"CYD_SIMULATOR_MENU_OWNER";
constexpr wchar_t kOriginalProcProperty[] = L"CYD_SIMULATOR_ORIGINAL_WNDPROC";

constexpr UINT kSaveScreenshot = 0x4101;
constexpr UINT kSwitchStateProfile = 0x4102;
constexpr UINT kResetStateAndRestart = 0x4103;
constexpr UINT kRestartAuto = 0x4110;
constexpr UINT kRestartDashboard = 0x4111;
constexpr UINT kRestartFirstBoot = 0x4112;
constexpr UINT kExit = 0x411F;
constexpr UINT kScaleFirst = 0x4121;
constexpr UINT kScaleLast = kScaleFirst + 7;
constexpr UINT kPerformanceOverlay = 0x4130;
constexpr UINT kPerformanceHost = 0x4140;
constexpr UINT kPerformanceEstimatedCyd = 0x4141;
constexpr UINT kFixedTimestep = 0x4142;

LRESULT CALLBACK simulatorWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  auto *owner = reinterpret_cast<NativeSimulatorMenu *>(GetPropW(window, kMenuOwnerProperty));
  if (owner && message == WM_COMMAND) {
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = static_cast<Sint32>(LOWORD(wParam));
    event.user.data1 = owner;
    SDL_PushEvent(&event);
    return 0;
  }

  const auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kOriginalProcProperty));
  return original ? CallWindowProcW(original, window, message, wParam, lParam)
                  : DefWindowProcW(window, message, wParam, lParam);
}

NativeMenuCommand commandFromId(UINT id) {
  if (id >= kScaleFirst && id <= kScaleLast) {
    return static_cast<NativeMenuCommand>(static_cast<int>(NativeMenuCommand::Scale1) +
                                          static_cast<int>(id - kScaleFirst));
  }
  switch (id) {
    case kSaveScreenshot: return NativeMenuCommand::SaveScreenshot;
    case kSwitchStateProfile: return NativeMenuCommand::SwitchStateProfile;
    case kResetStateAndRestart: return NativeMenuCommand::ResetStateAndRestart;
    case kRestartAuto: return NativeMenuCommand::RestartAuto;
    case kRestartDashboard: return NativeMenuCommand::RestartDashboard;
    case kRestartFirstBoot: return NativeMenuCommand::RestartFirstBoot;
    case kExit: return NativeMenuCommand::Exit;
    case kPerformanceOverlay: return NativeMenuCommand::TogglePerformanceOverlay;
    case kPerformanceHost: return NativeMenuCommand::PerformanceHost;
    case kPerformanceEstimatedCyd: return NativeMenuCommand::PerformanceEstimatedCyd;
    case kFixedTimestep: return NativeMenuCommand::ToggleFixedTimestep;
    default: return NativeMenuCommand::None;
  }
}

std::filesystem::path choosePath(HWND owner, const std::filesystem::path &current, const wchar_t *filter,
                                 const wchar_t *defaultExtension, bool confirmOverwrite) {
  wchar_t path[32768] = {};
  if (!current.empty()) {
    const std::wstring initial = std::filesystem::absolute(current).wstring();
    std::wcsncpy(path, initial.c_str(), std::size(path) - 1);
  }

  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.lpstrDefExt = defaultExtension;
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (confirmOverwrite) dialog.Flags |= OFN_OVERWRITEPROMPT;
  return GetSaveFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path();
}

}  // namespace

bool NativeSimulatorMenu::attach(SDL_Window *window, const NativeMenuState &state) {
  SDL_SysWMinfo info = {};
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return false;

  const HWND nativeWindow = info.info.win.window;
  HMENU root = CreateMenu();
  HMENU simulator = CreatePopupMenu();
  HMENU restart = CreatePopupMenu();
  HMENU view = CreatePopupMenu();
  HMENU scale = CreatePopupMenu();
  HMENU performance = CreatePopupMenu();
  if (!root || !simulator || !restart || !view || !scale || !performance) {
    if (root) DestroyMenu(root);
    return false;
  }

  AppendMenuW(simulator, MF_STRING, kSaveScreenshot, L"Save Screenshot...");
  AppendMenuW(simulator, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(simulator, MF_STRING, kSwitchStateProfile, L"Switch State Profile...");
  AppendMenuW(simulator, MF_STRING, kResetStateAndRestart, L"Reset Saved State and Restart");
  AppendMenuW(restart, MF_STRING, kRestartAuto, L"Automatic Start Screen");
  AppendMenuW(restart, MF_STRING, kRestartDashboard, L"Dashboard");
  AppendMenuW(restart, MF_STRING, kRestartFirstBoot, L"First-Boot Setup");
  AppendMenuW(simulator, MF_POPUP, reinterpret_cast<UINT_PTR>(restart), L"Restart At");
  AppendMenuW(simulator, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(simulator, MF_STRING, kExit, L"Exit");

  for (UINT index = 0; index < 8; ++index) {
    const std::wstring label = std::to_wstring(index + 1) + L"x";
    AppendMenuW(scale, MF_STRING, kScaleFirst + index, label.c_str());
  }
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(scale), L"Window Scale");
  AppendMenuW(view, MF_STRING, kPerformanceOverlay, L"Performance Statistics in Title Bar");

  AppendMenuW(performance, MF_STRING, kPerformanceHost, L"Host (Fast)");
  AppendMenuW(performance, MF_STRING, kPerformanceEstimatedCyd,
              L"Estimated CYD (40 MHz, Uncalibrated)");
  AppendMenuW(performance, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(performance, MF_STRING, kFixedTimestep, L"Fixed Timestep");

  AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(simulator), L"Simulator");
  AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View");
  AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(performance), L"Performance");

  if (!SetPropW(nativeWindow, kMenuOwnerProperty, this) || !SetMenu(nativeWindow, root)) {
    RemovePropW(nativeWindow, kMenuOwnerProperty);
    DestroyMenu(root);
    return false;
  }
  const auto original = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(simulatorWindowProc)));
  if (!original || !SetPropW(nativeWindow, kOriginalProcProperty, original)) {
    if (original) SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    SetMenu(nativeWindow, nullptr);
    RemovePropW(nativeWindow, kMenuOwnerProperty);
    RemovePropW(nativeWindow, kOriginalProcProperty);
    DestroyMenu(root);
    return false;
  }

  nativeWindow_ = nativeWindow;
  rootMenu_ = root;
  scaleMenu_ = scale;
  viewMenu_ = view;
  performanceMenu_ = performance;
  originalWindowProc_ = original;
  sync(state);
  DrawMenuBar(nativeWindow);
  return true;
}

void NativeSimulatorMenu::detach() {
  if (!nativeWindow_) return;
  const HWND window = static_cast<HWND>(nativeWindow_);
  if (originalWindowProc_) {
    SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProc_));
  }
  RemovePropW(window, kMenuOwnerProperty);
  RemovePropW(window, kOriginalProcProperty);
  SetMenu(window, nullptr);
  if (rootMenu_) DestroyMenu(static_cast<HMENU>(rootMenu_));
  DrawMenuBar(window);
  nativeWindow_ = nullptr;
  rootMenu_ = nullptr;
  scaleMenu_ = nullptr;
  viewMenu_ = nullptr;
  performanceMenu_ = nullptr;
  originalWindowProc_ = nullptr;
}

bool NativeSimulatorMenu::handleEvent(const SDL_Event &event, NativeMenuCommand &command) const {
  if (event.type != SDL_USEREVENT || event.user.data1 != this) return false;
  command = commandFromId(static_cast<UINT>(event.user.code));
  return command != NativeMenuCommand::None;
}

void NativeSimulatorMenu::sync(const NativeMenuState &state) const {
  if (!rootMenu_) return;
  CheckMenuRadioItem(static_cast<HMENU>(scaleMenu_), kScaleFirst, kScaleLast,
                     kScaleFirst + static_cast<UINT>(std::clamp(state.scale, 1, 8) - 1), MF_BYCOMMAND);
  CheckMenuItem(static_cast<HMENU>(viewMenu_), kPerformanceOverlay,
                MF_BYCOMMAND | (state.performanceOverlay ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuRadioItem(static_cast<HMENU>(performanceMenu_), kPerformanceHost, kPerformanceEstimatedCyd,
                     state.estimatedCydPerformance ? kPerformanceEstimatedCyd : kPerformanceHost,
                     MF_BYCOMMAND);
  CheckMenuItem(static_cast<HMENU>(performanceMenu_), kFixedTimestep,
                MF_BYCOMMAND | (state.fixedTimestep ? MF_CHECKED : MF_UNCHECKED));
  DrawMenuBar(static_cast<HWND>(nativeWindow_));
}

std::filesystem::path NativeSimulatorMenu::chooseScreenshotPath() const {
  static constexpr wchar_t filter[] = L"PPM framebuffer (*.ppm)\0*.ppm\0All files (*.*)\0*.*\0\0";
  return choosePath(static_cast<HWND>(nativeWindow_), L"cyd-screenshot.ppm", filter, L"ppm", true);
}

std::filesystem::path NativeSimulatorMenu::chooseStateProfilePath(const std::filesystem::path &current) const {
  static constexpr wchar_t filter[] = L"KAJO-Dash simulator state (*.bin)\0*.bin\0All files (*.*)\0*.*\0\0";
  return choosePath(static_cast<HWND>(nativeWindow_), current, filter, L"bin", false);
}

}  // namespace cyd::preview

#else

namespace cyd::preview {

bool NativeSimulatorMenu::attach(SDL_Window *, const NativeMenuState &) { return false; }
void NativeSimulatorMenu::detach() {}
bool NativeSimulatorMenu::handleEvent(const SDL_Event &, NativeMenuCommand &) const { return false; }
void NativeSimulatorMenu::sync(const NativeMenuState &) const {}
std::filesystem::path NativeSimulatorMenu::chooseScreenshotPath() const { return {}; }
std::filesystem::path NativeSimulatorMenu::chooseStateProfilePath(const std::filesystem::path &) const { return {}; }

}  // namespace cyd::preview

#endif
