#include "console_commands.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "chord_core.h"
#include "chords.h"
#include "config_server.h"
#include "console_log.h"
#include "fire_switch.h"
#include "hid_keyboard.h"
#include "mode_switches.h"
#include "output_switch.h"
#include "pin_map.h"

struct CmdPort {
  Stream *in;
  char buf[24];
  uint8_t len;
  bool overflow;
};

// `keys` toggles this. While on, a debounced buttons_mask() change prints
// bit-order names, for example LL+LU+RT0. Empty mask prints "-".
static bool g_key_trace = false;
static uint16_t g_key_trace_mask = 0;

static bool mode_switch_level_closed(ModeSwitch id) {
  switch (id) {
    case ModeSwitch::Wifi:
      return config_server_wifi_closed();
    case ModeSwitch::Output:
      return output_switch_closed();
    case ModeSwitch::Fire:
      return fire_switch_mode() == ChordFireMode::FirstUp;
    case ModeSwitch::Count:
      break;
  }
  return false;
}

static void print_switches() {
  char line[72];
  snprintf(line, sizeof(line), "switches %d", kModeSwitchCount);
  console_println(line);
  const OutputMode boot = output_switch_mode();
  for (int i = 0; i < kModeSwitchCount; ++i) {
    const ModeSwitchDesc &row = kModeSwitches[i];
    const bool closed = mode_switch_level_closed(row.id);
    const char *label = closed ? row.closed_label : row.open_label;
    if (row.id == ModeSwitch::Output) {
      label = boot == OutputMode::Ble ? row.closed_label : row.open_label;
    }
    const char *edge = mode_switch_restarts(row.change, boot) ? "restart" : "live";
    if (row.id == ModeSwitch::Wifi) {
      snprintf(line, sizeof(line), "%s GPIO%d %s %s %s %s", row.key, pin_gpio(row.pin),
               closed ? "closed" : "open", label, config_ap_is_up() ? "AP" : "AP down", edge);
    } else {
      snprintf(line, sizeof(line), "%s GPIO%d %s %s %s", row.key, pin_gpio(row.pin),
               closed ? "closed" : "open", label, edge);
    }
    console_println(line);
  }
}

static void format_key_trace(uint16_t mask, char *buf, size_t buflen) {
  if (buflen == 0) return;
  size_t used = 0;
  buf[0] = '\0';
  for (int i = 0; i < kChordKeyCount; ++i) {
    if ((mask & static_cast<uint16_t>(1u << i)) == 0) continue;
    const char *name = chord_key_name(i);
    if (name == nullptr) continue;
    const size_t len = strlen(name);
    const size_t extra = used == 0 ? 0 : 1;
    if (used + extra + len + 1 > buflen) break;
    if (extra != 0) buf[used++] = '+';
    memcpy(buf + used, name, len);
    used += len;
    buf[used] = '\0';
  }
  if (used == 0 && buflen > 1) {
    buf[0] = '-';
    buf[1] = '\0';
  }
}

static void dump_button_trace() {
  const size_t n = button_trace_count();
  if (n == 0) {
    console_println("trace empty");
    return;
  }
  char line[80];
  char keys[64];
  for (size_t i = 0; i < n; ++i) {
    ButtonTraceEvent ev;
    if (!button_trace_at(i, &ev)) break;
    if (ev.kind == ButtonTraceKind::Raw) {
      const char *name = ev.index < kChordKeyCount ? chord_key_name(ev.index) : nullptr;
      if (name == nullptr) name = "?";
      snprintf(line, sizeof(line), "%lu raw %s %s", static_cast<unsigned long>(ev.ms), name,
               ev.value != 0 ? "down" : "up");
    } else {
      format_key_trace(ev.value, keys, sizeof(keys));
      snprintf(line, sizeof(line), "%lu stable %s", static_cast<unsigned long>(ev.ms), keys);
    }
    console_println(line);
  }
  snprintf(line, sizeof(line), "trace %u", static_cast<unsigned>(n));
  console_println(line);
}

static void poll_cmd_port(CmdPort &port) {
  if (port.in == nullptr) return;
  while (port.in->available() > 0) {
    const int raw = port.in->read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);
    if (c == '\r') continue;
    if (c != '\n') {
      if (port.overflow) continue;
      if (port.len + 1 < sizeof(port.buf)) port.buf[port.len++] = c;
      else {
        port.overflow = true;
        port.len = 0;
      }
      continue;
    }
    if (port.overflow) {
      port.overflow = false;
      port.len = 0;
      console_println("指令過長");
      continue;
    }
    port.buf[port.len] = '\0';
    port.len = 0;

    char *arg = port.buf;
    while (*arg == ' ' || *arg == '\t') ++arg;
    if (strncmp(arg, "drops", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ' || arg[5] == '\t')) {
      char *rest = arg + 5;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      hid_print_drops();
      continue;
    }
    if (strncmp(arg, "keys", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ' || arg[4] == '\t')) {
      char *rest = arg + 4;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      g_key_trace = !g_key_trace;
      if (g_key_trace) g_key_trace_mask = buttons_mask();
      console_println(g_key_trace ? "keys on" : "keys off");
      continue;
    }
    if (strncmp(arg, "trace", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ' || arg[5] == '\t')) {
      char *rest = arg + 5;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      dump_button_trace();
      continue;
    }
    if (strncmp(arg, "switches", 8) == 0 && (arg[8] == '\0' || arg[8] == ' ' || arg[8] == '\t')) {
      char *rest = arg + 8;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      print_switches();
      continue;
    }
    if (strncmp(arg, "fire", 4) != 0) continue;
    if (arg[4] != '\0' && arg[4] != ' ' && arg[4] != '\t') continue;
    arg += 4;
    while (*arg == ' ' || *arg == '\t') ++arg;
    size_t n = strlen(arg);
    while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\t')) arg[--n] = '\0';
    if (arg[0] == '\0') {
      char line[40];
      snprintf(line, sizeof(line), "fire %s", chords_fire_mode_name());
      console_println(line);
      continue;
    }
    console_println("送出模式由開關決定");
  }
}

static CmdPort g_uart{&Serial0, {}, 0, false};
#if ARDUINO_USB_CDC_ON_BOOT
static CmdPort g_usb{&Serial, {}, 0, false};
#endif

void console_commands_poll() {
  poll_cmd_port(g_uart);
#if ARDUINO_USB_CDC_ON_BOOT
  poll_cmd_port(g_usb);
#endif
}

void console_commands_trace_keys() {
  if (!g_key_trace) return;
  const uint16_t mask = buttons_mask();
  if (mask == g_key_trace_mask) return;
  g_key_trace_mask = mask;
  char line[64];
  format_key_trace(mask, line, sizeof(line));
  console_println(line);
}
