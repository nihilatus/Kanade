#pragma once

// SoftAP plus the chord JSON API and the phone editor page.
// GPIO15 to GND turns the AP on; open (internal pull-up) turns it off,
// including while the keyboard is running. This pin is not a chord key.
// config_server_poll() debounces that switch. While the AP is up it reads and
// writes at most a small slice of one HTTP client and returns, so a slow or
// oversized request cannot stall the key scan. JSON parsing and LittleFS run
// on the other core. If that task cannot start, chord GET, POST, and reset
// answer 503 and do not run on the key scan. The live chord table is swapped
// only on this poll.
//
// The AP passphrase is the trust boundary. A non-empty WIFI_API_TOKEN is the
// API token and is not printed. When it is empty, the first boot generates a
// token, stores it in NVS, and prints it once on the serial console. /api/*
// then requires that token. A browser Origin other than the config page is
// still rejected. If the generated token cannot be stored, /api/* stays closed.

void config_server_begin();
void config_server_poll();
