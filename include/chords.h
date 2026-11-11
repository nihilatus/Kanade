#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chord_core.h"
#include "settings.h"

// Chord engine. Tracks the largest simultaneous set while any key is down,
// then matches the live table once.
//
// Two send timings. The GPIO switch is the record: open is "release", closed
// to GND is "first". A debounced change applies immediately and is not stored.
//   "release" — wait until every key is up, then kChordReleaseGuardMs, then send.
//               A different chord pressed during that wait sends immediately.
//   "first"   — after the peak, send when the first peak key goes up, then
//               drain until every key is up before a new chord can start.
// Each key is stable only after kChordDebounceMs. Timings live in settings.h.
//
// Boot loads /chords.json from LittleFS when the file is valid. If that file
// is missing, a complete /chords.json.tmp is checked first, then
// /chords.json.bak; a file that validates is renamed back to /chords.json.
// Otherwise the live table is a copy of the compiled factory rows in
// include/chord_map.inc.
// Matching is unchanged: required ⊆ mask ⊆ required ∪ optional, then more
// required bits, fewer optional bits, smaller id.

void chords_begin();
void chords_poll();  // call after buttons_poll()

// Drop the chord in progress and any auto-repeat. The live table and the
// send timing stay. Call when the AP comes up so a half-held chord is not
// sent after the AP goes off.
void chords_abandon();

// "release" or "first", whichever the switch last applied.
const char *chords_fire_mode_name();
// Apply fire_switch_mode(). Switching to first-up sends a chord already
// waiting in the release guard. No-op when the mode is already current.
// Does not write NVS.
void chords_apply_fire_switch();

static constexpr char kChordsPath[] = "/chords.json";
static constexpr char kChordsTmpPath[] = "/chords.json.tmp";
static constexpr char kChordsBakPath[] = "/chords.json.bak";

struct ChordView {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  const char *kana;
  const char *output;
};

size_t chords_count();
bool chords_at(size_t index, ChordView *out);
bool chords_using_saved();
bool chords_fs_ready();

enum class ChordStageStatus : uint8_t { Ok = 0, Rejected, NoMemory };

// Parse into a side buffer. The live table stays put until commit.
// err receives a short Traditional Chinese reason when this is not Ok.
// NoMemory is only the stage-buffer allocation failure.
ChordStageStatus chords_stage_json(const char *json, size_t len, char *err, size_t err_len);
// Copies the staged table over the live one. False leaves the live table as it
// was and keeps the staged copy for the caller to drop.
bool chords_commit_staged();
void chords_drop_staged();
// False only when the live buffers cannot be allocated. The table is then empty.
bool chords_load_factory();

// {"source":"factory"|"saved","count":N,"chords":[...]}
// Key masks are arrays of LL..RT2 names. Caller frees with chords_release().
// Null if the document cannot be built.
char *chords_format_json();
void chords_release(void *pointer);
