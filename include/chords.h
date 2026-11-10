#pragma once

// Minimal chord → romaji stub.
// Full engine later ports timing + mapping_table.csv from sibling
// "joystick writer" repo. Do not expect complete kana coverage here.

void chords_begin();
void chords_poll();  // call after buttons_poll()
