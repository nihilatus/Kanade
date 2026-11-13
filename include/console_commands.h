#pragma once

// UART0 (+ USB CDC when enabled) line commands and key-trace diagnostics.
// main polls these; parsing and formatting stay out of the wiring diagram.

void console_commands_poll();
void console_commands_trace_keys();
