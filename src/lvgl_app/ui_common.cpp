#include "ui_common.h"
#include "ui_style.h"

#include <string.h>

// ── Fonts ─────────────────────────────────────────────────────────────────────

struct FontEntry {
  uint8_t px;
  const lv_font_t *font;
};

// Descending; also the fallback chain for fitted text.
//
// Everything from 36 px up is restricted, and silently so: LVGL draws nothing
// for a codepoint a face does not carry, exactly like TFT_eSPI skipping a font
// it was not compiled with.
//
//   48/72/96  the speed faces -- "-./0123456789" only, no space, no letters,
//             no "%" and no degree sign
//   36        lv_font_rajdhani_36 is ASCII only; every smaller Rajdhani face
//             carries the 69 Latin-1 glyphs the DE/FR/ES/IT/FI strings need
//
// Only numeric readouts reach those tiers today -- the layout puts units,
// captions and the OEM name in their own smaller labels rather than in the
// value string -- so nothing is broken. But a text item placed at 36 px or
// above would lose its accented characters with no build error and no runtime
// complaint, so keep new items at 24 px or below unless they really are digits.
// tools/layout.json is where that choice gets made.
static const FontEntry kFonts[] = {
    {96, &lv_font_speed96},       {72, &lv_font_speed72},       {48, &lv_font_speed48},
    {36, &lv_font_rajdhani_36}, {28, &lv_font_rajdhani_24}, {24, &lv_font_rajdhani_24},
    {20, &lv_font_rajdhani_20}, {16, &lv_font_rajdhani_16}, {14, &lv_font_rajdhani_14},
    {12, &lv_font_rajdhani_12}, {10, &lv_font_rajdhani_12},
    {8, &lv_font_rajdhani_10},
};
static const int kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

const lv_font_t *layoutFontToLv(uint8_t layoutFontPx, const lv_font_t *fallback) {
  if (layoutFontPx == 0) return fallback;
  for (int i = 0; i < kFontCount; i++) {
    if (layoutFontPx >= kFonts[i].px) return kFonts[i].font;
  }
  return &lv_font_rajdhani_12;
}

// ── Basic objects ─────────────────────────────────────────────────────────────

void makePassive(lv_obj_t *obj) {
  lv_obj_clear_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                                         LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
}

void setLabelText(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

void setObjHidden(lv_obj_t *obj, bool hidden) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// What this object itself was last given for a property; absent until it is first set.
static bool localStyleIs(lv_obj_t *obj, lv_style_prop_t prop, lv_style_selector_t selector,
                         bool (*same)(const lv_style_value_t &, const lv_style_value_t &), const lv_style_value_t &want) {
  lv_style_value_t current;
  return lv_obj_get_local_style_prop(obj, prop, &current, selector) == LV_STYLE_RES_FOUND && same(current, want);
}
static bool sameNumber(const lv_style_value_t &a, const lv_style_value_t &b) { return a.num == b.num; }
static bool samePointer(const lv_style_value_t &a, const lv_style_value_t &b) { return a.ptr == b.ptr; }
static bool sameColor(const lv_style_value_t &a, const lv_style_value_t &b) { return a.color.full == b.color.full; }

void setObjTextAlign(lv_obj_t *obj, lv_text_align_t align) {
  lv_style_value_t want = {};
  want.num = align;
  if (!localStyleIs(obj, LV_STYLE_TEXT_ALIGN, 0, sameNumber, want)) lv_obj_set_style_text_align(obj, align, 0);
}

void setObjTextFont(lv_obj_t *obj, const lv_font_t *font) {
  lv_style_value_t want = {};
  want.ptr = font;
  if (!localStyleIs(obj, LV_STYLE_TEXT_FONT, 0, samePointer, want)) lv_obj_set_style_text_font(obj, font, 0);
}

void setObjTextColor(lv_obj_t *obj, lv_color_t color) {
  lv_style_value_t want = {};
  want.color = color;
  if (!localStyleIs(obj, LV_STYLE_TEXT_COLOR, 0, sameColor, want)) lv_obj_set_style_text_color(obj, color, 0);
}

void setObjBgColor(lv_obj_t *obj, lv_color_t color, lv_style_selector_t selector) {
  lv_style_value_t want = {};
  want.color = color;
  if (!localStyleIs(obj, LV_STYLE_BG_COLOR, selector, sameColor, want)) lv_obj_set_style_bg_color(obj, color, selector);
}

static lv_obj_t *makeBase(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  makePassive(obj);
  return obj;
}

lv_obj_t *makePanel(lv_obj_t *parent, int x, int y, int w, int h, int radius, lv_color_t border, lv_color_t bg,
                    bool filled) {
  lv_obj_t *obj = makeBase(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, border, 0);
  if (filled) {
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  }
  return obj;
}

lv_obj_t *makeFilledRect(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t border, lv_color_t bg) {
  return makePanel(parent, item.x, item.y, item.w, item.h, item.radius, border, bg, true);
}

lv_obj_t *makeHLine(lv_obj_t *parent, int x, int y, int w, lv_color_t color) {
  lv_obj_t *obj = makeBase(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, 1);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}

lv_obj_t *makeVLine(lv_obj_t *parent, int x, int y, int h, lv_color_t color) {
  lv_obj_t *obj = makeBase(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, 1, h);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}

lv_obj_t *makeLineBox(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t color) {
  if (item.w == 0) return makeVLine(parent, item.x, item.y, item.h, color);
  if (item.h == 0) return makeHLine(parent, item.x, item.y, item.w, color);
  return makePanel(parent, item.x, item.y, item.w, item.h, 0, color, lv_color_black(), false);
}

// ── Labels ────────────────────────────────────────────────────────────────────

static void placeLabel(lv_obj_t *label, int x, int y, uint8_t anchor, const lv_font_t *font, int maxWidth) {
  const int lineH = lv_font_get_line_height(font);
  int w;
  lv_text_align_t align;
  int px, py = y;
  if (maxWidth > 0) {
    // maxWidth items are centered on (x, y), like drawCenteredFittedText
    w = maxWidth + 8;
    align = LV_TEXT_ALIGN_CENTER;
    px = x - w / 2;
    py = y - lineH / 2;
  } else {
    switch (anchor) {
      case 1:  // middle-center
        w = 200;
        align = LV_TEXT_ALIGN_CENTER;
        px = x - w / 2;
        py = y - lineH / 2;
        break;
      case 2:
      case 4:  // top-right
        w = 140;
        align = LV_TEXT_ALIGN_RIGHT;
        px = x - w;
        break;
      case 3:  // top-center
        w = 200;
        align = LV_TEXT_ALIGN_CENTER;
        px = x - w / 2;
        break;
      case 0:
      default:  // top-left
        w = 220;
        align = LV_TEXT_ALIGN_LEFT;
        px = x;
        break;
    }
  }
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(label, w, lineH + 2);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_pos(label, px, py);
}

lv_obj_t *makeLabelFont(lv_obj_t *parent, const cyd_layout::Item &item, const char *text, lv_color_t color,
                        const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  makePassive(label);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_text(label, text);
  placeLabel(label, item.x, item.y, item.anchor, font, item.maxWidth);
  return label;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const cyd_layout::Item &item, const char *text, lv_color_t color) {
  return makeLabelFont(parent, item, text, color, layoutFontToLv(item.font));
}

lv_obj_t *makeLabelAt(lv_obj_t *parent, int x, int y, const char *text, lv_color_t color, const lv_font_t *font,
                      uint8_t anchor) {
  lv_obj_t *label = lv_label_create(parent);
  makePassive(label);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_text(label, text);
  placeLabel(label, x, y, anchor, font, 0);
  return label;
}

void wrapLabel(lv_obj_t *label, int x, int y, int width, lv_text_align_t align) {
  // placeLabel() pins labels to LV_LABEL_LONG_CLIP and one line of height, so
  // both have to be undone before a width will wrap anything.
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, width);
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_pos(label, x, y);
}

void setFittedText(lv_obj_t *label, const cyd_layout::Item &item, const char *text, const lv_font_t *preferred) {
  if (strcmp(lv_label_get_text(label), text) == 0) return;  // avoid needless invalidation
  const lv_font_t *chosen = preferred;
  if (item.maxWidth > 0) {
    int start = 0;
    while (start < kFontCount && kFonts[start].font != preferred) start++;
    if (start >= kFontCount) start = 0;
    for (int i = start; i < kFontCount; i++) {
      lv_point_t size;
      lv_txt_get_size(&size, text, kFonts[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      chosen = kFonts[i].font;
      if (size.x <= item.maxWidth) break;
    }
  }
  lv_obj_set_style_text_font(label, chosen, 0);
  // keep the box tight around the text and centered on (x, y): transformed
  // (zoomed) labels render via a layer buffer sized by the box, so a wide
  // box can fail to allocate and the label silently disappears
  if (item.maxWidth > 0 || item.anchor == 1) {
    lv_point_t size;
    lv_txt_get_size(&size, text, chosen, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int w = size.x + 8;
    lv_obj_set_width(label, w);
    lv_obj_set_x(label, item.x - w / 2);
    lv_obj_set_y(label, item.y - lv_font_get_line_height(chosen) / 2);
    lv_obj_set_height(label, lv_font_get_line_height(chosen) + 2);
  }
  lv_label_set_text(label, text);
}

void configureFittedLabel(lv_obj_t *label, const cyd_layout::Item &item, const char *tmpl,
                          const lv_font_t *preferred) {
  // pick the biggest font whose worst-case template fits maxWidth
  const lv_font_t *chosen = preferred;
  if (item.maxWidth > 0) {
    int start = 0;
    while (start < kFontCount && kFonts[start].font != preferred) start++;
    if (start >= kFontCount) start = 0;
    for (int i = start; i < kFontCount; i++) {
      lv_point_t size;
      lv_txt_get_size(&size, tmpl, kFonts[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      chosen = kFonts[i].font;
      if (size.x <= item.maxWidth) break;
    }
  }
  lv_obj_set_style_text_font(label, chosen, 0);
  lv_point_t size;
  lv_txt_get_size(&size, tmpl, chosen, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int w = size.x + 8;
  const int h = lv_font_get_line_height(chosen) + 2;
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_size(label, w, h);
  lv_obj_set_pos(label, item.x - w / 2, item.y - h / 2);
}

void setScaledValueText(lv_obj_t *label, const cyd_layout::Item &item, const char *text,
                        const lv_font_t *preferred) {
  if (!label || !text) return;
  // The template is a same-length row of zeros: with tabular digits that is the
  // widest this value can get without gaining a character, so every value in a
  // width class shares one font and one box.
  const size_t length = strlen(text);
  if ((int)length != (int)(intptr_t)lv_obj_get_user_data(label)) {
    lv_obj_set_user_data(label, (void *)(intptr_t)length);
    char widest[16];
    const size_t capacity = min(length, sizeof(widest) - 1);
    memset(widest, '0', capacity);
    widest[capacity] = '\0';
    configureFittedLabel(label, item, widest, preferred);
  }
  setLabelText(label, text);
}

// ── Icons ─────────────────────────────────────────────────────────────────────

// LVGL indexed-1bit image built from the shared PROGMEM icon data with the
// palette tinted to fg (index 0 transparent, index 1 opaque). Cached per
// icon+color because descriptors must outlive their lv_img objects.
struct IconCacheEntry {
  const uint8_t *data;
  uint32_t color;
  lv_img_dsc_t dsc;
};
static IconCacheEntry iconCache[48];
static int iconCacheCount = 0;

static const uint8_t *iconRawData(CydIconId icon) {
  if (icon < 0 || icon >= CYD_ICON_COUNT) {
    icon = CYD_ICON_SETTINGS;
  }
  return (const uint8_t *)pgm_read_ptr(&cydIconData[icon]);
}

static const lv_img_dsc_t *iconDescriptor(CydIconId icon, lv_color_t fg) {
  const uint8_t *raw = iconRawData(icon);
  const uint32_t colorKey = lv_color_to32(fg);
  for (int i = 0; i < iconCacheCount; i++) {
    if (iconCache[i].data == raw && iconCache[i].color == colorKey) return &iconCache[i].dsc;
  }
  if (iconCacheCount >= (int)(sizeof(iconCache) / sizeof(iconCache[0]))) {
    // never overwrite in place: live lv_imgs point into this array. One
    // screen cannot need this many combos; reuse the last slot as a fallback.
    iconCacheCount--;
  }

  const size_t paletteBytes = 2 * sizeof(lv_color32_t);
  uint8_t *buf = (uint8_t *)lv_mem_alloc(paletteBytes + CYD_ICON_BYTES);
  lv_color32_t *palette = (lv_color32_t *)buf;
  lv_color32_t full = {.full = lv_color_to32(fg)};
  palette[0] = full;
  palette[0].ch.alpha = 0;
  palette[1] = full;
  palette[1].ch.alpha = 255;
  memcpy_P(buf + paletteBytes, raw, CYD_ICON_BYTES);

  IconCacheEntry &entry = iconCache[iconCacheCount++];
  entry.data = raw;
  entry.color = colorKey;
  entry.dsc.header.cf = LV_IMG_CF_INDEXED_1BIT;
  entry.dsc.header.always_zero = 0;
  entry.dsc.header.reserved = 0;
  entry.dsc.header.w = CYD_ICON_SIZE;
  entry.dsc.header.h = CYD_ICON_SIZE;
  entry.dsc.data_size = paletteBytes + CYD_ICON_BYTES;
  entry.dsc.data = buf;
  return &entry.dsc;
}

void clearIconCache() {
  for (int i = 0; i < iconCacheCount; i++) {
    lv_mem_free((void *)iconCache[i].dsc.data);
    iconCache[i].data = NULL;
  }
  iconCacheCount = 0;
}

lv_obj_t *makeIcon(lv_obj_t *parent, int x, int y, CydIconId icon, lv_color_t fg) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, iconDescriptor(icon, fg));
  lv_obj_set_pos(img, x, y);
  makePassive(img);
  return img;
}

lv_obj_t *makeLayoutIcon(lv_obj_t *parent, const cyd_layout::Item &item, CydIconId icon, lv_color_t fg) {
  return makeIcon(parent, item.x, item.y, icon, fg);
}

void setIconSource(lv_obj_t *image, CydIconId icon, lv_color_t fg) {
  if (!image) return;
  const void *previous = lv_img_get_src(image);
  const uint8_t *previousData = previous && lv_img_src_get_type(previous) == LV_IMG_SRC_VARIABLE
                                    ? static_cast<const lv_img_dsc_t *>(previous)->data : nullptr;
  const lv_img_dsc_t *source = iconDescriptor(icon, fg);
  // LVGL 8 invalidates even an identical source. Check the backing data too:
  // the cache's capacity fallback can reuse its final descriptor slot.
  if (previous != source || previousData != source->data) lv_img_set_src(image, source);
}

// ── Battery widget ────────────────────────────────────────────────────────────

BatteryWidget makeBattery(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t fg) {
  const int w = item.w > 0 ? item.w : CYD_ICON_SIZE;
  const int h = item.h > 0 ? item.h : CYD_ICON_SIZE;
  const int terminalW = max(2, w / 5);
  const int bodyW = max(6, w - terminalW - 1);
  const int bodyH = max(6, h - 4);
  const int bodyY = (h - bodyH) / 2;
  const int tipH = max(4, bodyH / 2);
  const int tipY = (h - tipH) / 2;

  BatteryWidget widget;
  widget.root = makeBase(parent);
  lv_obj_set_pos(widget.root, item.x, item.y);
  lv_obj_set_size(widget.root, w, h);

  lv_obj_t *body = makeBase(widget.root);
  lv_obj_set_pos(body, 0, bodyY);
  lv_obj_set_size(body, bodyW, bodyH);
  lv_obj_set_style_border_width(body, 1, 0);
  lv_obj_set_style_border_color(body, fg, 0);

  lv_obj_t *tip = makeBase(widget.root);
  lv_obj_set_pos(tip, bodyW, tipY);
  lv_obj_set_size(tip, terminalW, tipH);
  lv_obj_set_style_bg_color(tip, fg, 0);
  lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);

  widget.innerW = max(1, bodyW - 4);
  widget.fill = makeBase(widget.root);
  lv_obj_set_pos(widget.fill, 2, bodyY + 2);
  lv_obj_set_size(widget.fill, 1, max(1, bodyH - 4));
  lv_obj_set_style_bg_opa(widget.fill, LV_OPA_COVER, 0);
  widget.lastPercent = -1;
  return widget;
}

SegBatteryWidget makeSegBattery(lv_obj_t *parent, const cyd_layout::Item &item, int blocks) {
  // An appearance change rebuilds the dashboard, so resolving it once is enough.
  const bool light = dashboardLightModeActive();
  const lv_color_t outline = light ? lv_color_black() : lv_color_white();
  const int w = item.w > 0 ? item.w : CYD_ICON_SIZE;
  const int h = item.h > 0 ? item.h : CYD_ICON_SIZE;
  const int count = constrain(blocks, 1, SEG_BATTERY_MAX_BLOCKS);
  const int terminalW = max(3, w / 12);
  const int bodyW = max(8, w - terminalW);  // the terminal butts against the body
  // Minimal chrome: 1 px border plus 1 px of breathing room. Ten blocks need
  // 10*blockW + 9 px of gaps, so every pixel spent here comes off the blocks.
  const int inset = 2;
  const int innerW = max(count, bodyW - 2 * inset);
  const int innerH = max(4, h - 2 * inset);
  const int gap = 1;
  const int blockW = max(1, (innerW - gap * (count - 1)) / count);

  lv_obj_t *root = makeBase(parent);
  lv_obj_set_pos(root, item.x, item.y);
  lv_obj_set_size(root, w, h);

  lv_obj_t *body = makeBase(root);
  lv_obj_set_pos(body, 0, 0);
  lv_obj_set_size(body, bodyW, h);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_border_width(body, 1, 0);
  lv_obj_set_style_border_color(body, outline, 0);

  const int tipH = max(4, h / 2);
  lv_obj_t *tip = makeBase(root);
  lv_obj_set_pos(tip, bodyW, (h - tipH) / 2);  // meets the body, no seam
  lv_obj_set_size(tip, terminalW, tipH);
  lv_obj_set_style_radius(tip, 1, 0);
  lv_obj_set_style_bg_color(tip, outline, 0);
  lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);

  SegBatteryWidget widget = {};
  widget.count = count;
  widget.lastLit = -1;
  widget.lastColor = 0;
  widget.light = light;
  widget.blinking = false;
  // centre the row so any pixel that does not divide evenly is split between
  // the two ends rather than piling up on the right
  const int usedW = count * blockW + gap * (count - 1);
  const int firstX = inset + max(0, (innerW - usedW) / 2);
  for (int i = 0; i < count; i++) {
    lv_obj_t *block = makeBase(root);
    lv_obj_set_pos(block, firstX + i * (blockW + gap), inset);
    lv_obj_set_size(block, blockW, innerH);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);  // unlit blocks read as empty
    widget.blocks[i] = block;
  }
  return widget;
}

// Driven 100 -> 0 -> 100 by a playback animation: the block is shown in the
// upper half of the range, so it starts visible and spends equal time on and
// off. Only a real change touches the style, keeping the redraw to one flip.
static void segBatteryBlinkCb(void *var, int32_t value) {
  lv_obj_t *block = static_cast<lv_obj_t *>(var);
  const lv_opa_t opa = value >= 50 ? LV_OPA_COVER : LV_OPA_TRANSP;
  if (lv_obj_get_style_bg_opa(block, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(block, opa, 0);
}

void setSegBatteryLevel(SegBatteryWidget &widget, int percent) {
  const int clamped = constrain(percent, 0, 100);
  // Whole steps only, rounded down: a block lights when that step is actually
  // there. Below one step the last block stays and blinks as the warning.
  int lit = clamped * widget.count / 100;
  const bool blinking = lit == 0;
  if (blinking) lit = 1;
  lv_color_t color;
  if (widget.light) color = lv_color_black();
  else if (lit <= 1) color = c565(cyd_ui::kBatteryCritical565);
  else if (lit == 2) color = c565(cyd_ui::kBatteryLow565);
  else color = c565(cyd_ui::kBatteryGood565);
  const uint32_t colorKey = lv_color_to32(color);
  if (lit == widget.lastLit && colorKey == widget.lastColor && blinking == widget.blinking) return;
  widget.lastLit = lit;
  widget.lastColor = colorKey;
  if (widget.blinking && !blinking) lv_anim_del(widget.blocks[0], segBatteryBlinkCb);
  for (int i = 0; i < widget.count; i++) {
    lv_obj_set_style_bg_opa(widget.blocks[i], i < lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (i < lit) lv_obj_set_style_bg_color(widget.blocks[i], color, 0);
  }
  if (blinking && !widget.blinking) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, widget.blocks[0]);
    lv_anim_set_exec_cb(&anim, segBatteryBlinkCb);
    lv_anim_set_values(&anim, 100, 0);
    lv_anim_set_time(&anim, cyd_ui::kBatteryBlinkMs);
    lv_anim_set_playback_time(&anim, cyd_ui::kBatteryBlinkMs);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
  }
  widget.blinking = blinking;
}

SegMeterWidget makeSegMeter(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t low, lv_color_t mid,
                            lv_color_t high, int blocks) {
  const int count = constrain(blocks, 1, SEG_METER_MAX_BLOCKS);
  const int gap = 2;
  const int available = max(count, item.w - gap * (count - 1));
  const int blockW = max(1, available / count);
  const int widerBlocks = max(0, available - blockW * count);
  int blockX = item.x;

  SegMeterWidget meter = {};
  meter.count = count;
  meter.lastLit = -1;
  for (int i = 0; i < count; i++) {
    // the top of the scale is the interesting part, so the warm colours only
    // take the last third rather than a even split
    const int position = i * 100 / count;
    const lv_color_t color = position < 60 ? low : (position < 85 ? mid : high);
    const int width = blockW + (i < widerBlocks ? 1 : 0);
    lv_obj_t *block = makeBase(parent);
    lv_obj_set_pos(block, blockX, item.y);
    lv_obj_set_size(block, width, item.h);
    lv_obj_set_style_radius(block, 1, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    // unlit blocks keep a dark ghost of their colour, so the whole scale stays
    // visible and the lit part reads as a level rather than a floating bar
    const lv_color_t unlit = lv_color_mix(color, lv_color_black(), 38);
    lv_obj_set_style_bg_color(block, unlit, 0);
    meter.blocks[i] = block;
    meter.colors[i] = color;
    meter.unlitColors[i] = unlit;
    blockX += width + gap;
  }
  return meter;
}

void setSegMeterValue(SegMeterWidget &meter, int value, int maxValue) {
  if (maxValue <= 0 || meter.count <= 0) return;
  const int clamped = constrain(value, 0, maxValue);
  const int lit = (clamped * meter.count + maxValue / 2) / maxValue;  // nearest block
  if (lit == meter.lastLit) return;
  const int from = min(lit, meter.lastLit < 0 ? 0 : meter.lastLit);
  const int to = max(lit, meter.lastLit);
  meter.lastLit = lit;
  for (int i = max(0, from); i < min(meter.count, to + 1); i++) {
    lv_obj_set_style_bg_color(
        meter.blocks[i], i < lit ? meter.colors[i] : meter.unlitColors[i], 0);
  }
}

void setSegMeterColors(SegMeterWidget &meter, lv_color_t lit, lv_color_t unlit) {
  for (int i = 0; i < meter.count; i++) {
    meter.colors[i] = lit;
    meter.unlitColors[i] = unlit;
    lv_obj_set_style_bg_color(meter.blocks[i], i < max(0, meter.lastLit) ? lit : unlit, 0);
  }
}

// ── Segmented ring ────────────────────────────────────────────────────────────

// A ring's blocks are about 3.25 px wide (the bar meters' are 5) with gaps of about
// 2.25 px between them, measured along the centre line. Both are rounded to whole
// degrees, so the pitch follows the radius and the block count with it.
static const int kSegRingBlockQuarterPx = 13;
static const int kSegRingGapQuarterPx = 9;

// Pixels along the ring's centre line as whole degrees, rounded. `centre2` is twice
// that line's radius, which keeps the arithmetic in integers: 180 / pi * px / (centre2 / 2).
static int segRingDegrees(int px, int centre2) {
  return (px * 11459 + centre2 * 50) / (centre2 * 100);
}

SegRingGeometry segRingGeometry(int radius, int thickness, int sweepDeg) {
  const int centre2 = max(2, 2 * radius - thickness);
  const int block = max(1, segRingDegrees(kSegRingBlockQuarterPx, 4 * centre2));
  const int gap = max(1, segRingDegrees(kSegRingGapQuarterPx, 4 * centre2));
  const int pitch = block + gap;
  const int count = max(1, (sweepDeg + gap) / pitch);
  SegRingGeometry g = {};
  g.count = (uint8_t)count;
  g.pitchDeg = (uint8_t)pitch;
  g.blockDeg = (uint8_t)(pitch - gap);
  g.extentDeg = (int16_t)(count * pitch - gap);
  g.startDeg = (int16_t)(270 - g.extentDeg / 2);
  return g;
}

int segRingThickness(int radius) {
  return max(4, (radius + 8) / 6);
}

static lv_point_t segRingPoint(const SegRingWidget &ring, int degrees, int radius) {
  lv_point_t point;
  point.x = ring.cx + ((lv_trigo_sin(degrees + 90) * radius) >> LV_TRIGO_SHIFT);
  point.y = ring.cy + ((lv_trigo_sin(degrees) * radius) >> LV_TRIGO_SHIFT);
  return point;
}

// Where a block can put a pixel: its four corners, plus a margin for the arc that
// bulges past them and for the antialiased edge.
// Majors are the blocks at k/majorSteps of the ring; the others are `thickness` deep and these `majorExtra` deeper.
static int segRingBlockExtra(const SegRingWidget &ring, int index) {
  if (!ring.majorExtra || !ring.majorSteps || ring.count < 2) return 0;
  for (int k = 0; k <= ring.majorSteps; k++)
    if ((k * (ring.count - 1) + ring.majorSteps / 2) / ring.majorSteps == index) return ring.majorExtra;
  return 0;
}

static lv_area_t segRingBlockArea(const SegRingWidget &ring, int index) {
  const int start = ring.startDeg + index * ring.pitchDeg;
  const int end = start + ring.blockDeg;
  const int inner = ring.radius - ring.thickness - segRingBlockExtra(ring, index);
  const int outer = ring.radius;
  const lv_point_t corners[4] = {segRingPoint(ring, start, outer), segRingPoint(ring, start, inner),
                                 segRingPoint(ring, end, outer), segRingPoint(ring, end, inner)};
  lv_area_t area = {corners[0].x, corners[0].y, corners[0].x, corners[0].y};
  for (int i = 1; i < 4; i++) {
    area.x1 = min<int>(area.x1, corners[i].x);
    area.y1 = min<int>(area.y1, corners[i].y);
    area.x2 = max<int>(area.x2, corners[i].x);
    area.y2 = max<int>(area.y2, corners[i].y);
  }
  area.x1 -= 2;
  area.y1 -= 2;
  area.x2 += 2;
  area.y2 += 2;
  return area;
}

static void segRingDrawCb(lv_event_t *event) {
  const SegRingWidget *ring = static_cast<const SegRingWidget *>(lv_event_get_user_data(event));
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.opa = LV_OPA_COVER;
  arc.rounded = false;
  const lv_point_t centre = {ring->cx, ring->cy};
  if (ring->hasFill) {
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.bg_color = ring->fill;
    disc.bg_opa = LV_OPA_COVER;
    disc.radius = LV_RADIUS_CIRCLE;
    const int reach = ring->radius;  // from the blocks' outer edge, so it shows under them
    const lv_area_t area = {(lv_coord_t)(ring->cx - reach), (lv_coord_t)(ring->cy - reach),
                            (lv_coord_t)(ring->cx + reach), (lv_coord_t)(ring->cy + reach)};
    lv_area_t clip = *ctx->clip_area;
    clip.y2 = min<int>(clip.y2, ring->fillBottom - 1);
    if (clip.y1 <= clip.y2) {
      const lv_area_t *outer = ctx->clip_area;
      ctx->clip_area = &clip;
      lv_draw_rect(ctx, &disc, &area);
      ctx->clip_area = outer;
    }
  }
  for (int i = 0; i < ring->count; i++) {
    // Most of a partial redraw misses most of the blocks: skip those before
    // lv_draw_arc builds its masks.
    const lv_area_t block = segRingBlockArea(*ring, i);
    lv_area_t visible;
    if (!_lv_area_intersect(&visible, &block, ctx->clip_area)) continue;
    arc.color = i < ring->litBlocks ? ring->lit : ring->unlit;
    // lv_draw_arc reduces angles past 360 itself, so a block over 3 o'clock needs no special case.
    const int start = ring->startDeg + i * ring->pitchDeg;
    const int extra = segRingBlockExtra(*ring, i);
    arc.width = ring->thickness + extra;
    lv_draw_arc(ctx, &arc, &centre, ring->radius, start, start + ring->blockDeg);
  }
}

void makeSegRing(SegRingWidget &ring, lv_obj_t *parent, int cx, int cy, int radius, int thickness, int sweepDeg,
                 lv_color_t lit, lv_color_t unlit) {
  const SegRingGeometry geometry = segRingGeometry(radius, thickness, sweepDeg);
  ring = {};
  ring.lit = lit;
  ring.unlit = unlit;
  ring.cx = (int16_t)cx;
  ring.cy = (int16_t)cy;
  ring.radius = (int16_t)radius;
  ring.startDeg = geometry.startDeg;
  ring.extentDeg = geometry.extentDeg;
  ring.thickness = (uint8_t)thickness;
  ring.count = geometry.count;
  ring.pitchDeg = geometry.pitchDeg;
  ring.blockDeg = geometry.blockDeg;
  ring.litBlocks = -1;
  ring.obj = makeBase(parent);
  // The object only has to cover what the blocks can touch.
  lv_obj_set_pos(ring.obj, cx - radius - 2, cy - radius - 2);
  lv_obj_set_size(ring.obj, 2 * radius + 4, 2 * radius + 4);
  lv_obj_add_event_cb(ring.obj, segRingDrawCb, LV_EVENT_DRAW_MAIN, &ring);
}

void setSegRingValue(SegRingWidget &ring, int value, int maxValue) {
  if (!ring.obj || maxValue <= 0 || ring.count == 0) return;
  const int clamped = constrain(value, 0, maxValue);
  const int lit = (clamped * ring.count + maxValue / 2) / maxValue;  // nearest block
  if (lit == ring.litBlocks) return;
  // The blocks from `from` up to, not including, `to` change colour.
  const int from = min(lit, ring.litBlocks < 0 ? 0 : (int)ring.litBlocks);
  const int to = max(lit, (int)ring.litBlocks);
  ring.litBlocks = (int8_t)lit;
  if (to <= from) return;
  lv_area_t damage = segRingBlockArea(ring, from);
  for (int i = from + 1; i < to; i++) {
    const lv_area_t block = segRingBlockArea(ring, i);
    damage.x1 = min<int>(damage.x1, block.x1);
    damage.y1 = min<int>(damage.y1, block.y1);
    damage.x2 = max<int>(damage.x2, block.x2);
    damage.y2 = max<int>(damage.y2, block.y2);
  }
  lv_obj_invalidate_area(ring.obj, &damage);
}

void setSegRingFill(SegRingWidget &ring, lv_color_t fill, int bottomY) {
  ring.fillBottom = (int16_t)bottomY;
  ring.fill = fill;
  ring.hasFill = true;
  if (ring.obj) lv_obj_invalidate(ring.obj);
}

void setSegRingMajors(SegRingWidget &ring, int extraPx, int steps) {
  ring.majorExtra = (uint8_t)extraPx;
  ring.majorSteps = (uint8_t)steps;
  if (ring.obj) lv_obj_invalidate(ring.obj);
}

// ── Glide ─────────────────────────────────────────────────────────────────────

static const uint32_t kGlideMinMs = 100;
static const uint32_t kGlideSmallMaxMs = 1000;  // a step of up to kGlideSmallStep
static const uint32_t kGlideBigMaxMs = 300;     // anything bigger
static const int kGlideSmallStep = kGlideScale / 20;  // 5% of the scale, about one reading of the speed

static void glideExecCb(void *var, int32_t position) {
  Glide *glide = static_cast<Glide *>(var);
  glide->position = position;
  glide->place(glide->context, position);
}

void glideInit(Glide &glide, void (*place)(void *context, int position), void *context) {
  lv_anim_del(&glide, glideExecCb);
  glide = {};
  glide.place = place;
  glide.context = context;
}

void glideStop(Glide &glide) {
  lv_anim_del(&glide, glideExecCb);
}

void glideAim(Glide &glide, int target, bool animate) {
  const uint32_t now = millis();
  if (!animate || !glide.placed) {
    lv_anim_del(&glide, glideExecCb);
    glide.placed = true;
    glide.target = target;
    glide.changedAt = now;
    glideExecCb(&glide, target);
    return;
  }
  if (target == glide.target) return;  // the same reading again: the glide carries on
  const uint32_t steady = now - glide.changedAt;
  const int step = abs(target - glide.position);
  glide.target = target;
  glide.changedAt = now;
  if (target == glide.position) {
    lv_anim_del(&glide, glideExecCb);
    return;
  }
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, &glide);
  lv_anim_set_exec_cb(&anim, glideExecCb);
  lv_anim_set_values(&anim, glide.position, target);
  lv_anim_set_time(&anim, constrain(steady + steady / 4, kGlideMinMs,
                                    step <= kGlideSmallStep ? kGlideSmallMaxMs : kGlideBigMaxMs));
  lv_anim_set_path_cb(&anim, lv_anim_path_linear);
  lv_anim_start(&anim);  // replaces a glide still under way
}

void setBatteryLevel(BatteryWidget &widget, int percent, lv_color_t goodColor) {
  const int clamped = constrain(percent, 0, 100);
  if (clamped == widget.lastPercent) return;
  widget.lastPercent = clamped;
  const int fillW = map(clamped, 0, 100, 0, widget.innerW);
  lv_obj_set_width(widget.fill, max(1, fillW));
  lv_obj_set_style_bg_opa(widget.fill, fillW > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(widget.fill, batteryLevelColorLv(clamped, goodColor), 0);
}

// ── Screens ───────────────────────────────────────────────────────────────────

lv_obj_t *makeScreen() {
  // Interface chrome is intentionally independent from the dashboard being
  // customized. Dashboard builders temporarily opt back into the selected UI
  // accent while their widgets are created and updated.
  setUiChromeAccent(true);
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}
