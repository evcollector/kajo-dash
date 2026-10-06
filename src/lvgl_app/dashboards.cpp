#include "dashboards.h"

#include "controller_manager.h"
#include "gauge_range_visual.h"
#include "ui_common.h"
#include "ui_style.h"

#include <math.h>

using namespace cyd_layout;

// Lit-tick dial (the Redline style): the radial scale ticks themselves light
// up in full-bright accent and thicken as the value rises. Tick objects live
// in dw.dots starting at `base`; dw.dotColors caches lit state per tick.
struct TickDial {
  int base;
  int count;
  int majorEvery;
  int maxValue;
  uint8_t litWidth;
  uint8_t unlitWidth;
  int lastLit;
  lv_color_t unlitMajor;
  lv_color_t unlitMinor;
  lv_color_t litColor;
};

// Two-layer outer needle shared by tick-based dials. Like Efficiency's
// pointer, it begins beyond the centre readout and reaches the tick ring.
struct TickNeedle {
  lv_obj_t *body;
  lv_obj_t *highlight;
  lv_point_t points[2];
  lv_point_t lastStart;
  lv_point_t lastEnd;
  int cx;
  int cy;
  int innerRadius;
  int outerRadius;
  int startDeg;
  int sweepDeg;
  int maxValue;
  int lastValue;
};

// Dynamic widget handles for the active dashboard. Rebuilt on every
// buildDashboard call, so only the pointers for the current theme are valid.
struct DashWidgets {
  lv_obj_t *speed;
  lv_obj_t *speedUnit;
  lv_obj_t *power;
  lv_obj_t *powerUnit;
  lv_obj_t *volts;
  lv_obj_t *voltsUnit;
  lv_obj_t *amps;
  lv_obj_t *ampsUnit;
  lv_obj_t *motor;
  lv_obj_t *escUnit;
  lv_obj_t *esc;
  lv_obj_t *trip;
  lv_obj_t *tripUnit;
  lv_obj_t *odo;
  lv_obj_t *odoUnit;
  lv_obj_t *avg;
  lv_obj_t *avgUnit;
  lv_obj_t *uptime;
  lv_obj_t *uptime2;
  lv_obj_t *battPct;
  BatteryWidget batt;
  TickDial speedDial;
  TickDial powerDial;
  TickNeedle speedNeedle;
  TickNeedle powerNeedle;
  lv_obj_t *barSpeed;   // Ride Console keeps a plain LVGL bar
  lv_obj_t *barPower;
  // Bar Graph: one segmented meter per row, in the Simple theme's block style.
  SegMeterWidget barMeters[5];
  lv_obj_t *barUnits[5];
  lv_obj_t *dots[90];       // tick storage shared by the tick-dial themes (Gauge's two dials use 82)
  lv_obj_t *scaleLabels[6];
  lv_obj_t *motorUnit;
  lv_obj_t *battUnit;
  SegBatteryWidget segBatt;                 // top-bar charge indicator
  lv_obj_t *topBattPct;  // Ride Console: top-bar % while battPct stays in the PACK card
  SegMeterWidget powerMeter;                // Simple: dense theme-colour effort meter
  lv_obj_t *rangeUnit;
  lv_obj_t *energyRate;                     // Trace: Wh per distance unit
  lv_obj_t *range;                          // Trace: estimated range remaining
  lv_obj_t *aux[8];                         // purpose-focused dashboard values
  lv_obj_t *dataCaptions[DASH_DATA_SLOTS_MAX];
  lv_obj_t *dataValues[DASH_DATA_SLOTS_MAX];
  lv_obj_t *dataUnits[DASH_DATA_SLOTS_MAX];
  lv_obj_t *dataIcons[DASH_DATA_SLOTS_MAX];
  // Most themes want their slot icons in the theme accent. Bar Graph colours
  // each row for the quantity it carries, so those slots pin their own tint
  // and keep it when the slot is reassigned.
  lv_color_t dataIconColors[DASH_DATA_SLOTS_MAX];
  bool dataIconFixed[DASH_DATA_SLOTS_MAX];
  lv_obj_t *chart;                          // Trace: rolling telemetry plot
  lv_chart_series_t *traceSpeedSeries;
  lv_chart_series_t *tracePowerSeries;
  uint32_t dotColors[90];   // last applied color per tick
  uint32_t speedColor;      // last applied speed text color
  lv_obj_t *hudMeters;      // Cyber HUD: paired inset segmented edge meters
  int hudSpeedLit;
  int hudPowerLit;
  lv_color_t hudMeterLit;
  lv_color_t hudMeterUnlit;
};
static DashWidgets dw;

static GaugeRangeVisual visualGaugeRanges[RANGE_COUNT];
static GaugeValueVisual visualSpeed;
static GaugeValueVisual visualPower;
static GaugeValueVisual visualCurrent;
static GaugeValueVisual visualMotorCurrent;
static int visualGaugeSource = -1;
static bool visualGaugeAutomatic = false;
static int lastRangeSpeed = -1;
static int lastRangePower = -1;
static int lastRangeEfficiency = -1;

static void resetVisualGaugeRanges() {
  visualGaugeSource = automaticGaugeSource();
  visualGaugeAutomatic = automaticGaugeRanges;
  for (int i = 0; i < RANGE_COUNT; ++i)
    visualGaugeRanges[i].reset(automaticGaugeMaximum(static_cast<GaugeRangeKind>(i)));
}

static void advanceVisualGaugeRanges() {
  if (visualGaugeSource != automaticGaugeSource() || visualGaugeAutomatic != automaticGaugeRanges)
    resetVisualGaugeRanges();
  const uint32_t now = millis();
  for (int i = 0; i < RANGE_COUNT; ++i)
    visualGaugeRanges[i].advance(automaticGaugeMaximum(static_cast<GaugeRangeKind>(i)), now);
}

static int displayGaugeMaximum(GaugeRangeKind kind) {
  if (demoPreviewIsFrozen()) return thumbnailGaugeMaximum(kind);
  return visualGaugeRanges[kind].shown ? visualGaugeRanges[kind].shown : automaticGaugeMaximum(kind);
}

static void resetVisualGaugeValues(const DashboardValues &values) {
  const uint32_t now = millis();
  visualSpeed.reset(values.speedKmh, now);
  visualPower.reset(values.watts, now);
  visualCurrent.reset(values.current, now);
  visualMotorCurrent.reset(values.motorCurrent, now);
}

static void advanceVisualGaugeValues(const DashboardValues &values) {
  const uint32_t now = millis();
  visualSpeed.advance(values.speedKmh, now);
  visualPower.advance(values.watts, now, true);
  visualCurrent.advance(values.current, now, true);
  visualMotorCurrent.advance(values.motorCurrent, now, true);
}

static int displaySpeedValue() { return static_cast<int>(lroundf(visualSpeed.shown)); }
static int displayPowerValue() { return static_cast<int>(lroundf(visualPower.shown)); }

static const lv_font_t *F1 = &lv_font_rajdhani_12;
static const lv_font_t *F2 = &lv_font_rajdhani_14;
static const lv_font_t *F3 = &lv_font_rajdhani_16;
static const lv_font_t *F3B = &lv_font_rajdhani_20;
static const lv_font_t *F4 = &lv_font_rajdhani_24;
static const lv_font_t *F7 = &lv_font_rajdhani_36;

static TelemetryFieldMask dashAvailableFields = TELEMETRY_FIELDS_ALL;

void setDashboardTelemetryFields(uint32_t available) {
  dashAvailableFields = available;
}

static bool dashHas(TelemetryField field) {
  return telemetryHas(dashAvailableFields, field);
}

static void formatAvailableRange(char *buffer, size_t size, int rangeKm, bool includeUnit = false) {
  if (!dashHas(TELEMETRY_FIELD_RANGE)) {
    snprintf(buffer, size, "-");
    return;
  }
  formatRangeText(buffer, size, rangeKm, includeUnit);
}

static void formatAvailableEnergyRate(char *buffer, size_t size, float rate, bool includeUnit,
                                      TelemetryField field = TELEMETRY_FIELD_RIDE_EFFICIENCY) {
  if (!dashHas(field)) {
    snprintf(buffer, size, "-");
    return;
  }
  formatEnergyRate(buffer, size, rate, includeUnit);
}

static void formatAvailableEnergy(char *buffer, size_t size, float value, int decimals,
                                  TelemetryField field) {
  if (!dashHas(field)) {
    snprintf(buffer, size, "-");
    return;
  }
  snprintf(buffer, size, decimals == 0 ? "%.0f Wh" : "%.1f Wh", value);
}

// The demo supplies its own consistent set; live rides use the real one.
static BatteryStats dashBatteryStats() {
  return demoModeIsActive() ? makeDemoBatteryStats() : getBatteryStats();
}

static lv_color_t dash565(uint16_t color) { return dashboardColorLv(c565(color)); }
static lv_color_t dashBlack() { return dashboardColorLv(lv_color_black()); }
static lv_color_t dashWhite() { return dashboardColorLv(lv_color_white()); }
static lv_color_t panelLv() { return dash565(COLOR565_PANEL); }
static lv_color_t dimLv() { return dash565(COLOR565_DIM); }
static lv_color_t labelLv() { return dash565(COLOR565_LABEL); }

// The time and battery text sit beside taller icons in the shared dashboard
// status row. Raising only the glyphs by one physical pixel gives both sides
// the same optical centre without disturbing the common bar geometry.
static lv_obj_t *makeTopBatteryLabel(lv_obj_t *parent, const Item &item, const char *text,
                                    lv_color_t color) {
  Item raised = item;
  raised.y -= 1;
  return makeLabel(parent, raised, text, color);
}

static lv_obj_t *makeTopTimeLabel(lv_obj_t *parent, const Item &item, const char *text,
                                  lv_color_t color) {
  Item raised = item;
  raised.y -= 1;
  return makeLabel(parent, raised, text, color);
}
static lv_color_t whiteLv() { return dashWhite(); }

// Theme signature colors: stock palette while the accent is Default, the
// user's accent once one is explicitly selected.
static lv_color_t themeColor(uint16_t stock565) {
  return accentTheme == ACCENT_DEFAULT ? dash565(stock565) : accentLv();
}
static lv_color_t themeColorDark(uint16_t stock565) {
  return accentTheme == ACCENT_DEFAULT ? dash565(stock565) : accentDarkLv();
}

// ── Lit-tick dials ────────────────────────────────────────────────────────────

static TickDial makeTickDial(lv_obj_t *scr, int base, int cx, int cy, int r, int count, int majorEvery,
                             float startDeg, float sweepDeg, int majorLen, int minorLen, int maxValue,
                             uint8_t litWidth, uint8_t unlitWidth, lv_point_t *pts) {
  const lv_color_t dark = accentDarkLv();
  for (int i = 0; i < count; i++) {
    const bool major = (i % majorEvery) == 0;
    const float a = radians(startDeg + sweepDeg * i / (count - 1));
    const int len = major ? majorLen : minorLen;
    const lv_coord_t x0 = (lv_coord_t)(cx + cosf(a) * (r - len));
    const lv_coord_t y0 = (lv_coord_t)(cy + sinf(a) * (r - len));
    const lv_coord_t x1 = (lv_coord_t)(cx + cosf(a) * r);
    const lv_coord_t y1 = (lv_coord_t)(cy + sinf(a) * r);
    // points relative to the object with the object placed at the tick's
    // corner: lv_line sizes itself to the max point coords, so absolute
    // points would give every tick a bounding box spanning from the screen
    // origin — and LVGL would redraw all of them whenever anything inside
    // that area (like the dial's center digits) changes
    const lv_coord_t ox = min(x0, x1);
    const lv_coord_t oy = min(y0, y1);
    pts[i * 2] = {(lv_coord_t)(x0 - ox), (lv_coord_t)(y0 - oy)};
    pts[i * 2 + 1] = {(lv_coord_t)(x1 - ox), (lv_coord_t)(y1 - oy)};
    lv_obj_t *tick = lv_line_create(scr);
    makePassive(tick);
    lv_line_set_points(tick, &pts[i * 2], 2);
    lv_obj_set_pos(tick, ox, oy);
    lv_obj_set_style_line_width(tick, unlitWidth, 0);
    lv_obj_set_style_line_color(tick, major ? whiteLv() : dark, 0);
    dw.dots[base + i] = tick;
    dw.dotColors[base + i] = 0;
  }
  TickDial dial = {base, count, majorEvery, maxValue, litWidth, unlitWidth, 0, whiteLv(), dark, accentLv()};
  return dial;
}

static void setTickDialUnlitPalette(TickDial &dial, lv_color_t major, lv_color_t minor) {
  dial.unlitMajor = major;
  dial.unlitMinor = minor;
  for (int i = 0; i < dial.count; i++) {
    const int idx = dial.base + i;
    if (dw.dots[idx] && dw.dotColors[idx] == 0)
      lv_obj_set_style_line_color(dw.dots[idx], (i % dial.majorEvery) == 0 ? major : minor, 0);
  }
}

static void setTickDialValue(TickDial &dial, int value) {
  const int lit = map(constrain(value, 0, dial.maxValue), 0, dial.maxValue, 0, dial.count);
  if (lit == dial.lastLit) return;
  const int first = min(lit, dial.lastLit);
  const int end = max(lit, dial.lastLit);
  // Register one enclosing damage area BEFORE the style setters. LVGL then
  // discards their contained rectangles instead of overflowing its 32-entry
  // queue on a large speed/power jump. Include both old/new line extents and
  // the content-size adjustment caused by changing line width.
  lv_area_t damage = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
  lv_obj_t *parent = nullptr;
  const int padding = 2 * max(dial.litWidth, dial.unlitWidth);
  for (int i = first; i < end; ++i) {
    lv_obj_t *tick = dw.dots[dial.base + i];
    if (!tick) continue;
    lv_area_t bounds;
    lv_obj_get_coords(tick, &bounds);
    damage.x1 = min<int>(damage.x1, bounds.x1 - padding);
    damage.y1 = min<int>(damage.y1, bounds.y1 - padding);
    damage.x2 = max<int>(damage.x2, bounds.x2 + padding);
    damage.y2 = max<int>(damage.y2, bounds.y2 + padding);
    parent = lv_obj_get_parent(tick);
  }
  if (parent) lv_obj_invalidate_area(parent, &damage);
  for (int i = first; i < end; i++) {
    const uint32_t state = i < lit ? 1 : 0;
    const int idx = dial.base + i;
    if (!dw.dots[idx]) continue;
    dw.dotColors[idx] = state;
    const bool major = (i % dial.majorEvery) == 0;
    if (state) {
      lv_obj_set_style_line_color(dw.dots[idx], dial.litColor, 0);
      lv_obj_set_style_line_width(dw.dots[idx], dial.litWidth, 0);
    } else {
      lv_obj_set_style_line_color(dw.dots[idx], major ? dial.unlitMajor : dial.unlitMinor, 0);
      lv_obj_set_style_line_width(dw.dots[idx], dial.unlitWidth, 0);
    }
  }
  dial.lastLit = lit;
}

static TickNeedle makeTickNeedle(lv_obj_t *scr, int cx, int cy, int innerRadius, int outerRadius,
                                 int startDeg, int sweepDeg, int maxValue, int bodyWidth, int highlightWidth) {
  TickNeedle needle = {};
  needle.cx = cx;
  needle.cy = cy;
  needle.innerRadius = innerRadius;
  needle.outerRadius = outerRadius;
  needle.startDeg = startDeg;
  needle.sweepDeg = sweepDeg;
  needle.maxValue = max(1, maxValue);
  needle.lastValue = -1;
  needle.body = lv_line_create(scr);
  makePassive(needle.body);
  lv_obj_set_style_line_color(needle.body, accentLv(), 0);
  lv_obj_set_style_line_width(needle.body, bodyWidth, 0);
  lv_obj_set_style_line_rounded(needle.body, false, 0);
  needle.highlight = lv_line_create(scr);
  makePassive(needle.highlight);
  lv_obj_set_style_line_color(needle.highlight, whiteLv(), 0);
  lv_obj_set_style_line_width(needle.highlight, highlightWidth, 0);
  lv_obj_set_style_line_rounded(needle.highlight, false, 0);
  return needle;
}

static void setTickNeedleValue(TickNeedle &needle, int value) {
  if (!needle.body || !needle.highlight) return;
  value = constrain(value, 0, needle.maxValue);
  if (value == needle.lastValue) return;
  const bool initialized = needle.lastValue >= 0;
  needle.lastValue = value;
  const float angle = radians((float)needle.startDeg + (float)needle.sweepDeg * value / needle.maxValue);
  const lv_coord_t x0 = (lv_coord_t)(needle.cx + cosf(angle) * needle.innerRadius);
  const lv_coord_t y0 = (lv_coord_t)(needle.cy + sinf(angle) * needle.innerRadius);
  const lv_coord_t x1 = (lv_coord_t)(needle.cx + cosf(angle) * needle.outerRadius);
  const lv_coord_t y1 = (lv_coord_t)(needle.cy + sinf(angle) * needle.outerRadius);
  // A one-watt change usually cannot move a pixel. Avoid two line layout
  // updates and damage rectangles until the rendered endpoints actually move.
  if (initialized && needle.lastStart.x == x0 && needle.lastStart.y == y0 &&
      needle.lastEnd.x == x1 && needle.lastEnd.y == y1) return;
  needle.lastStart = {x0, y0};
  needle.lastEnd = {x1, y1};
  const lv_coord_t ox = min(x0, x1);
  const lv_coord_t oy = min(y0, y1);
  needle.points[0] = {(lv_coord_t)(x0 - ox), (lv_coord_t)(y0 - oy)};
  needle.points[1] = {(lv_coord_t)(x1 - ox), (lv_coord_t)(y1 - oy)};
  lv_line_set_points(needle.body, needle.points, 2);
  lv_line_set_points(needle.highlight, needle.points, 2);
  lv_obj_set_pos(needle.body, ox, oy);
  lv_obj_set_pos(needle.highlight, ox, oy);
}


// tick point storage: must outlive the lv_line objects (one screen at a time)
static lv_point_t gaugeSpeedPts[41 * 2];
static lv_point_t gaugePowerPts[41 * 2];
static lv_point_t redlinePts[51 * 2];

static lv_obj_t *makeRedlineSpeedValue(lv_obj_t *scr, const char *text) {
  lv_obj_t *label = makeLabelAt(scr, 159, 90, text, whiteLv(), F7, 1);
  lv_obj_set_size(label, 250, 78);
  lv_obj_set_pos(label, 35, 82);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

// ── Shared value formatting (mirrors the update*Values functions) ─────────────

struct DashTexts {
  char speed[16];
  char power[12];
  char voltage[10];
  char voltageWithUnit[12];
  const char *voltageUnit;
  char current[10];
  char currentWithUnit[12];
  const char *currentUnit;
  char motor[10];
  char esc[10];
  char trip[10];
  char odo[10];
  char avg[10];
  char uptime[12];
  char uptimeShort[10];
  char battPct[8];
};

static void formatScaledValue(char *buffer, size_t size, float value, const char *smallUnit, const char *largeUnit,
                              const char **unitOut) {
  if (value > 999.0F) {
    formatFloat(buffer, size, value / 1000.0F, 1);
    *unitOut = largeUnit;
  } else {
    formatFloat(buffer, size, value, 1);
    *unitOut = smallUnit;
  }
}

// Worst-case speed text for sizing the speed labels once at build time:
// updates then repaint in place instead of refitting fonts/boxes per value.
static const char *speedFitTemplate() {
  return unitMode == UNITS_MACH ? "000.000" : "000";
}

static int speedGaugeMax() {
  if (demoPreviewIsFrozen()) return displayGaugeMaximum(RANGE_SPEED);
  return automaticGaugeRanges ? displayGaugeMaximum(RANGE_SPEED) : speedScaleMaxKmh();
}

static constexpr int SPEED_SCALE_INTERVALS = 5;

// Numeric speed dials share five equal intervals, always ending at the exact
// active ceiling. Thus 100 km/h reads 0..100 by 20, while 50 km/h reads
// 0..50 by 10 without any theme carrying its own fixed ceiling.
static float speedScaleMarkKmh(int mark) {
  return (float)speedGaugeMax() * constrain(mark, 0, SPEED_SCALE_INTERVALS) / SPEED_SCALE_INTERVALS;
}

static void formatSpeedScaleMark(int mark, char *buffer, size_t size) {
  const float shown = displaySpeed(speedScaleMarkKmh(mark));
  if (unitMode == UNITS_MACH) {
    snprintf(buffer, size, mark == 0 ? "0" : "%.2f", shown);
  } else {
    snprintf(buffer, size, "%d", (int)roundf(shown));
  }
}

// Shared effort ceiling; manual mode retains the configured peak.
static int powerBarMax() {
  if (demoPreviewIsFrozen()) return displayGaugeMaximum(RANGE_POWER);
  if (automaticGaugeRanges) return displayGaugeMaximum(RANGE_POWER);
  // the demo bike has its own ceiling, so its meter fills the way the rider
  // would see it rather than against the configured vehicle's peak
  if (demoModeIsActive()) return max(100, (int)lroundf(demoRidePeak().watts));
  return max(100, (int)peakPowerDeciKw * 100);
}

// Full pack voltage from the configured series count: the full-charge cell
// voltage (4.20 V for Li-ion), so the bar fills while charging and a rested full
// pack sits just below the end of it.
static int packVoltageMax() {
  if (demoPreviewIsFrozen()) return 84;  // thumbnail pack: 20 cells at 4.18 V
  return max(1, (int)lroundf((float)batterySeriesCount * batteryCellFullVolts()));
}

// The configured battery-side current limit: what the pack is allowed to
// deliver, rather than the motor-side figure, which may legitimately exceed it.
static int packCurrentMax() {
  if (demoPreviewIsFrozen()) return displayGaugeMaximum(RANGE_CURRENT);
  return automaticGaugeRanges ? displayGaugeMaximum(RANGE_CURRENT) : max(1, (int)batteryMaxAmps);
}

static void placeUnitAfterValue(lv_obj_t *valueLabel, lv_obj_t *unitLabel, const Item &unitItem, int gap = 4) {
  if (!valueLabel || !unitLabel) return;
  // set_pos/set_size only mark layout dirty in LVGL; force the coords to
  // resolve or the getters below return stale values during screen builds
  // (visible as misplaced units for the whole startup sweep, since updates
  // are frozen while it plays)
  lv_obj_update_layout(valueLabel);
  const lv_font_t *font = lv_obj_get_style_text_font(valueLabel, 0);
  lv_point_t size;
  lv_txt_get_size(&size, lv_label_get_text(valueLabel), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int textLeft = lv_obj_get_x(valueLabel);
  const int boxW = lv_obj_get_width(valueLabel);
  const lv_text_align_t align = (lv_text_align_t)lv_obj_get_style_text_align(valueLabel, 0);
  if (align == LV_TEXT_ALIGN_CENTER) {
    textLeft += max(0, (boxW - size.x) / 2);
  } else if (align == LV_TEXT_ALIGN_RIGHT) {
    textLeft += max(0, boxW - size.x);
  }
  lv_obj_set_pos(unitLabel, textLeft + size.x + gap, unitItem.y);
}

static void placeValueUnitAtRight(lv_obj_t *valueLabel, lv_obj_t *unitLabel, const Item &valueItem,
                                  const Item &unitItem, int right, int gap = 4) {
  if (!valueLabel || !unitLabel) return;
  const lv_font_t *valueFont = lv_obj_get_style_text_font(valueLabel, 0);
  const lv_font_t *unitFont = lv_obj_get_style_text_font(unitLabel, 0);
  lv_point_t valueSize;
  lv_point_t unitSize;
  lv_txt_get_size(&valueSize, lv_label_get_text(valueLabel), valueFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_txt_get_size(&unitSize, lv_label_get_text(unitLabel), unitFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  setObjTextAlign(valueLabel, LV_TEXT_ALIGN_LEFT);
  setObjTextAlign(unitLabel, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(valueLabel, valueSize.x, lv_font_get_line_height(valueFont) + 2);
  lv_obj_set_size(unitLabel, unitSize.x, lv_font_get_line_height(unitFont) + 2);
  lv_obj_set_pos(unitLabel, right - unitSize.x, unitItem.y);
  lv_obj_set_pos(valueLabel, right - unitSize.x - gap - valueSize.x, valueItem.y);
}

static void centerValueUnitOnBaseline(lv_obj_t *valueLabel, lv_obj_t *unitLabel, int centerX, int gap = 4,
                                      int unitYOffset = 0) {
  if (!valueLabel || !unitLabel) return;
  lv_obj_update_layout(valueLabel);
  const lv_font_t *valueFont = lv_obj_get_style_text_font(valueLabel, 0);
  const lv_font_t *unitFont = lv_obj_get_style_text_font(unitLabel, 0);
  lv_point_t valueSize, unitSize;
  lv_txt_get_size(&valueSize, lv_label_get_text(valueLabel), valueFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_txt_get_size(&unitSize, lv_label_get_text(unitLabel), unitFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int left = centerX - (valueSize.x + gap + unitSize.x) / 2;
  const int valueY = lv_obj_get_y(valueLabel);
  setObjTextAlign(valueLabel, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_width(valueLabel, valueSize.x + 1);
  lv_obj_set_x(valueLabel, left);
  lv_obj_set_pos(unitLabel, left + valueSize.x + gap, valueY + valueSize.y - unitSize.y + unitYOffset);
}

// Pin every tile readout to the same lower-left inset. Aligning the font
// baselines keeps small units visually attached to their value even when the
// number changes width or drops to a fallback font.
static void bottomLeftValueUnitInTile(lv_obj_t *valueLabel, lv_obj_t *unitLabel, const Item &tile,
                                      int leftInset = 7, int bottomInset = 5, int gap = 3) {
  if (!valueLabel || !unitLabel) return;
  lv_obj_update_layout(valueLabel);
  const lv_font_t *valueFont = lv_obj_get_style_text_font(valueLabel, 0);
  const lv_font_t *unitFont = lv_obj_get_style_text_font(unitLabel, 0);
  lv_point_t valueSize;
  lv_txt_get_size(&valueSize, lv_label_get_text(valueLabel), valueFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

  const int valueY = tile.y + tile.h - bottomInset - valueFont->line_height;
  const int valueBaseline = valueY + valueFont->line_height - valueFont->base_line;
  const int unitY = valueBaseline - (unitFont->line_height - unitFont->base_line);
  setObjTextAlign(valueLabel, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(valueLabel, valueSize.x + 1, valueFont->line_height + 2);
  lv_obj_set_pos(valueLabel, tile.x + leftInset, valueY);
  lv_obj_set_pos(unitLabel, tile.x + leftInset + valueSize.x + gap, unitY);
}

// Keep a centered value label's object bounds close to the pixels it draws.
// LVGL invalidates both the old and new coordinates when this crosses a digit
// boundary (9 -> 10, 99 -> 100), so shrinking the box cannot leave stale
// glyphs behind. Between boundaries the tabular speed digits retain the same
// width and only that small box is redrawn.
static void setTightCenteredLabelText(lv_obj_t *label, const Item &item, const char *text, int padding = 8) {
  if (!label) return;
  const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
  lv_point_t size;
  lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int boxW = size.x + padding;
  const int boxH = lv_font_get_line_height(font) + 2;
  lv_obj_set_size(label, boxW, boxH);
  lv_obj_set_pos(label, item.x - boxW / 2, item.y - boxH / 2);
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// Tight bounds for ordinary metric values and their units. Unlike the speed
// helper this honors the layout item's anchor/maxWidth semantics.
static void setTightLabelText(lv_obj_t *label, const Item &item, const char *text, int padding = 4) {
  if (!label) return;
  const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
  lv_point_t size;
  lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int boxW = size.x + padding;
  const int boxH = lv_font_get_line_height(font) + 2;
  int x = item.x;
  int y = item.y;
  if (item.maxWidth > 0 || item.anchor == 1) {
    x -= boxW / 2;
    y -= boxH / 2;
  } else if (item.anchor == 2 || item.anchor == 4) {
    x -= boxW;
  } else if (item.anchor == 3) {
    x -= boxW / 2;
  }
  lv_obj_set_size(label, boxW, boxH);
  lv_obj_set_pos(label, x, y);
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static DashTexts fmtTexts(const DashboardValues &v) {
  DashTexts t;
  if (dashHas(TELEMETRY_FIELD_SPEED)) formatSpeedValue(t.speed, sizeof(t.speed), v.speedKmh);
  else snprintf(t.speed, sizeof(t.speed), "-");
  if (dashHas(TELEMETRY_FIELD_POWER)) formatPowerValue(t.power, sizeof(t.power), v.watts);
  else snprintf(t.power, sizeof(t.power), "-");
  t.voltageUnit = "V";
  t.currentUnit = "A";
  if (dashHas(TELEMETRY_FIELD_VOLTAGE)) {
    formatScaledValue(t.voltage, sizeof(t.voltage), v.voltage, "V", "kV", &t.voltageUnit);
    snprintf(t.voltageWithUnit, sizeof(t.voltageWithUnit), "%s%s", t.voltage, t.voltageUnit);
  } else {
    snprintf(t.voltage, sizeof(t.voltage), "-");
    snprintf(t.voltageWithUnit, sizeof(t.voltageWithUnit), "-");
  }
  if (dashHas(TELEMETRY_FIELD_CURRENT)) {
    formatScaledValue(t.current, sizeof(t.current), v.current, "A", "kA", &t.currentUnit);
    snprintf(t.currentWithUnit, sizeof(t.currentWithUnit), "%s%s", t.current, t.currentUnit);
  } else {
    snprintf(t.current, sizeof(t.current), "-");
    snprintf(t.currentWithUnit, sizeof(t.currentWithUnit), "-");
  }
  if (dashHas(TELEMETRY_FIELD_MOTOR_TEMP)) snprintf(t.motor, sizeof(t.motor), "%d °C", v.motorTemp);
  else snprintf(t.motor, sizeof(t.motor), "-");
  if (dashHas(TELEMETRY_FIELD_ESC_TEMP)) snprintf(t.esc, sizeof(t.esc), "%d °C", v.escTemp);
  else snprintf(t.esc, sizeof(t.esc), "-");
  if (dashHas(TELEMETRY_FIELD_TRIP_DISTANCE)) formatFloat(t.trip, sizeof(t.trip), displayDistance(v.tripKm), 1);
  else snprintf(t.trip, sizeof(t.trip), "-");
  // The odometer arrives in km from the controller's tachometer like every
  // other distance, so it converts like every other distance. Themes must use
  // this string rather than v.odoKm: printing the raw value under a
  // distanceUnitLabel() suffix reported kilometres as miles.
  if (dashHas(TELEMETRY_FIELD_ODOMETER))
    snprintf(t.odo, sizeof(t.odo), "%d", (int)lroundf(displayDistance((float)v.odoKm)));
  else snprintf(t.odo, sizeof(t.odo), "-");
  if (dashHas(TELEMETRY_FIELD_AVG_SPEED)) formatFloat(t.avg, sizeof(t.avg), v.avgSpeedKmh, 1);
  else snprintf(t.avg, sizeof(t.avg), "-");
  if (dashHas(TELEMETRY_FIELD_UPTIME)) {
    formatUptime(t.uptime, sizeof(t.uptime), v.uptimeSeconds);
    formatUptimeShort(t.uptimeShort, sizeof(t.uptimeShort), v.uptimeSeconds);
  } else {
    snprintf(t.uptime, sizeof(t.uptime), "-");
    snprintf(t.uptimeShort, sizeof(t.uptimeShort), "-");
  }
  if (dashHas(TELEMETRY_FIELD_BATTERY_SOC)) snprintf(t.battPct, sizeof(t.battPct), "%d%%", v.batteryPercent);
  else snprintf(t.battPct, sizeof(t.battPct), "-");
  return t;
}

static void registerDataSlot(uint8_t slot, lv_obj_t *caption, lv_obj_t *value, lv_obj_t *unit = NULL,
                             lv_obj_t *icon = NULL, const lv_color_t *iconColor = NULL) {
  if (slot >= DASH_DATA_SLOTS_MAX) return;
  dw.dataCaptions[slot] = caption;
  dw.dataValues[slot] = value;
  dw.dataUnits[slot] = unit;
  dw.dataIcons[slot] = icon;
  dw.dataIconFixed[slot] = iconColor != NULL;
  if (iconColor) dw.dataIconColors[slot] = *iconColor;
}

static CydIconId dashboardDataIcon(DashboardDataItem item) {
  switch (item) {
    case DATA_TRIP: return CYD_ICON_TRIP;
    case DATA_ODOMETER: return CYD_ICON_ODO;
    case DATA_AVG_SPEED: return CYD_ICON_AVG_SPEED;
    case DATA_SPEED: return CYD_ICON_SPEED;
    case DATA_RANGE: return CYD_ICON_NAV;
    case DATA_RIDE_EFFICIENCY:
    case DATA_LIFETIME_EFFICIENCY: return CYD_ICON_EFFICIENCY;
    case DATA_RIDE_ENERGY: return CYD_ICON_WATTS;
    case DATA_REGEN_ENERGY: return CYD_ICON_POWER;
    case DATA_VOLTAGE: return CYD_ICON_VOLTAGE;
    case DATA_CURRENT:
    case DATA_MOTOR_CURRENT: return CYD_ICON_AMPERAGE;
    case DATA_MOTOR_TEMP: return CYD_ICON_TEMP_MOTOR;
    case DATA_ESC_TEMP: return CYD_ICON_TEMP_ESC;
    case DATA_BATTERY: return CYD_ICON_BATTERY;
    case DATA_UPTIME: return CYD_ICON_UPTIME;
    case DATA_POWER:
    case DATA_DUTY: return CYD_ICON_POWER;
    case DATA_LEARNED_CAPACITY: return CYD_ICON_BATTERY;
    case DATA_PACK_RESISTANCE: return CYD_ICON_CURRENT;
    default: return CYD_ICON_SETTINGS;
  }
}

static TelemetryField dashboardDataField(DashboardDataItem item) {
  if (item == DATA_RIDE_MODE) return TELEMETRY_FIELD_RIDE_MODE;
  switch (item) {
    case DATA_TRIP: return TELEMETRY_FIELD_TRIP_DISTANCE;
    case DATA_ODOMETER: return TELEMETRY_FIELD_ODOMETER;
    case DATA_AVG_SPEED: return TELEMETRY_FIELD_AVG_SPEED;
    case DATA_RANGE: return TELEMETRY_FIELD_RANGE;
    case DATA_RIDE_EFFICIENCY: return TELEMETRY_FIELD_RIDE_EFFICIENCY;
    case DATA_LIFETIME_EFFICIENCY: return TELEMETRY_FIELD_LIFETIME_EFFICIENCY;
    case DATA_RIDE_ENERGY: return TELEMETRY_FIELD_TRIP_ENERGY;
    case DATA_REGEN_ENERGY: return TELEMETRY_FIELD_REGEN_ENERGY;
    case DATA_VOLTAGE: return TELEMETRY_FIELD_VOLTAGE;
    case DATA_CURRENT: return TELEMETRY_FIELD_CURRENT;
    case DATA_MOTOR_TEMP: return TELEMETRY_FIELD_MOTOR_TEMP;
    case DATA_ESC_TEMP: return TELEMETRY_FIELD_ESC_TEMP;
    case DATA_BATTERY: return TELEMETRY_FIELD_BATTERY_SOC;
    case DATA_UPTIME: return TELEMETRY_FIELD_UPTIME;
    case DATA_POWER: return TELEMETRY_FIELD_POWER;
    case DATA_DUTY: return TELEMETRY_FIELD_DUTY;
    case DATA_LEARNED_CAPACITY: return TELEMETRY_FIELD_LEARNED_CAPACITY;
    case DATA_PACK_RESISTANCE: return TELEMETRY_FIELD_PACK_RESISTANCE;
    default: return TELEMETRY_FIELD_SPEED;
  }
}

static void formatDashboardData(char *buffer, size_t size, DashboardDataItem item, const DashboardValues &v) {
  if (!dashHas(dashboardDataField(item))) {
    snprintf(buffer, size, "-");
    return;
  }
  const DashTexts t = fmtTexts(v);
  const BatteryStats stats = dashBatteryStats();
  if (item == DATA_RIDE_MODE) { snprintf(buffer, size, "%s", rideModeName(telemetryRideMode())); return; }
  switch (item) {
    case DATA_DUTY:
      snprintf(buffer, size, "%.0f%%", fabsf(v.dutyCycle) * 100.0F);
      break;
    case DATA_TRIP:
      snprintf(buffer, size, "%s %s", t.trip, distanceUnitLabel());
      break;
    case DATA_ODOMETER:
      snprintf(buffer, size, "%s %s", t.odo, distanceUnitLabel());
      break;
    case DATA_AVG_SPEED:
      snprintf(buffer, size, "%s %s", t.avg, speedUnitLabel());
      break;
    case DATA_SPEED:
      snprintf(buffer, size, "%s %s", t.speed, speedUnitLabel());
      break;
    case DATA_RANGE:
      formatAvailableRange(buffer, size, stats.rangeKm, true);
      break;
    case DATA_RIDE_EFFICIENCY:
      formatAvailableEnergyRate(buffer, size, stats.tripWhPerKm, true);
      break;
    case DATA_LIFETIME_EFFICIENCY:
      formatAvailableEnergyRate(buffer, size, stats.lifetimeWhPerKm, true,
                                TELEMETRY_FIELD_LIFETIME_EFFICIENCY);
      break;
    case DATA_RIDE_ENERGY:
      formatAvailableEnergy(buffer, size, stats.tripWh, 0, TELEMETRY_FIELD_TRIP_ENERGY);
      break;
    case DATA_REGEN_ENERGY:
      formatAvailableEnergy(buffer, size, stats.tripRegenWh, 1, TELEMETRY_FIELD_REGEN_ENERGY);
      break;
    case DATA_VOLTAGE:
      snprintf(buffer, size, "%s", t.voltageWithUnit);
      break;
    case DATA_CURRENT:
      snprintf(buffer, size, "%s", t.currentWithUnit);
      break;
    case DATA_MOTOR_CURRENT: {
      char value[12];
      const char *unit = NULL;
      formatScaledValue(value, sizeof(value), v.motorCurrent, "A", "kA", &unit);
      snprintf(buffer, size, "%s%s", value, unit);
      break;
    }
    case DATA_MOTOR_TEMP:
      snprintf(buffer, size, "%d °C", v.motorTemp);
      break;
    case DATA_ESC_TEMP:
      snprintf(buffer, size, "%d °C", v.escTemp);
      break;
    case DATA_BATTERY:
      snprintf(buffer, size, "%d%%", v.batteryPercent);
      break;
    case DATA_UPTIME:
      snprintf(buffer, size, "%s", t.uptime);
      break;
    case DATA_POWER:
      formatPowerWithUnit(buffer, size, v.watts);
      break;
    case DATA_LEARNED_CAPACITY:
      if (stats.learnedCapacityAh > 0.0F)
        snprintf(buffer, size, "%.1f Ah", stats.learnedCapacityAh);
      else
        snprintf(buffer, size, "-");
      break;
    case DATA_PACK_RESISTANCE:
      if (stats.packMilliOhm > 0.0F)
        snprintf(buffer, size, "%.0f mOhm", stats.packMilliOhm);
      else
        snprintf(buffer, size, "-");
      break;
    default:
      buffer[0] = '\0';
      break;
  }
}

// Defined with the Pixel theme below: its readouts wear the widest bitmap face
// their text still fits into, rather than one fixed size.
static const lv_font_t *pixelFontForWidth(const char *text, int room);

static void applyDashboardDataOverrides(DashboardMode mode, const DashboardValues &v) {
  const uint8_t count = dashboardDataSlotCount(mode);
  for (uint8_t slot = 0; slot < count && slot < DASH_DATA_SLOTS_MAX; slot++) {
    lv_obj_t *value = dw.dataValues[slot];
    if (!value) continue;
    const DashboardDataItem selected = dashboardDataSelection(mode, slot);
    if (dw.dataIcons[slot])
      setIconSource(dw.dataIcons[slot], dashboardDataIcon(selected),
                    dw.dataIconFixed[slot] ? dw.dataIconColors[slot] : accentLv());
    if (selected == dashboardDataDefault(mode, slot)) continue;
    char text[32];
    formatDashboardData(text, sizeof(text), selected, v);
    if (dw.dataCaptions[slot]) setLabelText(dw.dataCaptions[slot], dashboardDataLabel(selected));
    // The few themes whose native secondary values use display-sized numeric
    // fonts need a compact font once a value gains a textual unit such as
    // Wh/km. Other themes retain their native slot typography.
    if (mode == MODE_LARGE_TILES || mode == MODE_REDLINE || mode == MODE_MINIMAL)
      lv_obj_set_style_text_font(value, F2, 0);
    else if (mode == MODE_BIG_READOUT || mode == MODE_MOTOR_DATA)
      lv_obj_set_style_text_font(value, F1, 0);
    else if (mode == MODE_BARS) {
      // A reassigned row prints value and unit in one label, which at the
      // native 20 px face can run past the card edge ("1250 KM" does). Step
      // down only when it actually would, so short readings keep full size.
      lv_point_t size;
      lv_txt_get_size(&size, text, &lv_font_rajdhani_20, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      lv_obj_set_style_text_font(value, size.x > 68 ? F2 : &lv_font_rajdhani_20, 0);
    } else if (mode == MODE_PIXEL_GAUGE)
      lv_obj_set_style_text_font(value, pixelFontForWidth(text, max(40, (int)lv_obj_get_width(value))), 0);
    setLabelText(value, text);
    if (dw.dataUnits[slot]) lv_obj_add_flag(dw.dataUnits[slot], LV_OBJ_FLAG_HIDDEN);
  }
}

// ── Startup sweep framework ───────────────────────────────────────────────────
// Instrument-cluster self-test on dashboard entry: gauges/bars/numbers sweep
// 0 -> full scale -> live value. updateDashboard() is frozen while a sweep
// runs so the 100 ms tick does not fight the animations. Animations use a
// widget as their lv_anim var, so deleting the screen also kills them; every
// sweep therefore needs its own identity widget.
//
// A theme whose instrument is more than one object (a dial with its needle
// and digits, a bar with its readout) sweeps it through a SweepFn, so every
// part moves on the same frame and the text is laid out exactly the way its
// update path lays it out.

typedef void (*SweepFn)(int value);

enum SweepKind : uint8_t { SWEEP_NUM, SWEEP_METER, SWEEP_FN };

struct SweepSlot {
  lv_obj_t *obj;
  SweepKind kind;
  SegMeterWidget *meter;  // SWEEP_METER
  SweepFn fn;             // SWEEP_FN
  int target;
  int max;
};
static SweepSlot sweepSlots[12];
static int sweepSlotCount = 0;
static uint32_t sweepEndMs = 0;
// The self-test sweep on entering a dashboard is switched off: the theme appears at its live
// readings and the glides take it from there. The sweep code stays, so turning this back on
// restores it (setDashboardStartupSweepEnabled still disables it around a restyle).
static constexpr bool kStartupSweepAvailable = false;
static bool startupSweepEnabled = true;

void setDashboardStartupSweepEnabled(bool enabled) {
  startupSweepEnabled = enabled;
}

// Dashboard animations and effects continue after the selector screen has
// restored its fixed menu palette. Keep those asynchronous callbacks scoped to
// the selected dashboard's own colours as well.
struct DashboardAccentScope {
  bool restoreChrome;
  DashboardAccentScope() : restoreChrome(uiChromeAccentEnabled()) { setUiChromeAccent(false); }
  ~DashboardAccentScope() { setUiChromeAccent(restoreChrome); }
};

static SweepSlot *findSweepSlot(void *obj) {
  for (int i = 0; i < sweepSlotCount; i++) {
    if (sweepSlots[i].obj == obj) return &sweepSlots[i];
  }
  return NULL;
}

static void sweepExecCb(void *obj, int32_t value) {
  DashboardAccentScope accentScope;
  SweepSlot *slot = findSweepSlot(obj);
  if (!slot) return;
  switch (slot->kind) {
    case SWEEP_NUM: {
      // Only for labels sized once from speedFitTemplate(), whose box already
      // holds the widest value the sweep can reach.
      char text[16];
      formatSpeedValue(text, sizeof(text), value);
      setLabelText(slot->obj, text);
      break;
    }
    case SWEEP_METER:
      if (slot->meter) setSegMeterValue(*slot->meter, value, slot->max);
      break;
    case SWEEP_FN:
      if (slot->fn) slot->fn(value);
      break;
  }
}

static void sweepReadyCb(lv_anim_t *a) {
  SweepSlot *slot = findSweepSlot(a->var);
  if (!slot || slot->target == slot->max) return;  // already resting at its value
  lv_anim_t back;
  lv_anim_init(&back);
  lv_anim_set_var(&back, slot->obj);
  lv_anim_set_exec_cb(&back, sweepExecCb);
  lv_anim_set_values(&back, slot->max, slot->target);
  lv_anim_set_time(&back, 350);
  lv_anim_set_path_cb(&back, lv_anim_path_ease_out);
  lv_anim_start(&back);
}

static void startSweep(SweepKind kind, lv_obj_t *obj, int target, int maxValue, uint16_t delayMs,
                       SegMeterWidget *meter = NULL, SweepFn fn = NULL) {
  if (!obj || maxValue <= 0) return;
  if (sweepSlotCount >= (int)(sizeof(sweepSlots) / sizeof(sweepSlots[0]))) {
    LV_LOG_WARN("startup sweep slots exhausted");
    return;
  }
  SweepSlot &slot = sweepSlots[sweepSlotCount++];
  slot = {obj, kind, meter, fn, target, maxValue};
  if (!kStartupSweepAvailable || !startupSweepEnabled) {
    // A rebuild of the theme already on screen: land on the live value
    // through the same draw path, without replaying the self-test.
    sweepExecCb(obj, target);
    sweepSlotCount--;
    return;
  }

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, sweepExecCb);
  lv_anim_set_values(&a, 0, maxValue);
  lv_anim_set_time(&a, 420);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_ready_cb(&a, sweepReadyCb);
  lv_anim_start(&a);

  const uint32_t end = millis() + delayMs + 420 + 350 + 100;
  if (end > sweepEndMs) sweepEndMs = end;
}

// The meter's first block stands in as the animation's identity.
static void sweepMeter(SegMeterWidget &meter, int value, int maxValue, uint16_t delayMs) {
  startSweep(SWEEP_METER, meter.blocks[0], constrain(value, 0, maxValue), maxValue, delayMs, &meter);
}

static void sweepNumber(lv_obj_t *label, int value, int maxValue) {
  startSweep(SWEEP_NUM, label, value, maxValue, 0);
}

static void sweepWith(lv_obj_t *identity, SweepFn fn, int target, int maxValue, uint16_t delayMs = 0) {
  startSweep(SWEEP_FN, identity, target, maxValue, delayMs, NULL, fn);
}

#ifdef CYD_LVGL_PREVIEW
// Preview-only: ends every running startup sweep on the value it would have
// settled on. Deleting the animations alone is not enough — their start values
// were applied at build time, so a dial, ring or chart reveal would be left
// sitting at zero.

void previewFinishStartupSweep() {
  for (int i = 0; i < sweepSlotCount; i++) {
    lv_anim_del(sweepSlots[i].obj, sweepExecCb);
    sweepExecCb(sweepSlots[i].obj, sweepSlots[i].target);
  }
  sweepSlotCount = 0;
  sweepEndMs = 0;
}
#endif

// A readout the controller does not report shows "-", and must not count
// through numbers it never had.
static void sweepSpeedWith(lv_obj_t *identity, SweepFn fn, const DashboardValues &v, uint16_t delayMs = 0) {
  if (dashHas(TELEMETRY_FIELD_SPEED)) sweepWith(identity, fn, v.speedKmh, speedGaugeMax(), delayMs);
}

static void sweepPowerWith(lv_obj_t *identity, SweepFn fn, const DashboardValues &v, uint16_t delayMs = 120) {
  if (dashHas(TELEMETRY_FIELD_POWER)) sweepWith(identity, fn, v.watts, powerBarMax(), delayMs);
}

// ── Glides ────────────────────────────────────────────────────────────────────
// The needles, rings and bars of the dashboard on screen that glide between readings, in the
// order their theme adds them (see Glide in ui_common.h). They belong to that one screen: a
// new screen drops its predecessor's, and so does deleting the screen, because an animation
// that outlived its widgets would draw into freed objects.
static constexpr int kMaxGlides = 6;
static Glide glides[kMaxGlides];
static int glideCount = 0;
static lv_obj_t *glideScreen = nullptr;

// Retain LVGL's elapsed time and easing state while instrument animations are
// off the scheduler. Resuming must not early-apply the original starting value.
static lv_anim_t pausedInstrumentAnimations[12 + kMaxGlides];
static int pausedInstrumentCount = 0;
static lv_timer_t *instrumentResumeTimer = nullptr;
static uint32_t instrumentPausedAt = 0;

static void discardPausedInstruments() {
  if (instrumentResumeTimer) lv_timer_del(instrumentResumeTimer);
  instrumentResumeTimer = nullptr;
  pausedInstrumentCount = 0;
}

static void resumeInstrumentsCb(lv_timer_t *) {
  instrumentResumeTimer = nullptr;
  if (sweepEndMs && sweepEndMs > instrumentPausedAt)
    sweepEndMs += millis() - instrumentPausedAt;
  for (int i = 0; i < pausedInstrumentCount; ++i) {
    pausedInstrumentAnimations[i].early_apply = false;
    lv_anim_start(&pausedInstrumentAnimations[i]);
  }
  pausedInstrumentCount = 0;
}

void pauseDashboardAnimations(uint32_t durationMs) {
  if (!glideScreen) return;
  if (!instrumentResumeTimer) {
    instrumentPausedAt = millis();
    auto suspend = [](void *var, lv_anim_exec_xcb_t callback) {
      lv_anim_t *animation = lv_anim_get(var, callback);
      if (!animation) return;
      pausedInstrumentAnimations[pausedInstrumentCount++] = *animation;
      lv_anim_del(var, animation->exec_cb);
    };
    for (int i = 0; i < sweepSlotCount; ++i) suspend(sweepSlots[i].obj, sweepExecCb);
    for (int i = 0; i < glideCount; ++i) suspend(&glides[i], nullptr);
    instrumentResumeTimer = lv_timer_create(resumeInstrumentsCb, durationMs, nullptr);
    lv_timer_set_repeat_count(instrumentResumeTimer, 1);
  } else {
    lv_timer_set_period(instrumentResumeTimer, durationMs);
    lv_timer_reset(instrumentResumeTimer);
  }
}

static void glideScreenDeleteCb(lv_event_t *event) {
  // A replacement screen may already have been built before this outgoing one is
  // deleted, so forget only the screen that is actually going.
  if (glideScreen != lv_event_get_target(event)) return;
  discardPausedInstruments();
  sweepSlotCount = 0;
  sweepEndMs = 0;
  for (int i = 0; i < glideCount; ++i) glideStop(glides[i]);
  glideCount = 0;
  glideScreen = nullptr;
}

static void glideBegin(lv_obj_t *scr) {
  discardPausedInstruments();
  for (int i = 0; i < glideCount; ++i) glideStop(glides[i]);
  glideCount = 0;
  glideScreen = scr;
  lv_obj_add_event_cb(scr, glideScreenDeleteCb, LV_EVENT_DELETE, nullptr);
}

static Glide &glideAdd(void (*place)(void *context, int position), void *context) {
  Glide &glide = glides[glideCount < kMaxGlides ? glideCount++ : kMaxGlides - 1];
  glideInit(glide, place, context);
  return glide;
}

// A reading as a position on a scale of 0..maximum; below zero is zero.
static int glidePosition(int value, int maximum) {
  return (int)lroundf(constrain((float)value / max(1, maximum), 0.0F, 1.0F) * kGlideScale);
}

#ifdef CYD_LVGL_PREVIEW
// Where an instrument is, and where it is heading, in kGlideScale ths; `index` is the order its theme adds them.
int previewGlidePosition(int index) { return glides[index].position; }
int previewGlideTarget(int index) { return glides[index].target; }
#endif

// ── HUD (Cyber HUD) ───────────────────────────────────────────────────────────

// Point storage for decorative lv_lines. The points must outlive the objects,
// so every stroke is carved out of this pool: slot 0 is the HUD's speed dial,
// slot 1 its power dial.
static lv_point_t strokePool[2][72];
static int strokePoolUsed[2];

// An lv_line sizes itself from its largest point coordinate, so absolute points
// would give every stroke a bounding box reaching back to the screen origin —
// and LVGL would re-render all of them whenever anything inside that region
// changed. Storing points relative to the stroke's own top-left corner keeps
// each box tight around the pixels it draws.
static lv_obj_t *makeStroke(lv_obj_t *parent, int slot, const lv_point_t *pts, int count, lv_color_t color,
                            int width) {
  if (strokePoolUsed[slot] + count > (int)(sizeof(strokePool[0]) / sizeof(lv_point_t))) return NULL;
  lv_point_t *stored = &strokePool[slot][strokePoolUsed[slot]];
  strokePoolUsed[slot] += count;

  lv_coord_t minX = pts[0].x;
  lv_coord_t minY = pts[0].y;
  for (int i = 1; i < count; i++) {
    minX = min(minX, pts[i].x);
    minY = min(minY, pts[i].y);
  }
  for (int i = 0; i < count; i++) {
    stored[i] = {(lv_coord_t)(pts[i].x - minX), (lv_coord_t)(pts[i].y - minY)};
  }

  lv_obj_t *line = lv_line_create(parent);
  makePassive(line);  // a dial-sized hit box would otherwise eat taps
  lv_line_set_points(line, stored, count);
  lv_obj_set_pos(line, minX, minY);
  lv_obj_set_style_line_width(line, width, 0);
  lv_obj_set_style_line_color(line, color, 0);
  lv_obj_set_style_line_rounded(line, width > 1, 0);
  return line;
}

// Point `fraction`/256 of the way from a to b.
static lv_point_t hexLerp(const lv_point_t &a, const lv_point_t &b, int fraction) {
  return {(lv_coord_t)(a.x + (b.x - a.x) * fraction / 256),
          (lv_coord_t)(a.y + (b.y - a.y) * fraction / 256)};
}

// Keep every Cyber HUD decoration on exactly the same six-sided geometry.
// `scale` is relative to the outer frame (256 = on the outline); smaller
// values move the vertices toward the panel centre by an even amount.
static void hudHexVertices(const Item &frame, lv_point_t (&vertices)[6], int scale = 256) {
  const int cx = frame.x + frame.w / 2;
  const int cy = frame.y + frame.h / 2;
  const int left = cx - frame.w / 2;
  const int right = cx + frame.w / 2;
  const int top = cy - frame.h / 2;
  const int bottom = cy + frame.h / 2;
  const int shoulder = 24;
  const lv_point_t outer[6] = {
      {(lv_coord_t)(left + shoulder), (lv_coord_t)top},
      {(lv_coord_t)(right - shoulder), (lv_coord_t)top},
      {(lv_coord_t)right, (lv_coord_t)cy},
      {(lv_coord_t)(right - shoulder), (lv_coord_t)bottom},
      {(lv_coord_t)(left + shoulder), (lv_coord_t)bottom},
      {(lv_coord_t)left, (lv_coord_t)cy},
  };
  for (int i = 0; i < 6; i++) {
    vertices[i] = {
        (lv_coord_t)(cx + (outer[i].x - cx) * scale / 256),
        (lv_coord_t)(cy + (outer[i].y - cy) * scale / 256),
    };
  }
}

// Seven edges join the ribbon's eight path vertices. Each edge may use a
// different division count while sharing one visually consistent segment
// pitch.
// Wanted block length along the ribbon. The solver below picks the nearby
// pitch that divides every edge most evenly, so this is a target, not a rule.
constexpr float kHudMeterBlockPx = 7.05f;
constexpr int kHudMeterTerminalBlocks = 2;
constexpr int kHudMeterPathEdges = 7;
constexpr int kHudMeterPathPoints = kHudMeterPathEdges + 1;
constexpr int kHudMeterTopEdge = 3;
constexpr int kHudMeterMaxBlocks = 72;
constexpr float kHudMeterGapPx = 2.0f;
constexpr float kHudMeterTopGapScale = 2.0f;
constexpr float kHudMeterHalfWidthPx = 4.0f;
// Clearance between the frame outline and the blocks. The Simple meter gets its
// clean look from exactly this: a bordered housing with the segments inset, so
// the frame stays a continuous container instead of something the bar sits on.
constexpr float kHudMeterWallGapPx = 2.0f;
constexpr float kHudMeterCentreInset = kHudMeterWallGapPx + kHudMeterHalfWidthPx;
constexpr float kHudMeterInnerWallInset = kHudMeterWallGapPx + 2.0f * kHudMeterHalfWidthPx;

// The centre hudHexVertices actually builds the shape around. Integer division,
// matching that function exactly, rather than the true arithmetic centre.
static void hudHexCentre(const Item &frame, float &cx, float &cy) {
  cx = (float)(frame.x + frame.w / 2);
  cy = (float)(frame.y + frame.h / 2);
}

// Shrinking a hexagon toward its centre is not a parallel outline: the offset
// comes out proportional to each edge's distance from the centre, so the top
// and bottom edges move in noticeably less than the pointed ends do. On this
// frame that was 4.4 px against 6.2 px, which is why blocks half-width 5 rode
// over the top and bottom outline while clearing it everywhere else. Offsetting
// each edge along its own normal and re-intersecting keeps the gap even, which
// is what lets the meter live in a channel of constant width.
static void hudInsetHex(const Item &frame, float inset, lv_point_t (&out)[6]) {
  lv_point_t base[6];
  hudHexVertices(frame, base);
  float cx, cy;
  hudHexCentre(frame, cx, cy);
  float originX[6], originY[6], dirX[6], dirY[6];
  for (int i = 0; i < 6; i++) {
    const int next = (i + 1) % 6;
    const float ex = (float)base[next].x - base[i].x;
    const float ey = (float)base[next].y - base[i].y;
    const float length = max(0.001f, sqrtf(ex * ex + ey * ey));
    float nx = -ey / length;
    float ny = ex / length;
    if ((cx - base[i].x) * nx + (cy - base[i].y) * ny < 0.0f) {  // point it inward
      nx = -nx;
      ny = -ny;
    }
    originX[i] = base[i].x + nx * inset;
    originY[i] = base[i].y + ny * inset;
    dirX[i] = ex;
    dirY[i] = ey;
  }
  for (int i = 0; i < 6; i++) {
    const int prev = (i + 5) % 6;
    const float cross = dirX[prev] * dirY[i] - dirY[prev] * dirX[i];
    if (fabsf(cross) < 0.001f) {  // parallel edges cannot meet; keep the offset point
      out[i] = {(lv_coord_t)lroundf(originX[i]), (lv_coord_t)lroundf(originY[i])};
      continue;
    }
    const float t = ((originX[i] - originX[prev]) * dirY[i] - (originY[i] - originY[prev]) * dirX[i]) / cross;
    out[i] = {(lv_coord_t)lroundf(originX[prev] + dirX[prev] * t),
              (lv_coord_t)lroundf(originY[prev] + dirY[prev] * t)};
  }
}

// The outer housing is one uniform 2 px outline. The inner wall and vents
// retain the panel depth without changing the perimeter's weight.
static void makeHexPanel(lv_obj_t *parent, const Item &frame, int slot) {
  strokePoolUsed[slot] = 0;
  const lv_color_t base = accentDarkLv();
  const lv_color_t inner = dashboardLightModeActive() && accentTheme == ACCENT_DEFAULT
                               ? lv_color_black() : lv_color_mix(accentDarkLv(), dashBlack(), 110);

  lv_point_t outer[6];
  hudHexVertices(frame, outer);
  lv_point_t v[7];
  for (int i = 0; i < 6; i++) v[i] = outer[i];
  v[6] = v[0];
  makeStroke(parent, slot, v, 7, base, 2);

  // Inner wall of the meter channel. Leave its bottom edge open so no static
  // line passes through the KM/H and W labels or the terminal meter blocks.
  lv_point_t innerHex[6];
  hudInsetHex(frame, kHudMeterInnerWallInset, innerHex);
  const lv_point_t nested[6] = {
      innerHex[4], innerHex[5], innerHex[0], innerHex[1], innerHex[2], innerHex[3],
  };
  makeStroke(parent, slot, nested, 6, inner, 1);

  // Three short vents hanging off the top inner wall retain a small amount of
  // instrumentation detail without interrupting the uniform outer perimeter.
  for (int i = 0; i < 3; i++) {
    // Hung from the inner wall pointing further in. On the outer edge they used
    // to drop straight through the blocks.
    const lv_point_t at = hexLerp(innerHex[0], innerHex[1], 108 + i * 20);
    const lv_point_t vent[2] = {at, {at.x, (lv_coord_t)(at.y + 4)}};
    makeStroke(parent, slot, vent, 2, inner, 1);
  }
}

static float hudPointDistance(const lv_point_t &a, const lv_point_t &b) {
  const float dx = (float)b.x - a.x;
  const float dy = (float)b.y - a.y;
  return sqrtf(dx * dx + dy * dy);
}

// A polygon keeps every straight run on one exact baseline. LVGL's thick-line
// rasterizer rounds each short dash independently, which made adjacent blocks
// wander by a pixel even though their mathematical centreline was straight.
static lv_area_t hudPolyBounds(const lv_point_t *points, uint16_t count) {
  lv_area_t bounds = {points[0].x, points[0].y, points[0].x, points[0].y};
  for (uint16_t i = 1; i < count; ++i) {
    bounds.x1 = min(bounds.x1, points[i].x);
    bounds.y1 = min(bounds.y1, points[i].y);
    bounds.x2 = max(bounds.x2, points[i].x);
    bounds.y2 = max(bounds.y2, points[i].y);
  }
  return bounds;
}

static void fillHudPoly(lv_draw_ctx_t *drawCtx, const lv_point_t *points, uint16_t count,
                        lv_color_t color) {
  if (count < 3) return;
  const lv_area_t bounds = hudPolyBounds(points, count);
  const lv_area_t &clip = *drawCtx->clip_area;
  // Reject before LVGL allocates a temporary polygon buffer and processes
  // its vertices. Most segments miss a small partial-buffer refresh entirely.
  if (bounds.x2 < clip.x1 || bounds.x1 > clip.x2 ||
      bounds.y2 < clip.y1 || bounds.y1 > clip.y2) return;
  lv_draw_rect_dsc_t fill;
  lv_draw_rect_dsc_init(&fill);
  fill.bg_color = color;
  fill.bg_opa = LV_OPA_COVER;
  lv_draw_polygon(drawCtx, &fill, points, count);
}

// The final convex polygons are fixed by the frame geometry. Cache them once
// so dashboard refreshes only choose their colours and submit them to LVGL.
struct HudMeterGeometry {
  bool ready;
  int total;
  uint8_t pointCounts[kHudMeterMaxBlocks];
  lv_point_t blocks[kHudMeterMaxBlocks][6];
  uint8_t continuousPointCounts[kHudMeterMaxBlocks];
  lv_point_t continuousBlocks[kHudMeterMaxBlocks][6];
};

// Clip a convex meter block to the horizontal bottom of the inset housing.
// The two terminal blocks deliberately continue beyond this line, so clipping
// them here produces a clean diagonal finish without painting over the 2 px
// frame clearance.
static uint8_t hudClipBlockToBottom(const lv_point_t *input, uint8_t inputCount,
                                    lv_coord_t bottom, lv_point_t *output) {
  uint8_t outputCount = 0;
  for (uint8_t index = 0; index < inputCount; index++) {
    const lv_point_t &from = input[index];
    const lv_point_t &to = input[(index + 1) % inputCount];
    const bool fromInside = from.y <= bottom;
    const bool toInside = to.y <= bottom;
    if (fromInside && toInside) {
      output[outputCount++] = to;
    } else if (fromInside != toInside) {
      const float dy = (float)to.y - from.y;
      const float t = fabsf(dy) < 0.001f ? 0.0f : ((float)bottom - from.y) / dy;
      output[outputCount++] = {
          (lv_coord_t)lroundf(from.x + ((float)to.x - from.x) * t), bottom};
      if (toInside) output[outputCount++] = to;
    }
  }
  return outputCount;
}

// Edges of different lengths do not divide into the same block size, so
// rounding each edge on its own makes the long top run visibly coarser than the
// diagonals. Instead pick the one pitch, near the requested size, that divides
// *every* edge most evenly, and cut all of them to it. On these two frames that
// lands within 0.6% and 2.6% respectively, against roughly 20% for any scheme
// that divides the sweep blindly.
//
// The longest edge is additionally held to an even number of blocks. That edge
// is the top one, so an even count puts a cut exactly on the frame's vertical
// centre line; the horizontal centre line is already a cut, because the seam at
// the left and right points is horizontal by symmetry.
static float hudSolvePitch(const float (&length)[kHudMeterPathEdges], int longest) {
  const float lo = kHudMeterBlockPx * 0.6f;
  const float hi = kHudMeterBlockPx * 1.5f;
  float bestPitch = kHudMeterBlockPx;
  float bestCost = 1e9f;
  for (int edge = 0; edge < kHudMeterPathEdges; edge++) {
    for (int divisions = 1; length[edge] / divisions >= lo; divisions++) {
      const float pitch = length[edge] / divisions;
      if (pitch > hi) continue;
      if (lroundf(length[longest] / pitch) < 1 || (lroundf(length[longest] / pitch) & 1)) continue;
      float worst = 0.0f;
      for (int other = 0; other < kHudMeterPathEdges; other++) {
        const float actual = length[other] / max(1L, lroundf(length[other] / pitch));
        worst = max(worst, fabsf(actual - pitch) / pitch);
      }
      // Half a unit of pull back towards the requested size, so a marginally
      // more even pitch cannot drag the blocks far off the intended length.
      const float cost = worst + 0.5f * fabsf(pitch - kHudMeterBlockPx) / kHudMeterBlockPx;
      if (cost < bestCost) {
        bestCost = cost;
        bestPitch = pitch;
      }
    }
  }
  return bestPitch;
}

static void hudMeterEdgeLengths(const lv_point_t (&centre)[kHudMeterPathPoints],
                                float (&length)[kHudMeterPathEdges], int &longest) {
  longest = 0;
  for (int edge = 0; edge < kHudMeterPathEdges; edge++) {
    length[edge] = max(0.001f, hudPointDistance(centre[edge], centre[edge + 1]));
    if (length[edge] > length[longest]) longest = edge;
  }
}

// The ribbon is one continuous band round the hexagon, cut into blocks. Cuts
// inside an edge run square across the band; a cut at a corner runs along that
// corner's miter seam, which is the line through the outer and inner offset
// vertices -- so the two blocks meeting there split along the join instead of
// one of them having to bend.
//
// Every block therefore lies on a single edge and comes out a trapezoid: its
// two rails are the parallel outer and inner offsets, its two ends are either
// square cuts or seam-parallel ones. All convex, which is what LVGL's polygon
// fill requires. Building these points once also makes every redraw bit-for-bit
// identical instead of repeating floating-point rounding at runtime.
static void hudBuildMeterGeometry(const lv_point_t (&centre)[kHudMeterPathPoints],
                                  const lv_point_t (&outer)[kHudMeterPathPoints],
                                  const lv_point_t (&inner)[kHudMeterPathPoints],
                                  lv_coord_t bottomClip,
                                  HudMeterGeometry &geometry) {
  if (geometry.ready) return;
  float length[kHudMeterPathEdges];
  int longest;
  hudMeterEdgeLengths(centre, length, longest);
  const float solvedPitch = hudSolvePitch(length, longest);

  int block = 0;
  for (int edge = 0; edge < kHudMeterPathEdges; edge++) {
    const float ux = (centre[edge + 1].x - centre[edge].x) / length[edge];
    const float uy = (centre[edge + 1].y - centre[edge].y) / length[edge];
    // Normal scaled to half the band thickness, pointing at the inner rail.
    // Which way that is depends on the direction of travel, and the two meters
    // run opposite ways round the frame so their windings differ. Taking the
    // sign from the inner offset rather than from a fixed rotation is what
    // keeps the corner quads from folding over on the mirrored meter.
    float nx = -uy * kHudMeterHalfWidthPx;
    float ny = ux * kHudMeterHalfWidthPx;
    if ((inner[edge].x - centre[edge].x) * nx + (inner[edge].y - centre[edge].y) * ny < 0.0f) {
      nx = -nx;
      ny = -ny;
    }

    // A cut on the seam is oblique to this edge, so backing off along the edge
    // by half a gap would leave a narrower gap than a square cut does. Dividing
    // by the cosine of the half-turn restores it.
    float headWiden = 1.0f;
    float tailWiden = 1.0f;
    if (edge > 0) {
      const float px = (centre[edge].x - centre[edge - 1].x) / length[edge - 1];
      const float py = (centre[edge].y - centre[edge - 1].y) / length[edge - 1];
      headWiden = constrain(1.0f / max(0.34f, sqrtf((1.0f + (px * ux + py * uy)) * 0.5f)), 1.0f, 3.0f);
    }
    if (edge < kHudMeterPathEdges - 1) {
      const float qx = (centre[edge + 2].x - centre[edge + 1].x) / length[edge + 1];
      const float qy = (centre[edge + 2].y - centre[edge + 1].y) / length[edge + 1];
      tailWiden = constrain(1.0f / max(0.34f, sqrtf((1.0f + (qx * ux + qy * uy)) * 0.5f)), 1.0f, 3.0f);
    }

    const int divisions = (int)max(1L, lroundf(length[edge] / solvedPitch));
    const float pitch = length[edge] / divisions;
    const float availableGap = max(0.0f, pitch - 1.0f);
    const float halfCornerGap = min(kHudMeterGapPx, availableGap) * 0.5f;
    // The long horizontal run needs stronger visual separation than the short
    // diagonals. Only its internal cuts are doubled; its two miter seams keep
    // the shared corner gap so the transition into each diagonal stays even.
    const float internalGap =
        edge == kHudMeterTopEdge ? kHudMeterGapPx * kHudMeterTopGapScale : kHudMeterGapPx;
    const float halfInternalGap = min(internalGap, availableGap) * 0.5f;
    for (int index = 0; index < divisions; index++) {
      lv_point_t startOuter, startInner, endOuter, endInner;
      lv_point_t continuousStartOuter, continuousStartInner;
      lv_point_t continuousEndOuter, continuousEndInner;

      if (index == 0 && edge > 0) {
        // Meets the previous edge: start on this corner's seam, pushed forward.
        continuousStartOuter = outer[edge];
        continuousStartInner = inner[edge];
        const float back = halfCornerGap * headWiden;
        startOuter = {(lv_coord_t)lroundf(outer[edge].x + ux * back),
                      (lv_coord_t)lroundf(outer[edge].y + uy * back)};
        startInner = {(lv_coord_t)lroundf(inner[edge].x + ux * back),
                      (lv_coord_t)lroundf(inner[edge].y + uy * back)};
      } else {
        const float continuousAt = index * pitch;
        const float continuousX = centre[edge].x + ux * continuousAt;
        const float continuousY = centre[edge].y + uy * continuousAt;
        continuousStartOuter = {(lv_coord_t)lroundf(continuousX - nx),
                                (lv_coord_t)lroundf(continuousY - ny)};
        continuousStartInner = {(lv_coord_t)lroundf(continuousX + nx),
                                (lv_coord_t)lroundf(continuousY + ny)};
        const float at = index * pitch + halfInternalGap;
        const float px = centre[edge].x + ux * at;
        const float py = centre[edge].y + uy * at;
        startOuter = {(lv_coord_t)lroundf(px - nx), (lv_coord_t)lroundf(py - ny)};
        startInner = {(lv_coord_t)lroundf(px + nx), (lv_coord_t)lroundf(py + ny)};
      }

      if (index == divisions - 1 && edge < kHudMeterPathEdges - 1) {
        continuousEndOuter = outer[edge + 1];
        continuousEndInner = inner[edge + 1];
        const float back = halfCornerGap * tailWiden;
        endOuter = {(lv_coord_t)lroundf(outer[edge + 1].x - ux * back),
                    (lv_coord_t)lroundf(outer[edge + 1].y - uy * back)};
        endInner = {(lv_coord_t)lroundf(inner[edge + 1].x - ux * back),
                    (lv_coord_t)lroundf(inner[edge + 1].y - uy * back)};
      } else {
        const float continuousAt = (index + 1) * pitch;
        const float continuousX = centre[edge].x + ux * continuousAt;
        const float continuousY = centre[edge].y + uy * continuousAt;
        continuousEndOuter = {(lv_coord_t)lroundf(continuousX - nx),
                              (lv_coord_t)lroundf(continuousY - ny)};
        continuousEndInner = {(lv_coord_t)lroundf(continuousX + nx),
                              (lv_coord_t)lroundf(continuousY + ny)};
        const float at = (index + 1) * pitch - halfInternalGap;
        const float px = centre[edge].x + ux * at;
        const float py = centre[edge].y + uy * at;
        endOuter = {(lv_coord_t)lroundf(px - nx), (lv_coord_t)lroundf(py - ny)};
        endInner = {(lv_coord_t)lroundf(px + nx), (lv_coord_t)lroundf(py + ny)};
      }

      const lv_point_t raw[4] = {startOuter, endOuter, endInner, startInner};
      const lv_point_t continuousRaw[4] = {
          continuousStartOuter, continuousEndOuter, continuousEndInner, continuousStartInner};
      lv_point_t clipped[6];
      lv_point_t continuousClipped[6];
      const uint8_t clippedCount = hudClipBlockToBottom(raw, 4, bottomClip, clipped);
      const uint8_t continuousClippedCount =
          hudClipBlockToBottom(continuousRaw, 4, bottomClip, continuousClipped);
      if (clippedCount < 3 || continuousClippedCount < 3) continue;
      if (block >= kHudMeterMaxBlocks) {
        geometry.total = block;
        geometry.ready = true;
        return;
      }
      geometry.pointCounts[block] = clippedCount;
      for (uint8_t point = 0; point < clippedCount; point++) {
        geometry.blocks[block][point] = clipped[point];
      }
      geometry.continuousPointCounts[block] = continuousClippedCount;
      for (uint8_t point = 0; point < continuousClippedCount; point++) {
        geometry.continuousBlocks[block][point] = continuousClipped[point];
      }
      block++;
    }
  }
  geometry.total = block;
  geometry.ready = true;
}

static void drawHudMeterPath(lv_draw_ctx_t *drawCtx, const HudMeterGeometry &geometry,
                             int litBlocks, lv_color_t lit, lv_color_t unlit) {
  const int litCount = constrain(litBlocks, 0, geometry.total);
  // Keep the complete dim track present at every value. Adjacent full-pitch
  // polygons meet into one continuous ribbon, then the deliberately segmented
  // active blocks are layered over it.
  for (int block = 0; block < geometry.total; block++) {
    fillHudPoly(drawCtx, geometry.continuousBlocks[block],
                geometry.continuousPointCounts[block], unlit);
  }
  for (int block = 0; block < litCount; block++) {
    fillHudPoly(drawCtx, geometry.blocks[block], geometry.pointCounts[block], lit);
  }
}

// Solved once each, because only the frames decide them.
static HudMeterGeometry hudSpeedGeometry = {};
static HudMeterGeometry hudPowerGeometry = {};

// Centreline, outer edge and inner edge of one meter, re-ordered so the band
// runs from its zero end to its full end. At each open end the same diagonal
// continues downward by two blocks. Those terminal polygons are later clipped
// to the hexagon's bottom edge inset by kHudMeterWallGapPx.
static void hudMeterBand(const Item &frame, const int (&order)[6],
                         lv_point_t (&centre)[kHudMeterPathPoints],
                         lv_point_t (&outer)[kHudMeterPathPoints],
                         lv_point_t (&inner)[kHudMeterPathPoints],
                         lv_coord_t &bottomClip) {
  lv_point_t mid[6], out[6], in[6];
  hudInsetHex(frame, kHudMeterCentreInset, mid);
  hudInsetHex(frame, kHudMeterWallGapPx, out);
  hudInsetHex(frame, kHudMeterInnerWallInset, in);
  for (int i = 0; i < 6; i++) {
    centre[i + 1] = mid[order[i]];
    outer[i + 1] = out[order[i]];
    inner[i + 1] = in[order[i]];
  }

  const float extension = kHudMeterTerminalBlocks * kHudMeterBlockPx;
  auto extendStart = [extension](lv_point_t (&rail)[kHudMeterPathPoints]) {
    const float dx = (float)rail[2].x - rail[1].x;
    const float dy = (float)rail[2].y - rail[1].y;
    const float length = max(0.001f, sqrtf(dx * dx + dy * dy));
    rail[0] = {(lv_coord_t)lroundf(rail[1].x - dx * extension / length),
               (lv_coord_t)lroundf(rail[1].y - dy * extension / length)};
  };
  auto extendEnd = [extension](lv_point_t (&rail)[kHudMeterPathPoints]) {
    const float dx = (float)rail[6].x - rail[5].x;
    const float dy = (float)rail[6].y - rail[5].y;
    const float length = max(0.001f, sqrtf(dx * dx + dy * dy));
    rail[7] = {(lv_coord_t)lroundf(rail[6].x + dx * extension / length),
               (lv_coord_t)lroundf(rail[6].y + dy * extension / length)};
  };
  extendStart(centre);
  extendStart(outer);
  extendStart(inner);
  extendEnd(centre);
  extendEnd(outer);
  extendEnd(inner);
  bottomClip = max(outer[1].y, outer[6].y);
}

static void hudEnsureMeterGeometry() {
  static const int speedOrder[6] = {4, 5, 0, 1, 2, 3};
  // Both meters now advance clockwise. Keeping the same vertex order also
  // makes their progress behavior predictable despite the mirrored housings.
  static const int powerOrder[6] = {4, 5, 0, 1, 2, 3};
  lv_point_t centre[kHudMeterPathPoints], outer[kHudMeterPathPoints], inner[kHudMeterPathPoints];
  lv_coord_t bottomClip;
  if (!hudSpeedGeometry.ready) {
    hudMeterBand(HUD_SPEED_FRAME, speedOrder, centre, outer, inner, bottomClip);
    hudBuildMeterGeometry(centre, outer, inner, bottomClip, hudSpeedGeometry);
  }
  if (!hudPowerGeometry.ready) {
    hudMeterBand(HUD_POWER_FRAME, powerOrder, centre, outer, inner, bottomClip);
    hudBuildMeterGeometry(centre, outer, inner, bottomClip, hudPowerGeometry);
  }
}

static void hudEdgeMetersDrawCb(lv_event_t *event) {
  lv_draw_ctx_t *drawCtx = lv_event_get_draw_ctx(event);
  hudEnsureMeterGeometry();
  // Leave the faces transparent so the actual dashboard background shows
  // through, including custom solid colors and gradients, with no extra fill.
  // The band fills the channel between the frame outline and the inner wall
  // without touching either - the same bordered housing the Simple meter uses.
  // Active blocks stay segmented; the empty remainder is a continuous band.
  drawHudMeterPath(drawCtx, hudSpeedGeometry, dw.hudSpeedLit,
                   dw.hudMeterLit, dw.hudMeterUnlit);
  drawHudMeterPath(drawCtx, hudPowerGeometry, dw.hudPowerLit,
                   dw.hudMeterLit, dw.hudMeterUnlit);
}

static void invalidateHudMeterChange(const HudMeterGeometry &geometry, int oldLit, int newLit) {
  if (oldLit == newLit) return;
  if (oldLit < 0) {
    lv_obj_invalidate(dw.hudMeters);
    return;
  }
  // Only active blocks change; the continuous track and shade remain fixed.
  // Union damage per meter to bound the number of LVGL invalidation entries
  // even on a zero-to-full jump (overflow forces a whole-screen refresh).
  lv_area_t damage = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
  for (int block = min(oldLit, newLit); block < max(oldLit, newLit); ++block) {
    if (geometry.pointCounts[block] < 3) continue;
    const lv_area_t bounds = hudPolyBounds(geometry.blocks[block], geometry.pointCounts[block]);
    damage.x1 = min(damage.x1, bounds.x1);
    damage.y1 = min(damage.y1, bounds.y1);
    damage.x2 = max(damage.x2, bounds.x2);
    damage.y2 = max(damage.y2, bounds.y2);
  }
  if (damage.x1 > damage.x2) return;
  // Conservative allowance for polygon edge antialiasing.
  damage.x1--; damage.y1--; damage.x2++; damage.y2++;
  lv_obj_invalidate_area(dw.hudMeters, &damage);
}

static void setHudEdgeMeterValues(int speed, int watts) {
  if (!dw.hudMeters) return;
  // Each frame solves its own division, so the two meters can differ in how
  // many blocks they hold.
  hudEnsureMeterGeometry();
  const int speedBlocks = max(1, hudSpeedGeometry.total);
  const int powerBlocks = max(1, hudPowerGeometry.total);
  const int speedMax = max(1, speedGaugeMax());
  const int speedLit = constrain((speed * speedBlocks + speedMax / 2) / speedMax, 0, speedBlocks);
  const int powerMax = max(1, powerBarMax());
  const int powerLit = constrain((watts * powerBlocks + powerMax / 2) / powerMax, 0, powerBlocks);
  if (speedLit == dw.hudSpeedLit && powerLit == dw.hudPowerLit) return;
  invalidateHudMeterChange(hudSpeedGeometry, dw.hudSpeedLit, speedLit);
  invalidateHudMeterChange(hudPowerGeometry, dw.hudPowerLit, powerLit);
  dw.hudSpeedLit = speedLit;
  dw.hudPowerLit = powerLit;
}

static void makeHudEdgeMeters(lv_obj_t *parent, lv_color_t accent) {
  dw.hudMeters = lv_obj_create(parent);
  lv_obj_remove_style_all(dw.hudMeters);
  makePassive(dw.hudMeters);
  lv_obj_set_pos(dw.hudMeters, 0, 30);
  lv_obj_set_size(dw.hudMeters, 320, 108);
  dw.hudSpeedLit = -1;
  dw.hudPowerLit = -1;
  dw.hudMeterLit = dashboardLightModeActive() ? lv_color_black() : accent;
  dw.hudMeterUnlit = dashboardLightModeActive()
                         ? lv_color_white()
                         : lv_color_mix(accent, lv_color_black(), 38);
  lv_obj_add_event_cb(dw.hudMeters, hudEdgeMetersDrawCb, LV_EVENT_DRAW_MAIN, NULL);
}

// The two edge meters redraw together, so each sweep keeps the other's value.
static int hudSweepSpeed = 0;
static int hudSweepPower = 0;

static void hudSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setScaledValueText(dw.speed, HUD_SPEED, text, layoutFontToLv(HUD_SPEED.font));
  hudSweepSpeed = value;
  setHudEdgeMeterValues(hudSweepSpeed, hudSweepPower);
}

static void hudPowerSweep(int value) {
  char text[16];
  formatPowerValue(text, sizeof(text), value);
  setScaledValueText(dw.power, HUD_POWER, text, layoutFontToLv(HUD_POWER.font));
  setLabelText(dw.powerUnit, powerUnitLabel(value));
  hudSweepPower = value;
  setHudEdgeMeterValues(hudSweepSpeed, hudSweepPower);
}

static void buildHud(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t accent = accentLv();
  DashTexts t = fmtTexts(v);
  char text[24];

  // Draw the meter first, then place the housing over it so its
  // outer frame and open inner wall retain crisp uninterrupted edges.
  makeHudEdgeMeters(scr, accent);
  makeHexPanel(scr, HUD_SPEED_FRAME, 0);
  makeHexPanel(scr, HUD_POWER_FRAME, 1);
  setHudEdgeMeterValues(displaySpeedValue(), displayPowerValue());

  // Both readouts size themselves from the value in hand and step down for
  // longer values rather than spilling into the meter channel.
  dw.speed = makeLabel(scr, HUD_SPEED, t.speed, whiteLv());
  setScaledValueText(dw.speed, HUD_SPEED, t.speed, layoutFontToLv(HUD_SPEED.font));
  // unit captions follow their own layout items now, instead of an offset from
  // the number, which left them hanging below the hexagons
  dw.speedUnit = makeLabel(scr, HUD_SPEED_UNIT, speedUnitLabel(), whiteLv());
  dw.power = makeLabel(scr, HUD_POWER, t.power, whiteLv());
  setScaledValueText(dw.power, HUD_POWER, t.power, layoutFontToLv(HUD_POWER.font));
  dw.powerUnit = makeLabel(scr, HUD_POWER_UNIT, powerUnitLabel(v.watts), whiteLv());

  makeLayoutIcon(scr, HUD_CLOCK_ICON, CYD_ICON_UPTIME, accent);
  dw.uptime = makeTopTimeLabel(scr, HUD_TOP_TIME, t.uptime, whiteLv());
  // Long vehicle names step down a font size rather than growing into the
  // clock and battery either side; short ones keep the full F20.
  lv_obj_t *oemLabel = makeLabelFont(scr, HUD_OEM, "", accent, layoutFontToLv(HUD_OEM.font));
  setFittedText(oemLabel, HUD_OEM, dashOemName(), layoutFontToLv(HUD_OEM.font));
  dw.battPct = makeTopBatteryLabel(scr, HUD_TOP_BATT_PCT, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, HUD_TOP_BATT_ICON, 5);

  lv_obj_t *voltsIcon = makeLayoutIcon(scr, HUD_VOLTS_ICON, CYD_ICON_VOLTAGE, accent);
  dw.volts = makeLabel(scr, HUD_VOLTS, t.voltageWithUnit, whiteLv());
  registerDataSlot(0, makeLabel(scr, HUD_VOLTS_LABEL, metricVoltsLabel(), whiteLv()), dw.volts, NULL, voltsIcon);
  lv_obj_t *ampsIcon = makeLayoutIcon(scr, HUD_AMPS_ICON, CYD_ICON_AMPERAGE, accent);
  dw.amps = makeLabel(scr, HUD_AMPS, t.currentWithUnit, whiteLv());
  registerDataSlot(1, makeLabel(scr, HUD_AMPS_LABEL, metricAmpsLabel(), whiteLv()), dw.amps, NULL, ampsIcon);
  lv_obj_t *motorIcon = makeLayoutIcon(scr, HUD_MOTOR_ICON, CYD_ICON_TEMP_MOTOR, accent);
  dw.motor = makeLabel(scr, HUD_MOTOR_TEMP, t.motor, whiteLv());
  registerDataSlot(2, makeLabel(scr, HUD_MOTOR_TEMP_LABEL, metricMotorLabel(), whiteLv()), dw.motor, NULL, motorIcon);
  lv_obj_t *escIcon = makeLayoutIcon(scr, HUD_ESC_ICON, CYD_ICON_TEMP_ESC, accent);
  dw.esc = makeLabel(scr, HUD_ESC_TEMP, t.esc, whiteLv());
  registerDataSlot(3, makeLabel(scr, HUD_ESC_TEMP_LABEL, "ESC", whiteLv()), dw.esc, NULL, escIcon);

  lv_obj_t *tripIcon = makeLayoutIcon(scr, HUD_TRIP_ICON, CYD_ICON_TRIP, accent);
  dw.trip = makeLabel(scr, HUD_TRIP, t.trip, whiteLv());
  registerDataSlot(4, makeLabel(scr, HUD_TRIP_LABEL, metricTripLabel(), whiteLv()), dw.trip, NULL, tripIcon);
  lv_obj_t *odoIcon = makeLayoutIcon(scr, HUD_ODO_ICON, CYD_ICON_ODO, accent);
  dw.odo = makeLabel(scr, HUD_ODO, t.odo, whiteLv());
  registerDataSlot(5, makeLabel(scr, HUD_ODO_LABEL, "ODO", whiteLv()), dw.odo, NULL, odoIcon);
  lv_obj_t *avgIcon = makeLayoutIcon(scr, HUD_AVG_ICON, CYD_ICON_AVG_SPEED, accent);
  dw.avg = makeLabel(scr, HUD_AVG, t.avg, whiteLv());
  snprintf(text, sizeof(text), "%s %s", metricAvgLabel(), speedUnitLabel());
  registerDataSlot(6, makeLabel(scr, HUD_AVG_LABEL, text, whiteLv()), dw.avg, NULL, avgIcon);
  // the top bar already carries uptime, so this cell earns its place with
  // consumption instead
  lv_obj_t *rateIcon = makeLayoutIcon(scr, HUD_RATE_ICON, CYD_ICON_EFFICIENCY, accent);
  formatAvailableEnergyRate(text, sizeof(text), dashBatteryStats().tripWhPerKm, false);
  dw.energyRate = makeLabel(scr, HUD_RATE, text, whiteLv());
  registerDataSlot(7, makeLabel(scr, HUD_RATE_LABEL, energyRateLabel(), whiteLv()), dw.energyRate, NULL, rateIcon);

  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  hudSweepSpeed = displaySpeedValue();
  hudSweepPower = displayPowerValue();
  sweepSpeedWith(dw.speed, hudSpeedSweep, v);
  sweepPowerWith(dw.power, hudPowerSweep, v);
}

// ── Update-rate tiers ─────────────────────────────────────────────────────────
// Every value class has its own refresh cadence so the 100 ms tick only
// repaints what the eye actually tracks in real time:
//   fast (every tick) — speed digits, the primary gauge visual, and watts
//   elec (200 ms)     — volts and amps
//   mid  (500 ms)     — temps and running clocks
//   slow (2 s)        — battery, trip, odo, avg: slow-moving by nature
static uint32_t elecTierLastMs = 0;
static uint32_t midTierLastMs = 0;
static uint32_t slowTierLastMs = 0;
static bool elecDue = false;
static bool midDue = false;
static bool slowDue = false;

static void refreshUpdateTiers(bool force) {
  const uint32_t now = millis();
  elecDue = force || now - elecTierLastMs >= 200;
  if (elecDue) elecTierLastMs = now;
  midDue = force || now - midTierLastMs >= 500;
  if (midDue) midTierLastMs = now;
  slowDue = force || now - slowTierLastMs >= 2000;
  if (slowDue) slowTierLastMs = now;
}

static void updateHud(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  setHudEdgeMeterValues(displaySpeedValue(), displayPowerValue());
  setScaledValueText(dw.speed, HUD_SPEED, t.speed, layoutFontToLv(HUD_SPEED.font));
  setScaledValueText(dw.power, HUD_POWER, t.power, layoutFontToLv(HUD_POWER.font));
  setLabelText(dw.powerUnit, powerUnitLabel(v.watts));
  if (elecDue) {
    setLabelText(dw.volts, t.voltageWithUnit);
    setLabelText(dw.amps, t.currentWithUnit);
  }
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
    setLabelText(dw.motor, t.motor);
    setLabelText(dw.esc, t.esc);
  }
  if (slowDue) {
    char text[24];
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    setLabelText(dw.trip, t.trip);
    setLabelText(dw.odo, t.odo);
    setLabelText(dw.avg, t.avg);
    formatAvailableEnergyRate(text, sizeof(text), dashBatteryStats().tripWhPerKm, false);
    setLabelText(dw.energyRate, text);
  }
}

// ── Dual Gauge ────────────────────────────────────────────────────────────────

// The lit ticks and the needle of a dial are one glide: glides[0] is the speed's, glides[1] the power's.
static void gaugeSpeedPlace(void *, int position) {
  setTickDialValue(dw.speedDial, position);
  setTickNeedleValue(dw.speedNeedle, position);
}

static void gaugePowerPlace(void *, int position) {
  setTickDialValue(dw.powerDial, position);
  setTickNeedleValue(dw.powerNeedle, position);
}

static void gaugeSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setScaledValueText(dw.speed, GAUGE_SPEED, text, layoutFontToLv(GAUGE_SPEED.font));
  glideAim(glides[0], glidePosition(value, speedGaugeMax()), false);
}

static void gaugePowerSweep(int value) {
  char text[16];
  formatPowerValue(text, sizeof(text), value);
  setScaledValueText(dw.power, GAUGE_POWER, text, layoutFontToLv(GAUGE_POWER.font));
  setLabelText(dw.powerUnit, powerUnitLabel(value));
  glideAim(glides[1], glidePosition(value, powerBarMax()), false);
}

static void buildGauge(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t accent = accentLv();
  const lv_color_t dark = accentDarkLv();
  DashTexts t = fmtTexts(v);

  // top bar carries the same furniture as the Cyber HUD
  makeLayoutIcon(scr, GAUGE_CLOCK_ICON, CYD_ICON_UPTIME, accent);
  dw.uptime = makeTopTimeLabel(scr, GAUGE_UPTIME, t.uptime, whiteLv());
  lv_obj_t *oemLabel = makeLabelFont(scr, GAUGE_OEM, "", accent, layoutFontToLv(GAUGE_OEM.font));
  setFittedText(oemLabel, GAUGE_OEM, dashOemName(), layoutFontToLv(GAUGE_OEM.font));
  dw.battPct = makeTopBatteryLabel(scr, GAUGE_BATTERY_PCT, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, GAUGE_BATTERY_ICON, 5);


  // Redline-style lit-tick dials on the full circular 140deg/260deg sweep:
  // dense thin ticks (white majors, dark minors), lit ticks glow accent
  dw.speedDial = makeTickDial(scr, 0, GAUGE_SPEED_GAUGE.cx, GAUGE_SPEED_GAUGE.cy, GAUGE_SPEED_GAUGE.r, 41, 5,
                              140.0F, 260.0F, 12, 7, kGlideScale, 3, 2, gaugeSpeedPts);
  // shorter ticks than the speed ring: the wider number inside needs the clear
  // radius more than the ring needs the extra tick length
  dw.powerDial = makeTickDial(scr, 41, GAUGE_POWER_GAUGE.cx, GAUGE_POWER_GAUGE.cy, GAUGE_POWER_GAUGE.r, 41, 5,
                              140.0F, 260.0F, 8, 5, kGlideScale, 3, 2, gaugePowerPts);
  // The unlit half of a ring is scale, not data. Built white, the majors read
  // as loudly as the lit accent and the eye has to hunt for the value, so both
  // tiers are mixed well down towards the face — present enough to show where
  // the scale runs, quiet enough that the lit arc is what you see first.
  if (dashboardLightModeActive()) {
    const lv_color_t unlit = lv_color_hex(0xE1E1E1);
    setTickDialUnlitPalette(dw.speedDial, unlit, unlit);
    setTickDialUnlitPalette(dw.powerDial, unlit, unlit);
  } else {
    const lv_color_t unlitMajor = lv_color_mix(whiteLv(), dashBlack(), 96);
    const lv_color_t unlitMinor = lv_color_mix(dark, dashBlack(), 140);
    setTickDialUnlitPalette(dw.speedDial, unlitMajor, unlitMinor);
    setTickDialUnlitPalette(dw.powerDial, unlitMajor, unlitMinor);
  }
  // Keep the tip at the gauge ring while shortening the radial span by 20%.
  dw.speedNeedle = makeTickNeedle(scr, GAUGE_SPEED_GAUGE.cx, GAUGE_SPEED_GAUGE.cy, 44,
                                  GAUGE_SPEED_GAUGE.r, 140, 260, kGlideScale, 5, 2);
  dw.powerNeedle = makeTickNeedle(scr, GAUGE_POWER_GAUGE.cx, GAUGE_POWER_GAUGE.cy, 38,
                                  GAUGE_POWER_GAUGE.r, 140, 260, kGlideScale, 4, 2);

  lv_obj_t *voltsIcon = makeLayoutIcon(scr, GAUGE_VOLTS_ICON, CYD_ICON_VOLTAGE, accent);
  dw.volts = makeLabel(scr, GAUGE_VOLTS, t.voltage, whiteLv());
  dw.voltsUnit = makeLabel(scr, GAUGE_VOLTS_EXTRA2, t.voltageUnit, labelLv());
  registerDataSlot(0, makeLabel(scr, GAUGE_VOLTS_LABEL, metricVoltsLabel(), labelLv()), dw.volts, dw.voltsUnit,
                   voltsIcon);
  lv_obj_t *ampsIcon = makeLayoutIcon(scr, GAUGE_AMPS_ICON, CYD_ICON_AMPERAGE, accent);
  dw.amps = makeLabel(scr, GAUGE_AMPS, t.current, whiteLv());
  dw.ampsUnit = makeLabel(scr, GAUGE_AMPS_EXTRA2, t.currentUnit, labelLv());
  registerDataSlot(1, makeLabel(scr, GAUGE_AMPS_LABEL, metricAmpsLabel(), labelLv()), dw.amps, dw.ampsUnit,
                   ampsIcon);
  lv_obj_t *motorIcon = makeLayoutIcon(scr, GAUGE_MOTOR_ICON, CYD_ICON_TEMP_MOTOR, accent);
  char temperature[8];
  snprintf(temperature, sizeof(temperature), "%d", v.motorTemp);
  dw.motor = makeLabel(scr, GAUGE_MOTOR_TEMP, temperature, whiteLv());
  dw.motorUnit = makeLabel(scr, GAUGE_MOTOR_TEMP_EXTRA2, "°C", labelLv());
  registerDataSlot(2, makeLabel(scr, GAUGE_MOTOR_TEMP_LABEL, metricMotorLabel(), labelLv()), dw.motor, dw.motorUnit,
                   motorIcon);
  lv_obj_t *escIcon = makeLayoutIcon(scr, GAUGE_ESC_ICON, CYD_ICON_TEMP_ESC, accent);
  snprintf(temperature, sizeof(temperature), "%d", v.escTemp);
  dw.esc = makeLabel(scr, GAUGE_ESC_TEMP, temperature, whiteLv());
  dw.escUnit = makeLabel(scr, GAUGE_ESC_TEMP_EXTRA2, "°C", labelLv());
  registerDataSlot(3, makeLabel(scr, GAUGE_ESC_TEMP_LABEL, "ESC", labelLv()), dw.esc, dw.escUnit, escIcon);

  placeUnitAfterValue(dw.volts, dw.voltsUnit, GAUGE_VOLTS_EXTRA2, 3);
  placeUnitAfterValue(dw.amps, dw.ampsUnit, GAUGE_AMPS_EXTRA2, 3);
  placeUnitAfterValue(dw.motor, dw.motorUnit, GAUGE_MOTOR_TEMP_EXTRA2, 3);
  placeUnitAfterValue(dw.esc, dw.escUnit, GAUGE_ESC_TEMP_EXTRA2, 3);

  // both readouts sit on their ring's centre and take the largest font the
  // value fits, so a two-digit speed fills the dial instead of floating in it
  dw.speed = makeLabel(scr, GAUGE_SPEED, t.speed, whiteLv());
  setScaledValueText(dw.speed, GAUGE_SPEED, t.speed, layoutFontToLv(GAUGE_SPEED.font));
  dw.speedUnit = makeLabel(scr, GAUGE_SPEED_UNIT, speedUnitLabel(), whiteLv());
  dw.power = makeLabel(scr, GAUGE_POWER, t.power, whiteLv());
  setScaledValueText(dw.power, GAUGE_POWER, t.power, layoutFontToLv(GAUGE_POWER.font));
  dw.powerUnit = makeLabel(scr, GAUGE_POWER_UNIT, powerUnitLabel(v.watts), whiteLv());

  lv_obj_t *tripCaption = makeLabel(scr, GAUGE_TRIP_EXTRA1, metricTripLabel(), accent);
  dw.trip = makeLabel(scr, GAUGE_TRIP, t.trip, whiteLv());
  dw.tripUnit = makeLabel(scr, GAUGE_TRIP_EXTRA2, distanceUnitLabel(), labelLv());
  lv_obj_t *odoCaption = makeLabel(scr, GAUGE_ODO_EXTRA1, "ODO", accent);
  dw.odo = makeLabel(scr, GAUGE_ODO, t.odo, whiteLv());
  dw.odoUnit = makeLabel(scr, GAUGE_ODO_EXTRA2, distanceUnitLabel(), labelLv());
  lv_obj_t *avgCaption = makeLabel(scr, GAUGE_AVG_EXTRA1, metricAvgLabel(), accent);
  dw.avg = makeLabel(scr, GAUGE_AVG, t.avg, whiteLv());
  dw.avgUnit = makeLabel(scr, GAUGE_AVG_EXTRA2, speedUnitLabel(), labelLv());
  lv_obj_t *timeCaption = makeLabel(scr, GAUGE_TIME_EXTRA1, metricUptimeLabel(), accent);
  dw.uptime2 = makeLabel(scr, GAUGE_TIME, t.uptime, whiteLv());
  registerDataSlot(4, tripCaption, dw.trip, dw.tripUnit);
  registerDataSlot(5, odoCaption, dw.odo, dw.odoUnit);
  registerDataSlot(6, avgCaption, dw.avg, dw.avgUnit);
  registerDataSlot(7, timeCaption, dw.uptime2);

  placeUnitAfterValue(dw.trip, dw.tripUnit, GAUGE_TRIP_EXTRA2, 3);
  placeUnitAfterValue(dw.odo, dw.odoUnit, GAUGE_ODO_EXTRA2, 3);
  placeUnitAfterValue(dw.avg, dw.avgUnit, GAUGE_AVG_EXTRA2, 3);
  glideAdd(gaugeSpeedPlace, nullptr);
  glideAdd(gaugePowerPlace, nullptr);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()), false);
  glideAim(glides[1], glidePosition(v.watts, powerBarMax()), false);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  sweepSpeedWith(dw.speed, gaugeSpeedSweep, v);
  sweepPowerWith(dw.power, gaugePowerSweep, v);
}

static void updateGauge(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  setScaledValueText(dw.speed, GAUGE_SPEED, t.speed, layoutFontToLv(GAUGE_SPEED.font));
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()));
  glideAim(glides[1], glidePosition(v.watts, powerBarMax()));
  setScaledValueText(dw.power, GAUGE_POWER, t.power, layoutFontToLv(GAUGE_POWER.font));
  setLabelText(dw.powerUnit, powerUnitLabel(v.watts));
  if (elecDue) {
    setLabelText(dw.volts, t.voltage);
    setLabelText(dw.voltsUnit, t.voltageUnit);
    setLabelText(dw.amps, t.current);
    setLabelText(dw.ampsUnit, t.currentUnit);
    placeUnitAfterValue(dw.volts, dw.voltsUnit, GAUGE_VOLTS_EXTRA2, 3);
    placeUnitAfterValue(dw.amps, dw.ampsUnit, GAUGE_AMPS_EXTRA2, 3);
  }
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
    setLabelText(dw.uptime2, t.uptime);
    char temperature[8];
    snprintf(temperature, sizeof(temperature), "%d", v.motorTemp);
    setLabelText(dw.motor, temperature);
    snprintf(temperature, sizeof(temperature), "%d", v.escTemp);
    setLabelText(dw.esc, temperature);
    placeUnitAfterValue(dw.motor, dw.motorUnit, GAUGE_MOTOR_TEMP_EXTRA2, 3);
    placeUnitAfterValue(dw.esc, dw.escUnit, GAUGE_ESC_TEMP_EXTRA2, 3);
  }
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    setLabelText(dw.tripUnit, distanceUnitLabel());
    setLabelText(dw.odoUnit, distanceUnitLabel());
    setLabelText(dw.avgUnit, speedUnitLabel());
    setLabelText(dw.trip, t.trip);
    setLabelText(dw.odo, t.odo);
    setLabelText(dw.avg, t.avg);
    placeUnitAfterValue(dw.trip, dw.tripUnit, GAUGE_TRIP_EXTRA2, 3);
    placeUnitAfterValue(dw.odo, dw.odoUnit, GAUGE_ODO_EXTRA2, 3);
    placeUnitAfterValue(dw.avg, dw.avgUnit, GAUGE_AVG_EXTRA2, 3);
  }
}

// ── Simple ────────────────────────────────────────────────────────────────────

static void simpleSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setScaledValueText(dw.speed, SIMPLE_SPEED, text, layoutFontToLv(SIMPLE_SPEED.font));
}

static void simplePowerSweep(int value) {
  char text[24];
  formatPowerWithUnit(text, sizeof(text), value);
  setLabelText(dw.power, text);
  setSegMeterValue(dw.powerMeter, value, powerBarMax());
}

static void buildSimple(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t accent = accentLv();
  DashTexts t = fmtTexts(v);
  const BatteryStats stats = dashBatteryStats();
  char text[24];

  makeLayoutIcon(scr, SIMPLE_CLOCK_ICON, CYD_ICON_UPTIME, accent);
  dw.uptime = makeTopTimeLabel(scr, SIMPLE_TIME, t.uptime, whiteLv());
  lv_obj_t *oem = makeLabelFont(scr, SIMPLE_OEM, "", accent, layoutFontToLv(SIMPLE_OEM.font));
  setFittedText(oem, SIMPLE_OEM, dashOemName(), layoutFontToLv(SIMPLE_OEM.font));
  dw.battPct = makeTopBatteryLabel(scr, SIMPLE_BATTERY, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, SIMPLE_BATTERY_ICON, 5);

  // speed owns the left half and takes the biggest font its digits allow
  dw.speed = makeLabel(scr, SIMPLE_SPEED, t.speed, whiteLv());
  setScaledValueText(dw.speed, SIMPLE_SPEED, t.speed, layoutFontToLv(SIMPLE_SPEED.font));
  dw.speedUnit = makeLabel(scr, SIMPLE_SPEED_UNIT, speedUnitLabel(), accent);

  // charge card: the level as a picture, the range as the headline. A rider
  // who does not care about volts still needs to know how far they can get.
  makePanel(scr, SIMPLE_BATTERY_CARD.x, SIMPLE_BATTERY_CARD.y, SIMPLE_BATTERY_CARD.w, SIMPLE_BATTERY_CARD.h,
            SIMPLE_BATTERY_CARD.radius, accent, dash565(0x2104), true);
  lv_obj_t *rangeCaption = makeLabel(scr, SIMPLE_RANGE_LABEL, metricRangeLabel(), labelLv());
  formatAvailableRange(text, sizeof(text), stats.rangeKm, false);
  dw.range = makeLabel(scr, SIMPLE_RANGE, text, whiteLv());
  dw.rangeUnit = makeLabel(scr, SIMPLE_RANGE_EXTRA1, distanceUnitLabel(), whiteLv());

  // effort as a bar: no watt figure to interpret, just how hard it is pulling
  makeLabel(scr, SIMPLE_POWER_LABEL, metricPowerLabel(), labelLv());
  formatPowerWithUnit(text, sizeof(text), v.watts);
  dw.power = makeLabel(scr, SIMPLE_POWER, text, whiteLv());
  // A dense single-colour meter follows the selected theme. Keep the blocks
  // inset so the surrounding frame remains a clear, continuous container.
  lv_obj_t *powerFrame = makePanel(scr, SIMPLE_POWER_BAR.x, SIMPLE_POWER_BAR.y, SIMPLE_POWER_BAR.w,
                                   SIMPLE_POWER_BAR.h, 3, accentDarkLv(), dashBlack(), true);
  lv_obj_set_style_border_opa(powerFrame, LV_OPA_70, 0);
  Item powerSegments = SIMPLE_POWER_BAR;
  powerSegments.x += 3;
  powerSegments.y += 3;
  powerSegments.w -= 6;
  powerSegments.h -= 6;
  dw.powerMeter = makeSegMeter(scr, powerSegments, accent, accent, accent, 40);
  if (dashboardLightModeActive()) setSegMeterColors(dw.powerMeter, lv_color_black(), lv_color_white());

  makeLineBox(scr, SIMPLE_DIVIDER, panelLv());
  lv_obj_t *tripCaption = makeLabel(scr, SIMPLE_TRIP_LABEL, metricTripLabel(), labelLv());
  dw.trip = makeLabel(scr, SIMPLE_TRIP, t.trip, whiteLv());
  dw.tripUnit = makeLabel(scr, SIMPLE_TRIP_EXTRA1, distanceUnitLabel(), whiteLv());
  lv_obj_t *odoCaption = makeLabel(scr, SIMPLE_ODO_LABEL, "ODO", labelLv());
  dw.odo = makeLabel(scr, SIMPLE_ODO, t.odo, whiteLv());
  dw.odoUnit = makeLabel(scr, SIMPLE_ODO_EXTRA1, distanceUnitLabel(), whiteLv());
  registerDataSlot(0, rangeCaption, dw.range, dw.rangeUnit);
  registerDataSlot(1, tripCaption, dw.trip, dw.tripUnit);
  registerDataSlot(2, odoCaption, dw.odo, dw.odoUnit);

  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  setSegMeterValue(dw.powerMeter, displayPowerValue(), powerBarMax());
  centerValueUnitOnBaseline(dw.range, dw.rangeUnit,
                            SIMPLE_BATTERY_CARD.x + SIMPLE_BATTERY_CARD.w / 2, 5);
  placeUnitAfterValue(dw.trip, dw.tripUnit, SIMPLE_TRIP_EXTRA1, 4);
  placeValueUnitAtRight(dw.odo, dw.odoUnit, SIMPLE_ODO, SIMPLE_ODO_EXTRA1, 312, 4);
  sweepSpeedWith(dw.speed, simpleSpeedSweep, v);
  sweepPowerWith(dw.power, simplePowerSweep, v);
}

static void updateSimple(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char text[24];
  setScaledValueText(dw.speed, SIMPLE_SPEED, t.speed, layoutFontToLv(SIMPLE_SPEED.font));
  setSegMeterValue(dw.powerMeter, displayPowerValue(), powerBarMax());
  formatPowerWithUnit(text, sizeof(text), v.watts);
  setLabelText(dw.power, text);
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
  }
  if (slowDue) {
    const BatteryStats stats = dashBatteryStats();
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    formatAvailableRange(text, sizeof(text), stats.rangeKm, false);
    setLabelText(dw.range, text);
    centerValueUnitOnBaseline(dw.range, dw.rangeUnit,
                              SIMPLE_BATTERY_CARD.x + SIMPLE_BATTERY_CARD.w / 2, 5);
    setLabelText(dw.trip, t.trip);
    setLabelText(dw.odo, t.odo);
    placeUnitAfterValue(dw.trip, dw.tripUnit, SIMPLE_TRIP_EXTRA1, 4);
    placeValueUnitAtRight(dw.odo, dw.odoUnit, SIMPLE_ODO, SIMPLE_ODO_EXTRA1, 312, 4);
  }
}

// ── Bar Graph ─────────────────────────────────────────────────────────────────

// 16 blocks across a 110 px row reproduces the Simple theme's meter at roughly
// the proportions it has there: a block about two and a half times the gap
// beside it. Unlit blocks keep makeSegMeter's dark ghost of their own colour,
// so the whole scale stays readable and the lit part reads as a level.
static constexpr int BARS_METER_BLOCKS = 16;
// Each row sits on its own card, drawn first so the icon, title, meter and
// value all land on top of it. The rows are on an even 43 px pitch, so one
// geometry derived from the meter's own frame fits all five.
static constexpr int BARS_ROW_CARD_X = 4;
static constexpr int BARS_ROW_CARD_W = 220;
static constexpr int BARS_ROW_CARD_H = 40;

// Light mode with no accent chosen is a plain high-contrast face: black on
// white, rather than five hues on a pale ground. Dark mode, and any explicitly
// chosen accent, keep the per-quantity colouring.
static bool barsHighContrast() {
  return dashboardLightModeActive() && accentTheme == ACCENT_DEFAULT;
}

static lv_color_t barsColorLv(uint16_t stock565) {
  return barsHighContrast() ? lv_color_black() : themeColor(stock565);
}

// Only the charge row's segments track their own reading: green while the pack
// is healthy, then amber and red as it empties. Its icon, title and percentage
// stay green, so the row keeps its identity while the meter carries the warning.
// Full scale for a data item driving one of the meter rows, or 0 when the item
// has no honest ceiling. A trip distance or an odometer has no "full", and a
// meter that invented one would misrepresent the reading — those rows show the
// caption and value with the meter hidden instead.
static constexpr int BARS_SLOT_COUNT = 5;

static int barsSlotScaleMax(DashboardDataItem item) {
  switch (item) {
    case DATA_DUTY: return 100;
    case DATA_BATTERY: return 100;
    case DATA_SPEED:
    case DATA_AVG_SPEED: return speedGaugeMax();
    case DATA_POWER: return powerBarMax();
    case DATA_VOLTAGE: return packVoltageMax();
    case DATA_CURRENT: return packCurrentMax();
    case DATA_MOTOR_CURRENT: return automaticGaugeRanges ? displayGaugeMaximum(RANGE_MOTOR_CURRENT) : max(1, (int)motorMaxAmps);
    case DATA_MOTOR_TEMP:
    case DATA_ESC_TEMP: return 120;  // beyond this the controller is derating anyway
    case DATA_RIDE_EFFICIENCY:
    case DATA_LIFETIME_EFFICIENCY: return automaticGaugeRanges ? displayGaugeMaximum(RANGE_EFFICIENCY) : 40;  // matches the Efficiency dial's ceiling
    case DATA_LEARNED_CAPACITY: return max(1, (int)batteryCapacityDeciAh / 10);
    default: return 0;
  }
}

static int barsSlotValue(DashboardDataItem item, const DashboardValues &v, const BatteryStats &stats) {
  switch (item) {
    case DATA_DUTY: return (int)lroundf(fabsf(v.dutyCycle) * 100.0F);
    case DATA_BATTERY: return v.batteryPercent;
    case DATA_SPEED: return v.speedKmh;
    case DATA_AVG_SPEED: return (int)lroundf(v.avgSpeedKmh);
    case DATA_POWER: return v.watts;
    case DATA_VOLTAGE: return (int)lroundf(v.voltage);
    case DATA_CURRENT: return (int)lroundf(v.current);
    case DATA_MOTOR_CURRENT: return (int)lroundf(v.motorCurrent);
    case DATA_MOTOR_TEMP: return v.motorTemp;
    case DATA_ESC_TEMP: return v.escTemp;
    case DATA_RIDE_EFFICIENCY: return (int)lroundf(stats.tripWhPerKm);
    case DATA_LIFETIME_EFFICIENCY: return (int)lroundf(stats.lifetimeWhPerKm);
    case DATA_LEARNED_CAPACITY: return (int)lroundf(stats.learnedCapacityAh);
    default: return 0;
  }
}

static void hideBarsMeter(SegMeterWidget &meter) {
  for (int i = 0; i < meter.count; i++)
    if (meter.blocks[i]) lv_obj_add_flag(meter.blocks[i], LV_OBJ_FLAG_HIDDEN);
}

// Every meter reads whatever its slot currently holds. With the default
// assignment this is exactly the old fixed wiring; with a reassigned slot the
// row follows the new item, or hides its meter if that item has no scale.
static void updateBarsMeters(const DashboardValues &v) {
  const BatteryStats stats = dashBatteryStats();
  for (int i = 0; i < BARS_SLOT_COUNT; i++) {
    if (!dw.barMeters[i].count) continue;
    const DashboardDataItem item = dashboardDataSelection(MODE_BARS, i);
    const int scale = barsSlotScaleMax(item);
    if (scale <= 0) continue;
    int reading = barsSlotValue(item, v, stats);
    if (item == DATA_SPEED) reading = displaySpeedValue();
    else if (item == DATA_POWER) reading = displayPowerValue();
    else if (item == DATA_CURRENT) reading = static_cast<int>(lroundf(visualCurrent.shown));
    else if (item == DATA_MOTOR_CURRENT) reading = static_cast<int>(lroundf(visualMotorCurrent.shown));
    setSegMeterValue(dw.barMeters[i], reading, scale);
  }
}

static uint32_t barsBatteryColorKey = 0;

static lv_color_t barsBatteryLitLv(int percent) {
  if (!barsHighContrast()) return batteryLevelColorLv(percent, barsColorLv(0x07E0));
  // The shared warning colours are invisible on white, so the high-contrast
  // face darkens them rather than dropping the warning altogether.
  if (percent <= 15) return lv_color_hex(0xB00000);
  if (percent <= 35) return lv_color_hex(0x8A5A00);
  return lv_color_black();
}

static void setBarsBatteryColor(int percent) {
  if (!dw.barMeters[0].count) return;
  const lv_color_t lit = barsBatteryLitLv(percent);
  const uint32_t key = lv_color_to32(lit);
  if (key == barsBatteryColorKey) return;
  barsBatteryColorKey = key;
  const lv_color_t ground = dashboardLightModeActive() ? lv_color_white() : lv_color_black();
  setSegMeterColors(dw.barMeters[0], lit, lv_color_mix(lit, ground, 38));
}

static SegMeterWidget makeBarsRow(lv_obj_t *scr, const Item &frame, const Item &labelItem,
                                  const Item &valueItem, const char *label, lv_color_t color,
                                  lv_obj_t **valueOut, const Item *iconItem = NULL,
                                  CydIconId icon = CYD_ICON_SETTINGS,
                                  lv_obj_t **captionOut = NULL, lv_obj_t **iconOut = NULL) {
  // A tint of the row's own colour on the border ties the card to its data
  // without competing with the meter it contains.
  makePanel(scr, BARS_ROW_CARD_X, frame.y - 20, BARS_ROW_CARD_W, BARS_ROW_CARD_H, 4,
            lv_color_mix(color, dashBlack(), 70), panelLv(), true);
  lv_obj_t *iconObj = iconItem ? makeLayoutIcon(scr, *iconItem, icon, color) : NULL;
  lv_obj_t *caption = makeLabelAt(scr, labelItem.x, frame.y - 15, label, color,
                                  layoutFontToLv(labelItem.font));
  if (captionOut) *captionOut = caption;
  if (iconOut) *iconOut = iconObj;
  // One colour for all three stops: these rows name a quantity rather than a
  // danger level, so the scale should not change hue as it fills.
  SegMeterWidget meter = makeSegMeter(scr, frame, color, color, color, BARS_METER_BLOCKS);
  // makeSegMeter ghosts its unlit blocks toward black, which on the light
  // face turns the empty half of every row into a solid dark block. Ghost
  // toward the card instead, so unlit reads as "not yet" in both appearances.
  if (barsHighContrast())
    setSegMeterColors(meter, lv_color_black(), lv_color_hex(0xD2D2D2));
  else if (dashboardLightModeActive())
    setSegMeterColors(meter, color, lv_color_mix(color, lv_color_white(), 38));
  *valueOut = makeLabel(scr, valueItem, "", whiteLv());
  return meter;
}

static void barsTexts(const DashboardValues &v, char out[5][24]) {
  DashTexts t = fmtTexts(v);
  snprintf(out[0], 24, "%d", v.batteryPercent);
  snprintf(out[1], 24, "%s", t.speed);
  formatPowerValue(out[2], 24, v.watts);
  snprintf(out[3], 24, "%s", t.voltage);
  snprintf(out[4], 24, "%s", t.current);
}

static void setBarValueWithUnit(int idx, lv_obj_t *value, const char *number, const char *unit) {
  setLabelText(value, number);
  setLabelText(dw.barUnits[idx], unit);
}

static void placeBarUnit(lv_obj_t *value, lv_obj_t *unit, const Item &unitItem, int gap = 4) {
  placeUnitAfterValue(value, unit, unitItem, gap);
}

static void updateBarUnitPositions() {
  placeBarUnit(dw.battPct, dw.barUnits[0], BARS_BATTERY_EXTRA1, 3);
  placeBarUnit(dw.speed, dw.barUnits[1], BARS_SPEED_EXTRA1, 4);
  placeBarUnit(dw.power, dw.barUnits[2], BARS_POWER_EXTRA1, 4);
  placeBarUnit(dw.volts, dw.barUnits[3], BARS_VOLTS_EXTRA1, 4);
  placeBarUnit(dw.amps, dw.barUnits[4], BARS_AMPS_EXTRA1, 4);
}

static void buildBars(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t cyan = barsColorLv(0x05FF);
  DashTexts t = fmtTexts(v);
  char rows[5][24];
  barsTexts(v, rows);
  char text[24];

  // One hue per quantity: charge green, motion cyan, effort red, potential
  // yellow and current orange. An explicitly chosen accent still unifies all
  // five, since every colour here goes through barsColorLv().
  lv_obj_t *rowCaptions[BARS_SLOT_COUNT] = {};
  lv_obj_t *rowIcons[BARS_SLOT_COUNT] = {};
  const lv_color_t rowColors[BARS_SLOT_COUNT] = {barsColorLv(0x07E0), cyan, barsColorLv(0xF8A2),
                                                 barsColorLv(0xF664), barsColorLv(0xFB40)};
  dw.barMeters[0] = makeBarsRow(scr, BARS_BATTERY_BAR_FRAME, BARS_BATTERY_LABEL, BARS_BATTERY,
                                metricBatteryLabel(), rowColors[0], &dw.battPct,
                                &BARS_BATTERY_ICON, CYD_ICON_BATTERY,
                                &rowCaptions[0], &rowIcons[0]);
  dw.barMeters[1] = makeBarsRow(scr, BARS_SPEED_BAR_FRAME, BARS_SPEED_LABEL, BARS_SPEED,
                                metricSpeedLabel(), rowColors[1], &dw.speed, &BARS_SPEED_ICON, CYD_ICON_SPEED,
                                &rowCaptions[1], &rowIcons[1]);
  dw.barMeters[2] = makeBarsRow(scr, BARS_POWER_BAR_FRAME, BARS_POWER_LABEL, BARS_POWER,
                                metricPowerLabel(), rowColors[2], &dw.power, &BARS_POWER_ICON,
                                CYD_ICON_POWER,
                                &rowCaptions[2], &rowIcons[2]);
  dw.barMeters[3] = makeBarsRow(scr, BARS_VOLTAGE_BAR_FRAME, BARS_VOLTS_LABEL, BARS_VOLTS,
                                metricVoltsLabel(), rowColors[3], &dw.volts, &BARS_VOLTAGE_ICON,
                                CYD_ICON_VOLTAGE,
                                &rowCaptions[3], &rowIcons[3]);
  dw.barMeters[4] = makeBarsRow(scr, BARS_CURRENT_BAR_FRAME, BARS_AMPS_LABEL, BARS_AMPS,
                                metricBatteryAmpsLabel(), rowColors[4], &dw.amps, &BARS_CURRENT_ICON,
                                CYD_ICON_AMPERAGE,
                                &rowCaptions[4], &rowIcons[4]);
  lv_obj_t *rowValues[BARS_SLOT_COUNT] = {dw.battPct, dw.speed, dw.power, dw.volts, dw.amps};
  dw.barUnits[0] = makeLabel(scr, BARS_BATTERY_EXTRA1, "%", whiteLv());
  dw.barUnits[1] = makeLabel(scr, BARS_SPEED_EXTRA1, speedUnitLabel(), whiteLv());
  dw.barUnits[2] = makeLabel(scr, BARS_POWER_EXTRA1, powerUnitLabel(v.watts), whiteLv());
  dw.barUnits[3] = makeLabel(scr, BARS_VOLTS_EXTRA1, t.voltageUnit, whiteLv());
  dw.barUnits[4] = makeLabel(scr, BARS_AMPS_EXTRA1, t.currentUnit, whiteLv());
  setBarValueWithUnit(0, dw.battPct, rows[0], "%");
  setBarValueWithUnit(1, dw.speed, rows[1], speedUnitLabel());
  setBarValueWithUnit(2, dw.power, rows[2], powerUnitLabel(v.watts));
  setBarValueWithUnit(3, dw.volts, rows[3], t.voltageUnit);
  setBarValueWithUnit(4, dw.amps, rows[4], t.currentUnit);
  updateBarUnitPositions();
  // Slots 0..4 are the meter rows: caption, value, unit and icon, so a
  // reassigned slot renames, re-icons and re-scales the whole row.
  for (int i = 0; i < BARS_SLOT_COUNT; i++)
    registerDataSlot(i, rowCaptions[i], rowValues[i], dw.barUnits[i], rowIcons[i], &rowColors[i]);

  // The right-hand readouts sit in their own card, matching the five row
  // cards, rather than being fenced off by a rule. Same top and bottom as the
  // rows beside it, so the two columns read as one grid.
  makePanel(scr, 230, 12, 86, 212, 4, lv_color_mix(cyan, dashBlack(), 70), panelLv(), true);
  lv_obj_t *motorCaption = makeLabel(scr, BARS_MOTOR_TEMP_LABEL, metricMotorLabel(), cyan);
  dw.motor = makeLabel(scr, BARS_MOTOR_TEMP, t.motor, whiteLv());
  lv_obj_t *escCaption = makeLabel(scr, BARS_ESC_TEMP_LABEL, "ESC", cyan);
  dw.esc = makeLabel(scr, BARS_ESC_TEMP, t.esc, whiteLv());
  lv_obj_t *tripCaption = makeLabel(scr, BARS_TRIP_LABEL, metricTripLabel(), cyan);
  snprintf(text, sizeof(text), "%s %s", t.trip, distanceUnitLabel());
  dw.trip = makeLabel(scr, BARS_TRIP, text, whiteLv());
  lv_obj_t *odoCaption = makeLabel(scr, BARS_ODO_LABEL, "ODO", cyan);
  snprintf(text, sizeof(text), "%s %s", t.odo, distanceUnitLabel());
  dw.odo = makeLabel(scr, BARS_ODO, text, whiteLv());
  lv_obj_t *avgCaption = makeLabel(scr, BARS_AVG_LABEL, metricAvgLabel(), cyan);
  snprintf(text, sizeof(text), "%s %s", t.avg, speedUnitLabel());
  dw.avg = makeLabel(scr, BARS_AVG, text, whiteLv());
  lv_obj_t *upCaption = makeLabel(scr, BARS_UP_LABEL, txt("UP", "AIKA", "ZEIT", "DURÉE", "TIEMPO", "TEMPO"), cyan);
  dw.uptime = makeLabel(scr, BARS_UP, t.uptimeShort, whiteLv());
  registerDataSlot(5, motorCaption, dw.motor);
  registerDataSlot(6, escCaption, dw.esc);
  registerDataSlot(7, tripCaption, dw.trip);
  registerDataSlot(8, odoCaption, dw.odo);
  registerDataSlot(9, avgCaption, dw.avg);
  registerDataSlot(10, upCaption, dw.uptime);

  // The five meter rows are configurable slots now, so each is seeded and
  // swept from whatever it holds rather than from a fixed source.
  barsBatteryColorKey = 0;  // the meters were just rebuilt
  const BatteryStats barStats = dashBatteryStats();
  for (int i = 0; i < BARS_SLOT_COUNT; i++) {
    const DashboardDataItem item = dashboardDataSelection(MODE_BARS, i);
    const int scale = barsSlotScaleMax(item);
    if (scale <= 0) {
      hideBarsMeter(dw.barMeters[i]);  // nothing this item could honestly fill
      continue;
    }
    const int reading = barsSlotValue(item, v, barStats);
    if (item == DATA_BATTERY) setBarsBatteryColor(reading);
    setSegMeterValue(dw.barMeters[i], reading, scale);
    sweepMeter(dw.barMeters[i], reading, scale, (uint16_t)(i * 60));  // staggered, top row first
  }
}

static void updateBars(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char rows[5][24];
  barsTexts(v, rows);
  char text[24];

  updateBarsMeters(v);
  // fast: the speed and power rows
  setBarValueWithUnit(1, dw.speed, rows[1], speedUnitLabel());
  placeBarUnit(dw.speed, dw.barUnits[1], BARS_SPEED_EXTRA1, 4);
  setBarValueWithUnit(2, dw.power, rows[2], powerUnitLabel(v.watts));
  placeBarUnit(dw.power, dw.barUnits[2], BARS_POWER_EXTRA1, 4);

  if (elecDue) {
    setBarValueWithUnit(3, dw.volts, rows[3], t.voltageUnit);
    setBarValueWithUnit(4, dw.amps, rows[4], t.currentUnit);
    placeBarUnit(dw.volts, dw.barUnits[3], BARS_VOLTS_EXTRA1, 4);
    placeBarUnit(dw.amps, dw.barUnits[4], BARS_AMPS_EXTRA1, 4);
  }
  if (midDue) {
    setLabelText(dw.motor, t.motor);
    setLabelText(dw.esc, t.esc);
    setLabelText(dw.uptime, t.uptimeShort);
  }
  if (slowDue) {
    setBarValueWithUnit(0, dw.battPct, rows[0], "%");
    placeBarUnit(dw.battPct, dw.barUnits[0], BARS_BATTERY_EXTRA1, 3);
    if (dashboardDataSelection(MODE_BARS, 0) == DATA_BATTERY) setBarsBatteryColor(v.batteryPercent);
    snprintf(text, sizeof(text), "%s %s", t.trip, distanceUnitLabel());
    setLabelText(dw.trip, text);
    snprintf(text, sizeof(text), "%s %s", t.odo, distanceUnitLabel());
    setLabelText(dw.odo, text);
    snprintf(text, sizeof(text), "%s %s", t.avg, speedUnitLabel());
    setLabelText(dw.avg, text);
  }
}

// ── Motor Data ─────────────────────────────────────────────────────────────
// Six fixed instruments: a segmented ring (the bar meters' blocks bent round the
// dial) with the shared outer needle. Only footer slots may be reassigned, so a
// gauge never changes its meaning.
//
// Rings and needles glide (see Glide in ui_common.h): each new reading carries them, in a
// straight line, from wherever they are to the new position, so a steady acceleration is one
// continuous motion instead of a step a reading. The numbers show the live reading at once.
// The first reading, and every step of the startup sweep, is placed without gliding.
constexpr int kMotorDataUnlitMix = 52;    // of 255 toward the full-brightness color, over the ground
constexpr int kMotorDataDiscMix = 14;     // of 255 toward the dial's color, over the dashboard ground (well below the 38 of an unlit block)
constexpr int kMotorDataMajorExtraPx = 3;  // the longer blocks, reaching inward, as the dual gauge's major ticks
constexpr int kMotorDataMajorSteps = 5;    // at every fifth of the dial
constexpr int kMotorDataPowerThickness = 7;  // the input power ring's blocks are shorter, leaving its number room
constexpr int kMotorDataSweepDeg = 240;  // the most a ring may span, centred on 12 o'clock
struct MotorDataInstrument {
  SegRingWidget ring;
  TickNeedle needle;
  lv_obj_t *value;
  lv_obj_t *caption;
};
static MotorDataInstrument motorData[6];

static void motorDataPlace(void *context, int position) {
  MotorDataInstrument &instrument = *static_cast<MotorDataInstrument *>(context);
  setSegRingValue(instrument.ring, position, kGlideScale);
  setTickNeedleValue(instrument.needle, position);
}

// Instrument order throughout: phase A, speed, phase V, battery A, input power, duty.
static const Item *const motorDataGauges[] = {&MOTOR_DATA_PHASE_AMPS_GAUGE, &MOTOR_DATA_SPEED_GAUGE,
  &MOTOR_DATA_PHASE_VOLTS_GAUGE, &MOTOR_DATA_BATT_AMPS_GAUGE, &MOTOR_DATA_POWER_GAUGE, &MOTOR_DATA_DUTY_GAUGE};
static const Item *const motorDataValues[] = {&MOTOR_DATA_PHASE_AMPS, &MOTOR_DATA_SPEED,
  &MOTOR_DATA_PHASE_VOLTS, &MOTOR_DATA_BATT_AMPS, &MOTOR_DATA_POWER, &MOTOR_DATA_DUTY};
static const TelemetryField motorDataFields[] = {TELEMETRY_FIELD_MOTOR_CURRENT, TELEMETRY_FIELD_SPEED,
  TELEMETRY_FIELD_PHASE_VOLTAGE, TELEMETRY_FIELD_CURRENT, TELEMETRY_FIELD_POWER, TELEMETRY_FIELD_DUTY};

static int motorDataMaximum(int i) {
  switch (i) {
    case 0: return ((automaticGaugeRanges || demoPreviewIsFrozen()) ? displayGaugeMaximum(RANGE_MOTOR_CURRENT) : max(1, (int)motorMaxAmps)) * 10;
    case 1: return speedGaugeMax();
    // The phase voltage ceiling tracks the configured pack, not the current sample.
    case 2: return max(1, (int)lroundf(packVoltageMax() * 10.0F / sqrtf(3.0F)));
    case 3: return packCurrentMax() * 10;
    case 4: return powerBarMax();
    default: return 100;  // duty, in percent
  }
}
static int motorDataReading(int i, const DashboardValues &v) {
  switch (i) {
    case 0: return (int)lroundf(v.motorCurrent * 10);
    case 1: return v.speedKmh;
    case 2: return (int)lroundf(v.phaseVoltage * 10);
    case 3: return (int)lroundf(v.current * 10);
    case 4: return v.watts;
    default: return (int)lroundf(fabsf(v.dutyCycle) * 100.0F);  // percent; the direction is dropped
  }
}
static void setMotorDataReading(int i, int value, bool available = true, bool glide = true) {
  MotorDataInstrument &instrument = motorData[i];
  const float fraction = (float)abs(value) / max(1, motorDataMaximum(i));
  glideAim(glides[i], available ? (int)lroundf(constrain(fraction, 0.0F, 1.0F) * kGlideScale) : 0,
           glide && available);
  setObjHidden(instrument.needle.body, !available);
  setObjHidden(instrument.needle.highlight, !available);
  char text[24];
  if (!available) snprintf(text, sizeof(text), "--");
  else if (i == 1) formatSpeedValue(text, sizeof(text), value);
  else if (i == 4) formatPowerValue(text, sizeof(text), value);
  else if (i == 5) snprintf(text, sizeof(text), "%d", abs(value));
  else snprintf(text, sizeof(text), "%.1f", value / 10.0F);
  setScaledValueText(instrument.value, *motorDataValues[i], text, layoutFontToLv(motorDataValues[i]->font));
  if (i == 4) {
    snprintf(text, sizeof(text), "%s", powerUnitLabel(value));
    setFittedText(instrument.caption, MOTOR_DATA_POWER_LABEL, text, F1);
  }
}
template<int I> static void motorDataSweep(int value) { setMotorDataReading(I, value, true, false); }
static const SweepFn motorDataSweeps[] = {motorDataSweep<0>, motorDataSweep<1>, motorDataSweep<2>,
  motorDataSweep<3>, motorDataSweep<4>, motorDataSweep<5>};

static void updateMotorData(const DashboardValues &v) {
  if (!glideScreen) return;
  const DashTexts t = fmtTexts(v);
  for (int i = 0; i < 6; ++i) setMotorDataReading(i, motorDataReading(i, v), dashHas(motorDataFields[i]));
  if (midDue) setLabelText(dw.uptime, t.uptime);
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  }
  for (int i = 0; i < 4; ++i) {
    char text[32];
    formatDashboardData(text, sizeof(text), dashboardDataDefault(MODE_MOTOR_DATA, i), v);
    if (strcmp(text, "-") == 0) snprintf(text, sizeof(text), "--");
    setLabelText(dw.dataValues[i], text);
  }
}

static void buildMotorData(lv_obj_t *scr, const DashboardValues &v) {
  const DashTexts t = fmtTexts(v);
  // The top bar is the Dual Gauge and Cyber HUD one, with a plain white clock icon.
  makeLayoutIcon(scr, MOTOR_DATA_CLOCK_ICON, CYD_ICON_UPTIME, whiteLv());
  dw.uptime = makeTopTimeLabel(scr, MOTOR_DATA_TIME, t.uptime, whiteLv());
  // The vehicle name is plain white with the default accent, the chosen accent otherwise.
  lv_obj_t *title = makeLabelFont(scr, MOTOR_DATA_OEM, "", accentTheme == ACCENT_DEFAULT ? whiteLv() : accentLv(),
                                  layoutFontToLv(MOTOR_DATA_OEM.font));
  setFittedText(title, MOTOR_DATA_OEM, dashOemName(), layoutFontToLv(MOTOR_DATA_OEM.font));
  dw.battPct = makeTopBatteryLabel(scr, MOTOR_DATA_BATTERY, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, MOTOR_DATA_BATTERY_ICON, 5);
  const Item *const captions[] = {&MOTOR_DATA_PHASE_AMPS_LABEL, &MOTOR_DATA_SPEED_LABEL,
    &MOTOR_DATA_PHASE_VOLTS_LABEL, &MOTOR_DATA_BATT_AMPS_LABEL, &MOTOR_DATA_POWER_LABEL, &MOTOR_DATA_DUTY_LABEL};
  const char *const names[] = {
    txt("PHASE A", "VAIHE A", "PHASE A", "PHASE A", "FASE A", "FASE A"), speedUnitLabel(),
    txt("PHASE V", "VAIHE V", "PHASE V", "PHASE V", "FASE V", "FASE V"),
    txt("BATT A", "AKKU A", "AKKU A", "BATT A", "BAT A", "BATT A"), "",
    txt("DUTY %", "PWM %", "PWM %", "PWM %", "PWM %", "PWM %")};
  // Phase A, speed, phase V, battery A, input power, duty.
  const uint16_t stock[] = {cyd_ui::kMotorDataPhase565, cyd_ui::kMotorDataSpeed565, cyd_ui::kMotorDataVoltage565,
    cyd_ui::kMotorDataCurrent565, cyd_ui::kMotorDataPower565, cyd_ui::kMotorDataDuty565};
  const uint16_t lightStock[] = {cyd_ui::kMotorDataPhaseLight565, cyd_ui::kMotorDataSpeedLight565,
    cyd_ui::kMotorDataVoltageLight565, cyd_ui::kMotorDataCurrentLight565, cyd_ui::kMotorDataPowerLight565,
    cyd_ui::kMotorDataDutyLight565};
  for (int i = 0; i < 6; ++i) {
    MotorDataInstrument &instrument = motorData[i];
    const Item &g = *motorDataGauges[i];
    const lv_color_t color = themeColor(dashboardLightModeActive() ? lightStock[i] : stock[i]);
    // Keep inactive scales readable on the pale light-mode gauge faces.
    makeSegRing(instrument.ring, scr, g.cx, g.cy, g.r, i == 4 ? kMotorDataPowerThickness : segRingThickness(g.r), kMotorDataSweepDeg, color,
                lv_color_mix(color, dashBlack(), dashboardLightModeActive()
                    ? cyd_ui::kMotorDataLightUnlitMix : kMotorDataUnlitMix));
    setSegRingFill(instrument.ring, lv_color_mix(color, dashBlack(), kMotorDataDiscMix),
                    MOTOR_DATA_FOOTER_TOP.y);  // the footer bar stays clear of the discs
    setSegRingMajors(instrument.ring, kMotorDataMajorExtraPx, kMotorDataMajorSteps);
    instrument.needle = makeTickNeedle(scr, g.cx, g.cy, g.r - (i == 1 ? 16 : 12), g.r,
        instrument.ring.startDeg, instrument.ring.extentDeg, kGlideScale, 4, 2);
    glideAdd(motorDataPlace, &instrument);
    lv_obj_set_style_line_color(instrument.needle.body, color, 0);
    const lv_color_t numberColor = lv_color_mix(whiteLv(), color, 225);
    // Readouts are white in dark appearance and black on the pale light faces.
    instrument.value = makeLabel(scr, *motorDataValues[i], "", whiteLv());
    instrument.caption = makeLabel(scr, *captions[i], "", numberColor);
    setFittedText(instrument.caption, *captions[i], names[i], F1);
  }
  makeLineBox(scr, MOTOR_DATA_FOOTER_TOP, dimLv());
  const Item *const footerLabels[] = {&MOTOR_DATA_MOTOR_LABEL, &MOTOR_DATA_ESC_LABEL, &MOTOR_DATA_BATT_VOLTS_LABEL,
    &MOTOR_DATA_TRIP_LABEL};
  const Item *const footerValues[] = {&MOTOR_DATA_MOTOR, &MOTOR_DATA_ESC, &MOTOR_DATA_BATT_VOLTS, &MOTOR_DATA_TRIP};
  for (int i = 0; i < 4; ++i) {
    const DashboardDataItem item = dashboardDataDefault(MODE_MOTOR_DATA, i);
    lv_obj_t *caption = makeLabel(scr, *footerLabels[i], "", labelLv());
    setFittedText(caption, *footerLabels[i], dashboardDataLabel(item), F1);
    lv_obj_t *value = makeLabel(scr, *footerValues[i], "", whiteLv());
    configureFittedLabel(value, *footerValues[i], "-999.9 °C", F2);
    registerDataSlot(i, caption, value);
  }
  updateMotorData(v);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  for (int i = 0; i < 6; ++i) if (dashHas(motorDataFields[i]))
    sweepWith(motorData[i].value, motorDataSweeps[i], abs(motorDataReading(i, v)), motorDataMaximum(i));
}

// ── Pixel ────────────────────────────────────────────────────────────────────
// A deliberately spare instrument face: one oversized speed readout owns the
// left field, a stacked rail carries charge / ride mode / trip down the right,
// and two ride figures sit along the bottom. Cells are separated by dotted
// rules instead of boxes, and every glyph comes from the 5x7 bitmap face, so
// the whole theme stays hard-edged whatever size it is drawn at.
// Every coordinate, size and font scale below is even. The theme draws nothing
// but solid axis-aligned rectangles and integer-magnified 1 bpp glyphs, none of
// it antialiased, so snapping all of that to a 2 px grid renders exactly what a
// 160x120 framebuffer upscaled 2x would — without the framebuffer, the blit, or
// the loss of LVGL's partial redraw. Centred labels keep their box widths a
// multiple of 4 so LVGL's own centring lands on the grid too.
static constexpr int PIXEL_RAIL_RULE_X = 224;  // rule splitting the rail off
static constexpr int PIXEL_RAIL_X = 232;       // rail text column
static constexpr int PIXEL_RAIL_W = 84;
static constexpr int PIXEL_RAIL_RULE_Y = 78;   // under the charge cell
static constexpr int PIXEL_ROW_RULE_Y = 166;   // above the lower instrument row
static constexpr int PIXEL_RIDE_RULE_X = 106;  // between them
static constexpr int PIXEL_RIDE_LEFT_X = 8;
static constexpr int PIXEL_RIDE_RIGHT_X = 114;
static constexpr int PIXEL_RIDE_W = 96;
// The middle cell is bounded by two rules rather than by a rule and the screen
// edge, so it can run wider: enough for a four-digit wattage plus its unit to
// stay at the value tier instead of dropping to the caption tier mid-throttle.
static constexpr int PIXEL_MID_W = 108;
// The rail's mode cell shares these rows with the two lower instruments, so
// all three captions and all three values line up across the bottom.
static constexpr int PIXEL_CAPTION_Y = 178;
static constexpr int PIXEL_VALUE_Y = 200;
static constexpr int PIXEL_BATTERY_BLOCKS = 4;
static constexpr int PIXEL_SPEED_Y = 16;       // top of the 98 px speed glyphs
static constexpr int PIXEL_SPEED_W = 224;      // multiple of 4: see above
static constexpr int PIXEL_SPEED_CX = 112;     // centre of the left field

static lv_color_t pixelRuleColor;
static lv_obj_t *pixelBatteryBlocks[PIXEL_BATTERY_BLOCKS];
static int pixelBatteryLastLit;
static uint32_t pixelBatteryLastColor;

// Light mode with no accent chosen is not the dark face inverted: it is a
// high-contrast instrument face, pure black on pure white, which is what stays
// readable with the sun on the panel. Choosing an accent opts back into the
// coloured presentation in both appearances.
static bool pixelHighContrast() {
  return dashboardLightModeActive() && accentTheme == ACCENT_DEFAULT;
}

// Warm off-white: the numerals read as a lit filament rather than as UI white.
// Both of these stay low-saturation neutrals so light mode still inverts them.
static lv_color_t pixelInkLv() {
  return pixelHighContrast() ? lv_color_black() : dashboardColorLv(lv_color_hex(0xE3DCC6));
}
static lv_color_t pixelCanvasLv() {
  return pixelHighContrast() ? lv_color_white() : dashboardColorLv(lv_color_hex(0x0E0E0C));
}
static lv_color_t pixelAccentLv() {
  return pixelHighContrast() ? lv_color_black() : accentLv();
}

// The shared level colours are tuned for a dark face, and amber on white is
// close to invisible — so the high-contrast face darkens the two warning steps
// rather than dropping them. A flat pack still has to shout.
static lv_color_t pixelBatteryColor(int percent) {
  if (!pixelHighContrast()) return batteryLevelColorLv(percent, pixelAccentLv());
  if (percent <= 15) return lv_color_hex(0xB00000);
  if (percent <= 35) return lv_color_hex(0x8A5A00);
  return lv_color_black();
}

static lv_obj_t *makePixelLabelBox(lv_obj_t *parent, int x, int y, int w, int h, const char *value,
                                   lv_color_t color, const lv_font_t *font,
                                   lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *label = lv_label_create(parent);
  makePassive(label);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, w, h);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_label_set_text(label, value);
  return label;
}

static lv_obj_t *makePixelBlock(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color) {
  lv_obj_t *block = lv_obj_create(parent);
  lv_obj_remove_style_all(block);
  makePassive(block);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_size(block, w, h);
  lv_obj_set_style_bg_color(block, color, 0);
  lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
  return block;
}

// The bitmap face is fixed-width, so text geometry is arithmetic rather than
// measurement: 5 px of ink on a 6 px grid, every dimension times the scale.
static int pixelFontScale(const lv_font_t *font) { return lv_font_get_line_height(font) / 8; }

static int pixelTextWidth(const char *text, int scale) {
  int width = 0;
  for (const char *p = text; *p; p++) {
    if (((uint8_t)*p & 0xC0) == 0x80) continue;  // UTF-8 continuation byte
    width += (*p == ' ' || *p == '.') ? 3 * scale : 6 * scale;
  }
  return width > 0 ? width - scale : 0;
}

// LVGL centres text on its advance width, and this fixed-width face leaves one
// blank cell of advance after the last glyph — so centred text settles half a
// cell left of true centre, which at the speed tier is a visible 7 px. Offset
// the box by that half cell, rounded down to even so the grid still holds.
static int pixelCentredBoxX(int centreX, int boxW, const lv_font_t *font) {
  return centreX - boxW / 2 + ((pixelFontScale(font) / 2) & ~1);
}

// Only even scales exist here: an odd one magnifies the 5x7 cell into 3x3 or
// 5x5 blocks, which stop landing on the theme's 2 px grid. So the ladder steps
// 28 -> 14 with nothing between, and the 8 px face survives purely as an escape
// hatch for a reassigned slot whose text fits nothing else — the one thing in
// the theme that may sit off-grid, since clipping the value would be worse.
static const lv_font_t *pixelFontForWidth(const char *text, int room) {
  if (pixelTextWidth(text, 4) <= room) return &lv_font_pixel_32;
  if (pixelTextWidth(text, 2) <= room) return &lv_font_pixel_16;
  return &lv_font_pixel_8;
}

// Two digits own the cleared left field at the full 112 px 5x7 tier. A third
// digit would reach the rail, so it steps down to the 80 px tier and re-centres
// vertically. Both are the same square-pixel face as the rest of the theme.
static void applyPixelSpeedFont(lv_obj_t *label, const char *value) {
  const lv_font_t *font = strlen(value) <= 2 ? &lv_font_pixel_112 : &lv_font_pixel_80;
  setObjTextFont(label, font);
  lv_obj_set_pos(label, pixelCentredBoxX(PIXEL_SPEED_CX, PIXEL_SPEED_W, font),
                 PIXEL_SPEED_Y + (112 - lv_font_get_line_height(font)) / 2);
}

// A 4-on / 4-off dash, 2 px thick, in either direction: an 8 px period keeps
// every dash on the grid, where the old 7 px one put alternate dashes half a
// virtual pixel out. Only whole dashes are emitted, so a rule never ends on a
// stub.
static void drawPixelRule(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc, int x, int y, int length,
                          bool horizontal) {
  for (int offset = 0; offset + 4 <= length; offset += 8) {
    const int x1 = horizontal ? x + offset : x;
    const int y1 = horizontal ? y : y + offset;
    lv_area_t dash = {(lv_coord_t)x1, (lv_coord_t)y1,
                      (lv_coord_t)(horizontal ? x1 + 3 : x1 + 1),
                      (lv_coord_t)(horizontal ? y1 + 1 : y1 + 3)};
    lv_draw_rect(ctx, dsc, &dash);
  }
}

static void pixelChromeDrawCb(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.bg_color = pixelRuleColor;
  dot.bg_opa = LV_OPA_COVER;
  dot.radius = 0;
  drawPixelRule(ctx, &dot, PIXEL_RAIL_RULE_X, 4, 232, false);
  drawPixelRule(ctx, &dot, 4, PIXEL_ROW_RULE_Y, 312, true);
  drawPixelRule(ctx, &dot, PIXEL_RAIL_X - 2, PIXEL_RAIL_RULE_Y, 84, true);
  drawPixelRule(ctx, &dot, PIXEL_RIDE_RULE_X, PIXEL_ROW_RULE_Y + 8, 66, false);
}

// Charge as a chunky vertical cell. Only the blocks and the percentage follow
// the level colour; the shell stays the theme colour so a low pack reads as
// red contents in a green case rather than as a red icon.
static void setPixelBatteryLevel(int percent) {
  const int clamped = constrain(percent, 0, 100);
  int lit = clamped * PIXEL_BATTERY_BLOCKS / 100;
  if (lit == 0 && clamped > 0) lit = 1;
  const lv_color_t color = pixelBatteryColor(clamped);
  const uint32_t colorKey = lv_color_to32(color);
  if (lit == pixelBatteryLastLit && colorKey == pixelBatteryLastColor) return;
  pixelBatteryLastLit = lit;
  pixelBatteryLastColor = colorKey;
  for (int i = 0; i < PIXEL_BATTERY_BLOCKS; i++) {
    if (!pixelBatteryBlocks[i]) continue;
    // Block 0 is the left end of the cell, so the fill drains from the right.
    const bool on = i < lit;
    lv_obj_set_style_bg_opa(pixelBatteryBlocks[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (on) lv_obj_set_style_bg_color(pixelBatteryBlocks[i], color, 0);
  }
}

// Short uppercase name of the ride mode the *controller* reports being in.
// Three letters, the way a vehicle dash abbreviates them. With the middle font
// tier gone a longer name would drop the whole readout from 28 px to 14 px, so
// the cell would change weight depending on which mode happened to be set.
//
// This deliberately ignores the Speeds settings. Nothing in this firmware sends
// a mode to a controller, so naming the one picked in a menu would be the dash
// asserting something about the bike that was never true. A controller that
// does not report a mode gets a dash, which is the honest reading.
static const char *pixelRideModeName() {
  return rideModeName(telemetryRideMode());
}

// A value with its unit trailing on the same baseline. The unit has to be
// repositioned whenever the value changes width, which for a fixed-width face
// is a calculation rather than a re-layout.
static void setPixelTrailingValue(lv_obj_t *value, lv_obj_t *unit, int x, const char *text,
                                  const char *unitText, int room) {
  // The unit shares the cell, so the value may only size itself to what is
  // left over once the unit and the gap before it are accounted for.
  const int reserved = (unit && unitText[0]) ? pixelTextWidth(unitText, 2) + 6 : 0;
  const lv_font_t *font = pixelFontForWidth(text, room - reserved);
  const int scale = pixelFontScale(font);
  setObjTextFont(value, font);
  setLabelText(value, text);
  if (!unit) return;
  setLabelText(unit, unitText);
  // Units sit on the value's baseline, whatever tier the value ended up at.
  lv_obj_set_pos(unit, x + pixelTextWidth(text, scale) + 6, PIXEL_VALUE_Y + 7 * scale - 14);
}

// A value with its unit parked on its own line underneath, for the narrow rail.
static void setPixelStackedValue(lv_obj_t *value, lv_obj_t *unit, const char *text,
                                 const char *unitText, int room) {
  setObjTextFont(value, pixelFontForWidth(text, room));
  setLabelText(value, text);
  if (unit) setLabelText(unit, unitText);
}

// The generic number sweep cannot be used here: a third digit has to step the
// readout down a pixel tier or it would run into the rail.
static void pixelGaugeSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  applyPixelSpeedFont(dw.speed, text);
  setLabelText(dw.speed, text);
}

static void buildPixelGauge(lv_obj_t *scr, const DashboardValues &v) {
  const DashTexts t = fmtTexts(v);
  const BatteryStats stats = dashBatteryStats();
  const lv_color_t accent = pixelAccentLv();
  const lv_color_t ink = pixelInkLv();
  char text[32];

  // Near-black rather than black: the cream numerals sit on a warm ground the
  // way an instrument face does. A chosen background colour still wins, since
  // applyDashboardGradient has already painted its panels over the screen.
  lv_obj_set_style_bg_color(scr, pixelCanvasLv(), 0);

  // One custom object carries every rule, so the grid costs four draw calls'
  // worth of dashes instead of hundreds of LVGL objects. Neutral grey keeps
  // the dividers subordinate to both the cream readout and the accent data.
  pixelRuleColor = pixelHighContrast() ? lv_color_hex(0x9A9A9A) : dashboardColorLv(lv_color_hex(0x707070));
  lv_obj_t *chrome = lv_obj_create(scr);
  lv_obj_remove_style_all(chrome);
  makePassive(chrome);
  lv_obj_set_pos(chrome, 0, 0);
  lv_obj_set_size(chrome, 320, 240);
  lv_obj_add_event_cb(chrome, pixelChromeDrawCb, LV_EVENT_DRAW_MAIN, NULL);

  // Speed owns the whole left field, with nothing else competing for it.
  dw.speed = makePixelLabelBox(scr, 0, PIXEL_SPEED_Y, PIXEL_SPEED_W, 112, t.speed, ink,
                               &lv_font_pixel_112, LV_TEXT_ALIGN_CENTER);
  applyPixelSpeedFont(dw.speed, t.speed);
  dw.speedUnit = makePixelLabelBox(scr, pixelCentredBoxX(PIXEL_SPEED_CX, PIXEL_SPEED_W, &lv_font_pixel_32),
                                   126, PIXEL_SPEED_W, 32, speedUnitLabel(), ink, &lv_font_pixel_32,
                                   LV_TEXT_ALIGN_CENTER);

  // Charge cell: the same four blocks as before, but laid along the rail rather
  // than up it. Half the height for the same reading, and the 18 rows that
  // gives back are what let the two cells below stop crowding themselves.
  const lv_color_t battery = pixelBatteryColor(v.batteryPercent);
  makePixelBlock(scr, 296, 16, 6, 10, accent);   // terminal
  makePixelBlock(scr, 242, 8, 54, 2, accent);    // shell
  makePixelBlock(scr, 242, 32, 54, 2, accent);
  makePixelBlock(scr, 242, 8, 2, 26, accent);
  makePixelBlock(scr, 294, 8, 2, 26, accent);
  for (int i = 0; i < PIXEL_BATTERY_BLOCKS; i++) {
    pixelBatteryBlocks[i] = makePixelBlock(scr, 246 + i * 12, 12, 10, 18, battery);
  }
  pixelBatteryLastLit = -1;
  pixelBatteryLastColor = 0;
  setPixelBatteryLevel(v.batteryPercent);
  // Centred on the battery itself rather than on the rail, and 96 wide so that
  // a full "100%" still fits at the value tier.
  dw.battPct = makePixelLabelBox(scr, pixelCentredBoxX(272, 96, &lv_font_pixel_32), 42, 96, 32,
                                 t.battPct, battery, &lv_font_pixel_32, LV_TEXT_ALIGN_CENTER);

  // Trip occupies the middle rail cell, with its unit stacked underneath so a
  // decimal value keeps the full-width pixel tier.
  // 14 + 28 + 14 of glyph now has 86 rows to sit in, so the stack gets an even
  // 8 px above the caption and between every line rather than 4 px and 2 px.
  lv_obj_t *tripCaption = makePixelLabelBox(scr, PIXEL_RAIL_X, 88, PIXEL_RAIL_W, 16,
                                            dashboardDataLabel(DATA_TRIP), ink, &lv_font_pixel_16);
  dw.trip = makePixelLabelBox(scr, PIXEL_RAIL_X, 110, PIXEL_RAIL_W, 32, "", accent,
                              &lv_font_pixel_32);
  dw.tripUnit = makePixelLabelBox(scr, PIXEL_RAIL_X, 146, PIXEL_RAIL_W, 16, "", accent,
                                  &lv_font_pixel_16);
  setPixelStackedValue(dw.trip, dw.tripUnit, t.trip, distanceUnitLabel(), PIXEL_RAIL_W);

  // Ride mode is a fixed setting rather than swappable ride data, and anchors
  // the bottom of the rail like the reference instrument face.
  const char *mode = pixelRideModeName();
  const lv_font_t *modeFont = pixelFontForWidth(mode, PIXEL_RAIL_W);
  makePixelLabelBox(scr, PIXEL_RAIL_X, PIXEL_CAPTION_Y, PIXEL_RAIL_W, 16,
                    txt("MODE", "TILA", "MODUS", "MODE", "MODO", "MODO"), ink, &lv_font_pixel_16);
  makePixelLabelBox(scr, PIXEL_RAIL_X, PIXEL_VALUE_Y + (32 - lv_font_get_line_height(modeFont)) / 2,
                    PIXEL_RAIL_W, 32, mode, accent, modeFont);

  // Range and live power fill the two lower cells. Both remain configurable,
  // but their default presentation keeps the large number and small unit on a
  // shared baseline just like the reference.
  lv_obj_t *rangeCaption = makePixelLabelBox(scr, PIXEL_RIDE_LEFT_X, PIXEL_CAPTION_Y, PIXEL_RIDE_W, 16,
                                             dashboardDataLabel(DATA_RANGE), ink, &lv_font_pixel_16);
  dw.range = makePixelLabelBox(scr, PIXEL_RIDE_LEFT_X, PIXEL_VALUE_Y, PIXEL_RIDE_W, 32, "", accent,
                               &lv_font_pixel_32);
  dw.rangeUnit = makePixelLabelBox(scr, PIXEL_RIDE_LEFT_X, PIXEL_VALUE_Y, 46, 16, "", accent,
                                   &lv_font_pixel_16);
  formatAvailableRange(text, sizeof(text), stats.rangeKm, false);
  setPixelTrailingValue(dw.range, dw.rangeUnit, PIXEL_RIDE_LEFT_X, text, distanceUnitLabel(),
                        PIXEL_RIDE_W);

  lv_obj_t *powerCaption = makePixelLabelBox(scr, PIXEL_RIDE_RIGHT_X, PIXEL_CAPTION_Y, PIXEL_MID_W, 16,
                                             dashboardDataLabel(DATA_POWER), ink, &lv_font_pixel_16);
  dw.power = makePixelLabelBox(scr, PIXEL_RIDE_RIGHT_X, PIXEL_VALUE_Y, PIXEL_MID_W, 32, "", accent,
                               &lv_font_pixel_32);
  dw.powerUnit = makePixelLabelBox(scr, PIXEL_RIDE_RIGHT_X, PIXEL_VALUE_Y, 46, 16, "", accent,
                                   &lv_font_pixel_16);
  setPixelTrailingValue(dw.power, dw.powerUnit, PIXEL_RIDE_RIGHT_X, t.power, powerUnitLabel(v.watts),
                        PIXEL_MID_W);

  registerDataSlot(0, tripCaption, dw.trip, dw.tripUnit);
  registerDataSlot(1, rangeCaption, dw.range, dw.rangeUnit);
  registerDataSlot(2, powerCaption, dw.power, dw.powerUnit);
  sweepSpeedWith(dw.speed, pixelGaugeSpeedSweep, v);
}

static void updatePixelGauge(const DashboardValues &v) {
  const DashTexts t = fmtTexts(v);
  char text[32];
  applyPixelSpeedFont(dw.speed, t.speed);
  setLabelText(dw.speed, t.speed);
  setPixelTrailingValue(dw.power, dw.powerUnit, PIXEL_RIDE_RIGHT_X, t.power, powerUnitLabel(v.watts),
                        PIXEL_MID_W);
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setObjTextColor(dw.battPct, pixelBatteryColor(v.batteryPercent));
    setPixelBatteryLevel(v.batteryPercent);
    setPixelStackedValue(dw.trip, dw.tripUnit, t.trip, distanceUnitLabel(), PIXEL_RAIL_W);
    formatAvailableRange(text, sizeof(text), dashBatteryStats().rangeKm, false);
    setPixelTrailingValue(dw.range, dw.rangeUnit, PIXEL_RIDE_LEFT_X, text, distanceUnitLabel(),
                          PIXEL_RIDE_W);
  }
}

// ── Tiles ─────────────────────────────────────────────────────────────────────

struct LargeTilePalette {
  lv_color_t canvas;
  lv_color_t tile;
  lv_color_t border;
  lv_color_t ink;
  lv_color_t muted;
};

static LargeTilePalette largeTilePalette() {
  // Default deliberately stays neutral: Tiles is the light, highly
  // legible dashboard. Explicit choices turn the tile faces into that colour.
  const bool isDefault = accentTheme == ACCENT_DEFAULT;
  const uint16_t tile565 = isDefault ? 0xFFFF : accentColor565();
  const int r = ((tile565 >> 11) & 0x1F) * 255 / 31;
  const int g = ((tile565 >> 5) & 0x3F) * 255 / 63;
  const int b = (tile565 & 0x1F) * 255 / 31;
  // Rec. 601 luma is sufficient here and deliberately chooses white ink for
  // red/magenta/blue, black for orange/green/cyan/yellow/white.
  const bool useDarkInk = (r * 299 + g * 587 + b * 114) / 1000 >= 145;

  LargeTilePalette palette;
  palette.tile = dashboardColorLv(c565(tile565));
  palette.ink = dashboardColorLv(useDarkInk ? lv_color_black() : lv_color_white());
  palette.muted = lv_color_mix(palette.ink, palette.tile, 190);
  palette.border = lv_color_mix(palette.ink, palette.tile, 90);
  palette.canvas = isDefault ? dash565(0xE71C) : accentDarkLv();
  return palette;
}

static void largeTilesSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setScaledValueText(dw.speed, LARGE_TILES_SPEED, text, layoutFontToLv(LARGE_TILES_SPEED.font));
  bottomLeftValueUnitInTile(dw.speed, dw.speedUnit, LARGE_TILES_SPEED_TILE, 8, 4, 4);
}

static void buildLargeTiles(lv_obj_t *scr, const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  const LargeTilePalette palette = largeTilePalette();
  const lv_color_t tileBg = palette.tile;
  const lv_color_t tileBorder = palette.border;
  const lv_color_t ink = palette.ink;
  const lv_color_t muted = palette.muted;
  char motor[12], odo[12], battery[12];
  snprintf(motor, sizeof(motor), "%d", v.motorTemp);
  snprintf(odo, sizeof(odo), "%s", t.odo);
  snprintf(battery, sizeof(battery), "%d", v.batteryPercent);

  if (!dashboardGradientEnabled && dashboardGradientTheme == ACCENT_DEFAULT)
    lv_obj_set_style_bg_color(scr, palette.canvas, 0);

  const Item *tiles[] = {&LARGE_TILES_SPEED_TILE, &LARGE_TILES_BATTERY_TILE, &LARGE_TILES_VOLTS_TILE,
                         &LARGE_TILES_AMPS_TILE,  &LARGE_TILES_POWER_TILE,   &LARGE_TILES_TRIP_TILE,
                         &LARGE_TILES_TEMP_TILE,  &LARGE_TILES_ODO_TILE};
  for (const Item *tile : tiles) {
    makeFilledRect(scr, *tile, tileBorder, tileBg);
  }

  makeLayoutIcon(scr, LARGE_TILES_SPEED_ICON, CYD_ICON_SPEED, muted);
  makeLabel(scr, LARGE_TILES_SPEED_EXTRA1, metricSpeedLabel(), muted);
  dw.speed = makeLabel(scr, LARGE_TILES_SPEED, t.speed, ink);
  setScaledValueText(dw.speed, LARGE_TILES_SPEED, t.speed, layoutFontToLv(LARGE_TILES_SPEED.font));
  dw.speedUnit = makeLabel(scr, LARGE_TILES_SPEED_EXTRA2, speedUnitLabel(), ink);

  dw.batt = makeBattery(scr, LARGE_TILES_BATTERY_ICON, muted);
  lv_obj_t *batteryCaption = makeLabel(scr, LARGE_TILES_BATTERY_EXTRA1, metricBatteryLabel(), muted);
  dw.battPct = makeLabel(scr, LARGE_TILES_BATTERY, battery, ink);
  dw.battUnit = makeLabel(scr, LARGE_TILES_BATTERY_EXTRA2, "%", ink);

  makeLayoutIcon(scr, LARGE_TILES_VOLTS_ICON, CYD_ICON_VOLTAGE, muted);
  lv_obj_t *voltsCaption = makeLabel(scr, LARGE_TILES_VOLTS_EXTRA1, metricVoltsLabel(), muted);
  dw.volts = makeLabel(scr, LARGE_TILES_VOLTS, t.voltage, ink);
  dw.voltsUnit = makeLabel(scr, LARGE_TILES_VOLTS_EXTRA2, t.voltageUnit, ink);
  makeLayoutIcon(scr, LARGE_TILES_AMPS_ICON, CYD_ICON_AMPERAGE, muted);
  lv_obj_t *ampsCaption = makeLabel(scr, LARGE_TILES_AMPS_EXTRA1, metricAmpsLabel(), muted);
  dw.amps = makeLabel(scr, LARGE_TILES_AMPS, t.current, ink);
  dw.ampsUnit = makeLabel(scr, LARGE_TILES_AMPS_EXTRA2, t.currentUnit, ink);
  makeLayoutIcon(scr, LARGE_TILES_POWER_ICON, CYD_ICON_POWER, muted);
  lv_obj_t *powerCaption = makeLabel(scr, LARGE_TILES_POWER_EXTRA1, metricPowerLabel(), muted);
  dw.power = makeLabel(scr, LARGE_TILES_POWER, t.power, ink);
  dw.powerUnit = makeLabel(scr, LARGE_TILES_POWER_EXTRA2, powerUnitLabel(v.watts), ink);
  makeLayoutIcon(scr, LARGE_TILES_TRIP_ICON, CYD_ICON_TRIP, muted);
  lv_obj_t *tripCaption = makeLabel(scr, LARGE_TILES_TRIP_EXTRA1, metricTripLabel(), muted);
  dw.trip = makeLabel(scr, LARGE_TILES_TRIP, t.trip, ink);
  dw.tripUnit = makeLabel(scr, LARGE_TILES_TRIP_EXTRA2, distanceUnitLabel(), ink);
  makeLayoutIcon(scr, LARGE_TILES_TEMP_ICON, CYD_ICON_TEMP_MOTOR, muted);
  lv_obj_t *motorCaption = makeLabel(scr, LARGE_TILES_MOTOR_TEMP_EXTRA1, metricMotorLabel(), muted);
  dw.motor = makeLabel(scr, LARGE_TILES_MOTOR_TEMP, motor, ink);
  dw.motorUnit = makeLabel(scr, LARGE_TILES_MOTOR_TEMP_EXTRA2, "°C", ink);
  makeLayoutIcon(scr, LARGE_TILES_ODO_ICON, CYD_ICON_ODO, muted);
  lv_obj_t *odoCaption = makeLabel(scr, LARGE_TILES_ODO_EXTRA1, "ODO", muted);
  dw.odo = makeLabel(scr, LARGE_TILES_ODO, odo, ink);
  dw.odoUnit = makeLabel(scr, LARGE_TILES_ODO_EXTRA2, distanceUnitLabel(), ink);
  registerDataSlot(0, batteryCaption, dw.battPct, dw.battUnit);
  registerDataSlot(1, voltsCaption, dw.volts, dw.voltsUnit);
  registerDataSlot(2, ampsCaption, dw.amps, dw.ampsUnit);
  registerDataSlot(3, powerCaption, dw.power, dw.powerUnit);
  registerDataSlot(4, tripCaption, dw.trip, dw.tripUnit);
  registerDataSlot(5, motorCaption, dw.motor, dw.motorUnit);
  registerDataSlot(6, odoCaption, dw.odo, dw.odoUnit);

  setScaledValueText(dw.battPct, LARGE_TILES_BATTERY, battery, layoutFontToLv(LARGE_TILES_BATTERY.font));
  setScaledValueText(dw.volts, LARGE_TILES_VOLTS, t.voltage, layoutFontToLv(LARGE_TILES_VOLTS.font));
  setScaledValueText(dw.amps, LARGE_TILES_AMPS, t.current, layoutFontToLv(LARGE_TILES_AMPS.font));
  setScaledValueText(dw.power, LARGE_TILES_POWER, t.power, layoutFontToLv(LARGE_TILES_POWER.font));
  setScaledValueText(dw.trip, LARGE_TILES_TRIP, t.trip, layoutFontToLv(LARGE_TILES_TRIP.font));
  setScaledValueText(dw.motor, LARGE_TILES_MOTOR_TEMP, motor, layoutFontToLv(LARGE_TILES_MOTOR_TEMP.font));
  setScaledValueText(dw.odo, LARGE_TILES_ODO, odo, layoutFontToLv(LARGE_TILES_ODO.font));
  bottomLeftValueUnitInTile(dw.speed, dw.speedUnit, LARGE_TILES_SPEED_TILE, 8, 4, 4);
  bottomLeftValueUnitInTile(dw.battPct, dw.battUnit, LARGE_TILES_BATTERY_TILE, 8);
  bottomLeftValueUnitInTile(dw.volts, dw.voltsUnit, LARGE_TILES_VOLTS_TILE);
  bottomLeftValueUnitInTile(dw.amps, dw.ampsUnit, LARGE_TILES_AMPS_TILE);
  bottomLeftValueUnitInTile(dw.power, dw.powerUnit, LARGE_TILES_POWER_TILE);
  bottomLeftValueUnitInTile(dw.trip, dw.tripUnit, LARGE_TILES_TRIP_TILE);
  bottomLeftValueUnitInTile(dw.motor, dw.motorUnit, LARGE_TILES_TEMP_TILE);
  bottomLeftValueUnitInTile(dw.odo, dw.odoUnit, LARGE_TILES_ODO_TILE);
  setBatteryLevel(dw.batt, v.batteryPercent, muted);
  lv_obj_set_style_bg_color(dw.batt.fill, ink, 0);
  sweepSpeedWith(dw.speed, largeTilesSpeedSweep, v);
}

static void updateLargeTiles(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char text[12];
  setScaledValueText(dw.speed, LARGE_TILES_SPEED, t.speed, layoutFontToLv(LARGE_TILES_SPEED.font));
  setScaledValueText(dw.power, LARGE_TILES_POWER, t.power, layoutFontToLv(LARGE_TILES_POWER.font));
  setLabelText(dw.powerUnit, powerUnitLabel(v.watts));
  bottomLeftValueUnitInTile(dw.speed, dw.speedUnit, LARGE_TILES_SPEED_TILE, 8, 4, 4);
  bottomLeftValueUnitInTile(dw.power, dw.powerUnit, LARGE_TILES_POWER_TILE);
  if (elecDue) {
    setScaledValueText(dw.volts, LARGE_TILES_VOLTS, t.voltage, layoutFontToLv(LARGE_TILES_VOLTS.font));
    setLabelText(dw.voltsUnit, t.voltageUnit);
    setScaledValueText(dw.amps, LARGE_TILES_AMPS, t.current, layoutFontToLv(LARGE_TILES_AMPS.font));
    setLabelText(dw.ampsUnit, t.currentUnit);
    bottomLeftValueUnitInTile(dw.volts, dw.voltsUnit, LARGE_TILES_VOLTS_TILE);
    bottomLeftValueUnitInTile(dw.amps, dw.ampsUnit, LARGE_TILES_AMPS_TILE);
  }
  if (midDue) {
    snprintf(text, sizeof(text), "%d", v.motorTemp);
    setScaledValueText(dw.motor, LARGE_TILES_MOTOR_TEMP, text, layoutFontToLv(LARGE_TILES_MOTOR_TEMP.font));
    bottomLeftValueUnitInTile(dw.motor, dw.motorUnit, LARGE_TILES_TEMP_TILE);
  }
  if (slowDue) {
    const LargeTilePalette palette = largeTilePalette();
    snprintf(text, sizeof(text), "%d", v.batteryPercent);
    setScaledValueText(dw.battPct, LARGE_TILES_BATTERY, text, layoutFontToLv(LARGE_TILES_BATTERY.font));
    setBatteryLevel(dw.batt, v.batteryPercent, palette.muted);
    setObjBgColor(dw.batt.fill, palette.ink);
    setScaledValueText(dw.trip, LARGE_TILES_TRIP, t.trip, layoutFontToLv(LARGE_TILES_TRIP.font));
    setLabelText(dw.tripUnit, distanceUnitLabel());
    snprintf(text, sizeof(text), "%s", t.odo);
    setScaledValueText(dw.odo, LARGE_TILES_ODO, text, layoutFontToLv(LARGE_TILES_ODO.font));
    setLabelText(dw.odoUnit, distanceUnitLabel());
    bottomLeftValueUnitInTile(dw.battPct, dw.battUnit, LARGE_TILES_BATTERY_TILE, 8);
    bottomLeftValueUnitInTile(dw.trip, dw.tripUnit, LARGE_TILES_TRIP_TILE);
    bottomLeftValueUnitInTile(dw.odo, dw.odoUnit, LARGE_TILES_ODO_TILE);
  }
}

// ── Ride Console ──────────────────────────────────────────────────────────────
// Two surfaces, one accent. Structure comes from filled panels split by
// hairlines rather than from an outlined box around every reading, and colour
// is spent only on the furniture — captions, bars, battery. Every value is
// white, with a grey caption above it and a grey detail line below, so the
// hierarchy is carried by size and brightness instead of by hue.

// The screen margin matches the status-strip items in the generated layout, so
// the clock icon and the battery line up with the panel edges below them.
static constexpr int BIG2_MARGIN = 6;
static constexpr int BIG2_WIDTH = 320 - 2 * BIG2_MARGIN;
static constexpr int BIG2_PAD = 10;       // content inset inside a panel
static constexpr int BIG2_CAPTION_Y = 42; // hero caption row: label left, unit right
static constexpr int BIG2_HERO_Y = 32;
static constexpr int BIG2_HERO_H = 116;
static constexpr int BIG2_SPLIT_X = 200;  // speed | power divider inside the hero
static constexpr int BIG2_STRIP_Y = 154;
static constexpr int BIG2_STRIP_H = 78;
static constexpr int BIG2_CELL_W = BIG2_WIDTH / 4;
// Content edges of the two hero columns
static constexpr int BIG2_SPEED_L = BIG2_MARGIN + BIG2_PAD;             // 16
static constexpr int BIG2_SPEED_R = BIG2_SPLIT_X - BIG2_PAD;            // 190
static constexpr int BIG2_POWER_L = BIG2_SPLIT_X + 8;                   // 208
static constexpr int BIG2_POWER_R = 320 - BIG2_MARGIN - BIG2_PAD;       // 304
static constexpr int BIG2_POWER_CX = (BIG2_POWER_L + BIG2_POWER_R) / 2; // 256
// Elevated panel fill, the hairline around it, and the slightly brighter rule
// that divides content inside a panel. One surface tone and two line tones for
// the whole dashboard.
static constexpr uint16_t BIG2_SURFACE = 0x2945;
static constexpr uint16_t BIG2_EDGE = 0x39E7;
static constexpr uint16_t BIG2_RULE = 0x4A49;
static constexpr uint16_t BIG2_MUTED = 0x7BEF;  // decorative captions, dimmer than data labels

static const Item BIG2_SPEED = {(BIG2_SPEED_L + BIG2_SPEED_R) / 2, 94, 0, 0, 0, 0, 0, 0, 0,
                                BIG2_SPEED_R - BIG2_SPEED_L, 72, 1, 0};

static int big2CellCenter(int index) { return BIG2_MARGIN + index * BIG2_CELL_W + BIG2_CELL_W / 2; }

// Panels stay translucent so a gradient background still reads through them,
// and the border is faint enough to define an edge without framing the content.
static lv_obj_t *makeBig2Surface(lv_obj_t *scr, int x, int y, int w, int h) {
  const bool light = dashboardLightModeActive();
  lv_obj_t *panel = makePanel(scr, x, y, w, h, 8,
                              light ? lv_color_hex(0xDCD8CE) : dash565(BIG2_EDGE),
                              light ? lv_color_white() : dash565(BIG2_SURFACE), true);
  lv_obj_set_style_bg_opa(panel, light ? LV_OPA_COVER : LV_OPA_80, 0);
  lv_obj_set_style_border_opa(panel, light ? LV_OPA_COVER : LV_OPA_50, 0);
  return panel;
}

// One column of the secondary strip. The columns are separated by hairlines
// instead of four outlined cards, so the row reads as a single block of
// statistics rather than as four competing boxes.
static lv_obj_t *makeBig2Cell(lv_obj_t *scr, int index, const char *caption, const char *value, const char *detail,
                              lv_obj_t **valueOut, lv_obj_t **detailOut) {
  const int cx = big2CellCenter(index);
  lv_obj_t *captionLabel = makeLabelAt(scr, cx, 163, caption, labelLv(), F1, 3);
  *valueOut = makeLabelAt(scr, cx, 190, value, whiteLv(), F3B, 1);
  *detailOut = makeLabelAt(scr, cx, 210, detail, labelLv(), F1, 3);
  return captionLabel;
}

// Pill-shaped bar with a hairline track. Used for both the speed scale and the
// symmetrical regen/drive beam, so the two meters share one visual language.
static lv_obj_t *makeBig2Bar(lv_obj_t *scr, int x, int y, int w, int h) {
  lv_obj_t *bar = lv_bar_create(scr);
  makePassive(bar);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, w, h);
  lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, dash565(BIG2_EDGE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_60, LV_PART_MAIN);
  // The indicator keeps a tighter radius than the track: rounding a short fill
  // to half its own height turns it into a slider knob rather than a reading.
  lv_obj_set_style_radius(bar, min(h / 2, 3), LV_PART_INDICATOR);
  return bar;
}

// The speed bar is glides[0], the power beam glides[1]. The beam runs both ways, so its position
// is signed and it wears the colour of the side it is on: crossing zero changes it as the beam does.
static lv_color_t bigReadoutDriveColor;
static lv_color_t bigReadoutRegenColor;

// A glide frame that moves a bar less than a pixel changes nothing, and LVGL repaints the whole bar for it.
static int bigReadoutSpeedPixels;
static int bigReadoutBeamPixels;

static void bigReadoutSpeedPlace(void *, int position) {
  const int pixels = position * (BIG2_SPEED_R - BIG2_SPEED_L) / kGlideScale;
  if (pixels == bigReadoutSpeedPixels) return;
  bigReadoutSpeedPixels = pixels;
  lv_bar_set_value(dw.barSpeed, position, LV_ANIM_OFF);
}

static void bigReadoutPowerPlace(void *, int position) {
  const int pixels = position * ((BIG2_POWER_R - BIG2_POWER_L) / 2) / kGlideScale;
  if (pixels == bigReadoutBeamPixels) return;
  bigReadoutBeamPixels = pixels;
  setObjBgColor(dw.barPower, position < 0 ? bigReadoutRegenColor : bigReadoutDriveColor, LV_PART_INDICATOR);
  lv_bar_set_value(dw.barPower, position, LV_ANIM_OFF);
}

static int bigReadoutBeamPosition(int watts) {
  const int maximum = watts < 0 && automaticGaugeRanges ? displayGaugeMaximum(RANGE_REGEN) : max(100, powerBarMax());
  return (int)lroundf(constrain((float)watts / maximum, -1.0F, 1.0F) * kGlideScale);
}

static void bigReadoutSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setLabelText(dw.speed, text);
  glideAim(glides[0], glidePosition(value, speedGaugeMax()), false);
}

static void bigReadoutPowerSweep(int value) {
  char text[16];
  formatPowerValue(text, sizeof(text), value);
  setLabelText(dw.power, text);
  setLabelText(dw.powerUnit, powerUnitLabel(value));
  glideAim(glides[1], bigReadoutBeamPosition(value), false);
}

static void buildBigReadout(lv_obj_t *scr, const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  const lv_color_t accent = accentLv();
  // The regen fill is the only second hue on the screen: a symmetrical beam
  // needs its two halves to be told apart, and nothing else here does.
  const lv_color_t regen = seriesContrastLv(accent);
  const bool light = dashboardLightModeActive();
  const lv_color_t rule = light ? lv_color_hex(0xDCD8CE) : dash565(BIG2_RULE);
  const lv_color_t muted = light ? lv_color_hex(0x2C3136) : dash565(BIG2_MUTED);
  char value[32];
  char detail[32];

  if (light && !dashboardGradientEnabled && dashboardGradientTheme == ACCENT_DEFAULT)
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xE5E2D8), 0);

  // Status strip: no frame or separator, just the chrome.
  makeLayoutIcon(scr, BIG_READOUT_TIME_ICON, CYD_ICON_UPTIME, accent);
  dw.uptime = makeLabel(scr, BIG_READOUT_TIME, t.uptime, whiteLv());
  lv_obj_t *bigOem = makeLabelFont(scr, BIG_READOUT_OEM, "", accent, F2);
  setFittedText(bigOem, BIG_READOUT_OEM, dashOemName(), F2);
  dw.topBattPct = makeTopBatteryLabel(scr, BIG_READOUT_BATTERY, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, BIG_READOUT_BATTERY_ICON, 5);

  // Hero card: the speed owns most of the width, power sits behind a hairline.
  makeBig2Surface(scr, BIG2_MARGIN, BIG2_HERO_Y, BIG2_WIDTH, BIG2_HERO_H);
  makeVLine(scr, BIG2_SPLIT_X, BIG2_HERO_Y + 16, BIG2_HERO_H - 32, rule);

  // Each hero caption row carries its label on the left and its unit on the
  // right. Parking the unit there keeps it clear of the number entirely, so a
  // value that gains or loses a digit never shifts anything around it.
  makeIcon(scr, BIG2_SPEED_L, BIG2_CAPTION_Y - 2, CYD_ICON_SPEED, accent);
  makeLabelAt(scr, BIG2_SPEED_L + 22, BIG2_CAPTION_Y, metricSpeedLabel(), accent, F1, 0);
  dw.speedUnit = makeLabelAt(scr, BIG2_SPEED_R, BIG2_CAPTION_Y, speedUnitLabel(), labelLv(), F1, 2);
  dw.speed = makeLabelFont(scr, BIG2_SPEED, t.speed, whiteLv(), layoutFontToLv(BIG2_SPEED.font));
  configureFittedLabel(dw.speed, BIG2_SPEED, speedFitTemplate(), layoutFontToLv(BIG2_SPEED.font));
  setLabelText(dw.speed, t.speed);
  dw.barSpeed = makeBig2Bar(scr, BIG2_SPEED_L, 136, BIG2_SPEED_R - BIG2_SPEED_L, 5);
  lv_bar_set_range(dw.barSpeed, 0, kGlideScale);
  lv_obj_set_style_bg_color(dw.barSpeed, accent, LV_PART_INDICATOR);
  // The bar shows a proportion, so both sides stay in km/h. Driving it with
  // converted speed against a km/h scale made it under-read in every
  // non-metric unit, and read as permanently empty in Mach.
  bigReadoutSpeedPixels = -1;
  glideAdd(bigReadoutSpeedPlace, nullptr);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()), false);

  makeIcon(scr, BIG2_POWER_L, BIG2_CAPTION_Y - 2, CYD_ICON_POWER, accent);
  makeLabelAt(scr, BIG2_POWER_L + 22, BIG2_CAPTION_Y, metricPowerLabelShort(), accent, F1, 0);
  dw.powerUnit = makeLabelAt(scr, BIG2_POWER_R, BIG2_CAPTION_Y, powerUnitLabel(v.watts), labelLv(), F1, 2);
  formatPowerValue(value, sizeof(value), v.watts);
  dw.power = makeLabelAt(scr, BIG2_POWER_CX, 80, value, whiteLv(), F4, 1);
  // A notch above the beam marks zero. Crossing the bar with it would put a
  // line straight through the fill, which always starts from that same point.
  makeVLine(scr, BIG2_POWER_CX, 105, 3, muted);
  dw.barPower = makeBig2Bar(scr, BIG2_POWER_L, 110, BIG2_POWER_R - BIG2_POWER_L, 10);
  lv_bar_set_range(dw.barPower, -kGlideScale, kGlideScale);
  lv_bar_set_mode(dw.barPower, LV_BAR_MODE_SYMMETRICAL);
  makeLabelAt(scr, BIG2_POWER_L, 128, "REGEN", muted, F1, 0);
  makeLabelAt(scr, BIG2_POWER_R, 128, "DRIVE", muted, F1, 2);
  bigReadoutDriveColor = accent;
  bigReadoutRegenColor = regen;
  bigReadoutBeamPixels = INT_MIN;
  glideAdd(bigReadoutPowerPlace, nullptr);
  glideAim(glides[1], bigReadoutBeamPosition(v.watts), false);

  // Secondary strip: one surface, three rules, four columns.
  makeBig2Surface(scr, BIG2_MARGIN, BIG2_STRIP_Y, BIG2_WIDTH, BIG2_STRIP_H);
  for (int i = 1; i < 4; i++) {
    makeVLine(scr, BIG2_MARGIN + i * BIG2_CELL_W, BIG2_STRIP_Y + 12, BIG2_STRIP_H - 24, rule);
  }

  const BatteryStats stats = dashBatteryStats();
  lv_obj_t *packCaption = makeBig2Cell(scr, 0, txt("PACK", "AKKU", "AKKU", "BATTERIE", "BATERÍA", "BATTERIA"),
                                       t.voltageWithUnit, t.battPct, &dw.volts, &dw.battPct);
  formatAvailableEnergyRate(detail, sizeof(detail), stats.tripWhPerKm, true);
  lv_obj_t *loadCaption = makeBig2Cell(scr, 1, txt("LOAD", "KUORMA", "LAST", "CHARGE", "CARGA", "CARICO"),
                                       t.currentWithUnit, detail, &dw.amps, &dw.energyRate);
  snprintf(detail, sizeof(detail), "ESC %s", t.esc);
  lv_obj_t *thermalCaption = makeBig2Cell(scr, 2, txt("THERMAL", "LÄMPÖ", "TEMP.", "THERM.", "TÉRMICO", "TERMICO"),
                                          t.motor, detail, &dw.motor, &dw.esc);
  // Range headlines the cell, trip is the detail under it. No "TRIP" prefix:
  // it overflows the column in German/French/Italian and merely repeats the
  // caption in the languages where it does fit.
  formatAvailableRange(value, sizeof(value), stats.rangeKm, true);
  snprintf(detail, sizeof(detail), "%s %s", t.trip, distanceUnitLabel());
  lv_obj_t *rideCaption = makeBig2Cell(scr, 3, txt("RIDE", "MATKA", "FAHRT", "TRAJET", "VIAJE", "VIAGGIO"),
                                       value, detail, &dw.range, &dw.trip);
  registerDataSlot(0, packCaption, dw.volts);
  registerDataSlot(1, loadCaption, dw.amps);
  registerDataSlot(2, thermalCaption, dw.motor);
  registerDataSlot(3, rideCaption, dw.range);

  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  sweepSpeedWith(dw.speed, bigReadoutSpeedSweep, v);
  sweepPowerWith(dw.power, bigReadoutPowerSweep, v);
}

static void updateBigReadout(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char value[32];
  setLabelText(dw.speed, t.speed);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()));
  formatPowerValue(value, sizeof(value), v.watts);
  setLabelText(dw.power, value);
  setLabelText(dw.powerUnit, powerUnitLabel(v.watts));
  glideAim(glides[1], bigReadoutBeamPosition(v.watts));
  if (elecDue) {
    setLabelText(dw.volts, t.voltageWithUnit);
    setLabelText(dw.amps, t.currentWithUnit);
  }
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
    setLabelText(dw.motor, t.motor);
    snprintf(value, sizeof(value), "ESC %s", t.esc);
    setLabelText(dw.esc, value);
  }
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setLabelText(dw.topBattPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    const BatteryStats stats = dashBatteryStats();
    formatAvailableEnergyRate(value, sizeof(value), stats.tripWhPerKm, true);
    setLabelText(dw.energyRate, value);
    formatAvailableRange(value, sizeof(value), stats.rangeKm, true);
    setLabelText(dw.range, value);
    snprintf(value, sizeof(value), "%s %s", t.trip, distanceUnitLabel());
    setLabelText(dw.trip, value);
  }
}

// ── Redline ───────────────────────────────────────────────────────────────────

static void updateRedlineUnitPositions() {
  placeUnitAfterValue(dw.motor, dw.motorUnit, REDLINE_MOTOR_EXTRA1, 4);
  placeUnitAfterValue(dw.volts, dw.voltsUnit, REDLINE_VOLTS_EXTRA1, 4);
  placeUnitAfterValue(dw.amps, dw.ampsUnit, REDLINE_CURRENT_EXTRA1, 4);
  placeUnitAfterValue(dw.power, dw.powerUnit, REDLINE_POWER_UNIT, 4);
  placeUnitAfterValue(dw.range, dw.rangeUnit, REDLINE_RANGE_EXTRA1, 3);
  placeUnitAfterValue(dw.trip, dw.tripUnit, REDLINE_TRIP_EXTRA1, 3);
  placeUnitAfterValue(dw.odo, dw.odoUnit, REDLINE_ODO_EXTRA1, 3);
}

// Redline's values update frequently, but Montserrat's numeric glyphs are
// tabular: most changes repaint inside the same tight box. Only resize and
// reposition a label when the new rendered width actually crosses a boundary.
// Returning true tells the caller that its trailing unit also needs moving.
static bool setRedlineStableTightText(lv_obj_t *label, const Item &item, const char *text,
                                      bool centered = false, int padding = 4) {
  if (!label || !text) return false;
  if (strcmp(lv_label_get_text(label), text) == 0) return false;
  const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
  lv_point_t size;
  lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int targetWidth = size.x + padding;
  const int targetHeight = lv_font_get_line_height(font) + 2;
  const bool geometryChanged = lv_obj_get_width(label) != targetWidth || lv_obj_get_height(label) != targetHeight;
  if (geometryChanged) {
    if (centered)
      setTightCenteredLabelText(label, item, text, padding);
    else
      setTightLabelText(label, item, text, padding);
  } else {
    setLabelText(label, text);
  }
  return geometryChanged;
}

// The lit ticks, the needle and the digits sweep as one instrument.
static void redlineSpeedPlace(void *, int position) {
  setTickDialValue(dw.speedDial, position);
  setTickNeedleValue(dw.speedNeedle, position);
}

static void redlineSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setTightCenteredLabelText(dw.speed, REDLINE_SPEED, text);
  glideAim(glides[0], glidePosition(value, speedGaugeMax()), false);
}

static void buildRedline(lv_obj_t *scr, const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  const lv_color_t color = accentLv();
  const lv_color_t dark = accentDarkLv();
  char value[20];

  // Chrome, drawn rather than blitted from a background bitmap. Three pieces:
  // an outer frame, a panel behind each pair of side stats, and a strip along
  // the foot split into the three distance cells. Built first so every readout
  // lands on top of them.
  const lv_color_t frame = lv_color_mix(color, dashBlack(), 90);
  const lv_color_t panelFill = panelLv();
  makePanel(scr, 2, 2, 316, 236, 6, frame, dashBlack(), false);
  makePanel(scr, 6, 98, 84, 82, 4, frame, panelFill, true);
  makePanel(scr, 230, 98, 84, 82, 4, frame, panelFill, true);
  makePanel(scr, 6, 188, 308, 44, 4, frame, panelFill, true);
  makeVLine(scr, 109, 194, 32, frame);
  makeVLine(scr, 212, 194, 32, frame);

  // Shared status row, the same furniture the Dual Gauge and Simple themes
  // carry: clock at the left, charge and its segmented icon at the right.
  makeLayoutIcon(scr, REDLINE_CLOCK_ICON, CYD_ICON_UPTIME, color);
  dw.uptime = makeTopTimeLabel(scr, REDLINE_UPTIME, t.uptime, whiteLv());
  dw.battPct = makeTopBatteryLabel(scr, REDLINE_BATTERY_PCT, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, REDLINE_BATTERY_ICON, 5);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);

  // lit-tick dial (start 218deg, sweep 104deg): individual small line
  // objects instead of one lv_meter — a meter would be a screen-sized widget
  // fully redrawn whenever anything on top of it changes
  dw.speedDial = makeTickDial(scr, 0, REDLINE_SPEED_GAUGE.cx, REDLINE_SPEED_GAUGE.cy, REDLINE_SPEED_GAUGE.r, 51,
                              10, 218.0F, 104.0F, 19, 11, kGlideScale, 3, 2, redlinePts);
  dw.speedNeedle = makeTickNeedle(scr, REDLINE_SPEED_GAUGE.cx, REDLINE_SPEED_GAUGE.cy, 132,
                                  REDLINE_SPEED_GAUGE.r, 218, 104, kGlideScale, 6, 3);

  const Item *scaleItems[6] = {&REDLINE_S0, &REDLINE_S20, &REDLINE_S40, &REDLINE_S60, &REDLINE_S80, &REDLINE_S100};
  for (int i = 0; i < 6; i++) {
    char scaleText[10];
    formatSpeedScaleMark(i, scaleText, sizeof(scaleText));
    dw.scaleLabels[i] = makeLabel(scr, *scaleItems[i], scaleText, i == 5 ? color : whiteLv());
  }

  makeLabel(scr, REDLINE_OEM, dashOemName(), color);

  dw.speed = makeRedlineSpeedValue(scr, t.speed);
  configureFittedLabel(dw.speed, REDLINE_SPEED, speedFitTemplate(), layoutFontToLv(REDLINE_SPEED.font));
  dw.speedUnit = makeLabel(scr, REDLINE_SPEED_UNIT, speedUnitLabel(), whiteLv());

  snprintf(value, sizeof(value), "%d", v.motorTemp);
  dw.motor = makeLabelFont(scr, REDLINE_MOTOR, value, whiteLv(), F3B);
  dw.motorUnit = makeLabel(scr, REDLINE_MOTOR_EXTRA1, "°C", whiteLv());
  lv_obj_t *motorCaption = makeLabel(scr, REDLINE_MOTOR_LABEL, metricMotorLabel(), color);
  dw.volts = makeLabelFont(scr, REDLINE_VOLTS, t.voltage, whiteLv(), F3B);
  dw.voltsUnit = makeLabel(scr, REDLINE_VOLTS_EXTRA1, t.voltageUnit, whiteLv());
  lv_obj_t *voltsCaption = makeLabel(scr, REDLINE_VOLTS_LABEL,
                                     txt("VOLTAGE", "JÄNNITE", "SPANNUNG", "TENSION", "VOLTAJE", "TENSIONE"), color);
  dw.amps = makeLabelFont(scr, REDLINE_CURRENT, t.current, whiteLv(), F3B);
  dw.ampsUnit = makeLabel(scr, REDLINE_CURRENT_EXTRA1, t.currentUnit, whiteLv());
  lv_obj_t *ampsCaption = makeLabel(scr, REDLINE_CURRENT_LABEL,
                                    txt("CURRENT", "VIRTA", "STROM", "COURANT", "CORRIENTE", "CORRENTE"), color);
  dw.power = makeLabelFont(scr, REDLINE_POWER, t.power, whiteLv(), F3B);
  dw.powerUnit = makeLabel(scr, REDLINE_POWER_UNIT, powerUnitLabel(v.watts), whiteLv());
  lv_obj_t *powerCaption = makeLabel(scr, REDLINE_POWER_LABEL, metricPowerLabel(), color);

  lv_obj_t *tripCaption = makeLabel(scr, REDLINE_TRIP_LABEL, metricTripLabel(), color);
  dw.trip = makeLabel(scr, REDLINE_TRIP, t.trip, whiteLv());
  dw.tripUnit = makeLabel(scr, REDLINE_TRIP_EXTRA1, distanceUnitLabel(), whiteLv());
  lv_obj_t *rangeCaption = makeLabel(scr, REDLINE_RANGE_LABEL, metricRangeLabel(), color);
  formatAvailableRange(value, sizeof(value), dashBatteryStats().rangeKm);
  dw.range = makeLabel(scr, REDLINE_RANGE, value, whiteLv());
  dw.rangeUnit = makeLabel(scr, REDLINE_RANGE_EXTRA1, distanceUnitLabel(), whiteLv());
  lv_obj_t *odoCaption = makeLabel(scr, REDLINE_ODO_LABEL, "ODO", color);
  dw.odo = makeLabel(scr, REDLINE_ODO, t.odo, whiteLv());
  dw.odoUnit = makeLabel(scr, REDLINE_ODO_EXTRA1, distanceUnitLabel(), whiteLv());
  registerDataSlot(0, motorCaption, dw.motor, dw.motorUnit);
  registerDataSlot(1, voltsCaption, dw.volts, dw.voltsUnit);
  registerDataSlot(2, ampsCaption, dw.amps, dw.ampsUnit);
  registerDataSlot(3, powerCaption, dw.power, dw.powerUnit);
  registerDataSlot(4, tripCaption, dw.trip, dw.tripUnit);
  registerDataSlot(5, rangeCaption, dw.range, dw.rangeUnit);
  registerDataSlot(6, odoCaption, dw.odo, dw.odoUnit);

  setTightCenteredLabelText(dw.speed, REDLINE_SPEED, t.speed);
  setTightLabelText(dw.motor, REDLINE_MOTOR, lv_label_get_text(dw.motor));
  setTightLabelText(dw.motorUnit, REDLINE_MOTOR_EXTRA1, "°C");
  setTightLabelText(dw.volts, REDLINE_VOLTS, t.voltage);
  setTightLabelText(dw.voltsUnit, REDLINE_VOLTS_EXTRA1, t.voltageUnit);
  setTightLabelText(dw.amps, REDLINE_CURRENT, t.current);
  setTightLabelText(dw.ampsUnit, REDLINE_CURRENT_EXTRA1, t.currentUnit);
  setTightLabelText(dw.power, REDLINE_POWER, t.power);
  setTightLabelText(dw.powerUnit, REDLINE_POWER_UNIT, powerUnitLabel(v.watts));
  setTightLabelText(dw.trip, REDLINE_TRIP, t.trip);
  setTightLabelText(dw.tripUnit, REDLINE_TRIP_EXTRA1, distanceUnitLabel());
  setTightLabelText(dw.range, REDLINE_RANGE, lv_label_get_text(dw.range));
  setTightLabelText(dw.rangeUnit, REDLINE_RANGE_EXTRA1, distanceUnitLabel());
  setTightLabelText(dw.odo, REDLINE_ODO, t.odo);
  setTightLabelText(dw.odoUnit, REDLINE_ODO_EXTRA1, distanceUnitLabel());
  updateRedlineUnitPositions();
  glideAdd(redlineSpeedPlace, nullptr);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()), false);
  sweepSpeedWith(dw.speed, redlineSpeedSweep, v);
}

static void updateRedline(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char value[20];

  // full rate: only the speed digits and the dial's arc bar
  setRedlineStableTightText(dw.speed, REDLINE_SPEED, t.speed, true, 8);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()));

  // watts at full rate; volts/amps on the 200 ms tier; temp on the 500 ms
  // tier — the side stats sit inside the dial area, so theirs are the
  // pricier redraws
  const bool powerMoved = setRedlineStableTightText(dw.power, REDLINE_POWER, t.power);
  const char *powerUnit = powerUnitLabel(v.watts);
  bool powerUnitChanged = false;
  if (strcmp(lv_label_get_text(dw.powerUnit), powerUnit) != 0) {
    setTightLabelText(dw.powerUnit, REDLINE_POWER_UNIT, powerUnit);
    powerUnitChanged = true;
  }
  if (powerMoved || powerUnitChanged) placeUnitAfterValue(dw.power, dw.powerUnit, REDLINE_POWER_UNIT, 4);
  if (elecDue) {
    const bool voltsMoved = setRedlineStableTightText(dw.volts, REDLINE_VOLTS, t.voltage);
    bool voltsUnitChanged = false;
    if (strcmp(lv_label_get_text(dw.voltsUnit), t.voltageUnit) != 0) {
      setTightLabelText(dw.voltsUnit, REDLINE_VOLTS_EXTRA1, t.voltageUnit);
      voltsUnitChanged = true;
    }
    const bool ampsMoved = setRedlineStableTightText(dw.amps, REDLINE_CURRENT, t.current);
    bool ampsUnitChanged = false;
    if (strcmp(lv_label_get_text(dw.ampsUnit), t.currentUnit) != 0) {
      setTightLabelText(dw.ampsUnit, REDLINE_CURRENT_EXTRA1, t.currentUnit);
      ampsUnitChanged = true;
    }
    if (voltsMoved || voltsUnitChanged) placeUnitAfterValue(dw.volts, dw.voltsUnit, REDLINE_VOLTS_EXTRA1, 4);
    if (ampsMoved || ampsUnitChanged) placeUnitAfterValue(dw.amps, dw.ampsUnit, REDLINE_CURRENT_EXTRA1, 4);
  }
  if (midDue) {
    snprintf(value, sizeof(value), "%d", v.motorTemp);
    if (setRedlineStableTightText(dw.motor, REDLINE_MOTOR, value))
      placeUnitAfterValue(dw.motor, dw.motorUnit, REDLINE_MOTOR_EXTRA1, 4);
  }
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
  }
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    const bool tripMoved = setRedlineStableTightText(dw.trip, REDLINE_TRIP, t.trip);
    formatAvailableRange(value, sizeof(value), dashBatteryStats().rangeKm);
    const bool rangeMoved = setRedlineStableTightText(dw.range, REDLINE_RANGE, value);
    const bool odoMoved = setRedlineStableTightText(dw.odo, REDLINE_ODO, t.odo);
    if (tripMoved) placeUnitAfterValue(dw.trip, dw.tripUnit, REDLINE_TRIP_EXTRA1, 3);
    if (rangeMoved) placeUnitAfterValue(dw.range, dw.rangeUnit, REDLINE_RANGE_EXTRA1, 3);
    if (odoMoved) placeUnitAfterValue(dw.odo, dw.odoUnit, REDLINE_ODO_EXTRA1, 3);
  }
}

// ── Trace ─────────────────────────────────────────────────────────────────────

// Rolling telemetry history, stored as percentages of the configured maxima so
// speed and power share one axis. Sampling runs while any theme is on screen
// (updateDashboard, the live-telemetry path only) so switching to Trace shows
// the last 60 s instead of an empty chart. The 162 intervals map one-to-one to
// the chart's 162-pixel content width, so every new sample moves the existing
// shape exactly one pixel instead of alternately one and two pixels. That
// avoids subtle rerasterization wobble while retaining an approximately
// one-minute window.
static const uint16_t kTraceSamples = 163;
static const uint32_t kTracePeriodMs = 370;
static float traceSpeedPct[kTraceSamples];
static float tracePowerPct[kTraceSamples];
static uint16_t traceCount = 0;  // samples recorded so far, capped at kTraceSamples
static uint16_t traceHead = 0;   // next slot to write
static uint32_t traceLastMs = 0;
// Selector/demo samples use the same append-only mechanics without replacing
// the rider's live minute of history while they browse themes.
static float demoTraceSpeedPct[kTraceSamples];
static float demoTracePowerPct[kTraceSamples];
static uint16_t demoTraceCount = 0;
static uint16_t demoTraceHead = 0;
static uint32_t demoTraceLastMs = 0;
static bool traceSampleAdded = false;
// Chart columns shown so far, left to right: the startup sweep draws the plot
// in rather than having it appear all at once.
static uint16_t traceRevealPoints = kTraceSamples;

static int tracePowerMax() {
  return powerBarMax();
}

static uint8_t tracePercent(float value, int maxValue) {
  if (maxValue <= 0) return 0;
  return (uint8_t)constrain((int)(value * 100.0F / maxValue + 0.5F), 0, 100);
}

static bool traceSampleDue(uint32_t now, uint32_t &lastMs) {
  if (lastMs == 0) {
    lastMs = now;
    return true;
  }
  const uint32_t elapsed = now - lastMs;
  if (elapsed < kTracePeriodMs) return false;
  // The UI calls us every 100 ms, which cannot represent 370 ms directly.
  // Retain the fractional phase so updates alternate as needed instead of
  // rounding every interval up to 400 ms and stretching the time window.
  lastMs = now - (elapsed % kTracePeriodMs);
  return true;
}

static bool recordTraceSample(const DashboardValues &v) {
  const uint32_t now = millis();
  if (!traceSampleDue(now, traceLastMs)) return false;
  traceSpeedPct[traceHead] = max(0, v.speedKmh);
  tracePowerPct[traceHead] = max(0, v.watts);
  traceHead = (traceHead + 1) % kTraceSamples;
  if (traceCount < kTraceSamples) traceCount++;
  return true;
}

static void appendDemoTraceSample(float absoluteSeconds) {
  const DemoRide sample = demoRideAt(absoluteSeconds, false);
  demoTraceSpeedPct[demoTraceHead] = sample.speedKmh;
  demoTracePowerPct[demoTraceHead] = max(0.0F, sample.watts);
  demoTraceHead = (demoTraceHead + 1) % kTraceSamples;
  if (demoTraceCount < kTraceSamples) demoTraceCount++;
}

static void seedThumbnailTrace(float *speed, float *power) {
  for (uint16_t i = 0; i < kTraceSamples; i++) {
    const float phase = (float)i / (kTraceSamples - 1);
    const float ride = 0.52F + 0.30F * sinf(phase * 6.4F) + 0.11F * sinf(phase * 17.0F);
    speed[i] = constrain(ride, .06F, .96F) * thumbnailGaugeMaximum(RANGE_SPEED);
    power[i] = constrain(ride * .72F + .16F * sinf(phase * 11), .03F, .9F) * thumbnailGaugeMaximum(RANGE_POWER);
  }
}

static void seedDemoTraceHistory() {
  if (demoPreviewIsFrozen()) {
    seedThumbnailTrace(demoTraceSpeedPct, demoTracePowerPct);
    demoTraceHead = 0;
    demoTraceCount = kTraceSamples;
    demoTraceLastMs = 600000;
    return;
  }
  const float now = demoRideSeconds();
  const float spacing = kTracePeriodMs / 1000.0F;
  demoTraceHead = 0;
  demoTraceCount = 0;
  for (uint16_t i = 0; i < kTraceSamples; i++) {
    const uint16_t age = kTraceSamples - 1 - i;
    if (now >= age * spacing) appendDemoTraceSample(now - age * spacing);
  }
  // A full sequential seed wraps the next-write head back to zero.
  demoTraceLastMs = (uint32_t)(demoRideSeconds() * 1000);
}

static bool recordDemoTraceSample() {
  const uint32_t now = (uint32_t)(demoRideSeconds() * 1000);
  if (now >= demoTraceLastMs && now - demoTraceLastMs < kTracePeriodMs) return false;
  seedDemoTraceHistory();
  return true;
}

// Smooth each point from its own value and the two samples immediately before
// it. Because the filter is trailing, adding a new sample never reshapes the
// existing history; the plotted curve simply advances across the chart.
static int smoothedTraceValue(const float *history, uint16_t head, uint16_t count, uint16_t age, int maximum) {
  if (age >= count) return LV_CHART_POINT_NONE;
  float weightedTotal = 0;
  int totalWeight = 0;
  const uint8_t weights[3] = {2, 1, 1};
  for (uint16_t offset = 0; offset < 3 && age + offset < count; offset++) {
    const uint16_t slot = (head + kTraceSamples - 1 - age - offset) % kTraceSamples;
    weightedTotal += history[slot] * weights[offset];
    totalWeight += weights[offset];
  }
  return tracePercent(weightedTotal / totalWeight, maximum);
}

// Repaints the whole series from raw measurements using the current range.
// Cheaper than it looks (163
// array writes and one redraw) and it cannot drift out of sync with the
// history the way incremental shifting would when updates are throttled.
static void fillTraceChart() {
  if (!dw.chart) return;
  const bool demo = demoModeIsActive();
  const uint16_t head = demo ? demoTraceHead : traceHead;
  const uint16_t count = demo ? demoTraceCount : traceCount;
  const float *speedHistory = demo ? demoTraceSpeedPct : traceSpeedPct;
  const float *powerHistory = demo ? demoTracePowerPct : tracePowerPct;
  for (uint16_t i = 0; i < kTraceSamples; i++) {
    const uint16_t age = kTraceSamples - 1 - i;  // 0 = newest, drawn rightmost
    const bool shown = i < traceRevealPoints;
    const int speedValue = shown ? smoothedTraceValue(speedHistory, head, count, age, speedGaugeMax())
                                 : LV_CHART_POINT_NONE;
    const int powerValue = shown ? smoothedTraceValue(powerHistory, head, count, age, tracePowerMax())
                                 : LV_CHART_POINT_NONE;
    lv_chart_set_value_by_id(dw.chart, dw.traceSpeedSeries, i, speedValue);
    lv_chart_set_value_by_id(dw.chart, dw.tracePowerSeries, i, powerValue);
  }
  lv_chart_refresh(dw.chart);
}

#ifdef CYD_LVGL_PREVIEW
void previewSeedTrace() {
  seedThumbnailTrace(traceSpeedPct, tracePowerPct);
  traceHead = 0;
  traceCount = kTraceSamples;
}
#endif

static void traceRevealSweep(int value) {
  traceRevealPoints = (uint16_t)constrain(value, 0, (int)kTraceSamples);
  fillTraceChart();
}

static void buildTrace(lv_obj_t *scr, const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  const lv_color_t accent = accentLv();
  const lv_color_t powerColor = seriesContrastLv(accent);
  char text[24];

  if (demoModeIsActive()) seedDemoTraceHistory();

  // Echo Ride Console's unified telemetry strip, but keep Trace more subdued:
  // a near-black raised surface and quiet internal rules instead of five
  // separate bottom cells floating directly on the canvas.
  lv_obj_t *footer = makePanel(scr, 4, 177, 312, 57, 7, dash565(0x2945), dash565(0x1082), true);
  lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(footer, LV_OPA_60, 0);
  for (int x = 64; x <= 256; x += 64) makeVLine(scr, x, 184, 44, dash565(0x2945));

  makeLayoutIcon(scr, TRACE_TIME_ICON, CYD_ICON_UPTIME, accent);
  dw.uptime = makeTopTimeLabel(scr, TRACE_TIME, t.uptime, whiteLv());
  lv_obj_t *traceOem = makeLabelFont(scr, TRACE_OEM, "", accent, layoutFontToLv(TRACE_OEM.font));
  setFittedText(traceOem, TRACE_OEM, dashOemName(), layoutFontToLv(TRACE_OEM.font));
  dw.battPct = makeTopBatteryLabel(scr, TRACE_BATTERY, t.battPct, whiteLv());
  dw.segBatt = makeSegBattery(scr, TRACE_BATTERY_ICON, 5);

  dw.speed = makeLabel(scr, TRACE_SPEED, t.speed, whiteLv());
  configureFittedLabel(dw.speed, TRACE_SPEED, speedFitTemplate(), layoutFontToLv(TRACE_SPEED.font));
  dw.speedUnit = makeLabel(scr, TRACE_SPEED_UNIT, speedUnitLabel(), accent);
  // the two readouts wear their series colors, which is what identifies them
  formatPowerValue(text, sizeof(text), v.watts);
  dw.power = makeLabel(scr, TRACE_POWER, text, powerColor);
  dw.powerUnit = makeLabel(scr, TRACE_POWER_EXTRA1, powerUnitLabel(v.watts), powerColor);

  makePanel(scr, TRACE_CHART_PANEL.x, TRACE_CHART_PANEL.y, TRACE_CHART_PANEL.w, TRACE_CHART_PANEL.h,
            TRACE_CHART_PANEL.radius, themeColorDark(0x03B6), dash565(0x0841), true);
  makeLineBox(scr, TRACE_LEGEND_SPEED_DASH, accent);
  makeLabel(scr, TRACE_LEGEND_SPEED, metricSpeedLabel(), accent);
  makeLineBox(scr, TRACE_LEGEND_POWER_DASH, powerColor);
  makeLabel(scr, TRACE_LEGEND_POWER, metricPowerLabelShort(), powerColor);
  const uint32_t traceSpanMs = (kTraceSamples - 1) * kTracePeriodMs;
  snprintf(text, sizeof(text), "%u s", (unsigned)((traceSpanMs + 500) / 1000));
  makeLabel(scr, TRACE_CHART_SPAN, text, labelLv());

  dw.chart = lv_chart_create(scr);
  makePassive(dw.chart);  // taps fall through to the screen, which opens the menu
  lv_obj_set_pos(dw.chart, TRACE_CHART_AREA.x, TRACE_CHART_AREA.y);
  lv_obj_set_size(dw.chart, TRACE_CHART_AREA.w, TRACE_CHART_AREA.h);
  lv_chart_set_type(dw.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(dw.chart, kTraceSamples);
  lv_chart_set_range(dw.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(dw.chart, 0, 0);
  lv_obj_set_style_bg_opa(dw.chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(dw.chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dw.chart, 0, LV_PART_MAIN);
  lv_obj_set_style_line_opa(dw.chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_size(dw.chart, 0, LV_PART_INDICATOR);  // line only, no point markers
  lv_obj_set_style_line_width(dw.chart, 2, LV_PART_ITEMS);
  // The chart is wider than its point count, keeping LVGL out of its crowded
  // vertical-stroke fallback. Complex drawing then antialiases the diagonal
  // segments, while rounded joins remove sharp one-pixel corners.
  lv_obj_set_style_line_rounded(dw.chart, true, LV_PART_ITEMS);
  dw.tracePowerSeries = lv_chart_add_series(dw.chart, powerColor, LV_CHART_AXIS_PRIMARY_Y);
  dw.traceSpeedSeries = lv_chart_add_series(dw.chart, accent, LV_CHART_AXIS_PRIMARY_Y);
  traceRevealPoints = kTraceSamples;
  fillTraceChart();

  // Trace is the analytics theme, so the strip leads with the efficiency
  // figures; volts/motor/trip carry the numbers worth keeping an eye on.
  const BatteryStats stats = dashBatteryStats();
  lv_obj_t *rateCaption = makeLabel(scr, TRACE_RATE_LABEL, energyRateLabel(), labelLv());
  formatAvailableEnergyRate(text, sizeof(text), stats.tripWhPerKm, false);
  dw.energyRate = makeLabel(scr, TRACE_RATE, text, powerColor);
  lv_obj_t *rangeCaption = makeLabel(scr, TRACE_RANGE_LABEL, metricRangeLabel(), labelLv());
  formatAvailableRange(text, sizeof(text), stats.rangeKm);
  dw.range = makeLabel(scr, TRACE_RANGE, text, accent);
  lv_obj_t *voltsCaption = makeLabel(scr, TRACE_VOLTS_LABEL, metricVoltsLabel(), labelLv());
  dw.volts = makeLabel(scr, TRACE_VOLTS, t.voltageWithUnit, whiteLv());
  lv_obj_t *motorCaption = makeLabel(scr, TRACE_MOTOR_LABEL, metricMotorLabel(), labelLv());
  dw.motor = makeLabel(scr, TRACE_MOTOR_TEMP, t.motor, whiteLv());
  lv_obj_t *tripCaption = makeLabel(scr, TRACE_TRIP_LABEL, metricTripLabel(), labelLv());
  snprintf(text, sizeof(text), "%s %s", t.trip, distanceUnitLabel());
  dw.trip = makeLabel(scr, TRACE_TRIP, text, whiteLv());
  registerDataSlot(0, rateCaption, dw.energyRate);
  registerDataSlot(1, rangeCaption, dw.range);
  registerDataSlot(2, voltsCaption, dw.volts);
  registerDataSlot(3, motorCaption, dw.motor);
  registerDataSlot(4, tripCaption, dw.trip);

  setLabelText(dw.speed, t.speed);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  centerValueUnitOnBaseline(dw.power, dw.powerUnit, 70, 4, -3);
  if (dashHas(TELEMETRY_FIELD_SPEED)) sweepNumber(dw.speed, v.speedKmh, speedGaugeMax());
  // One way only: the reveal ends with the whole history on screen.
  sweepWith(dw.chart, traceRevealSweep, kTraceSamples, kTraceSamples);
}

static void updateTrace(const DashboardValues &v) {
  DashTexts t = fmtTexts(v);
  char text[24];
  setLabelText(dw.speed, t.speed);
  formatPowerValue(text, sizeof(text), v.watts);
  setLabelText(dw.power, text);
  setLabelText(dw.powerUnit, powerUnitLabel(v.watts));
  centerValueUnitOnBaseline(dw.power, dw.powerUnit, 70, 4, -3);
  const bool traceChanged = demoModeIsActive() ? recordDemoTraceSample() : traceSampleAdded;
  if (traceChanged) fillTraceChart();
  if (elecDue) {
    setLabelText(dw.volts, t.voltageWithUnit);
  }
  if (midDue) {
    setLabelText(dw.uptime, t.uptime);
    setLabelText(dw.motor, t.motor);
  }
  if (slowDue) {
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    snprintf(text, sizeof(text), "%s %s", t.trip, distanceUnitLabel());
    setLabelText(dw.trip, text);
    const BatteryStats stats = dashBatteryStats();
    formatAvailableEnergyRate(text, sizeof(text), stats.tripWhPerKm, false);
    setLabelText(dw.energyRate, text);
    formatAvailableRange(text, sizeof(text), stats.rangeKm);
    setLabelText(dw.range, text);
  }
}

// ── Minimal Ride ──────────────────────────────────────────────────────────────

static const Item MINIMAL_CENTER_SPEED = {160, 132, 0, 0, 0, 0, 0, 0, 0, 146, 96, 1, 0};
static const int MINIMAL_DIAL_CENTER_Y = 138;
static const int MINIMAL_RING_SEGMENTS = 32;
static const int MINIMAL_RING_BG_SEGMENTS = 12;
static const int MINIMAL_RING_TICK_INTERVALS = SPEED_SCALE_INTERVALS * 5;
static const int MINIMAL_RING_START = 140;
static const int MINIMAL_RING_SWEEP = 260;
static const int MINIMAL_RING_RADIUS = 104;
static lv_obj_t *minimalSpeedRingObj = NULL;
static int minimalSpeedRingShown = 0;  // where the ring and needle were last drawn, in kGlideScale ths

// Minimal and Efficiency draw the same kind of ring: arcs of a horseshoe, ticks and a needle. A point
// at `angle` degrees and `radius` from the ring's centre, the box that holds a stretch of ring between
// two angles, and whether a draw callback's clip reaches that box.
static lv_point_t ringPoint(const lv_point_t &center, int angle, int radius) {
  lv_point_t point;
  point.x = center.x + ((lv_trigo_sin(angle + 90) * radius) >> LV_TRIGO_SHIFT);
  point.y = center.y + ((lv_trigo_sin(angle) * radius) >> LV_TRIGO_SHIFT);
  return point;
}

// Includes the inner needle tip and the outer ring, with raster/round-cap margin.
// Angles are integral in the renderer; walking the same angles is conservative.
static lv_area_t ringSweepBounds(const lv_point_t &center, int radius, int start, int end) {
  lv_area_t area = {32767, 32767, -32768, -32768};
  for (int angle = start; angle <= end; ++angle) {
    for (int r : {radius * 62 / 104, radius}) {
      const lv_point_t p = ringPoint(center, angle, r);
      area.x1 = min(area.x1, p.x); area.x2 = max(area.x2, p.x);
      area.y1 = min(area.y1, p.y); area.y2 = max(area.y2, p.y);
    }
  }
  area.x1 -= 4; area.y1 -= 4; area.x2 += 4; area.y2 += 4;
  return area;
}

static bool ringClipTouches(lv_draw_ctx_t *ctx, const lv_area_t &area) {
  const lv_area_t &clip = *ctx->clip_area;
  return area.x1 <= clip.x2 && area.x2 >= clip.x1 && area.y1 <= clip.y2 && area.y2 >= clip.y1;
}

static lv_point_t minimalDialPoint(int angle, int radius) {
  while (angle >= 360) angle -= 360;
  while (angle < 0) angle += 360;
  lv_point_t point;
  point.x = 160 + ((lv_trigo_sin(angle + 90) * radius) >> LV_TRIGO_SHIFT);
  point.y = MINIMAL_DIAL_CENTER_Y + ((lv_trigo_sin(angle) * radius) >> LV_TRIGO_SHIFT);
  return point;
}

struct MinimalGradientPalette {
  uint32_t low;
  uint32_t neon;
  uint32_t hot;
  uint32_t max;
};

static MinimalGradientPalette minimalGradientPalette() {
  // These are dial palettes, not background colours. Every selectable theme
  // keeps a recognizable low-speed hue, blooms into a brighter neon partner,
  // then shares the same heat-language of amber and reddish orange near max.
  switch (accentTheme) {
    case ACCENT_ORANGE: return {0xFF5A00, 0xFFC000, 0xFFE600, 0xFF3000};
    case ACCENT_BLUE: return {0x0048FF, 0x00C8FF, 0xFFB300, 0xFF3A12};
    case ACCENT_GREEN: return {0x00A84F, 0x39FF14, 0xFFD000, 0xFF4A00};
    case ACCENT_PURPLE: return {0x6C35FF, 0xDA2CFF, 0xFFAD00, 0xFF3A16};
    case ACCENT_RED: return {0xFF1744, 0xFF5A00, 0xFFC400, 0xFF2400};
    case ACCENT_CYAN: return {0x00AACC, 0x00FFF0, 0xFFD000, 0xFF4800};
    case ACCENT_YELLOW: return {0xD6A800, 0xFFF600, 0xFF9D00, 0xFF3500};
    case ACCENT_WHITE: return {0x70BFFF, 0xF8FFFF, 0xFFBF00, 0xFF4215};
    case ACCENT_MAGENTA: return {0xB000FF, 0xFF19D1, 0xFF9F00, 0xFF3512};
    case ACCENT_DEFAULT:
    default: return {0x005CFF, 0x00D9FF, 0xFFB000, 0xFF3B0A};
  }
}

static lv_color_t minimalSpeedGradientColor(int percent, bool active) {
  percent = constrain(percent, 0, 100);
  const MinimalGradientPalette palette = minimalGradientPalette();
  uint32_t from;
  uint32_t to;
  int mix;
  if (percent <= 34) {
    from = palette.low;
    to = palette.neon;
    mix = percent * 100 / 34;
  } else if (percent <= 70) {
    from = palette.neon;
    to = palette.hot;
    mix = (percent - 34) * 100 / 36;
  } else {
    from = palette.hot;
    to = palette.max;
    mix = (percent - 70) * 100 / 30;
  }
  int r = (int)((from >> 16) & 0xFF) + ((int)((to >> 16) & 0xFF) - (int)((from >> 16) & 0xFF)) * mix / 100;
  int g = (int)((from >> 8) & 0xFF) + ((int)((to >> 8) & 0xFF) - (int)((from >> 8) & 0xFF)) * mix / 100;
  int b = (int)(from & 0xFF) + ((int)(to & 0xFF) - (int)(from & 0xFF)) * mix / 100;
  if (!active) {
    // The remaining scale is neutral instrument furniture; keeping theme hue
    // out of it makes the live, coloured sweep read much more immediately.
    const int grey = 31 + percent * 7 / 100;
    r = grey;
    g = grey + 1;
    b = grey + 2;
  }
  return dashboardColorLv(lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b));
}

static void drawMinimalArcSegment(lv_draw_ctx_t *ctx, lv_draw_arc_dsc_t *dsc, const lv_point_t *center,
                                  int radius, int start, int end) {
  if (start < 360 && end > 360) {
    lv_draw_arc(ctx, dsc, center, radius, start, 360);
    lv_draw_arc(ctx, dsc, center, radius, 0, end - 360);
  } else {
    if (start >= 360) {
      start -= 360;
      end -= 360;
    }
    lv_draw_arc(ctx, dsc, center, radius, start, end);
  }
}

static void minimalSpeedRingDrawCb(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  const lv_point_t center = {160, MINIMAL_DIAL_CENTER_Y};
  const int position = constrain(minimalSpeedRingShown, 0, kGlideScale);
  const int activePercent = position * 100 / kGlideScale;
  const int activeAngle = MINIMAL_RING_START + position * MINIMAL_RING_SWEEP / kGlideScale;
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.opa = LV_OPA_COVER;
  dsc.rounded = false;

  // First paint the complete subdued scale.
  for (int i = 0; i < MINIMAL_RING_BG_SEGMENTS; i++) {
    const int percent = (i * 100 + 50) / MINIMAL_RING_BG_SEGMENTS;
    dsc.color = minimalSpeedGradientColor(percent, false);
    dsc.width = 10;
    const int start = MINIMAL_RING_START + i * MINIMAL_RING_SWEEP / MINIMAL_RING_BG_SEGMENTS;
    const int end = MINIMAL_RING_START + (i + 1) * MINIMAL_RING_SWEEP / MINIMAL_RING_BG_SEGMENTS + 1;
    // A repaint rarely reaches most of the ring, and an arc costs the same wherever the clip is.
    if (!ringClipTouches(ctx, ringSweepBounds(center, MINIMAL_RING_RADIUS, start, end))) continue;
    drawMinimalArcSegment(ctx, &dsc, &center, MINIMAL_RING_RADIUS, start, end);
  }

  // Then overlay the live colour up to the exact same angle used by the
  // needle. Full segments overlap by one degree to hide antialiasing seams;
  // the final partial segment stops exactly at activeAngle, keeping both
  // moving elements locked together on the same frame and pixel boundary.
  dsc.width = 13;
  for (int i = 0; i < MINIMAL_RING_SEGMENTS; i++) {
    const int start = MINIMAL_RING_START + i * MINIMAL_RING_SWEEP / MINIMAL_RING_SEGMENTS;
    if (start >= activeAngle) break;
    const int nominalEnd = MINIMAL_RING_START + (i + 1) * MINIMAL_RING_SWEEP / MINIMAL_RING_SEGMENTS;
    const int end = min(nominalEnd, activeAngle);
    const int percent = (i * 100 + 50) / MINIMAL_RING_SEGMENTS;
    if (!ringClipTouches(ctx, ringSweepBounds(center, MINIMAL_RING_RADIUS, start, end + 1))) continue;
    dsc.color = minimalSpeedGradientColor(percent, true);
    drawMinimalArcSegment(ctx, &dsc, &center, MINIMAL_RING_RADIUS, start, end < activeAngle ? end + 1 : end);
  }

  // Five minor divisions per numbered interval keep the same visual density
  // at every configured top speed. Every fifth tick is a major and therefore
  // lines up exactly with one of the six numeric labels.
  lv_draw_line_dsc_t tick;
  lv_draw_line_dsc_init(&tick);
  tick.round_start = false;
  tick.round_end = false;
  for (int tickIndex = 0; tickIndex <= MINIMAL_RING_TICK_INTERVALS; tickIndex++) {
    const int percent = tickIndex * 100 / MINIMAL_RING_TICK_INTERVALS;
    const int angle = MINIMAL_RING_START + tickIndex * MINIMAL_RING_SWEEP / MINIMAL_RING_TICK_INTERVALS;
    const bool major = (tickIndex % 5) == 0;
    const bool active = tickIndex * kGlideScale <= position * MINIMAL_RING_TICK_INTERVALS;
    tick.color = minimalSpeedGradientColor(percent, active);
    tick.width = major ? 2 : 1;
    lv_point_t inner = minimalDialPoint(angle, major ? 80 : 86);
    lv_point_t outer = minimalDialPoint(angle, (tickIndex == 0 || tickIndex == MINIMAL_RING_TICK_INTERVALS) ? 104 : 96);
    lv_draw_line(ctx, &tick, &inner, &outer);
  }

  // A short outer needle supplies an exact directional cue without ever
  // crossing the large digital speed value in the centre.
  const int needleAngle = activeAngle;
  lv_point_t needleInner = minimalDialPoint(needleAngle, 64);
  lv_point_t needleOuter = minimalDialPoint(needleAngle, 104);
  lv_draw_line_dsc_t needle;
  lv_draw_line_dsc_init(&needle);
  needle.color = minimalSpeedGradientColor(activePercent, true);
  needle.width = 6;
  needle.round_start = true;
  needle.round_end = false;
  lv_draw_line(ctx, &needle, &needleInner, &needleOuter);
  needle.color = dashWhite();
  needle.width = 3;
  lv_draw_line(ctx, &needle, &needleInner, &needleOuter);
}

// The glide's place callback. Only what lies between the old and the new needle changes (the lit arc and
// ticks, and the needle, whose colour follows the percentage), so only that stretch of ring is repainted.
static int minimalRingAngle(int position) { return MINIMAL_RING_START + position * MINIMAL_RING_SWEEP / kGlideScale; }
static int minimalRingTicksLit(int position) { return position * MINIMAL_RING_TICK_INTERVALS / kGlideScale; }

static void minimalSpeedPlace(void *, int position) {
  position = constrain(position, 0, kGlideScale);
  const int old = minimalSpeedRingShown;
  if (position == old) return;
  minimalSpeedRingShown = position;
  if (!minimalSpeedRingObj) return;
  const int oldAngle = minimalRingAngle(old);
  const int newAngle = minimalRingAngle(position);
  if (oldAngle == newAngle && old * 100 / kGlideScale == position * 100 / kGlideScale &&
      minimalRingTicksLit(old) == minimalRingTicksLit(position))
    return;
  const lv_point_t center = {160, MINIMAL_DIAL_CENTER_Y};
  // A tick lights when the needle reaches it, which can be a hair before the whole degree changes.
  const lv_area_t damage = ringSweepBounds(center, MINIMAL_RING_RADIUS, min(oldAngle, newAngle) - 1,
                                           max(oldAngle, newAngle) + 2);
  lv_obj_invalidate_area(minimalSpeedRingObj, &damage);
}

static void minimalSpeedRingDeleteCb(lv_event_t *) {
  minimalSpeedRingObj = NULL;
}

static lv_obj_t *makeMinimalCornerMetric(lv_obj_t *scr, int x, int y, bool right, CydIconId icon,
                                         const char *caption, const char *value, lv_color_t accent,
                                         lv_obj_t **captionOut, lv_obj_t **iconOut) {
  const int width = 62;
  // The widest caption in any language ("AUTONOMÍA", 59 px) must fit its box: LVGL draws the part of a
  // glyph that overhangs a label in a full repaint but not in a partial one, so a caption that
  // overhangs changes shape whenever something beside it is redrawn. The right-hand one keeps its
  // right edge, so the box grows to the left.
  const int captionWidth = 60;
  lv_obj_t *iconObj = makeIcon(scr, right ? x + width - 16 : x, y, icon, accent);
  if (iconOut) *iconOut = iconObj;
  lv_obj_t *captionLabel = makeLabelAt(scr, right ? x + width - 20 - captionWidth : x + 20, y + 1, caption, labelLv(), F1, 0);
  lv_obj_set_width(captionLabel, captionWidth);
  lv_obj_set_style_text_align(captionLabel, right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(captionLabel, LV_LABEL_LONG_CLIP);
  if (captionOut) *captionOut = captionLabel;

  lv_obj_t *valueLabel = makeLabelAt(scr, x, y + 18, value, whiteLv(), F3, 0);
  lv_obj_set_width(valueLabel, width);
  lv_obj_set_style_text_align(valueLabel, right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_CLIP);
  return valueLabel;
}

static void minimalSpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setScaledValueText(dw.speed, MINIMAL_CENTER_SPEED, text, &lv_font_speed96);
  glideAim(glides[0], glidePosition(value, speedGaugeMax()), false);
}

static void buildMinimal(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t accent = accentLv();
  const DashTexts t = fmtTexts(v);
  const BatteryStats stats = dashBatteryStats();
  char rangeText[20];
  char powerText[20];
  char tripText[20];
  char odoText[20];

  // A single custom-drawn horseshoe keeps the gradient smooth without the
  // heap cost of dozens of separate LVGL arc widgets.
  minimalSpeedRingShown = glidePosition(v.speedKmh, speedGaugeMax());
  minimalSpeedRingObj = lv_obj_create(scr);
  lv_obj_remove_style_all(minimalSpeedRingObj);
  lv_obj_clear_flag(minimalSpeedRingObj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(minimalSpeedRingObj, 50, 33);
  lv_obj_set_size(minimalSpeedRingObj, 220, 220);
  lv_obj_add_event_cb(minimalSpeedRingObj, minimalSpeedRingDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(minimalSpeedRingObj, minimalSpeedRingDeleteCb, LV_EVENT_DELETE, NULL);
  glideAdd(minimalSpeedPlace, NULL);
  glideAim(glides[0], minimalSpeedRingShown, false);

  // Sparse proportional labels retain the analog-dial character while the
  // large central number remains the primary way to read exact speed.
  for (int mark = 0; mark <= SPEED_SCALE_INTERVALS; mark++) {
    const int angle = MINIMAL_RING_START + mark * MINIMAL_RING_SWEEP / SPEED_SCALE_INTERVALS;
    const lv_point_t pos = minimalDialPoint(angle, 70);
    char scaleText[10];
    formatSpeedScaleMark(mark, scaleText, sizeof(scaleText));
    lv_obj_t *scale = makeLabelAt(scr, pos.x, pos.y, scaleText, labelLv(), F1, 1);
    dw.scaleLabels[mark] = scale;
    lv_obj_set_width(scale, 28);
    lv_obj_set_x(scale, pos.x - 14);
  }

  dw.speed = makeLabelFont(scr, MINIMAL_CENTER_SPEED, t.speed, whiteLv(), &lv_font_speed96);
  setScaledValueText(dw.speed, MINIMAL_CENTER_SPEED, t.speed, &lv_font_speed96);
  dw.speedUnit = makeLabelAt(scr, 160, 170, speedUnitLabel(), accent, F3B, 3);

  // Four common ride metrics occupy the otherwise unused outer corners.
  // They deliberately have no surrounding tiles or secondary visualisation,
  // and each one is registered with the per-theme Data customizer.
  formatAvailableRange(rangeText, sizeof(rangeText), stats.rangeKm, true);
  formatPowerWithUnit(powerText, sizeof(powerText), v.watts);
  snprintf(tripText, sizeof(tripText), "%s %s", t.trip, distanceUnitLabel());
  snprintf(odoText, sizeof(odoText), "%s %s", t.odo, distanceUnitLabel());
  lv_obj_t *rangeCaption = NULL;
  lv_obj_t *tripCaption = NULL;
  lv_obj_t *odoCaption = NULL;
  lv_obj_t *rangeIcon = NULL;
  lv_obj_t *tripIcon = NULL;
  lv_obj_t *odoIcon = NULL;
  dw.range = makeMinimalCornerMetric(scr, 3, 6, false, CYD_ICON_NAV, dashboardDataLabel(DATA_RANGE),
                                     rangeText, accent, &rangeCaption, &rangeIcon);
  dw.battPct = makeTopBatteryLabel(scr, HUD_TOP_BATT_PCT, t.battPct, whiteLv());
  const Item minimalBatteryIcon = HUD_TOP_BATT_ICON;
  dw.segBatt = makeSegBattery(scr, minimalBatteryIcon, 5);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  lv_obj_t *oem = makeLabelAt(scr, 160, 6, dashOemName(), labelLv(), F2, 3);
  lv_obj_set_width(oem, 150);
  lv_obj_set_x(oem, 85);
  lv_obj_set_style_text_align(oem, LV_TEXT_ALIGN_CENTER, 0);
  dw.power = makeLabelAt(scr, 160, 220, powerText, whiteLv(), F3, 3);
  dw.trip = makeMinimalCornerMetric(scr, 1, 201, false, CYD_ICON_TRIP, dashboardDataLabel(DATA_TRIP),
                                    tripText, accent, &tripCaption, &tripIcon);
  dw.odo = makeMinimalCornerMetric(scr, 257, 201, true, CYD_ICON_ODO, dashboardDataLabel(DATA_ODOMETER),
                                   odoText, accent, &odoCaption, &odoIcon);
  registerDataSlot(0, rangeCaption, dw.range, NULL, rangeIcon);
  registerDataSlot(1, NULL, dw.power);
  registerDataSlot(2, tripCaption, dw.trip, NULL, tripIcon);
  registerDataSlot(3, odoCaption, dw.odo, NULL, odoIcon);
  sweepSpeedWith(dw.speed, minimalSpeedSweep, v);
}

static void updateMinimal(const DashboardValues &v) {
  char text[24];
  formatSpeedValue(text, sizeof(text), v.speedKmh);
  setScaledValueText(dw.speed, MINIMAL_CENTER_SPEED, text, &lv_font_speed96);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()));
  formatPowerWithUnit(text, sizeof(text), v.watts);
  setLabelText(dw.power, text);
  if (slowDue) {
    // The shared text shows "-" when the controller reports no charge, the
    // way this theme's own build and every other theme do.
    const DashTexts t = fmtTexts(v);
    setLabelText(dw.battPct, t.battPct);
    setSegBatteryLevel(dw.segBatt, v.batteryPercent);
    const BatteryStats stats = dashBatteryStats();
    formatAvailableRange(text, sizeof(text), stats.rangeKm, true);
    setLabelText(dw.range, text);
    snprintf(text, sizeof(text), "%s %s", t.trip, distanceUnitLabel());
    setLabelText(dw.trip, text);
    snprintf(text, sizeof(text), "%s %s", t.odo, distanceUnitLabel());
    setLabelText(dw.odo, text);
  }
}

// ── Efficiency ───────────────────────────────────────────────────────────────
// Two independent segmented horseshoes keep the exact visual language of the
// selected concept without allocating one LVGL object per segment. Speed is the
// dominant left-hand instrument; the efficiency dial at right is 35% smaller.
// The rolling plot is a second custom object, so its one-second updates never
// repaint the larger dial area. Its 300 columns map directly to 300 one-second
// samples: one physical pixel per second for a five-minute window.
static constexpr int EFFICIENCY_SCALE_MAX_X10 = 400;  // 40.0 Wh/km
static constexpr int EFFICIENCY_DIAL_SEGMENTS = 32;
static constexpr int EFFICIENCY_DIAL_BG_SEGMENTS = 12;
static constexpr int EFFICIENCY_DIAL_START = MINIMAL_RING_START;
static constexpr int EFFICIENCY_DIAL_SWEEP = MINIMAL_RING_SWEEP;
static constexpr int EFFICIENCY_HISTORY_SAMPLES = 300;
static constexpr uint32_t EFFICIENCY_HISTORY_INTERVAL_MS = 1000U;
// Plot window, in screen coordinates: the baseline row and the height one full
// scale reaches above it.
static constexpr int EFFICIENCY_PLOT_X0 = 20;
static constexpr int EFFICIENCY_PLOT_W = 300;
static constexpr int EFFICIENCY_PLOT_BASE = 197;
static constexpr int EFFICIENCY_PLOT_H = 34;
static lv_obj_t *efficiencyDialObj = NULL;
static lv_obj_t *efficiencyGraphObj = NULL;
static int efficiencyDialSpeed = 0;  // where the speed dial is drawn, in kGlideScale ths
// Live consumption, lightly damped — not the ride average, which is already
// printed inside this dial and again in the RIDE AVG tile below it.
static int efficiencyDialRateX10 = 0;
// Full scale for that needle. Twice the rider's own long-run consumption, so
// the needle rests at the centre when this ride matches their usual
// efficiency, swings left when it is beating it and right when it is not. An
// unlabelled dial needs a landmark, and this one calibrates itself per vehicle
// without another setting. Resolved once per build so the scale cannot drift
// under the needle mid-ride.
static int efficiencyDialScaleX10 = EFFICIENCY_SCALE_MAX_X10;
static uint16_t efficiencyLiveHistory[EFFICIENCY_HISTORY_SAMPLES] = {};
static uint16_t efficiencyHistoryCount = 0;
static uint32_t efficiencyHistoryLastMs = 0;
static uint32_t efficiencyNeedleLastMs = 0;
static float efficiencyFilteredRateX10 = 0;

struct EfficiencyDialGeometry {
  lv_point_t center;
  int radius;
  lv_area_t bounds;
  lv_area_t background[EFFICIENCY_DIAL_BG_SEGMENTS];
  lv_area_t active[EFFICIENCY_DIAL_SEGMENTS];
  lv_color_t backgroundColors[EFFICIENCY_DIAL_BG_SEGMENTS];
  lv_color_t activeColors[EFFICIENCY_DIAL_SEGMENTS];
};
static EfficiencyDialGeometry efficiencyDialGeometry[2];

static lv_color_t efficiencyScaleColor(int percent) {
  percent = constrain(percent, 0, 100);
  const uint32_t low = 0x00E83A;
  const uint32_t middle = 0xFFD000;
  const uint32_t high = 0xFF0800;
  const uint32_t from = percent < 45 ? low : middle;
  const uint32_t to = percent < 45 ? middle : high;
  // Reach full red by 85% so narrow high peaks do not remain orange merely
  // because the neighbouring samples are lower.
  const int mix = percent < 45 ? percent * 100 / 45 : min(100, (percent - 45) * 100 / 40);
  const int r = (int)((from >> 16) & 0xFF) + ((int)((to >> 16) & 0xFF) - (int)((from >> 16) & 0xFF)) * mix / 100;
  const int g = (int)((from >> 8) & 0xFF) + ((int)((to >> 8) & 0xFF) - (int)((from >> 8) & 0xFF)) * mix / 100;
  const int b = (int)(from & 0xFF) + ((int)(to & 0xFF) - (int)(from & 0xFF)) * mix / 100;
  return dashboardColorLv(lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b));
}

static lv_color_t efficiencyConsumptionDialColor(int percent, bool active) {
  lv_color_t color = efficiencyScaleColor(percent);
  if (active) return color;
  const uint8_t grey = 31 + constrain(percent, 0, 100) * 7 / 100;
  return dashboardColorLv(lv_color_make(grey, grey + 1, grey + 2));
}

static lv_color_t efficiencyDialColor(bool consumption, int percent, bool active) {
  return consumption ? efficiencyConsumptionDialColor(percent, active)
                     : minimalSpeedGradientColor(percent, active);
}

static void prepareEfficiencyDialGeometry() {
  for (int dial = 0; dial < 2; ++dial) {
    auto &g = efficiencyDialGeometry[dial];
    g.center = dial ? lv_point_t{245, 99} : lv_point_t{94, 97};
    g.radius = dial ? 50 : 67;
    g.bounds = ringSweepBounds(g.center, g.radius, EFFICIENCY_DIAL_START, EFFICIENCY_DIAL_START + EFFICIENCY_DIAL_SWEEP + 1);
    for (int i = 0; i < EFFICIENCY_DIAL_BG_SEGMENTS; ++i) {
      const int start = EFFICIENCY_DIAL_START + i * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_BG_SEGMENTS;
      const int end = EFFICIENCY_DIAL_START + (i + 1) * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_BG_SEGMENTS + 1;
      g.background[i] = ringSweepBounds(g.center, g.radius, start, end);
      g.backgroundColors[i] = efficiencyDialColor(dial != 0, (i * 100 + 50) / EFFICIENCY_DIAL_BG_SEGMENTS, false);
    }
    for (int i = 0; i < EFFICIENCY_DIAL_SEGMENTS; ++i) {
      const int start = EFFICIENCY_DIAL_START + i * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_SEGMENTS;
      const int end = EFFICIENCY_DIAL_START + (i + 1) * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_SEGMENTS + 1;
      g.active[i] = ringSweepBounds(g.center, g.radius, start, end);
      g.activeColors[i] = efficiencyDialColor(dial != 0, (i * 100 + 50) / EFFICIENCY_DIAL_SEGMENTS, true);
    }
  }
}

static void invalidateEfficiencyDialChange(int dial, int oldValue, int newValue, int maximum) {
  maximum = max(1, maximum);
  oldValue = constrain(oldValue, 0, maximum); newValue = constrain(newValue, 0, maximum);
  const int oldAngle = EFFICIENCY_DIAL_START + oldValue * EFFICIENCY_DIAL_SWEEP / maximum;
  const int newAngle = EFFICIENCY_DIAL_START + newValue * EFFICIENCY_DIAL_SWEEP / maximum;
  // Needle color also depends on percent, even when its angle is unchanged.
  if (oldAngle == newAngle && oldValue * 100 / maximum == newValue * 100 / maximum) return;
  const auto &g = efficiencyDialGeometry[dial];
  const lv_area_t damage = ringSweepBounds(g.center, g.radius, min(oldAngle, newAngle), max(oldAngle, newAngle) + 1);
  if (efficiencyDialObj) lv_obj_invalidate_area(efficiencyDialObj, &damage);
}

static void drawEfficiencyMinimalDial(lv_draw_ctx_t *ctx, int dial, int value, int maximum) {
  const auto &geometry = efficiencyDialGeometry[dial];
  if (!ringClipTouches(ctx, geometry.bounds)) return;
  const lv_point_t &center = geometry.center;
  const int radius = geometry.radius;
  const bool consumption = dial != 0;
  maximum = max(1, maximum);
  value = constrain(value, 0, maximum);
  const int activeAngle = EFFICIENCY_DIAL_START + value * EFFICIENCY_DIAL_SWEEP / maximum;
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.opa = LV_OPA_COVER;
  arc.rounded = false;

  // Preserve Gauge's physical stroke widths even on the smaller dials.
  arc.width = 10;
  for (int i = 0; i < EFFICIENCY_DIAL_BG_SEGMENTS; i++) {
    if (!ringClipTouches(ctx, geometry.background[i])) continue;
    arc.color = geometry.backgroundColors[i];
    const int start = EFFICIENCY_DIAL_START + i * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_BG_SEGMENTS;
    const int end = EFFICIENCY_DIAL_START + (i + 1) * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_BG_SEGMENTS + 1;
    drawMinimalArcSegment(ctx, &arc, &center, radius, start, end);
  }

  arc.width = 13;
  for (int i = 0; i < EFFICIENCY_DIAL_SEGMENTS; i++) {
    const int start = EFFICIENCY_DIAL_START + i * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_SEGMENTS;
    if (start >= activeAngle) break;
    const int nominalEnd = EFFICIENCY_DIAL_START + (i + 1) * EFFICIENCY_DIAL_SWEEP / EFFICIENCY_DIAL_SEGMENTS;
    const int end = min(nominalEnd, activeAngle);
    if (!ringClipTouches(ctx, geometry.active[i])) continue;
    arc.color = geometry.activeColors[i];
    drawMinimalArcSegment(ctx, &arc, &center, radius, start, end < activeAngle ? end + 1 : end);
  }

  const int percent = value * 100 / maximum;
  lv_point_t inner = ringPoint(center, activeAngle, radius * 62 / 104);
  lv_point_t outer = ringPoint(center, activeAngle, radius);
  lv_draw_line_dsc_t needle;
  lv_draw_line_dsc_init(&needle);
  needle.color = efficiencyDialColor(consumption, percent, true);
  needle.width = max(4, radius * 6 / 104);
  needle.round_start = true;
  needle.round_end = false;
  lv_draw_line(ctx, &needle, &inner, &outer);
  needle.color = dashWhite();
  needle.width = max(2, radius * 3 / 104);
  lv_draw_line(ctx, &needle, &inner, &outer);
}

static void efficiencyDialDrawCb(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  drawEfficiencyMinimalDial(ctx, 0, efficiencyDialSpeed, kGlideScale);
  drawEfficiencyMinimalDial(ctx, 1, efficiencyDialRateX10, efficiencyDialScaleX10);
}

// The plot is scaled to the ride it is actually showing: the floor is the
// quietest the last five minutes got and the top is that window's peak, so a
// steady cruise still fills the window with its own detail instead of hugging a
// fixed zero. The scale labels carry the real Wh/km figures, so nothing about
// the reading is implied by the height alone.
struct EfficiencyWindow {
  int floorX10;
  int topX10;
  int span;  // never zero, so a perfectly flat window still maps
};

static EfficiencyWindow efficiencyCachedWindow = {0, 0, 1};
static uint8_t efficiencyCachedHeights[EFFICIENCY_PLOT_W] = {};
static lv_color_t efficiencyRowColors[EFFICIENCY_PLOT_H + 1];

// A centered 1-2-1 interpolation softens isolated one-second steps while
// preserving one history entry per physical x column and keeping peaks close
// to their original time and magnitude.
static int interpolatedEfficiencySample(int index) {
  if (efficiencyHistoryCount == 0) return 0;
  index = constrain(index, 0, (int)efficiencyHistoryCount - 1);
  const int previous = efficiencyLiveHistory[max(0, index - 1)];
  const int current = efficiencyLiveHistory[index];
  const int next = efficiencyLiveHistory[min((int)efficiencyHistoryCount - 1, index + 1)];
  return (previous + current * 2 + next + 2) / 4;
}

static EfficiencyWindow efficiencyWindow() {
  if (efficiencyHistoryCount == 0) return EfficiencyWindow{0, 0, 1};
  int lowest = interpolatedEfficiencySample(0);
  int highest = lowest;
  for (int i = 1; i < efficiencyHistoryCount; i++) {
    const int value = interpolatedEfficiencySample(i);
    lowest = min(lowest, value);
    highest = max(highest, value);
  }
  return EfficiencyWindow{lowest, highest, max(1, highest - lowest)};
}

static int efficiencyPlotHeight(int consumptionX10, const EfficiencyWindow &window) {
  return constrain((consumptionX10 - window.floorX10) * EFFICIENCY_PLOT_H / window.span, 0,
                   EFFICIENCY_PLOT_H);
}

static void prepareEfficiencyGraph() {
  efficiencyCachedWindow = efficiencyWindow();
  for (int i = 0; i < efficiencyHistoryCount; ++i)
    efficiencyCachedHeights[i] = (uint8_t)efficiencyPlotHeight(interpolatedEfficiencySample(i), efficiencyCachedWindow);
  for (int level = 0; level <= EFFICIENCY_PLOT_H; ++level) {
    const int value = efficiencyCachedWindow.floorX10 + level * efficiencyCachedWindow.span / EFFICIENCY_PLOT_H;
    efficiencyRowColors[level] = efficiencyScaleColor(value * 100 / EFFICIENCY_SCALE_MAX_X10);
  }
}

static void efficiencyGraphDrawCb(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  const lv_area_t plot = {EFFICIENCY_PLOT_X0 - 1, EFFICIENCY_PLOT_BASE - EFFICIENCY_PLOT_H,
                          EFFICIENCY_PLOT_X0 + EFFICIENCY_PLOT_W - 1, EFFICIENCY_PLOT_BASE};
  if (!ringClipTouches(ctx, plot)) return;
  const auto &clip = *ctx->clip_area;
  lv_draw_line_dsc_t grid;
  lv_draw_line_dsc_init(&grid);
  grid.color = dashboardColorLv(lv_color_hex(0x3A3D40));
  grid.width = 1;
  grid.opa = LV_OPA_50;
  for (int level = 0; level <= 2; level++) {
    const int y = EFFICIENCY_PLOT_BASE - EFFICIENCY_PLOT_H + level * EFFICIENCY_PLOT_H / 2;
    if (y < clip.y1 || y > clip.y2) continue;
    for (int x = EFFICIENCY_PLOT_X0 - 1; x < EFFICIENCY_PLOT_X0 + EFFICIENCY_PLOT_W + 1; x += 18) {
      lv_point_t from = {(lv_coord_t)x, (lv_coord_t)y};
      lv_point_t to = {(lv_coord_t)min(x + 9, EFFICIENCY_PLOT_X0 + EFFICIENCY_PLOT_W - 1),
                       (lv_coord_t)y};
      lv_draw_line(ctx, &grid, &from, &to);
    }
  }

  // Each lightly interpolated history entry still owns exactly one screen
  // column. Fill the bars one horizontal row at a time so their gradient runs
  // vertically. Each row converts its real Wh/km value through the absolute
  // 0..40 Wh/km colour ramp. The dial above is scaled to the rider instead, so
  // the plot stays the one thing here that reads the same on every ride.
  lv_draw_rect_dsc_t fill;
  lv_draw_rect_dsc_init(&fill);
  fill.bg_opa = LV_OPA_COVER;
  fill.radius = 0;
  const int firstColumn = max(0, (int)clip.x1 - EFFICIENCY_PLOT_X0);
  const int lastColumn = min((int)efficiencyHistoryCount - 1, (int)clip.x2 - EFFICIENCY_PLOT_X0);
  for (int level = max(0, EFFICIENCY_PLOT_BASE - (int)clip.y2);
       level <= min(EFFICIENCY_PLOT_H, EFFICIENCY_PLOT_BASE - (int)clip.y1); level++) {
    fill.bg_color = efficiencyRowColors[level];
    const lv_coord_t y = (lv_coord_t)(EFFICIENCY_PLOT_BASE - level);
    int runStart = -1;
    for (int i = firstColumn; i <= lastColumn + 1; i++) {
      const bool covered = i <= lastColumn && efficiencyCachedHeights[i] >= level;
      if (covered && runStart < 0) runStart = i;
      if (covered || runStart < 0) continue;
      lv_area_t span = {(lv_coord_t)(EFFICIENCY_PLOT_X0 + runStart), y,
                        (lv_coord_t)(EFFICIENCY_PLOT_X0 + i - 1), y};
      lv_draw_rect(ctx, &fill, &span);
      runStart = -1;
    }
  }
}

static void efficiencyDialDeleteCb(lv_event_t *) { efficiencyDialObj = NULL; }
static void efficiencyGraphDeleteCb(lv_event_t *) { efficiencyGraphObj = NULL; }

static int currentEfficiencyX10(const DashboardValues &v, const BatteryStats &stats) {
  if (v.speedKmh >= 3 && v.watts > 0) {
    return constrain((int)lroundf((float)v.watts * 10.0F / v.speedKmh), 0, automaticGaugeRanges ? displayGaugeMaximum(RANGE_EFFICIENCY) * 10 : EFFICIENCY_SCALE_MAX_X10);
  }
  const float fallback = stats.tripWhPerKm > 0.0F ? stats.tripWhPerKm : stats.lifetimeWhPerKm;
  return constrain((int)lroundf(fallback * 10.0F), 0, EFFICIENCY_SCALE_MAX_X10);
}

static int efficiencyBaselineScaleX10(const BatteryStats &stats) {
  float baseline = stats.lifetimeWhPerKm;
  if (baseline <= 0.0F) baseline = stats.tripWhPerKm;
  if (baseline <= 0.0F) baseline = 20.0F;  // nothing learned from this vehicle yet
  return constrain((int)lroundf(baseline * 20.0F), 60, EFFICIENCY_SCALE_MAX_X10 * 2);
}

static void resetEfficiencyHistory() {
  memset(efficiencyLiveHistory, 0, sizeof(efficiencyLiveHistory));
  efficiencyHistoryCount = 0;
  efficiencyHistoryLastMs = millis();
}

// Sample actual simulated seconds, including skipped seconds at high compression.
// Never invent history from before this ride began.
static void seedEfficiencyHistoryUntil(const BatteryStats &stats, unsigned seconds) {
  const float fallback = stats.tripWhPerKm > 0.0F ? stats.tripWhPerKm : stats.lifetimeWhPerKm;
  efficiencyHistoryCount = min((unsigned)EFFICIENCY_HISTORY_SAMPLES, seconds);
  for (int i = 0; i < efficiencyHistoryCount; ++i) {
    const DemoRide ride = demoRideAt(seconds - efficiencyHistoryCount + i + 1, false);
    const float rate = ride.speedKmh >= 3 && ride.watts > 0 ? ride.watts / ride.speedKmh : fallback;
    efficiencyLiveHistory[i] = (uint16_t)constrain((int)lroundf(rate * 10), 0, EFFICIENCY_SCALE_MAX_X10);
  }
  efficiencyHistoryLastMs = seconds * 1000U;
}

static void seedEfficiencyHistory(const BatteryStats &stats) {
  seedEfficiencyHistoryUntil(stats, (unsigned)demoRideSeconds());
}

// The three gridlines are the window's peak, its floor, and the midpoint
// between them — so each label always names the reading at its own row.
static void updateEfficiencyGraphStatLabels() {
  if (efficiencyHistoryCount == 0) {
    for (int i = 0; i < 3; i++) {
      if (dw.aux[3 + i]) setLabelText(dw.aux[3 + i], "");
    }
    return;
  }
  const EfficiencyWindow &window = efficiencyCachedWindow;
  const int rows[3] = {window.topX10, (window.floorX10 + window.topX10) / 2, window.floorX10};
  char text[12];
  for (int i = 0; i < 3; i++) {
    lv_obj_t *label = dw.aux[3 + i];
    if (!label) continue;
    snprintf(text, sizeof(text), "%d", (rows[i] + 5) / 10);
    setLabelText(label, text);
  }
}

static void pushEfficiencyHistory(int valueX10) {
  if (efficiencyHistoryCount < EFFICIENCY_HISTORY_SAMPLES) {
    efficiencyLiveHistory[efficiencyHistoryCount++] = (uint16_t)valueX10;
  } else {
    for (int i = 0; i < EFFICIENCY_HISTORY_SAMPLES - 1; i++) {
      efficiencyLiveHistory[i] = efficiencyLiveHistory[i + 1];
    }
    efficiencyLiveHistory[EFFICIENCY_HISTORY_SAMPLES - 1] = (uint16_t)valueX10;
  }
  prepareEfficiencyGraph();
  updateEfficiencyGraphStatLabels();
  if (efficiencyGraphObj) lv_obj_invalidate(efficiencyGraphObj);
}

#ifdef CYD_LVGL_PREVIEW
// Native regression hooks: test both a real empty ride and the seeded preview.
void previewResetEfficiencyHistory() {
  resetEfficiencyHistory();
  prepareEfficiencyGraph();
  updateEfficiencyGraphStatLabels();
  if (efficiencyGraphObj) lv_obj_invalidate(efficiencyGraphObj);
}
// The still previews are not in demo mode, so the ride clock is idle and the
// graph would be empty. Fill it from the demo ride's first ten minutes, which
// matches the preview's 00:10:00 ride timer.
void previewSeedEfficiency() {
  seedEfficiencyHistoryUntil(dashBatteryStats(), 600);
  prepareEfficiencyGraph();
  updateEfficiencyGraphStatLabels();
  if (efficiencyGraphObj) lv_obj_invalidate(efficiencyGraphObj);
}
int previewEfficiencyNeedleRateX10() { return efficiencyDialRateX10; }
int previewEfficiencyHistoryCount() { return efficiencyHistoryCount; }
#endif

static lv_obj_t *makeEfficiencyFooterMetric(lv_obj_t *scr, int centerX, const char *caption, const char *value,
                                            lv_obj_t **captionOut) {
  lv_obj_t *captionLabel = makeLabelAt(scr, centerX, 211, caption, labelLv(), F1, 1);
  lv_obj_set_width(captionLabel, 78);
  lv_obj_set_x(captionLabel, centerX - 39);
  lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
  if (captionOut) *captionOut = captionLabel;
  lv_obj_t *valueLabel = makeLabelAt(scr, centerX, 228, value, whiteLv(), F2, 1);
  lv_obj_set_width(valueLabel, 78);
  lv_obj_set_x(valueLabel, centerX - 39);
  lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_CLIP);
  return valueLabel;
}

static void efficiencySpeedPlace(void *, int position) {
  invalidateEfficiencyDialChange(0, efficiencyDialSpeed, position, kGlideScale);
  efficiencyDialSpeed = position;
}

static void efficiencySpeedSweep(int value) {
  char text[16];
  formatSpeedValue(text, sizeof(text), value);
  setLabelText(dw.speed, text);
  glideAim(glides[0], glidePosition(value, speedGaugeMax()), false);
}

static void efficiencyRateSweep(int value) {
  invalidateEfficiencyDialChange(1, efficiencyDialRateX10, value, efficiencyDialScaleX10);
  efficiencyDialRateX10 = value;
}

static void buildEfficiency(lv_obj_t *scr, const DashboardValues &v) {
  const lv_color_t accent = accentLv();
  const lv_color_t topColor = themeColor(0xFFFF);
  const DashTexts t = fmtTexts(v);
  const BatteryStats stats = dashBatteryStats();
  char text[32];

  // Match the compact shared ride-status bar used by Gauge and Motor Data, leaving
  // every pixel below it available to the instruments.
  Item clockItem = MOTOR_DATA_CLOCK_ICON;
  Item timeItem = MOTOR_DATA_TIME;
  Item oemItem = MOTOR_DATA_OEM;
  Item batteryTextItem = MOTOR_DATA_BATTERY;
  Item batteryIconItem = MOTOR_DATA_BATTERY_ICON;
  clockItem.y -= 2;
  timeItem.y -= 2;
  oemItem.y -= 2;
  batteryTextItem.y -= 2;
  batteryIconItem.y -= 2;
  makeLayoutIcon(scr, clockItem, CYD_ICON_UPTIME, topColor);
  dw.uptime = makeTopTimeLabel(scr, timeItem, t.uptime, topColor);
  lv_obj_t *oem = makeLabelFont(scr, oemItem, "", topColor, layoutFontToLv(oemItem.font));
  setFittedText(oem, oemItem, dashOemName(), layoutFontToLv(oemItem.font));
  dw.battPct = makeTopBatteryLabel(scr, batteryTextItem, t.battPct, topColor);
  dw.segBatt = makeSegBattery(scr, batteryIconItem, 5);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);

  const float displayedRate = stats.tripWhPerKm > 0.0F ? stats.tripWhPerKm : stats.lifetimeWhPerKm;
  efficiencyDialSpeed = glidePosition(v.speedKmh, speedGaugeMax());
  glideAdd(efficiencySpeedPlace, nullptr);
  glideAim(glides[0], efficiencyDialSpeed, false);
  efficiencyDialScaleX10 = (automaticGaugeRanges || demoPreviewIsFrozen()) ? displayGaugeMaximum(RANGE_EFFICIENCY) * 10 : efficiencyBaselineScaleX10(stats);
  efficiencyDialRateX10 = currentEfficiencyX10(v, stats);
  efficiencyFilteredRateX10 = efficiencyDialRateX10;
  efficiencyNeedleLastMs = millis();
  prepareEfficiencyDialGeometry();
  efficiencyDialObj = lv_obj_create(scr);
  lv_obj_remove_style_all(efficiencyDialObj);
  makePassive(efficiencyDialObj);
  lv_obj_set_pos(efficiencyDialObj, 0, 28);
  lv_obj_set_size(efficiencyDialObj, 320, 114);
  lv_obj_add_event_cb(efficiencyDialObj, efficiencyDialDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(efficiencyDialObj, efficiencyDialDeleteCb, LV_EVENT_DELETE, NULL);

  dw.speed = makeLabelAt(scr, 94, 92, t.speed, whiteLv(), &lv_font_speed48, 1);
  dw.speedUnit = makeLabelAt(scr, 94, 120, speedUnitLabel(), accent, F1, 1);

  if (dashHas(TELEMETRY_FIELD_RIDE_EFFICIENCY) || dashHas(TELEMETRY_FIELD_LIFETIME_EFFICIENCY))
    formatEnergyRate(text, sizeof(text), displayedRate, false);
  else
    snprintf(text, sizeof(text), "-");
  dw.energyRate = makeLabelAt(scr, 245, 94, text, whiteLv(), F4, 1);
  makeLabelAt(scr, 245, 116, energyRateLabel(), accent, F1, 1);

  efficiencyGraphObj = lv_obj_create(scr);
  lv_obj_remove_style_all(efficiencyGraphObj);
  makePassive(efficiencyGraphObj);
  lv_obj_set_pos(efficiencyGraphObj, EFFICIENCY_PLOT_X0 - 1, EFFICIENCY_PLOT_BASE - EFFICIENCY_PLOT_H);
  lv_obj_set_size(efficiencyGraphObj, EFFICIENCY_PLOT_W + 1, EFFICIENCY_PLOT_H + 1);
  lv_obj_add_event_cb(efficiencyGraphObj, efficiencyGraphDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(efficiencyGraphObj, efficiencyGraphDeleteCb, LV_EVENT_DELETE, NULL);
  resetEfficiencyHistory();
  // The theme selector runs its live previews in demo mode, and the rendered
  // stills are built with CYD_LVGL_PREVIEW. Neither is true on a real ride.
#ifdef CYD_LVGL_PREVIEW
  seedEfficiencyHistory(stats);
#else
  if (demoModeIsActive()) seedEfficiencyHistory(stats);
#endif

  lv_obj_t *rangeCaption = NULL;
  lv_obj_t *averageCaption = NULL;
  lv_obj_t *usedCaption = NULL;
  char graphTitle[64];
  snprintf(graphTitle, sizeof(graphTitle), "%s - %s",
           txt("POWER CONSUMPTION", "ENERGIANKULUTUS", "ENERGIEVERBRAUCH",
               "CONSOMMATION", "CONSUMO DE ENERGÍA", "CONSUMO ENERGETICO"),
           energyRateLabel());
  lv_obj_t *graphTitleLabel = makeLabelAt(scr, 4, 146, graphTitle, labelLv(), F1, 0);
  lv_obj_set_width(graphTitleLabel, 248);
  lv_label_set_long_mode(graphTitleLabel, LV_LABEL_LONG_CLIP);
  dw.aux[3] = makeLabelAt(scr, 4, 159, "0", efficiencyScaleColor(100), F1, 0);
  dw.aux[4] = makeLabelAt(scr, 4, 174, "0", efficiencyScaleColor(50), F1, 0);
  dw.aux[5] = makeLabelAt(scr, 4, 189, "0", efficiencyScaleColor(0), F1, 0);
  makeLabelAt(scr, 316, 146, "5 MIN", labelLv(), F1, 4);
  prepareEfficiencyGraph();
  updateEfficiencyGraphStatLabels();

  makePanel(scr, 0, 202, 320, 1, 0, dimLv(), dimLv(), true);
  formatAvailableRange(text, sizeof(text), stats.rangeKm, true);
  dw.range = makeEfficiencyFooterMetric(
      scr, 40, metricRangeLabel(), text,
      &rangeCaption);
  formatAvailableEnergyRate(text, sizeof(text), stats.tripWhPerKm, true);
  dw.aux[0] = makeEfficiencyFooterMetric(
      scr, 120, txt("RIDE AVG", "AJON KA.", "FAHRT-SCHNITT", "MOY. TRAJET", "PROM. VIAJE", "MEDIA VIAG."), text,
      &averageCaption);
  formatAvailableEnergy(text, sizeof(text), stats.tripWh, 0, TELEMETRY_FIELD_TRIP_ENERGY);
  dw.aux[1] = makeEfficiencyFooterMetric(
      scr, 200, txt("RIDE USED", "AJON WH", "FAHRT WH", "WH TRAJET", "WH VIAJE", "WH VIAGGIO"), text,
      &usedCaption);
  formatAvailableEnergy(text, sizeof(text), stats.tripRegenWh, 1, TELEMETRY_FIELD_REGEN_ENERGY);
  lv_obj_t *regenCaption = NULL;
  dw.aux[2] = makeEfficiencyFooterMetric(scr, 280,
      txt("REGEN", "PALAUTUS", "REKUP.", "RÉGÉN.", "REGEN.", "RECUPERO"), text, &regenCaption);
  registerDataSlot(0, rangeCaption, dw.range);
  registerDataSlot(1, averageCaption, dw.aux[0]);
  registerDataSlot(2, usedCaption, dw.aux[1]);
  registerDataSlot(3, regenCaption, dw.aux[2]);
  sweepSpeedWith(dw.speed, efficiencySpeedSweep, v);
  // The live-consumption needle has no readout of its own; the label beside
  // it is only the sweep's identity.
  sweepWith(dw.energyRate, efficiencyRateSweep, efficiencyDialRateX10, efficiencyDialScaleX10, 120);
}

static void updateEfficiency(const DashboardValues &v) {
  char text[32];
  formatSpeedValue(text, sizeof(text), v.speedKmh);
  setLabelText(dw.speed, text);
  glideAim(glides[0], glidePosition(v.speedKmh, speedGaugeMax()));

  const BatteryStats stats = dashBatteryStats();
  const uint32_t now = millis();
  if (demoModeIsActive()) {
    const uint32_t simulatedMs = (uint32_t)demoRideSeconds() * 1000U;
    if (simulatedMs != efficiencyHistoryLastMs) {
      seedEfficiencyHistory(stats);
      prepareEfficiencyGraph();
      updateEfficiencyGraphStatLabels();
      if (efficiencyGraphObj) lv_obj_invalidate(efficiencyGraphObj);
    }
  } else if ((uint32_t)(now - efficiencyHistoryLastMs) >= EFFICIENCY_HISTORY_INTERVAL_MS) {
    efficiencyHistoryLastMs = now;
    const int sample = currentEfficiencyX10(v, stats);
    pushEfficiencyHistory(sample);
  }
  // Independent, elapsed-time smoothing: responsive at 10 Hz without changing
  // the graph's one-sample-per-second/five-minute time axis.
  const uint32_t elapsed = now - efficiencyNeedleLastMs;
  if (elapsed > 0) {
    efficiencyNeedleLastMs = now;
    const int target = currentEfficiencyX10(v, stats);
    efficiencyFilteredRateX10 += (target - efficiencyFilteredRateX10) * (1.0F - expf(-(float)elapsed / 350.0F));
    const int next = (int)lroundf(efficiencyFilteredRateX10);
    invalidateEfficiencyDialChange(1, efficiencyDialRateX10, next, efficiencyDialScaleX10);
    efficiencyDialRateX10 = next;
  }
  if (midDue) {
    formatUptime(text, sizeof(text), v.uptimeSeconds);
    setLabelText(dw.uptime, text);
  }
  const float displayedRate = stats.tripWhPerKm > 0.0F ? stats.tripWhPerKm : stats.lifetimeWhPerKm;
  if (elecDue) {
    if (dashHas(TELEMETRY_FIELD_RIDE_EFFICIENCY) || dashHas(TELEMETRY_FIELD_LIFETIME_EFFICIENCY))
      formatEnergyRate(text, sizeof(text), displayedRate, false);
    else
      snprintf(text, sizeof(text), "-");
    setLabelText(dw.energyRate, text);
  }
  if (!midDue) return;
  if (dashHas(TELEMETRY_FIELD_BATTERY_SOC)) snprintf(text, sizeof(text), "%d%%", v.batteryPercent);
  else snprintf(text, sizeof(text), "-");
  setLabelText(dw.battPct, text);
  setSegBatteryLevel(dw.segBatt, v.batteryPercent);
  formatAvailableRange(text, sizeof(text), stats.rangeKm, true);
  setLabelText(dw.range, text);
  formatAvailableEnergyRate(text, sizeof(text), stats.tripWhPerKm, true);
  setLabelText(dw.aux[0], text);
  formatAvailableEnergy(text, sizeof(text), stats.tripWh, 0, TELEMETRY_FIELD_TRIP_ENERGY);
  setLabelText(dw.aux[1], text);
  formatAvailableEnergy(text, sizeof(text), stats.tripRegenWh, 1, TELEMETRY_FIELD_REGEN_ENERGY);
  setLabelText(dw.aux[2], text);
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

static lv_obj_t *makeGradientHalf(lv_obj_t *scr, int x, int y, int w, int h, lv_color_t from, lv_color_t to,
                                  lv_grad_dir_t direction, uint8_t mainStop, uint8_t gradStop) {
  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_remove_style_all(panel);
  makePassive(panel);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, w, h);
  lv_obj_set_style_bg_color(panel, from, 0);
  lv_obj_set_style_bg_grad_color(panel, to, 0);
  lv_obj_set_style_bg_grad_dir(panel, direction, 0);
  lv_obj_set_style_bg_main_stop(panel, mainStop, 0);
  lv_obj_set_style_bg_grad_stop(panel, gradStop, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  return panel;
}

void applyDashboardGradient(lv_obj_t *scr) {
  if (!dashboardGradientEnabled) {
    // Flat + Default preserves each dashboard's native background. Choosing a
    // real background colour replaces it with that solid colour.
    if (dashboardGradientTheme != ACCENT_DEFAULT) {
      lv_obj_set_style_bg_color(scr, dashboardColorLv(gradientAccentLv()), 0);
      lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    }
    return;
  }

  const lv_color_t accent = dashboardColorLv(gradientAccentLv());
  const lv_color_t black = dashBlack();
  const int position = constrain((int)dashboardGradientPosition, 15, 85);

  if (!dashboardGradientBell) {
    // Keep the chosen colour through the movable stop, then complete the fade
    // early enough to leave a broader, calmer black field at the far end.
    const int colorStop = position * 255 / 100;
    const int reverseColorStop = (100 - position) * 255 / 100;
    const int fadeSpan = 128;
    lv_obj_set_style_bg_color(scr, dashboardGradientReverse ? black : accent, 0);
    lv_obj_set_style_bg_grad_color(scr, dashboardGradientReverse ? accent : black, 0);
    lv_obj_set_style_bg_grad_dir(scr, dashboardGradientHorizontal ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(scr,
                                  dashboardGradientReverse ? max(0, reverseColorStop - fadeSpan) : colorStop, 0);
    lv_obj_set_style_bg_grad_stop(scr,
                                  dashboardGradientReverse ? reverseColorStop : min(255, colorStop + fadeSpan), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return;
  }

  // Bell mode joins two LVGL gradients at the movable centre. Normally this
  // is a colour peak on black edges; reverse makes it a black trough between
  // colour edges. It needs no bitmap or extra framebuffer.
  const lv_color_t edge = dashboardGradientReverse ? accent : black;
  const lv_color_t center = dashboardGradientReverse ? black : accent;
  // Inverse bell: colour is concentrated near the two outer edges and the
  // black trough occupies most of the centre. Normal bell: retain a smaller
  // but clearly solid black band along each outer edge.
  const uint8_t firstMain = dashboardGradientReverse ? 0 : 51;
  const uint8_t firstGrad = dashboardGradientReverse ? 96 : 255;
  const uint8_t secondMain = dashboardGradientReverse ? 159 : 0;
  const uint8_t secondGrad = dashboardGradientReverse ? 255 : 204;
  if (dashboardGradientHorizontal) {
    const int split = 320 * position / 100;
    makeGradientHalf(scr, 0, 0, split, 240, edge, center, LV_GRAD_DIR_HOR, firstMain, firstGrad);
    makeGradientHalf(scr, split, 0, 320 - split, 240, center, edge, LV_GRAD_DIR_HOR, secondMain, secondGrad);
  } else {
    const int split = 240 * position / 100;
    makeGradientHalf(scr, 0, 0, 320, split, edge, center, LV_GRAD_DIR_VER, firstMain, firstGrad);
    makeGradientHalf(scr, 0, split, 320, 240 - split, center, edge, LV_GRAD_DIR_VER, secondMain, secondGrad);
  }
}

void buildDashboardMode(lv_obj_t *scr, DashboardMode mode, const DashboardValues &values) {
  const bool restoreChrome = uiChromeAccentEnabled();
  setUiChromeAccent(false);
  const uint32_t firstChild = lv_obj_get_child_cnt(scr);  // anything above it is not this dashboard's
  memset(&dw, 0, sizeof(dw));
  resetVisualGaugeRanges();
  resetVisualGaugeValues(values);
  lastRangeSpeed = lastRangePower = lastRangeEfficiency = -1;
  glideBegin(scr);
  sweepSlotCount = 0;     // orphan sweep anims die with their old screen
  sweepEndMs = 0;
  elecTierLastMs = 0;     // first post-build update refreshes every tier
  midTierLastMs = 0;
  slowTierLastMs = 0;
  setAccentRenderMode(mode);  // Default accent follows the rendered theme
  clearIconCache();           // stale descriptors are never overwritten in place
  lv_obj_set_style_bg_color(scr, dashBlack(), 0);
  applyDashboardGradient(scr);
  switch (mode) {
    case MODE_EFFICIENCY:
      buildEfficiency(scr, values);
      break;
    case MODE_MINIMAL:
      buildMinimal(scr, values);
      break;
    case MODE_TRACE:
      buildTrace(scr, values);
      break;
    case MODE_REDLINE:
      buildRedline(scr, values);
      break;
    case MODE_BIG_READOUT:
      buildBigReadout(scr, values);
      break;
    case MODE_LARGE_TILES:
      buildLargeTiles(scr, values);
      break;
    case MODE_PIXEL_GAUGE:
      buildPixelGauge(scr, values);
      break;
    case MODE_MOTOR_DATA:
      buildMotorData(scr, values);
      break;
    case MODE_BARS:
      buildBars(scr, values);
      break;
    case MODE_SIMPLE:
      buildSimple(scr, values);
      break;
    case MODE_GAUGE:
      buildGauge(scr, values);
      break;
    case MODE_HUD:
    default:
      buildHud(scr, values);
      break;
  }
  applyDashboardDataOverrides(mode, values);
  tightenLabelRepaints(scr, firstChild);
  setUiChromeAccent(restoreChrome);
}

void buildDashboard(lv_obj_t *scr, const DashboardValues &values) {
  buildDashboardMode(scr, dashboardMode, values);
}

static void refreshGaugeRanges(DashboardMode mode) {
  advanceVisualGaugeRanges();
  const int speed = speedGaugeMax(), power = powerBarMax();
  const bool changed = lastRangeSpeed != speed || lastRangePower != power;
  lastRangeSpeed = speed; lastRangePower = power;
  const int efficiency = automaticGaugeRanges ? displayGaugeMaximum(RANGE_EFFICIENCY) * 10 : efficiencyDialScaleX10;
  const bool efficiencyChanged = lastRangeEfficiency != efficiency;
  lastRangeEfficiency = efficiency;
  efficiencyDialScaleX10 = efficiency;
  if (!changed && !efficiencyChanged) return;
  for (int i=0; i<6; ++i) if (dw.scaleLabels[i]) {
    char text[12]; formatSpeedScaleMark(i, text, sizeof(text)); setLabelText(dw.scaleLabels[i], text);
  }
  if (mode == MODE_EFFICIENCY && efficiencyDialObj) lv_obj_invalidate(efficiencyDialObj);
  if (mode == MODE_TRACE) fillTraceChart();
}

void updateDashboardMode(DashboardMode mode, const DashboardValues &values, bool force) {
  if (instrumentResumeTimer) return;
  if (!force && millis() < sweepEndMs) return;  // let the startup sweep play out
  const bool restoreChrome = uiChromeAccentEnabled();
  setUiChromeAccent(false);
  observePreviewGaugeRanges(values, dashAvailableFields);
  const bool sourceChanged = visualGaugeSource != automaticGaugeSource() ||
                             visualGaugeAutomatic != automaticGaugeRanges;
  refreshGaugeRanges(mode);
  if (sourceChanged) resetVisualGaugeValues(values);
  else advanceVisualGaugeValues(values);
  refreshUpdateTiers(force);
  switch (mode) {
    case MODE_EFFICIENCY:
      updateEfficiency(values);
      break;
    case MODE_MINIMAL:
      updateMinimal(values);
      break;
    case MODE_TRACE:
      updateTrace(values);
      break;
    case MODE_REDLINE:
      updateRedline(values);
      break;
    case MODE_BIG_READOUT:
      updateBigReadout(values);
      break;
    case MODE_LARGE_TILES:
      updateLargeTiles(values);
      break;
    case MODE_PIXEL_GAUGE:
      updatePixelGauge(values);
      break;
    case MODE_MOTOR_DATA:
      updateMotorData(values);
      break;
    case MODE_BARS:
      updateBars(values);
      break;
    case MODE_SIMPLE:
      updateSimple(values);
      break;
    case MODE_GAUGE:
      updateGauge(values);
      break;
    case MODE_HUD:
    default:
      updateHud(values);
      break;
  }
  applyDashboardDataOverrides(mode, values);
  setUiChromeAccent(restoreChrome);
}

void updateDashboard(const DashboardValues &values) {
  // history is sampled from live telemetry only: the DASH UI selector calls
  // updateDashboardMode directly with animated dummy values
  traceSampleAdded = !demoModeIsActive() && recordTraceSample(values);
  updateDashboardMode(dashboardMode, values);
  traceSampleAdded = false;
}
