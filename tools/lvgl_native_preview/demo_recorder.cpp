// Frame-exact scene recorder for demo videos.
//
// Boots the real firmware UI on the host, drives it with scripted touch input in simulated time
// and writes every video frame as raw RGB. A scene is a small text script (see
// docs/demo-video.md); tools/make_demo_video.py pipes the frames into ffmpeg and dresses them.
//
// Nothing here follows the wall clock. LVGL advances on the firmware's own loop cadence
// (loop() is lv_timer_handler() then delay(5)), and video frames are sampled at exact ticks
// between those steps, so a scene renders identically on any machine at any speed. Frames are
// read straight from the framebuffer, never through refreshNow(), so partial-repaint results
// show exactly as the panel would draw them.
//
// A scene that cannot find its label or violates an expectation fails with exit code 1; the
// ctest suite runs every committed scene without encoding, which makes them UI regression
// checks as well as video sources.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <lvgl.h>

#include "Preferences.h"
#include "app_state.h"
#include "companion_ble.h"
#include "firmware_update_ble.h"
#include "host_runtime.h"
#include "screens.h"

extern void previewSetInteractiveMode(bool enabled);
extern void previewSetLoggingState(RideLoggingMode mode, bool recording);
extern void previewSetCardState(bool ready, bool checking);
extern void previewSetLightSensor(int raw, int targetPercent);
extern void previewSetCompanionState(CompanionBleState state, bool paused, uint32_t seconds, const char *message);
extern void previewSetFirmwareUpdateActive(bool active);
extern void previewSetFirmwareUpdateState(FirmwareUpdateBleState state, bool configured, uint32_t received,
                                          uint32_t total, const char *message);

namespace fs = std::filesystem;

namespace {

constexpr int kWidth = cyd::preview::kDisplayWidth;
constexpr int kHeight = cyd::preview::kDisplayHeight;
constexpr size_t kPixels = static_cast<size_t>(kWidth) * kHeight;
// The firmware's loop(): lv_timer_handler(); delay(5). The interactive simulator steps the same way.
constexpr uint32_t kGridMs = 5;
constexpr uint32_t kDefaultHoldMs = 90;
constexpr uint32_t kDefaultSettleMs = 300;
// A finger on the 2.8 inch panel, in display pixels. Drawn into the video only, never the UI.
constexpr float kRingRadius = 11.0F;
constexpr uint32_t kRingPressMs = 110;
constexpr uint32_t kRingReleaseMs = 280;
// The compressed image the renderer's update screens show (905.0 KiB).
constexpr uint32_t kUpdateBytes = 926687;

static_assert(LV_COLOR_DEPTH == 16, "the recorder converts RGB565 framebuffers");

class SceneError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// ------------------------------------------------------------------------------ scene file

struct Token {
  std::string text;
  bool quoted = false;
};

struct Step {
  int line = 0;
  std::string command;
  std::vector<Token> args;
  std::map<std::string, std::string> options;
};

std::string sceneFile;

[[noreturn]] void fail(int line, const std::string &message) {
  throw SceneError(sceneFile + ":" + std::to_string(line) + ": " + message);
}

[[noreturn]] void fail(const Step &step, const std::string &message) {
  fail(step.line, step.command + ": " + message);
}

std::vector<Token> tokenize(const std::string &line, int lineNumber) {
  std::vector<Token> tokens;
  size_t i = 0;
  while (i < line.size()) {
    const char c = line[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (c == '#') break;
    Token token;
    if (c == '"') {
      token.quoted = true;
      ++i;
      bool closed = false;
      while (i < line.size()) {
        const char d = line[i++];
        if (d == '"') {
          closed = true;
          break;
        }
        if (d == '\\' && i < line.size()) {
          const char escaped = line[i++];
          token.text += escaped == 'n' ? '\n' : escaped;
        } else {
          token.text += d;
        }
      }
      if (!closed) fail(lineNumber, "unterminated string");
    } else {
      while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) token.text += line[i++];
    }
    tokens.push_back(std::move(token));
  }
  return tokens;
}

bool isOption(const Token &token) {
  if (token.quoted) return false;
  const size_t equals = token.text.find('=');
  if (equals == std::string::npos || equals == 0 || equals + 1 >= token.text.size()) return false;
  if (!std::islower(static_cast<unsigned char>(token.text[0]))) return false;
  for (size_t i = 0; i < equals; ++i) {
    const char c = token.text[i];
    if (!std::islower(static_cast<unsigned char>(c)) && c != '-') return false;
  }
  return true;
}

// signature: one letter per positional argument (i integer, s string, w word); every letter after
// a '?' is optional, and a trailing '+' lets the last letter repeat. options: the key=value names
// the command accepts.
struct CommandSpec {
  const char *name;
  const char *signature;
  const char *options;
};

constexpr CommandSpec kCommands[] = {
    {"title", "s", ""},
    {"subtitle", "s", ""},
    {"note", "s", ""},
    {"boot", "?w", ""},
    {"demo", "w", ""},
    {"logging", "w", ""},
    {"card", "w", ""},
    {"wait", "i", ""},
    {"tap", "ii", "hold settle"},
    {"tap-label", "s", "nth hold settle"},
    {"drag", "iiiii", "settle ease"},
    {"path", "iiiii+", "settle ease"},
    {"hold", "iii", "settle"},
    {"light", "ii", "over"},
    {"companion", "w?s", "seconds paused"},
    {"update", "w?s", "key received total"},
    {"update-request", "?w", ""},
    {"update-progress", "ii", ""},
    {"caption", "s?s", "for"},
    {"caption-off", "", ""},
    {"still", "w", ""},
    {"expect-label", "s", ""},
    {"expect-no-label", "s", ""},
    {"dump", "", ""},
};

const CommandSpec *findSpec(const std::string &name) {
  for (const CommandSpec &spec : kCommands)
    if (name == spec.name) return &spec;
  return nullptr;
}

bool parseInteger(const std::string &text, long &value) {
  if (text.empty()) return false;
  try {
    size_t consumed = 0;
    value = std::stol(text, &consumed);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

bool oneOf(const std::string &value, std::initializer_list<const char *> allowed) {
  for (const char *candidate : allowed)
    if (value == candidate) return true;
  return false;
}

// Scene metadata, and the fixtures the firmware reads while it builds a screen, may come before
// boot: the dashboard only creates its logging pill when logging is on, and it titles itself
// DEMO MODE only if a demo ride is already running. Everything else plays on the timeline and
// needs the firmware running.
bool isTimelineCommand(const std::string &name) {
  return !oneOf(name, {"title", "subtitle", "note", "boot", "logging", "card", "demo", "light", "companion", "update"});
}

void validate(const Step &step) {
  const CommandSpec *spec = findSpec(step.command);
  if (!spec) fail(step, "unknown command");

  size_t required = 0;
  size_t total = 0;
  bool optional = false;
  bool repeats = false;
  std::string kinds;
  for (const char *p = spec->signature; *p; ++p) {
    if (*p == '?') {
      optional = true;
      continue;
    }
    if (*p == '+') {
      repeats = true;
      continue;
    }
    kinds += *p;
    ++total;
    if (!optional) ++required;
  }
  if (step.args.size() < required || (!repeats && step.args.size() > total)) {
    fail(step, "expects " + (repeats ? "at least " + std::to_string(required)
                             : required == total ? std::to_string(total)
                                                 : std::to_string(required) + " to " + std::to_string(total)) +
                   " argument(s), got " + std::to_string(step.args.size()));
  }
  for (size_t i = 0; i < step.args.size(); ++i) {
    long value = 0;
    const char kind = i < kinds.size() ? kinds[i] : kinds.back();
    if (kind == 'i' && !parseInteger(step.args[i].text, value))
      fail(step, "argument " + std::to_string(i + 1) + " must be an integer, got '" + step.args[i].text + "'");
  }

  std::istringstream allowed(spec->options);
  std::vector<std::string> allowedKeys;
  for (std::string key; allowed >> key;) allowedKeys.push_back(key);
  for (const auto &option : step.options) {
    if (std::find(allowedKeys.begin(), allowedKeys.end(), option.first) == allowedKeys.end())
      fail(step, "unknown option '" + option.first + "'");
    if (option.first == "ease") {
      if (!oneOf(option.second, {"linear", "smooth"})) fail(step, "ease must be linear or smooth");
    } else if (option.first == "paused" || option.first == "key") {
      if (!oneOf(option.second, {"yes", "no"})) fail(step, "option '" + option.first + "' must be yes or no");
    } else {
      long value = 0;
      if (!parseInteger(option.second, value) || value < 0)
        fail(step, "option '" + option.first + "' must be a non-negative integer");
      if ((option.first == "nth" || option.first == "for") && value < 1)
        fail(step, "option '" + option.first + "' must be at least 1");
    }
  }

  const auto word = [&](size_t index) { return step.args[index].text; };
  if (step.command == "boot" && !step.args.empty() && !oneOf(word(0), {"dashboard", "first-boot"}))
    fail(step, "expected dashboard or first-boot");
  if (step.command == "demo" && !oneOf(word(0), {"off", "1", "5", "15", "30", "60"}))
    fail(step, "expected off or a demo speed of 1, 5, 15, 30 or 60");
  if (step.command == "logging" && !oneOf(word(0), {"off", "on", "recording"}))
    fail(step, "expected off, on or recording");
  if (step.command == "card" && !oneOf(word(0), {"ready", "missing", "checking"}))
    fail(step, "expected ready, missing or checking");
  if (step.command == "companion" && !oneOf(word(0), {"off", "preparing", "advertising", "connected", "error"}))
    fail(step, "expected off, preparing, advertising, connected or error");
  if (step.command == "update" &&
      !oneOf(word(0), {"locked", "ready", "preparing", "advertising", "connected", "confirm-downgrade", "receiving",
                       "ready-to-reboot", "error", "cancelled"}))
    fail(step, "expected locked, ready, preparing, advertising, connected, confirm-downgrade, receiving, "
               "ready-to-reboot, error or cancelled");
  if (step.command == "update-request" && !step.args.empty() && !oneOf(word(0), {"on", "off"}))
    fail(step, "expected on or off");
  if (step.command == "update-progress" && (std::stol(word(0)) < 0 || std::stol(word(0)) > 100))
    fail(step, "the percentage runs from 0 to 100");
  if (step.command == "light" && (std::stol(word(0)) < 0 || std::stol(word(0)) > 4095 || std::stol(word(1)) < 0 ||
                                  std::stol(word(1)) > 100))
    fail(step, "expects a raw reading of 0 to 4095 and a target of 0 to 100 percent");
  if (step.command == "path" && step.args.size() % 2 != 1) fail(step, "expects a duration, then x y pairs");
  if (step.command == "still") {
    for (const char c : word(0))
      if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) &&
          c != '_' && c != '-')
        fail(step, "a still name uses lower-case letters, digits, '-' and '_'");
  }
}

std::vector<Step> loadScene(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw SceneError(path.string() + ": cannot open the scene file");
  sceneFile = path.filename().string();

  std::vector<Step> steps;
  std::string text;
  int lineNumber = 0;
  bool booted = false;
  while (std::getline(in, text)) {
    ++lineNumber;
    if (lineNumber == 1 && text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
      text.erase(0, 3);
    if (!text.empty() && text.back() == '\r') text.pop_back();

    std::vector<Token> tokens = tokenize(text, lineNumber);
    if (tokens.empty()) continue;
    if (tokens[0].quoted) fail(lineNumber, "a line starts with a command, not a string");

    Step step;
    step.line = lineNumber;
    step.command = tokens[0].text;
    for (size_t i = 1; i < tokens.size(); ++i) {
      if (isOption(tokens[i])) {
        const size_t equals = tokens[i].text.find('=');
        step.options[tokens[i].text.substr(0, equals)] = tokens[i].text.substr(equals + 1);
      } else {
        step.args.push_back(tokens[i]);
      }
    }
    validate(step);

    if (step.command == "boot") {
      if (booted) fail(step, "a scene boots once");
      booted = true;
    } else if (isTimelineCommand(step.command) && !booted) {
      fail(step, "comes before boot; the scene must boot the firmware first");
    }
    steps.push_back(std::move(step));
  }
  if (!booted) throw SceneError(sceneFile + ": the scene never boots the firmware (add 'boot dashboard')");
  return steps;
}

// ------------------------------------------------------------------------------ recorder

struct Options {
  fs::path scene;
  std::string raw;  // "" encodes nothing, "-" is stdout, anything else a file
  fs::path events;
  fs::path stills;
  int fps = 60;
  int scale = 4;
  bool touch = true;
  bool backlight = true;
  bool trace = false;
  bool digest = false;
};

struct CaptionEvent {
  uint64_t frame = 0;
  bool off = false;
  std::string heading;
  std::string body;
  int64_t untilFrame = -1;
};

struct VisibleLabel {
  std::string text;
  int x = 0;
  int y = 0;
  lv_obj_t *object = nullptr;
};

struct Ring {
  float alpha = 0.0F;
  float radius = kRingRadius;
  float x = 0.0F;
  float y = 0.0F;
};

// The host has no backlight, so the video draws the brightness setting as a gain on the picture.
// Manual brightness is displayBrightnessPercent. Auto brightness walks toward the sensor's target
// two percent a tick, which is what serviceAutoBrightness() does in src/main_lvgl.cpp; the host
// does not compile that file, so the few lines are repeated here. Nothing else in the firmware
// reads the value, so a change there only makes the video's dimming stale, never wrong on screen.
int backlightApplied = -1;

void serviceBacklight() {
  if (!autoBrightnessEnabled) {
    backlightApplied = -1;
    return;
  }
  const int target = lightSensorTargetPct();
  if (backlightApplied < 0) backlightApplied = displayBrightnessPercent;
  if (backlightApplied < target)
    backlightApplied = std::min(target, backlightApplied + 2);
  else if (backlightApplied > target)
    backlightApplied = std::max(target, backlightApplied - 2);
}

int backlightPercent() {
  if (autoBrightnessEnabled && backlightApplied >= 0) return backlightApplied;
  return std::clamp<int>(displayBrightnessPercent, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
}

// The LED backlight scales luminance, and the video stores gamma-encoded values, so the gain is
// applied in linear light: 50 percent looks half as bright, not a quarter.
std::vector<uint8_t> backlightGain(int percent) {
  const auto toLinear = [](float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); };
  const auto toEncoded = [](float c) {
    return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
  };
  std::vector<uint8_t> table(256);
  for (int value = 0; value < 256; ++value) {
    const float linear = toLinear(static_cast<float>(value) / 255.0F) * static_cast<float>(percent) / 100.0F;
    table[value] = static_cast<uint8_t>(std::clamp(std::lround(toEncoded(linear) * 255.0F), 0L, 255L));
  }
  return table;
}

void uiTimer(lv_timer_t *) {
  uiDashboardTick();
  uiAutoReturnTick();
  uiSensorTick();
  serviceBacklight();
}

void collectLabels(lv_obj_t *object, std::vector<VisibleLabel> &out) {
  if (lv_obj_check_type(object, &lv_label_class) && lv_obj_is_visible(object)) {
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const int x = (area.x1 + area.x2) / 2;
    const int y = (area.y1 + area.y2) / 2;
    // Controls that slide off the display stay in the tree; a finger cannot reach them.
    const char *text = lv_label_get_text(object);
    if (text && *text && x >= 0 && x < kWidth && y >= 0 && y < kHeight) out.push_back({text, x, y, object});
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) collectLabels(lv_obj_get_child(object, i), out);
}

// Topmost first, so the first match is the label a finger would land on.
std::vector<VisibleLabel> visibleLabels() {
  std::vector<VisibleLabel> labels;
  collectLabels(lv_scr_act(), labels);
  collectLabels(lv_layer_top(), labels);
  collectLabels(lv_layer_sys(), labels);
  std::reverse(labels.begin(), labels.end());
  return labels;
}

std::string describeLabels(const std::vector<VisibleLabel> &labels) {
  std::ostringstream out;
  for (auto it = labels.rbegin(); it != labels.rend(); ++it)
    out << '[' << it->text << "]@" << it->x << ',' << it->y << ' ';
  return out.str();
}

std::string jsonString(const std::string &text) {
  std::string out = "\"";
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04x", c);
          out += escape;
        } else {
          out += raw;
        }
    }
  }
  return out + "\"";
}

float smoothstep(float t) {
  t = std::clamp(t, 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

// One pixel-wide anti-aliased edge: 1 inside [lo, hi], falling to 0 half a pixel outside.
float coverage(float distance, float lo, float hi) {
  return std::clamp(std::min(distance - lo, hi - distance) + 0.5F, 0.0F, 1.0F);
}

class Recorder {
 public:
  Recorder(Options options, FILE *raw) : options_(std::move(options)), raw_(raw) {
    const size_t outputBytes = static_cast<size_t>(kWidth) * options_.scale * kHeight * options_.scale * 3;
    base_.resize(outputBytes);
    if (options_.touch) scratch_.resize(outputBytes);
    previous_.resize(kPixels);
    lutBase_.resize(65536 * 3);
    for (uint32_t value = 0; value < 65536; ++value) {
      lv_color_t color;
      color.full = static_cast<uint16_t>(value);
      lv_color32_t expanded;
      expanded.full = lv_color_to32(color);
      lutBase_[value * 3] = expanded.ch.red;
      lutBase_[value * 3 + 1] = expanded.ch.green;
      lutBase_[value * 3 + 2] = expanded.ch.blue;
    }
    lut_ = lutBase_;
  }

  int run(const std::vector<Step> &steps) {
    for (const Step &step : steps) {
      if (step.command == "title") title_ = step.args[0].text;
      if (step.command == "subtitle") subtitle_ = step.args[0].text;
      if (step.command == "note") note_ = step.args[0].text;
    }
    // The host stubs show logging switched on for their screenshots; a stock firmware boots with
    // it off, so that is where a scene starts.
    previewSetLoggingState(RIDE_LOG_OFF, false);
    try {
      for (const Step &step : steps) execute(step);
      finish();
      return 0;
    } catch (const SceneError &error) {
      std::cerr << error.what() << '\n';
      if (booted_) std::cerr << "  visible labels: " << describeLabels(visibleLabels()) << '\n';
      return 1;
    }
  }

 private:
  Options options_;
  FILE *raw_ = nullptr;
  std::string title_;
  std::string subtitle_;
  std::string note_;
  bool booted_ = false;
  int pendingDemoSpeed_ = 0;  // a demo requested before boot; 0 for none

  // Time. simMs_ is scene time; gridMs_ is the last LVGL step. Frame f is sampled at tickMs(f).
  uint64_t simMs_ = 0;
  uint64_t gridMs_ = 0;
  uint64_t nextFrame_ = 0;

  struct Touch {
    bool down = false;
    bool everDown = false;
    float x = 0.0F;
    float y = 0.0F;
    uint64_t downMs = 0;
    uint64_t upMs = 0;
  } touch_;

  std::vector<CaptionEvent> captions_;
  std::vector<lv_color_t> previous_;
  std::vector<uint8_t> base_;
  std::vector<uint8_t> scratch_;
  std::vector<uint8_t> lutBase_;  // RGB565 to RGB888 as the firmware drew it
  std::vector<uint8_t> lut_;      // the same with the backlight applied
  int lutPercent_ = 100;
  bool haveBase_ = false;
  uint64_t digest_ = 1469598103934665603ULL;

  uint64_t tickMs(uint64_t frame) const {
    return (frame * 1000 + static_cast<uint64_t>(options_.fps) / 2) / static_cast<uint64_t>(options_.fps);
  }

  // First frame whose tick is at or after the given scene time.
  uint64_t frameAtOrAfter(uint64_t ms) const {
    uint64_t frame = ms * static_cast<uint64_t>(options_.fps) / 1000;
    while (tickMs(frame) < ms) ++frame;
    while (frame > 0 && tickMs(frame - 1) >= ms) --frame;
    return frame;
  }

  void sampleDueFrames() {
    while (tickMs(nextFrame_) <= simMs_) {
      emitFrame();
      ++nextFrame_;
    }
  }

  // Moves scene time forward. LVGL steps on its 5 ms grid; frames are sampled at their ticks,
  // after the grid step that falls on the same instant. Pointer changes made between calls
  // reach LVGL at the next grid step, like a touch read in the firmware's loop().
  void advance(uint32_t ms) {
    const uint64_t target = simMs_ + ms;
    for (;;) {
      sampleDueFrames();
      if (simMs_ >= target) return;
      const uint64_t nextGrid = gridMs_ + kGridMs;
      simMs_ = std::min({target, nextGrid, tickMs(nextFrame_)});
      if (simMs_ == nextGrid) {
        cyd::preview::advanceTime(kGridMs, kGridMs);
        gridMs_ = nextGrid;
      }
    }
  }

  // ---------------------------------------------------------------------------- video

  Ring ringAt(uint64_t ms) const {
    Ring ring;
    if (!options_.touch || !touch_.everDown) return ring;
    ring.x = touch_.x;
    ring.y = touch_.y;
    if (touch_.down) {
      const float k = std::clamp(static_cast<float>(ms - touch_.downMs) / kRingPressMs, 0.0F, 1.0F);
      const float eased = 1.0F - (1.0F - k) * (1.0F - k) * (1.0F - k);
      ring.radius = kRingRadius * (0.7F + 0.3F * eased);
      ring.alpha = 0.35F + 0.65F * eased;
    } else if (ms - touch_.upMs < kRingReleaseMs) {
      const float k = static_cast<float>(ms - touch_.upMs) / kRingReleaseMs;
      ring.radius = kRingRadius * (1.0F + 0.7F * smoothstep(k));
      ring.alpha = 1.0F - k;
    }
    return ring;
  }

  void upscale(const lv_color_t *framebuffer) {
    const int scale = options_.scale;
    const size_t rowBytes = static_cast<size_t>(kWidth) * scale * 3;
    for (int y = 0; y < kHeight; ++y) {
      uint8_t *row = base_.data() + static_cast<size_t>(y) * scale * rowBytes;
      uint8_t *out = row;
      for (int x = 0; x < kWidth; ++x) {
        const uint8_t *rgb = &lut_[framebuffer[y * kWidth + x].full * 3];
        for (int k = 0; k < scale; ++k, out += 3) {
          out[0] = rgb[0];
          out[1] = rgb[1];
          out[2] = rgb[2];
        }
      }
      for (int k = 1; k < scale; ++k) std::memcpy(row + static_cast<size_t>(k) * rowBytes, row, rowBytes);
    }
  }

  // White ring with a dark keyline either side, so it reads on the light themes as well.
  void drawRing(std::vector<uint8_t> &image, const Ring &ring) const {
    const int scale = options_.scale;
    const int width = kWidth * scale;
    const int height = kHeight * scale;
    const float cx = (ring.x + 0.5F) * scale;
    const float cy = (ring.y + 0.5F) * scale;
    const float radius = ring.radius * scale;
    const float band = 0.9F * scale;
    const float edge = 0.4F * scale;
    const float extent = radius + band / 2 + edge + 2.0F;
    const int x0 = std::max(0, static_cast<int>(cx - extent));
    const int x1 = std::min(width - 1, static_cast<int>(cx + extent));
    const int y0 = std::max(0, static_cast<int>(cy - extent));
    const int y1 = std::min(height - 1, static_cast<int>(cy + extent));
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const float d = std::hypot(x + 0.5F - cx, y + 0.5F - cy);
        const float white = coverage(d, radius - band / 2, radius + band / 2);
        const float keyline = coverage(d, radius + band / 2, radius + band / 2 + edge) +
                              coverage(d, radius - band / 2 - edge, radius - band / 2);
        const float fill = std::clamp(radius - band / 2 - d + 0.5F, 0.0F, 1.0F);
        uint8_t *pixel = &image[(static_cast<size_t>(y) * width + x) * 3];
        for (int c = 0; c < 3; ++c) {
          float value = pixel[c];
          value += (0.0F - value) * std::min(1.0F, 0.45F * keyline) * ring.alpha;
          value += (255.0F - value) * 0.2F * fill * ring.alpha;
          value += (255.0F - value) * 0.92F * white * ring.alpha;
          pixel[c] = static_cast<uint8_t>(std::clamp(value + 0.5F, 0.0F, 255.0F));
        }
      }
    }
  }

  // Rebuilds the colour table for a backlight level; the next frame is drawn from scratch.
  void applyBacklight(int percent) {
    lutPercent_ = percent;
    if (percent >= 100) {
      lut_ = lutBase_;
    } else {
      const std::vector<uint8_t> gain = backlightGain(percent);
      for (size_t i = 0; i < lutBase_.size(); ++i) lut_[i] = gain[lutBase_[i]];
    }
    haveBase_ = false;
  }

  void emitFrame() {
    const lv_color_t *framebuffer = cyd::preview::framebuffer();
    const Ring ring = ringAt(simMs_);
    const int backlight = options_.backlight ? backlightPercent() : 100;
    if (options_.digest) {
      const uint8_t *bytes = reinterpret_cast<const uint8_t *>(framebuffer);
      for (size_t i = 0; i < kPixels * sizeof(lv_color_t); ++i) digest_ = (digest_ ^ bytes[i]) * 1099511628211ULL;
      digest_ = (digest_ ^ static_cast<uint32_t>(ring.alpha * 1000.0F)) * 1099511628211ULL;
      digest_ = (digest_ ^ static_cast<uint32_t>(backlight)) * 1099511628211ULL;
      // With --trace the running hash is printed once a second, so two builds that disagree can
      // be compared to find the second where they first part.
      if (options_.trace && nextFrame_ % static_cast<uint64_t>(options_.fps) == 0) {
        char line[96];
        std::snprintf(line, sizeof(line), "%s digest=%016llx backlight=%d", stamp().c_str(),
                      static_cast<unsigned long long>(digest_), backlight);
        std::cerr << line << '\n';
      }
    }
    if (!raw_) return;
    if (backlight != lutPercent_) applyBacklight(backlight);

    if (!haveBase_ || std::memcmp(framebuffer, previous_.data(), kPixels * sizeof(lv_color_t)) != 0) {
      upscale(framebuffer);
      std::memcpy(previous_.data(), framebuffer, kPixels * sizeof(lv_color_t));
      haveBase_ = true;
    }
    const std::vector<uint8_t> *image = &base_;
    if (ring.alpha > 0.0F) {
      scratch_ = base_;
      drawRing(scratch_, ring);
      image = &scratch_;
    }
    if (std::fwrite(image->data(), 1, image->size(), raw_) != image->size())
      throw SceneError("could not write a frame (has the encoder stopped?)");
  }

  // ---------------------------------------------------------------------------- input

  void press(int x, int y) {
    x = std::clamp(x, 0, kWidth - 1);
    y = std::clamp(y, 0, kHeight - 1);
    cyd::preview::setPointer(true, x, y);
    touch_.down = true;
    touch_.everDown = true;
    touch_.x = static_cast<float>(x);
    touch_.y = static_cast<float>(y);
    touch_.downMs = simMs_;
  }

  void move(float x, float y) {
    const int ix = std::clamp(static_cast<int>(std::lround(x)), 0, kWidth - 1);
    const int iy = std::clamp(static_cast<int>(std::lround(y)), 0, kHeight - 1);
    cyd::preview::setPointer(true, ix, iy);
    touch_.x = x;
    touch_.y = y;
  }

  void release() {
    cyd::preview::setPointer(false, static_cast<int>(touch_.x), static_cast<int>(touch_.y));
    touch_.down = false;
    touch_.upMs = simMs_;
  }

  void tap(int x, int y, uint32_t holdMs, uint32_t settleMs) {
    press(x, y);
    advance(holdMs);
    release();
    advance(settleMs);
  }

  void drag(int x0, int y0, int x1, int y1, uint32_t durationMs, bool smooth, uint32_t settleMs) {
    press(x0, y0);
    advance(40);  // the finger lands before it moves
    const uint32_t steps = std::max<uint32_t>(1, durationMs / kGridMs);
    for (uint32_t i = 1; i <= steps; ++i) {
      const float t = static_cast<float>(i) / steps;
      const float u = smooth ? smoothstep(t) : t;
      move(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u);
      advance(kGridMs);
    }
    advance(30);
    release();
    advance(settleMs);
  }

  struct Point {
    float x;
    float y;
  };

  // A finger drawing through several points: a Catmull-Rom curve through them, travelled at an
  // eased pace along its length, so a squiggle looks like a hand and not a ruler.
  void drawPath(const std::vector<Point> &points, uint32_t durationMs, bool smooth, uint32_t settleMs) {
    constexpr int kSamplesPerSegment = 24;
    std::vector<Point> curve;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
      const Point &p0 = points[i == 0 ? 0 : i - 1];
      const Point &p1 = points[i];
      const Point &p2 = points[i + 1];
      const Point &p3 = points[i + 2 < points.size() ? i + 2 : points.size() - 1];
      for (int k = 0; k < kSamplesPerSegment; ++k) {
        const float t = static_cast<float>(k) / kSamplesPerSegment;
        const auto spline = [t](float a, float b, float c, float d) {
          return 0.5F * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t +
                         (-a + 3 * b - 3 * c + d) * t * t * t);
        };
        curve.push_back({spline(p0.x, p1.x, p2.x, p3.x), spline(p0.y, p1.y, p2.y, p3.y)});
      }
    }
    curve.push_back(points.back());
    std::vector<float> length(curve.size(), 0.0F);
    for (size_t i = 1; i < curve.size(); ++i)
      length[i] = length[i - 1] + std::hypot(curve[i].x - curve[i - 1].x, curve[i].y - curve[i - 1].y);

    press(static_cast<int>(std::lround(curve.front().x)), static_cast<int>(std::lround(curve.front().y)));
    advance(40);  // the finger lands before it moves
    const uint32_t steps = std::max<uint32_t>(1, durationMs / kGridMs);
    size_t segment = 0;
    for (uint32_t i = 1; i <= steps; ++i) {
      const float t = static_cast<float>(i) / steps;
      const float travelled = (smooth ? smoothstep(t) : t) * length.back();
      while (segment + 2 < curve.size() && length[segment + 1] < travelled) ++segment;
      const float span = length[segment + 1] - length[segment];
      const float k = span > 0.0F ? std::clamp((travelled - length[segment]) / span, 0.0F, 1.0F) : 0.0F;
      move(curve[segment].x + (curve[segment + 1].x - curve[segment].x) * k,
           curve[segment].y + (curve[segment + 1].y - curve[segment].y) * k);
      advance(kGridMs);
    }
    advance(30);
    release();
    advance(settleMs);
  }

  // Moves a fixture over time in the firmware's own 100 ms tick, so the screen sees it change the
  // way it would if the sensor or the link really did.
  template <typename Apply>
  void ramp(uint32_t durationMs, Apply apply) {
    const uint32_t slices = std::max<uint32_t>(1, durationMs / 100);
    const uint32_t slice = durationMs / slices;
    for (uint32_t i = 1; i <= slices; ++i) {
      apply(static_cast<float>(i) / slices);
      advance(i == slices ? durationMs - slice * (slices - 1) : slice);
    }
  }

  // ---------------------------------------------------------------------------- commands

  static uint32_t option(const Step &step, const char *key, uint32_t fallback) {
    const auto it = step.options.find(key);
    return it == step.options.end() ? fallback : static_cast<uint32_t>(std::stol(it->second));
  }

  static int integer(const Step &step, size_t index) { return static_cast<int>(std::stol(step.args[index].text)); }

  std::string stamp() const {
    char text[32];
    std::snprintf(text, sizeof(text), "[%8.3f s] ", static_cast<double>(simMs_) / 1000.0);
    return text;
  }

  void trace(const Step &step, const std::string &detail) const {
    if (!options_.trace) return;
    std::cerr << stamp() << sceneFile << ':' << step.line << ' ' << step.command << ' ' << detail << '\n';
  }

  void boot(const Step &step) {
    const bool firstBoot = !step.args.empty() && step.args[0].text == "first-boot";
    cyd::preview::initRuntime();
    previewPreferencesConfigure(nullptr, true);
    previewSetInteractiveMode(true);
    loadAppSettings();
    loadDisplayPanelProfile();
    loadBatteryStats();
    if (pendingDemoSpeed_ > 0) {
      demoTimeScale = static_cast<uint8_t>(pendingDemoSpeed_);
      setDemoMode(true);
    }
    firstBootConfigured = !firstBoot;
    backlightApplied = -1;
    lv_timer_create(uiTimer, 100, nullptr);
    if (firstBootConfigured) {
      uiShow(SCREEN_DASHBOARD);
    } else {
      configStep = 0;
      uiShow(SCREEN_CONFIG);
    }
    cyd::preview::advanceTime(0);
    cyd::preview::refreshNow();
    booted_ = true;
    trace(step, firstBoot ? "first-boot" : "dashboard");
  }

  // The first visible label with this text, topmost first; nth picks a later match.
  const VisibleLabel &findLabel(const Step &step, const std::string &text, uint32_t nth,
                                std::vector<VisibleLabel> &storage) const {
    storage = visibleLabels();
    uint32_t seen = 0;
    for (const VisibleLabel &label : storage)
      if (label.text == text && ++seen == nth) return label;
    fail(step, "no visible label \"" + text + "\"" + (nth > 1 ? " (match " + std::to_string(nth) + ")" : ""));
  }

  // A finger lands on the topmost clickable object. When the label is not inside that object the
  // scene is tapping through something, an overlay for instance, which is almost never intended.
  void warnIfCovered(const Step &step, const VisibleLabel &label) const {
    lv_point_t point = {static_cast<lv_coord_t>(label.x), static_cast<lv_coord_t>(label.y)};
    lv_obj_t *hit = lv_indev_search_obj(lv_layer_top(), &point);
    if (!hit) hit = lv_indev_search_obj(lv_scr_act(), &point);
    for (lv_obj_t *object = label.object; hit && object; object = lv_obj_get_parent(object))
      if (object == hit) return;
    std::cerr << sceneFile << ':' << step.line << ": warning: the tap on \"" << label.text << "\" at (" << label.x
              << ',' << label.y << ") "
              << (hit ? "lands on a different control; something may be covering it"
                      : "lands on nothing clickable")
              << '\n';
  }

  // The status the display would report during an update, set the way the renderer sets it. No
  // uploader exists on the host: this is the display's side of the conversation, scripted.
  void updateFixture(const Step &step) {
    struct Fixture {
      const char *word;
      FirmwareUpdateBleState state;
      const char *message;
    };
    static const Fixture kFixtures[] = {
        {"locked", FIRMWARE_UPDATE_BLE_LOCKED, "Release signing key not configured"},
        {"ready", FIRMWARE_UPDATE_BLE_READY, "Ready to enter update mode"},
        {"preparing", FIRMWARE_UPDATE_BLE_PREPARING, "Stopping logging and controller Bluetooth"},
        {"advertising", FIRMWARE_UPDATE_BLE_ADVERTISING, "Advertising as CYD Firmware Update"},
        {"connected", FIRMWARE_UPDATE_BLE_CONNECTED, "Uploader connected"},
        {"confirm-downgrade", FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE,
         "Signed older firmware - confirm downgrade on display"},
        {"receiving", FIRMWARE_UPDATE_BLE_RECEIVING, "Receiving compressed signed firmware"},
        {"ready-to-reboot", FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, "Firmware verified - restarting in 5 seconds"},
        {"error", FIRMWARE_UPDATE_BLE_ERROR, "The update could not continue"},
        {"cancelled", FIRMWARE_UPDATE_BLE_CANCELLED, "Upload cancelled by phone - restarting"},
    };
    const std::string &word = step.args[0].text;
    for (const Fixture &fixture : kFixtures) {
      if (word != fixture.word) continue;
      const bool transfer = oneOf(word, {"confirm-downgrade", "receiving", "ready-to-reboot", "cancelled"});
      const uint32_t total = option(step, "total", transfer ? kUpdateBytes : 0);
      const uint32_t received =
          option(step, "received", word == "ready-to-reboot" ? total : word == "cancelled" ? total / 3 : 0);
      const auto keyOption = step.options.find("key");
      const bool key = keyOption != step.options.end() ? keyOption->second == "yes" : word != "locked";
      const std::string message = step.args.size() > 1 ? step.args[1].text : std::string(fixture.message);
      trace(step, word);
      previewSetFirmwareUpdateState(fixture.state, key, received, total, message.c_str());
      return;
    }
    fail(step, "not implemented");
  }

  void execute(const Step &step) {
    const std::string &name = step.command;
    if (name == "title" || name == "subtitle" || name == "note") return;
    if (name == "boot") return boot(step);

    if (name == "wait") {
      trace(step, step.args[0].text + " ms");
      return advance(static_cast<uint32_t>(integer(step, 0)));
    }
    if (name == "tap") {
      trace(step, step.args[0].text + " " + step.args[1].text);
      return tap(integer(step, 0), integer(step, 1), option(step, "hold", kDefaultHoldMs),
                 option(step, "settle", kDefaultSettleMs));
    }
    if (name == "tap-label") {
      std::vector<VisibleLabel> storage;
      const VisibleLabel &label = findLabel(step, step.args[0].text, option(step, "nth", 1), storage);
      trace(step, "\"" + label.text + "\" -> " + std::to_string(label.x) + "," + std::to_string(label.y));
      warnIfCovered(step, label);
      return tap(label.x, label.y, option(step, "hold", kDefaultHoldMs), option(step, "settle", kDefaultSettleMs));
    }
    if (name == "drag") {
      trace(step, step.args[0].text + "," + step.args[1].text + " -> " + step.args[2].text + "," + step.args[3].text);
      const auto ease = step.options.find("ease");
      return drag(integer(step, 0), integer(step, 1), integer(step, 2), integer(step, 3),
                  static_cast<uint32_t>(integer(step, 4)), ease == step.options.end() || ease->second == "smooth",
                  option(step, "settle", kDefaultSettleMs));
    }
    if (name == "hold") {
      trace(step, step.args[0].text + "," + step.args[1].text + " for " + step.args[2].text + " ms");
      press(integer(step, 0), integer(step, 1));
      advance(static_cast<uint32_t>(integer(step, 2)));
      release();
      return advance(option(step, "settle", kDefaultSettleMs));
    }
    if (name == "path") {
      std::vector<Point> points;
      for (size_t i = 1; i + 1 < step.args.size(); i += 2)
        points.push_back({static_cast<float>(integer(step, i)), static_cast<float>(integer(step, i + 1))});
      trace(step, std::to_string(points.size()) + " points over " + step.args[0].text + " ms");
      const auto ease = step.options.find("ease");
      return drawPath(points, static_cast<uint32_t>(integer(step, 0)),
                      ease == step.options.end() || ease->second == "smooth", option(step, "settle", kDefaultSettleMs));
    }
    if (name == "light") {
      const int raw = integer(step, 0);
      const int target = integer(step, 1);
      const uint32_t over = option(step, "over", 0);
      trace(step, "raw " + step.args[0].text + ", target " + step.args[1].text + "%" +
                      (over ? " over " + std::to_string(over) + " ms" : std::string()));
      if (over == 0) {
        previewSetLightSensor(raw, target);
        return;
      }
      if (!booted_) fail(step, "a light sensor ramp needs the firmware running; put it after boot");
      const int fromRaw = lightSensorRaw();
      const int fromTarget = lightSensorTargetPct();
      return ramp(over, [&](float k) {
        previewSetLightSensor(fromRaw + static_cast<int>(std::lround((raw - fromRaw) * k)),
                              fromTarget + static_cast<int>(std::lround((target - fromTarget) * k)));
      });
    }
    if (name == "companion") {
      const std::string &state = step.args[0].text;
      const auto pausedOption = step.options.find("paused");
      const bool paused = pausedOption != step.options.end() && pausedOption->second == "yes";
      CompanionBleState value = COMPANION_BLE_OFF;
      std::string message = "Companion mode off";
      if (state == "preparing") {
        value = COMPANION_BLE_PREPARING;
        message = "Starting Bluetooth Link";
      } else if (state == "advertising") {
        value = COMPANION_BLE_ADVERTISING;
        message = "Ready for phone connection";
      } else if (state == "connected") {
        value = COMPANION_BLE_CONNECTED;
        message = paused ? "Phone connected - controller Bluetooth paused" : "Phone connected";
      } else if (state == "error") {
        value = COMPANION_BLE_ERROR;
        message = "Bluetooth Link error";
      }
      if (step.args.size() > 1) message = step.args[1].text;
      trace(step, state);
      previewSetCompanionState(value, paused, option(step, "seconds", 0), message.c_str());
      return;
    }
    if (name == "update") return updateFixture(step);
    if (name == "update-request") {
      const bool on = step.args.empty() || step.args[0].text == "on";
      trace(step, on ? "on" : "off");
      previewSetFirmwareUpdateActive(on);
      return;
    }
    if (name == "update-progress") {
      const FirmwareUpdateBleStatus current = firmwareUpdateBleStatus();
      const uint32_t total = current.totalBytes ? current.totalBytes : kUpdateBytes;
      const int64_t from = current.expectedOffset;
      const int64_t to = static_cast<int64_t>(total) * integer(step, 0) / 100;
      trace(step, step.args[0].text + "% over " + step.args[1].text + " ms");
      return ramp(static_cast<uint32_t>(integer(step, 1)), [&](float k) {
        previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_RECEIVING, true,
                                      static_cast<uint32_t>(from + std::llround(static_cast<double>(to - from) * k)),
                                      total, "Receiving compressed signed firmware");
      });
    }
    if (name == "demo") {
      trace(step, step.args[0].text);
      if (!booted_) {
        pendingDemoSpeed_ = step.args[0].text == "off" ? 0 : integer(step, 0);
        return;
      }
      if (step.args[0].text == "off") {
        setDemoMode(false);
        return;
      }
      const uint8_t speed = static_cast<uint8_t>(integer(step, 0));
      if (dashboardDemoModeEnabled) {
        // A running ride keeps its place: change speed the way the Developer options button does,
        // which bills the time already elapsed at the old rate.
        for (int i = 0; i < 5 && demoTimeScale != speed; ++i) cycleDemoTimeScale();
      } else {
        demoTimeScale = speed;
        setDemoMode(true);
      }
      return;
    }
    if (name == "logging") {
      trace(step, step.args[0].text);
      const std::string &mode = step.args[0].text;
      previewSetLoggingState(mode == "off" ? RIDE_LOG_OFF : RIDE_LOG_ON, mode == "recording");
      return;
    }
    if (name == "card") {
      trace(step, step.args[0].text);
      previewSetCardState(step.args[0].text == "ready", step.args[0].text == "checking");
      return;
    }
    if (name == "caption") {
      CaptionEvent event;
      event.frame = nextFrame_;
      event.heading = step.args[0].text;
      event.body = step.args.size() > 1 ? step.args[1].text : std::string();
      const auto until = step.options.find("for");
      if (until != step.options.end())
        event.untilFrame = static_cast<int64_t>(frameAtOrAfter(simMs_ + static_cast<uint64_t>(std::stol(until->second))));
      trace(step, "\"" + event.heading + "\"");
      captions_.push_back(std::move(event));
      return;
    }
    if (name == "caption-off") {
      trace(step, "");
      CaptionEvent event;
      event.frame = nextFrame_;
      event.off = true;
      captions_.push_back(std::move(event));
      return;
    }
    if (name == "still") return still(step);
    if (name == "expect-label") {
      std::vector<VisibleLabel> storage;
      findLabel(step, step.args[0].text, 1, storage);
      trace(step, "\"" + step.args[0].text + "\" ok");
      return;
    }
    if (name == "expect-no-label") {
      for (const VisibleLabel &label : visibleLabels())
        if (label.text == step.args[0].text) fail(step, "label \"" + label.text + "\" is visible but should not be");
      trace(step, "\"" + step.args[0].text + "\" absent");
      return;
    }
    if (name == "dump") {
      std::cerr << stamp() << sceneFile << ':' << step.line << " visible labels: "
                << describeLabels(visibleLabels()) << '\n';
      return;
    }
    fail(step, "not implemented");
  }

  // The framebuffer as the firmware drew it, at the panel's own 320x240.
  void still(const Step &step) {
    if (options_.stills.empty()) return;
    fs::create_directories(options_.stills);
    const fs::path path = options_.stills / (fs::path(sceneFile).stem().string() + "_" + step.args[0].text + ".ppm");
    std::ofstream out(path, std::ios::binary);
    if (!out) fail(step, "cannot write " + path.string());
    out << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
    const lv_color_t *framebuffer = cyd::preview::framebuffer();
    for (size_t i = 0; i < kPixels; ++i) out.write(reinterpret_cast<const char *>(&lutBase_[framebuffer[i].full * 3]), 3);
    trace(step, path.string());
  }

  void writeEvents() const {
    if (options_.events.empty()) return;
    if (!options_.events.parent_path().empty()) fs::create_directories(options_.events.parent_path());
    std::ofstream out(options_.events, std::ios::binary);
    if (!out) throw SceneError("cannot write " + options_.events.string());
    out << "{\n";
    out << "  \"scene\": " << jsonString(fs::path(sceneFile).stem().string()) << ",\n";
    out << "  \"title\": " << jsonString(title_) << ",\n";
    out << "  \"subtitle\": " << jsonString(subtitle_) << ",\n";
    out << "  \"note\": " << jsonString(note_) << ",\n";
    out << "  \"fps\": " << options_.fps << ",\n";
    out << "  \"scale\": " << options_.scale << ",\n";
    out << "  \"width\": " << kWidth * options_.scale << ",\n";
    out << "  \"height\": " << kHeight * options_.scale << ",\n";
    out << "  \"frames\": " << nextFrame_ << ",\n";
    out << "  \"captions\": [";
    for (size_t i = 0; i < captions_.size(); ++i) {
      const CaptionEvent &event = captions_[i];
      out << (i ? ",\n    " : "\n    ") << "{\"frame\": " << event.frame;
      if (event.off) {
        out << ", \"off\": true";
      } else {
        out << ", \"heading\": " << jsonString(event.heading) << ", \"body\": " << jsonString(event.body);
        if (event.untilFrame >= 0) out << ", \"until_frame\": " << event.untilFrame;
      }
      out << '}';
    }
    out << (captions_.empty() ? "]\n" : "\n  ]\n") << "}\n";
  }

  void finish() {
    // Every frame up to the end of the scene has been written; the stream only needs flushing.
    if (raw_) std::fflush(raw_);
    writeEvents();
    char summary[160];
    std::snprintf(summary, sizeof(summary), "%s: %llu frames, %.2f s at %d fps, %dx%d", sceneFile.c_str(),
                  static_cast<unsigned long long>(nextFrame_), static_cast<double>(nextFrame_) / options_.fps,
                  options_.fps, kWidth * options_.scale, kHeight * options_.scale);
    std::cerr << summary << '\n';
    if (options_.digest) {
      char text[64];
      std::snprintf(text, sizeof(text), "digest=%016llx", static_cast<unsigned long long>(digest_));
      std::cerr << text << '\n';
    }
  }
};

// ------------------------------------------------------------------------------ command line

void printUsage() {
  std::cerr << "usage: cyd_demo_recorder <scene.scn> [--raw=PATH|-] [--events=PATH] [--stills=DIR]\n"
               "                         [--fps=N] [--scale=N] [--no-touch] [--no-backlight] [--trace] [--digest]\n"
               "\n"
               "  --raw      write RGB24 frames to PATH, or to stdout with '-'; without it nothing is\n"
               "             encoded and the scene only runs (a fast check of every label and step)\n"
               "  --events   write the caption timeline as JSON for tools/make_demo_video.py\n"
               "  --stills   folder for 'still' steps, written as 320x240 PPM\n"
               "  --fps      video frame rate, 10 to 120 (default 60)\n"
               "  --scale    integer upscale of the 320x240 display (default 4)\n"
               "  --no-touch leave the touch ring out of the frames\n"
               "  --no-backlight  draw every frame at full brightness (the host has no backlight; by\n"
               "             default the brightness setting is drawn as a gain on the picture)\n"
               "  --trace    print every step with its scene time\n"
               "  --digest   print a hash of every frame the firmware drew\n";
}

bool parseInt(const std::string &text, int low, int high, int &value) {
  long parsed = 0;
  if (!parseInteger(text, parsed) || parsed < low || parsed > high) return false;
  value = static_cast<int>(parsed);
  return true;
}

bool parseArguments(int argc, char **argv, Options &options) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.rfind("--raw=", 0) == 0) {
      options.raw = argument.substr(6);
    } else if (argument.rfind("--events=", 0) == 0) {
      options.events = argument.substr(9);
    } else if (argument.rfind("--stills=", 0) == 0) {
      options.stills = argument.substr(9);
    } else if (argument.rfind("--fps=", 0) == 0) {
      if (!parseInt(argument.substr(6), 10, 120, options.fps)) return false;
    } else if (argument.rfind("--scale=", 0) == 0) {
      if (!parseInt(argument.substr(8), 1, 8, options.scale)) return false;
    } else if (argument == "--no-touch") {
      options.touch = false;
    } else if (argument == "--no-backlight") {
      options.backlight = false;
    } else if (argument == "--trace") {
      options.trace = true;
    } else if (argument == "--digest") {
      options.digest = true;
    } else if (argument.rfind("--", 0) == 0) {
      return false;
    } else if (options.scene.empty()) {
      options.scene = argument;
    } else {
      return false;
    }
  }
  return !options.scene.empty();
}

// Frames own stdout, so anything the firmware code prints there would corrupt the stream:
// keep the original handle for frames and point stdout at stderr.
FILE *openFrameStream(const std::string &target) {
  if (target.empty()) return nullptr;
  if (target != "-") {
    FILE *file = std::fopen(target.c_str(), "wb");
    if (!file) throw SceneError("cannot open " + target + " for writing");
    return file;
  }
  std::fflush(stdout);
#ifdef _WIN32
  const int frameFd = _dup(_fileno(stdout));
  _dup2(_fileno(stderr), _fileno(stdout));
  _setmode(frameFd, _O_BINARY);
  FILE *stream = _fdopen(frameFd, "wb");
#else
  const int frameFd = dup(STDOUT_FILENO);
  dup2(STDERR_FILENO, STDOUT_FILENO);
  FILE *stream = fdopen(frameFd, "wb");
#endif
  if (!stream) throw SceneError("cannot take over stdout for the frame stream");
  std::setvbuf(stream, nullptr, _IOFBF, 1 << 20);
  return stream;
}

}  // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parseArguments(argc, argv, options)) {
    printUsage();
    return 2;
  }
  try {
    const std::vector<Step> steps = loadScene(options.scene);
    FILE *raw = openFrameStream(options.raw);
    Recorder recorder(options, raw);
    const int result = recorder.run(steps);
    if (raw && raw != stdout) std::fclose(raw);
    return result;
  } catch (const SceneError &error) {
    std::cerr << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "demo recorder failed: " << error.what() << '\n';
    return 1;
  }
}
