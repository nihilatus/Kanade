#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure chord logic: key bits, rule match, peak snapshot, send state machine,
// and recipe parsing. No Arduino, GPIO, filesystem, or USB. Host tests and
// the firmware both call this.

static constexpr int kChordKeyCount = 14;
// Raw GPIO must hold this long before the chord mask changes.
static constexpr uint8_t kChordDebounceMs = 8;
// ReleaseAll waits this long after the debounced mask stays empty.
static constexpr uint32_t kChordReleaseGuardMs = 10;
// A subset or superset of the peak is the same chord only until this gap.
// The engine sees debounced edges, so a contact that re-closes as the release
// is accepted shows up about one debounce later. The practice page has no
// GPIO debounce; rolls of 15 ms already clear the guard, and 1–9 ms drops a
// nested chord such as あ after か. 10 ms keeps that bounce and still splits
// a nested mask that arrives with the guard already elapsed (a stalled poll).
static constexpr uint32_t kChordBounceMs = 10;
static_assert(kChordBounceMs > kChordDebounceMs,
              "bounce window must cover a debounced return");
static_assert(kChordBounceMs <= kChordReleaseGuardMs,
              "nested chords must split once the release guard has elapsed");
static constexpr uint8_t kChordTapGapMs = 12;
static constexpr uint8_t kChordGapNone = 0;
// Held arrows and Backspace: first tap after the peak stops growing, then
// repeat. The delay is measured from the same moment as the settle. One poll
// sends at most one tap, so a late poll does not catch up.
static constexpr uint16_t kChordRepeatSettleMs = 50;
static constexpr uint16_t kChordRepeatDelayMs = 400;
static constexpr uint8_t kChordRepeatIntervalMs = 33;
static_assert(kChordRepeatDelayMs > kChordRepeatSettleMs,
              "repeat starts after the first tap");
static_assert(kChordRepeatIntervalMs > kChordTapGapMs * 2,
              "repeat gap must outlast one press and release");

// Bit i is key i. Order is LL LU LR LD LT0 LT1 LT2 RL RU RR RD RT0 RT1 RT2.
const char *chord_key_name(int index);
int chord_key_index(const char *name);

struct ChordRule {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  const char *kana;
  const char *output;
};

// required ⊆ mask ⊆ required ∪ optional.
// More required bits, then fewer optional bits, then smaller id.
// -1 when nothing matches. rules may be null.
int chord_find_rule(const ChordRule *rules, size_t count, uint16_t mask);

struct ChordPeak {
  uint16_t mask;
  uint8_t count;
};

void chord_peak_clear(ChordPeak *peak);
// Largest set wins. Same size and a different mask: keep this later snapshot.
void chord_note_snapshot(ChordPeak *peak, uint16_t mask);
bool chord_peak_broken(const ChordPeak &peak, uint16_t mask);

enum class ChordPhase : uint8_t { Idle = 0, Holding, ReleaseGuard, Draining };
enum class ChordFireMode : uint8_t { ReleaseAll = 0, FirstUp = 1 };

struct ChordEngine {
  ChordPhase phase;
  ChordFireMode mode;
  ChordPeak peak;
  uint32_t guard_since;
};

struct ChordStep {
  bool fire;
  uint16_t fire_mask;
};

void chord_engine_reset(ChordEngine *engine);

// One poll. now_ms is the caller's clock. fire is set only when the mask
// that would be sent is non-empty.
// ReleaseAll: an empty mask waits kChordReleaseGuardMs. A mask that returns
// before kChordBounceMs and is a subset or superset of the peak stays one
// chord. Any later mask, including a nested one, sends the waiting peak on
// this step and starts a new hold.
ChordStep chord_engine_step(ChordEngine *engine, uint16_t mask, uint32_t now_ms);

// Switching to FirstUp while ReleaseGuard is waiting sends immediately.
ChordStep chord_engine_set_mode(ChordEngine *engine, ChordFireMode mode);

// True only for a whole output that is Up, Down, Left, Right, or Backspace.
bool chord_output_auto_repeats(const char *output);

enum class ChordRepeatPhase : uint8_t { Idle = 0, Watching, Sent, Repeating };

struct ChordRepeat {
  ChordRepeatPhase phase;
  uint16_t mask;
  uint32_t since_ms;
  uint32_t next_ms;
};

struct ChordRepeatStep {
  bool send;
  uint16_t mask;
  bool swallow_fire;
};

void chord_repeat_reset(ChordRepeat *rep);

// Call after chord_engine_step or chord_engine_set_mode with that step's fire
// fields. peak_mask is the engine peak while Holding, otherwise unused.
// peak_repeats is the caller's lookup of that peak. send is one tap of mask.
// swallow_fire drops the engine fire when this hold already produced a tap.
ChordRepeatStep chord_repeat_step(ChordRepeat *rep, ChordPhase phase, uint16_t peak_mask,
                                  bool peak_repeats, bool engine_fired, uint16_t fired_mask,
                                  uint32_t now_ms);

enum class ChordKeyKind : uint8_t {
  Char = 0,
  Shift,
  Ctrl,
  Alt,
  Gui,
  Backspace,
  Space,
  Enter,
  Esc,
  Up,
  Down,
  Left,
  Right,
  Tab,
  // USB HID Keyboard usage in ChordEvent.code. Firmware sends it with
  // pressRaw so usages above 0x77 (変換, 無変換, カタカナひらがな) are not
  // folded by the ASCII layout map.
  Raw,
};

enum class ChordEvOp : uint8_t { Write = 0, Press, ReleaseAll };

struct ChordEvent {
  ChordEvOp op;
  ChordKeyKind key;
  uint8_t code;
  uint8_t gap_after_ms;
};

static constexpr size_t kChordRecipeCap = 96;
static constexpr size_t kChordSkipCap = 32;

struct ChordSkip {
  char text[24];
  size_t length;
  bool too_long;
};

struct ChordRecipe {
  ChordEvent events[kChordRecipeCap];
  size_t count;
  ChordSkip skips[kChordSkipCap];
  size_t skip_count;
  // Skip storage filled up and later tokens were not recorded. Do not send:
  // one unknown token is typed around, but missing records would be silent holes.
  bool overflow_skips;
  // A token needed more room than the event buffer. Nothing should be sent.
  // count is 0, so walking events sends nothing even if this flag is missed.
  bool failed;
};

// False when recipe or out is null, or when the recipe does not fit (failed,
// count 0). An unknown token is a skip, not a failure. A token of 24 bytes or
// more is a too_long skip. The rest of the recipe still parses.
// tools/recipe.py matches this. test/recipe_vectors.tsv is the shared oracle,
// and the chord-map build fails when a factory output would be skipped.
bool chord_parse_recipe(const char *recipe, ChordRecipe *out);
