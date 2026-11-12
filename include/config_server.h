#pragma once

#include "output_policy.h"

// SoftAP plus the chord JSON API and the phone editor page.
// GPIO15 to GND turns the AP on; open (internal pull-up) turns it off.
// This pin is not a chord key. config_server_poll() debounces that switch.
// While the AP is up the caller does not scan chord keys or send USB HID.
// This poll still reads and writes at most a small slice of one HTTP client
// and returns, so a slow or oversized request cannot delay noticing the
// switch open and resuming the keyboard. JSON parsing, LittleFS, and timing
// NVS writes run on the other core. If that task cannot start, chord GET,
// POST, and reset, and timing POST and reset, answer 503 and do not run on
// this poll. GET /api/timing reads the values already loaded at boot. The
// live chord table is swapped only on this poll.
//
// config_ap_is_up() is true only after the AP has started. A closed switch
// whose passphrase is missing or rejected leaves the AP down. In USB mode the
// keyboard keeps running. In BLE mode a closed switch does not start BLE.
//
// The AP passphrase is the trust boundary. A non-empty WIFI_API_TOKEN is the
// API token and is not printed. When it is empty, the first boot generates a
// token, stores it in NVS, and prints it once on the serial console. /api/*
// then requires that token. A browser Origin other than the config page is
// still rejected. If the generated token cannot be stored, /api/* stays closed.

// Call before config_server_begin(). USB mode keeps the live wifi switch.
// BLE mode restarts on a debounced wifi edge and does not start or stop the
// AP in the process that noticed the edge. The level latched at begin is not
// an edge: a switch that is already closed only serves the AP.
void config_server_set_output_mode(OutputMode mode);
bool config_server_restart_requested();
// Debounced level. Valid after config_server_begin().
bool config_server_wifi_closed();

void config_server_begin();
void config_server_poll();
bool config_ap_is_up();
