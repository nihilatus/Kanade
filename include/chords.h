#pragma once

#include <Arduino.h>

// Chord engine. Tracks the largest simultaneous set while any key is down,
// confirms full release for 25 ms, then matches the active table once.
// The active table is include/chord_map.inc unless LittleFS has chords.json.

void chords_begin();
void chords_poll();  // call after buttons_poll()

// "saved" after a successful chords.json load or save, otherwise "factory".
const char *chords_source();

// Canonical JSON: {"source":"...","chords":[{id,kana,output,required,optional}]}
bool chords_export_json(String &out);

// Replace the live table and write chords.json. On failure the live table
// and the previous file stay as they were.
bool chords_save_json(const String &body, String &error);

// Delete chords.json and restore the compiled table.
bool chords_reset_saved(String &error);
