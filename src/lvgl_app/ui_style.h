#pragma once

#include <lvgl.h>

#include "app_state.h"

// Shared visual tokens for the 320x240 UI. Component constructors live in
// ui_common.cpp (generic drawing) and screens.cpp (interactive menu chrome).
// New UI should consume these names instead of copying their numeric values.
namespace cyd_ui {
// Replay uses fixed semantic colors, independent of dashboard customization.
// One per ride_replay::Field, so a field keeps its color in whichever chart
// shows it: speed cyan, power red, voltage yellow, pack current orange, phase
// current lime, battery green, motor temperature magenta, controller
// temperature violet. Pack and phase current are often charted together, so
// their colors are deliberately far apart rather than two shades of orange.
constexpr uint16_t kReplayTrace565[8] = {0x05FF, 0xF904, 0xFFE0, 0xFC60, 0x9FE0, 0x07E8, 0xF81F, 0x9A7F};
constexpr uint16_t kReplayBackground565[8] = {0x0083, 0x1800, 0x1080, 0x1860, 0x10C0, 0x00C1, 0x1803, 0x1043};
constexpr uint16_t kReplayFill565[8] = {0x0187, 0x4800, 0x2940, 0x4920, 0x2A40, 0x0202, 0x4809, 0x30CB};


constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
// Interactive controls that meet a display edge use the same inset as the
// canonical Back/Next buttons. Interior content may still choose a larger
// deliberate margin, but full-width button grids should consume this token.
constexpr int kEdgeInset = 2;
constexpr int kControlGap = 5;

constexpr int kControlRadius = 7;
constexpr int kPanelRadius = 8;
constexpr int kBorderWidth = 1;
constexpr int kControlTitleInset = 8;
constexpr int kControlTitleY = 6;

constexpr int kTopBarY = 4;
constexpr int kTopBarHeight = 36;
constexpr int kTopBarWidth = 84;
constexpr int kTopBarInset = 2;
constexpr int kTopBarRightX = kScreenWidth - kTopBarInset - kTopBarWidth;

constexpr int kStatusToastVisibleY = 197;
constexpr int kStatusToastHiddenY = 244;
constexpr uint32_t kMotionMs = 140;

constexpr uint16_t kChromeAccent565 = 0xFB40;
constexpr uint16_t kControlSurface565 = 0x1082;
constexpr uint16_t kIdleControlBorder565 = 0x4208;
constexpr uint16_t kActiveControlSurface565 = 0x3186;
constexpr uint16_t kPanelSurface565 = COLOR565_PANEL;

// Segmented dashboard battery. One fixed scheme for every theme, independent of
// the accent: in dark mode a white frame with green blocks that turn amber at
// two blocks left and red at one; in light mode frame and blocks are black.
// Below one block's worth the last block blinks in either mode.
constexpr uint16_t kBatteryGood565 = COLOR565_GREEN;
constexpr uint16_t kBatteryLow565 = 0xFD80;  // yellowish orange
constexpr uint16_t kBatteryCritical565 = 0xF800;
constexpr uint32_t kBatteryBlinkMs = 500;  // each of the on and off phases

inline lv_color_t chromeAccent() { return c565(kChromeAccent565); }
inline lv_color_t controlSurface() { return c565(kControlSurface565); }
inline lv_color_t idleControlBorder() { return c565(kIdleControlBorder565); }
inline lv_color_t activeControlSurface() { return c565(kActiveControlSurface565); }
inline lv_color_t panelSurface() { return c565(kPanelSurface565); }
inline lv_color_t mutedBorder() { return c565(COLOR565_DIM); }
inline lv_color_t secondaryText() { return c565(COLOR565_LABEL); }

}  // namespace cyd_ui
