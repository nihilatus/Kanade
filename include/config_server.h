#pragma once

// SoftAP plus the chord JSON API and the phone editor page.
// GPIO15 to GND turns the AP on; open (internal pull-up) turns it off,
// including while the keyboard is running. This pin is not a chord key.
// config_server_poll() debounces that switch. While the AP is up it reads and
// writes at most a small slice of one HTTP client and returns, so a slow or
// oversized request cannot stall the key scan. JSON parsing and LittleFS run
// on the other core. The live chord table is swapped only on this poll.

void config_server_begin();
void config_server_poll();
