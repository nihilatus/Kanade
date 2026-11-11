#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure chord logic: key bits, rule match, peak snapshot, send state machine,
// one poll's masks and wake time, and recipe parsing. No Arduino, GPIO,
// filesystem, or USB. Host tests and the firmware both call this.

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
// Rows with ChordRule::repeat: first tap after the peak stops growing, then
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
  // True when the machine-table column repeat is allow. The caller decides
  // whether a held peak may tap again. This is not the release/first timing.
  bool repeat;
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

// Masks this poll should send, in order, and the next absolute wake on the
// caller's uint32 clock. count is 0, 1, or 2. wake_ms is 0 when nothing is
// strictly in the future: a deadline that is already due is performed by the
// step that observes it, so the caller must not schedule that same time.
struct ChordDispatch {
  uint8_t count;
  uint16_t mask0;
  uint16_t mask1;
  uint32_t wake_ms;
};

// Called only while Holding with a non-empty peak, after this step. Null
// means no peak repeats. The rule table stays with the caller.
using ChordPeakRepeats = bool (*)(uint16_t mask);

// Engine step, then one repeat tap. A repeat that already tapped this hold
// drops the release copy of the same mask. mask0 is the repeat tap when one
// is due; an engine fire that was not swallowed follows it.
// A null engine or rep returns zeros and changes nothing.
ChordDispatch chord_dispatch_step(ChordEngine *engine, ChordRepeat *rep, uint16_t mask,
                                  uint32_t now_ms, ChordPeakRepeats peak_repeats);

// Same send and wake result for a mode change. Switching to FirstUp while
// ReleaseGuard is waiting can emit the waiting peak, and a repeat that
// already tapped that hold swallows it.
ChordDispatch chord_dispatch_set_mode(ChordEngine *engine, ChordRepeat *rep, ChordFireMode mode,
                                      uint32_t now_ms, ChordPeakRepeats peak_repeats);

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
// One settings output field. A longer token is a too_long skip, not a send.
static constexpr size_t kChordTokenMax = 63;

struct ChordSkip {
  // First 23 bytes of a skipped token. too_long leaves this empty; length
  // still holds the full token size.
  char text[24];
  size_t length;
  bool too_long;
};

struct ChordRecipe {
  ChordEvent events[kChordRecipeCap];
  size_t count;
  ChordSkip skips[kChordSkipCap];
  // Records stored in skips. Stops at kChordSkipCap.
  size_t skip_count;
  // Every skipped token, including ones whose text was not stored.
  size_t skip_total;
  // Skip storage filled up. Later tokens are still skipped and counted.
  // The stored text is only the log; the rest of the recipe is still sent.
  bool overflow_skips;
  // A token needed more room than the event buffer. Nothing should be sent.
  // count is 0, so walking events sends nothing even if this flag is missed.
  bool failed;
};

// False when recipe or out is null, or when the recipe does not fit (failed,
// count 0). An unknown token is a skip, not a failure. A token longer than
// kChordTokenMax is a too_long skip. The rest of the recipe still parses.
// tools/recipe.py matches this. test/recipe_vectors.tsv is the shared oracle,
// and the chord-map build fails when a factory output would be skipped.
bool chord_parse_recipe(const char *recipe, ChordRecipe *out);
