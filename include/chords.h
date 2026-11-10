#pragma once

// Chord engine. Tracks the largest simultaneous set while any key is down,
// confirms full release for 25 ms, then matches include/chord_map.inc once.

void chords_begin();
void chords_poll();  // call after buttons_poll()
