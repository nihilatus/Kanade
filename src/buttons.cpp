#include "buttons.h"

#include <esp_rom_sys.h>

#include "chord_core.h"
#include "chord_timing.h"

// The pull-up has just been enabled. Wait before the level is trusted.
static constexpr uint32_t kButtonSettleUs = 100;

static_assert(BTN_COUNT == kChordKeyCount, "button bits match chord_core");
static_assert(BTN_LL == 0, "chord button ids start at bit 0");

// One GPIO per key, active-low, internal pull-up.
// No matrix and no diodes: simultaneous chords cannot ghost.
// Pin numbers come from pin_map.h. See docs/pinout.md.

static ButtonDef g_buttons[BTN_COUNT];

namespace {
struct FillButtons {
  FillButtons() {
    for (uint8_t i = 0; i < BTN_COUNT; ++i) {
      g_buttons[i].pin = pin_gpio(static_cast<PinFn>(i));
      g_buttons[i].name = chord_key_name(static_cast<int>(i));
    }
  }
};

const FillButtons g_fill_buttons;
}  // namespace

const ButtonDef *const kButtons = g_buttons;

// Both edges. Cheap tactiles usually finish bouncing inside the bounce window
// (factory kChordBounceMs). The chord engine waits again after this mask is
// empty, so a longer debounce mostly adds release latency. The running value
// is the hotspot override, or kChordDebounceMs when that store is clear.

static bool raw_down[BTN_COUNT];
static bool stable_down[BTN_COUNT];
static bool prev_down[BTN_COUNT];
static bool edge_press[BTN_COUNT];
static bool edge_release[BTN_COUNT];
static uint32_t last_change_ms[BTN_COUNT];

static ButtonTraceEvent g_trace[kButtonTraceCap];
static size_t g_trace_head = 0;
static size_t g_trace_count = 0;
static uint16_t g_trace_stable = 0;

static_assert(sizeof(ButtonTraceEvent) == 8, "trace event is 8 bytes");

static void trace_reset() {
  g_trace_head = 0;
  g_trace_count = 0;
  g_trace_stable = 0;
}

static void trace_push(uint32_t ms, ButtonTraceKind kind, uint8_t index, uint16_t value) {
  g_trace[g_trace_head].ms = ms;
  g_trace[g_trace_head].value = value;
  g_trace[g_trace_head].kind = kind;
  g_trace[g_trace_head].index = index;
  g_trace_head = (g_trace_head + 1) % kButtonTraceCap;
  if (g_trace_count < kButtonTraceCap) ++g_trace_count;
}

size_t button_trace_count() { return g_trace_count; }

bool button_trace_at(size_t oldest_index, ButtonTraceEvent *out) {
  if (out == nullptr || oldest_index >= g_trace_count) return false;
  const size_t start = g_trace_count < kButtonTraceCap ? 0 : g_trace_head;
  *out = g_trace[(start + oldest_index) % kButtonTraceCap];
  return true;
}

static void sample_levels_stable() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    const bool down = digitalRead(kButtons[i].pin) == LOW;
    raw_down[i] = down;
    stable_down[i] = down;
    prev_down[i] = down;
    edge_press[i] = false;
    edge_release[i] = false;
    last_change_ms[i] = now;
  }
  g_trace_stable = buttons_mask();
}

void buttons_begin() {
  trace_reset();
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    pinMode(kButtons[i].pin, INPUT_PULLUP);
  }
  esp_rom_delay_us(kButtonSettleUs);
  sample_levels_stable();
}

void buttons_resync() { sample_levels_stable(); }

void buttons_poll() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    edge_press[i] = false;
    edge_release[i] = false;

    const bool down = digitalRead(kButtons[i].pin) == LOW;
    if (down != raw_down[i]) {
      raw_down[i] = down;
      last_change_ms[i] = now;
      trace_push(now, ButtonTraceKind::Raw, i, down ? 1 : 0);
    }

    if ((now - last_change_ms[i]) >= chord_timing_current().debounce_ms &&
        stable_down[i] != raw_down[i]) {
      stable_down[i] = raw_down[i];
    }

    if (stable_down[i] && !prev_down[i]) edge_press[i] = true;
    if (!stable_down[i] && prev_down[i]) edge_release[i] = true;
    prev_down[i] = stable_down[i];
  }

  const uint16_t mask = buttons_mask();
  if (mask != g_trace_stable) {
    trace_push(now, ButtonTraceKind::Stable, 0, mask);
    g_trace_stable = mask;
  }
}

bool button_pressed(ButtonId id) {
  return id < BTN_COUNT && stable_down[id];
}

bool button_just_pressed(ButtonId id) {
  return id < BTN_COUNT && edge_press[id];
}

bool button_just_released(ButtonId id) {
  return id < BTN_COUNT && edge_release[id];
}

uint16_t buttons_mask() {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if (stable_down[i]) mask |= static_cast<uint16_t>(1u << i);
  }
  return mask;
}
