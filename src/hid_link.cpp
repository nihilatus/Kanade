#include "hid_link.h"

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "class/hid/hid_device.h"

#include "hid_ble.h"
#include "host_layout.h"
#include "output_switch.h"

// app_main() calls USB.begin() before setup() when CDC is on. TinyUSB refuses
// a HID device added after that. This constructor runs first and registers the
// USB keyboard only when the latched boot level is open. A closed pin is BLE:
// leave HID off so the OTG port does not enumerate as a keyboard.
static USBHIDKeyboard *g_usb_keys = nullptr;

struct UsbKeyboardEarly {
  UsbKeyboardEarly() {
    if (!output_switch_boot_closed()) g_usb_keys = new USBHIDKeyboard();
  }
};

static UsbKeyboardEarly g_usb_early;

static USBHIDKeyboard &usb_keys() { return *g_usb_keys; }

static OutputMode g_mode = OutputMode::Usb;
static bool g_begun = false;

static void usb_begin() {
  if (g_usb_keys == nullptr) return;
  // ASCII goes through the active HostLayout. Only JIS is implemented.
  // a–z and the digits share usages with a US keyboard; the characters this
  // table actually sends that do not are '[' (0x30) and ']' (0x31).
  // Shift+8 / Shift+9 become （ ） when the host keyboard type matches
  // Kanade's layout. The USB country code stays unset: it does not switch
  // the host layout. Set the host keyboard type to Kanade's layout.
  // Hankaku (0x35), Henkan (0x8A), Muhenkan (0x8B), and Kana (0x88) are
  // pressRaw usages Windows maps under that keyboard type. Eisuu is LANG2
  // (0x91), which macOS uses for 英数 and Windows often ignores.
  // The stock keyboard descriptor already declares usages 0–255.
  USB.VID(0x303A);  // Espressif
  USB.PID(0x4004);  // local prototype — change before shipping
  USB.productName("Kanade");
  USB.manufacturerName("Kanade");
  USB.serialNumber("kanade-1");
  usb_keys().begin(kLayoutJis.ascii);
  USB.begin();
}

bool hid_link_begin(OutputMode mode) {
  if (g_begun) return true;
  g_mode = mode;
  if (mode == OutputMode::Usb) {
    if (g_usb_keys == nullptr) return false;
    usb_begin();
    g_begun = true;
    return true;
  }
  if (!hid_ble_begin()) return false;
  g_begun = true;
  return true;
}

bool hid_link_ready() {
  if (!g_begun) return false;
  if (g_mode == OutputMode::Usb) return static_cast<bool>(USB);
  return hid_ble_ready();
}

bool hid_link_can_send() {
  if (!hid_link_ready()) return false;
  if (g_mode == OutputMode::Usb) return tud_hid_ready();
  return true;
}

bool hid_link_take_drop() {
  if (!g_begun || g_mode != OutputMode::Ble) return false;
  return hid_ble_take_drop();
}

void hid_link_press(uint8_t code) {
  if (!hid_link_ready()) return;
  if (g_mode == OutputMode::Usb) usb_keys().press(code);
  else hid_ble_press(code);
}

void hid_link_press_raw(uint8_t code) {
  if (!hid_link_ready()) return;
  if (g_mode == OutputMode::Usb) usb_keys().pressRaw(code);
  else hid_ble_press_raw(code);
}

void hid_link_release_all() {
  if (!g_begun) return;
  if (g_mode == OutputMode::Usb) {
    if (hid_link_ready()) usb_keys().releaseAll();
    return;
  }
  hid_ble_release_all();
}
