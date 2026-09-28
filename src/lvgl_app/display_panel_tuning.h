#pragma once

#include <stdint.h>

#include "app_state.h"

// Panel tuning on the DISPLAY PANEL pages. Every board receives TFT_eSPI's
// ILI9341_2 init sequence, including the dual-USB boards, whose TPM408-2.8
// glass (printed on the module) is driven by an ST7789V. Those get
// ILI9341-format gamma tables and ILI9341
// power writes that land on different ST7789 registers (0xC5 is the ST7789
// VCOM offset). The chosen preset is written directly on every change and
// again after sleep-out on every boot.
// The choice is stored per display, beside the panel profile.
enum DisplayPanelTuning : uint8_t {
  PANEL_TUNING_BASELINE,
  PANEL_TUNING_CURVE_2,
  PANEL_TUNING_CURVE_4,
  PANEL_TUNING_ILI9341_CLASSIC,
  PANEL_TUNING_ST7789,
  PANEL_TUNING_ST7789_VCOM,
  PANEL_TUNING_COUNT
};

// Technical English, like the controller IDs shown beside it.
inline const char *displayPanelTuningName(uint8_t preset) {
  switch (preset) {
    case PANEL_TUNING_BASELINE: return "BASELINE";
    case PANEL_TUNING_CURVE_2: return "GAMMA CURVE 2";
    case PANEL_TUNING_CURVE_4: return "GAMMA CURVE 4";
    case PANEL_TUNING_ILI9341_CLASSIC: return "ILI9341 CLASSIC";
    case PANEL_TUNING_ST7789: return "ST7789 GAMMA";
    case PANEL_TUNING_ST7789_VCOM: return "ST7789 + VCOM";
    default: return "";
  }
}

inline const char *displayPanelTuningNote(uint8_t preset) {
  switch (preset) {
    case PANEL_TUNING_BASELINE: return "ILI9341_2 init tables, curve 1";
    case PANEL_TUNING_CURVE_2: return "Built-in curve 2 (ST7789: gamma 1.8)";
    case PANEL_TUNING_CURVE_4: return "Built-in curve 4 (ST7789: gamma 2.5)";
    case PANEL_TUNING_ILI9341_CLASSIC: return "Original ILI9341 driver gamma tables";
    case PANEL_TUNING_ST7789: return "ST7789-format gamma tables";
    case PANEL_TUNING_ST7789_VCOM: return "ST7789 tables, VCOM offset reset to 0";
    default: return "";
  }
}

// Each panel profile starts from the tuning that suits its glass: the
// original ILI9341 keeps its init tables, the dual-USB ST7789V (Alternate)
// looks best with ST7789-format gamma tables and a neutral VCOM offset. A
// stored choice overrides it.
inline uint8_t defaultDisplayPanelTuning(DisplayPanelProfile profile) {
  return profile == DISPLAY_PANEL_ALTERNATE ? PANEL_TUNING_ST7789_VCOM : PANEL_TUNING_BASELINE;
}

extern uint8_t displayPanelTuning;
// Provided by main_lvgl.cpp (no-ops in the native preview). Apply writes the
// current preset to the panel; save persists it with the panel profile.
void applyDisplayPanelTuning();
void saveDisplayPanelTuning();
