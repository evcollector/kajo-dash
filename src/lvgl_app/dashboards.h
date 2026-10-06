#pragma once
#include <lvgl.h>

#include "app_state.h"

// Builds the currently selected dashboard theme onto scr and populates it
// with the given values. updateDashboard refreshes the dynamic widgets.
void buildDashboardMode(lv_obj_t *scr, DashboardMode mode, const DashboardValues &values);
void buildDashboard(lv_obj_t *scr, const DashboardValues &values);
void updateDashboardMode(DashboardMode mode, const DashboardValues &values, bool force = false);
void updateDashboard(const DashboardValues &values);
// Supplies the availability metadata from the same controller snapshot as
// values. Unsupported fields are rendered as a neutral dash, never as zero.
void setDashboardTelemetryFields(uint32_t available);
void applyDashboardGradient(lv_obj_t *scr);
// The startup sweep marks entering a dashboard. Disable it around a rebuild
// that only restyles the theme already on screen (automatic light/dark).
void setDashboardStartupSweepEnabled(bool enabled);

#ifdef CYD_LVGL_PREVIEW
// Preview-only: seeds the Trace history so a rendered still shows a plot.
void previewSeedTrace();
// Preview-only: fills the Efficiency graph with demo-ride history. Call after
// the dashboard is built, which clears the history.
void previewSeedEfficiency();
// Preview-only: settles the dashboard's startup sweep immediately, leaving
// every instrument at its resting value (tests and rendered stills).
void previewFinishStartupSweep();
#endif
