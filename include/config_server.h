#pragma once

// SoftAP plus the chord JSON API and the phone editor page.
// GPIO15 to GND turns the AP on; open (internal pull-up) turns it off,
// including while the keyboard is running. This pin is not a chord key.
// config_server_poll() debounces that switch and calls server.handleClient()
// only while the AP is up.

void config_server_begin();
void config_server_poll();
