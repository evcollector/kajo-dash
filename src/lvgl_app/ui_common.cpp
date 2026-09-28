#include "ui_common.h"

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

lv_obj_t *makeRect(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t border) {
  return makePanel(parent, item.x, item.y, item.w, item.h, item.radius, border, lv_color_black(), false);
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

SegBatteryWidget makeSegBattery(lv_obj_t *parent, const cyd_layout::Item &item, lv_color_t outline, int blocks) {
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

void setSegBatteryLevel(SegBatteryWidget &widget, int percent, lv_color_t goodColor) {
  const int clamped = constrain(percent, 0, 100);
  // Whole steps only, rounded down: a block lights when that tenth is actually
  // there. Anything above empty keeps one lit so the pack never looks dead.
  int lit = clamped * widget.count / 100;
  if (lit == 0 && clamped > 0) lit = 1;
  const lv_color_t color = batteryLevelColorLv(clamped, goodColor);
  const uint32_t colorKey = lv_color_to32(color);
  if (lit == widget.lastLit && colorKey == widget.lastColor) return;
  widget.lastLit = lit;
  widget.lastColor = colorKey;
  for (int i = 0; i < widget.count; i++) {
    lv_obj_set_style_bg_opa(widget.blocks[i], i < lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (i < lit) lv_obj_set_style_bg_color(widget.blocks[i], color, 0);
  }
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

void setBatteryLevel(BatteryWidget &widget, int percent, lv_color_t goodColor) {
  const int clamped = constrain(percent, 0, 100);
  if (clamped == widget.lastPercent) return;
  widget.lastPercent = clamped;
  const int fillW = map(clamped, 0, 100, 0, widget.innerW);
  lv_obj_set_width(widget.fill, max(1, fillW));
  lv_obj_set_style_bg_opa(widget.fill, fillW > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(widget.fill, batteryLevelColorLv(clamped, goodColor), 0);
}

// ── Gauges ────────────────────────────────────────────────────────────────────

ArcGauge makeSegGauge(lv_obj_t *parent, int cx, int cy, int r, int maxValue, lv_color_t active,
                      lv_color_t inactive, lv_color_t tickMajor, lv_color_t tickMinor) {
  lv_obj_t *meter = lv_meter_create(parent);
  lv_obj_remove_style(meter, NULL, LV_PART_MAIN);
  // the meter always paints a needle-hub circle at its center; our gauges only
  // use arcs, and the hub would sit right on top of the value text
  lv_obj_remove_style(meter, NULL, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_size(meter, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  const int size = r * 2 + 2;
  lv_obj_set_size(meter, size, size);
  lv_obj_set_pos(meter, cx - size / 2, cy - size / 2);
  lv_obj_set_style_text_opa(meter, LV_OPA_TRANSP, LV_PART_TICKS);  // no tick labels

  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, scale, 29, 2, 7, tickMinor);
  lv_meter_set_scale_major_ticks(meter, scale, 4, 2, 12, tickMajor, 6);
  lv_meter_set_scale_range(meter, scale, 0, maxValue, 260, 140);

  ArcGauge gauge;
  gauge.maxValue = maxValue;
  // value band, roughly where updateGaugeArc paints its chunky segments
  gauge.indic = lv_meter_add_arc(meter, scale, 11, active, -13);
  lv_meter_set_indicator_start_value(meter, gauge.indic, 0);
  lv_meter_set_indicator_end_value(meter, gauge.indic, 0);
  gauge.arc = meter;
  gauge.lastValue = -1;
  (void)inactive;
  return gauge;
}

ArcGauge makeSimpleArc(lv_obj_t *parent, int cx, int cy, int r, int maxValue, int width, lv_color_t active,
                       lv_color_t inactive, int startDeg, int sweepDeg) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_arc_set_rotation(arc, startDeg);
  lv_arc_set_bg_angles(arc, 0, sweepDeg);
  lv_arc_set_range(arc, 0, maxValue);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(arc, r * 2, r * 2);
  lv_obj_set_pos(arc, cx - r, cy - r);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, inactive, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, active, LV_PART_INDICATOR);

  ArcGauge gauge;
  gauge.arc = arc;
  gauge.indic = NULL;
  gauge.maxValue = maxValue;
  gauge.lastValue = -1;
  return gauge;
}

void setArcValue(ArcGauge &gauge, int value) {
  const int clamped = constrain(value, 0, gauge.maxValue);
  if (clamped == gauge.lastValue) return;
  gauge.lastValue = clamped;
  if (gauge.indic) {
    lv_meter_set_indicator_end_value(gauge.arc, gauge.indic, clamped);
  } else {
    lv_arc_set_value(gauge.arc, clamped);
  }
}

// ── Bar rows ──────────────────────────────────────────────────────────────────

lv_obj_t *makeBarFrame(lv_obj_t *parent, const cyd_layout::Item &frame, lv_color_t color) {
  lv_obj_t *bar = lv_bar_create(parent);
  makePassive(bar);
  lv_obj_set_pos(bar, frame.x, frame.y);
  lv_obj_set_size(bar, frame.w, frame.h);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, color, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  return bar;
}

void setBarValue(lv_obj_t *bar, int amount, int maxAmount) {
  const int pct = map(constrain(amount, 0, maxAmount), 0, maxAmount, 0, 100);
  lv_bar_set_value(bar, pct, LV_ANIM_OFF);
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
