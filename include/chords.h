#pragma once

#include <stddef.h>
#include <stdint.h>

// Chord engine. Tracks the largest simultaneous set while any key is down,
// then matches the live table once.
//
// Two send timings, stored in NVS and switchable at runtime.
// With no stored key, boot uses CHORD_FIRE_DEFAULT_FIRST (0 = release).
// A stored value overrides that flag. Timings:
//   "release" — wait until every key is up, then kChordReleaseGuardMs, then send.
//   "first"   — after the peak, send when the first peak key goes up, then
//               drain until every key is up before a new chord can start.
// Each key is stable only after kChordDebounceMs. Both constants live in
// chord_core.h.
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

// "release" or "first". Setter returns false for any other name.
const char *chords_fire_mode_name();
bool chords_set_fire_mode_name(const char *name);

static constexpr size_t kChordsMaxRows = 256;
static constexpr size_t kChordsJsonMax = 48 * 1024;
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
void chords_commit_staged();
void chords_drop_staged();
void chords_load_factory();

// {"source":"factory"|"saved","count":N,"chords":[...]}
// Key masks are arrays of LL..RT2 names. Caller frees with chords_release().
// Null if the document cannot be built.
char *chords_format_json();
void chords_release(void *pointer);
