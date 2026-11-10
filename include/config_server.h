#pragma once

// SoftAP "JIS-Chord" plus the chord JSON API and the phone editor page.
// config_server_poll() is server.handleClient(); call it from loop().

void config_server_begin();
void config_server_poll();
