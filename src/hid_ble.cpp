#include "hid_ble.h"

#include <atomic>
#include <string.h>

#include <Arduino.h>
#include <freertos/task.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include "class/hid/hid_device.h"

#include "console_log.h"
#include "host_layout.h"

// Same 8-byte boot keyboard report USBHIDKeyboard sends, without a report-id
// prefix. The id lives in the report map and in the Report Reference
// descriptor. Usages run 0–255 so pressRaw of Henkan and the other JIS keys
// matches the USB descriptor.
static uint8_t g_report_map[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1))};

static constexpr uint8_t kReportId = 1;
static constexpr uint8_t kShiftBit = 0x80;
static constexpr uint8_t kAltGrBit = 0x40;
static constexpr uint8_t kIsoKey = 0x64;
static constexpr uint8_t kIsoReplacement = 0x32;
static constexpr uint8_t kLeftShiftMod = 0x02;
static constexpr uint8_t kRightAltMod = 0x40;
static constexpr uint16_t kNotifyBit = 0x0001;

struct KeyReport {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
};
static_assert(sizeof(KeyReport) == 8, "BLE input report matches the USB keyboard report");

static KeyReport g_report{};
static BLECharacteristic *g_input = nullptr;
static std::atomic<bool> g_connected{false};
static std::atomic<bool> g_subscribed{false};
static std::atomic<bool> g_drop{false};
static bool g_started = false;
// Notifications accepted by NimBLE and not yet reported back by NOTIFY_TX.
// Bluedroid has no transmit-complete event, so this stays zero there.
static std::atomic<uint8_t> g_inflight{0};
// Set to the caller around notify(). The synchronous onStatus runs on that
// task; NOTIFY_TX runs on the NimBLE host task. A plain flag would hide the
// completion if it arrived before notify() returned.
static std::atomic<TaskHandle_t> g_notify_task{nullptr};
static std::atomic<bool> g_notify_ok{false};

static bool notifications_on() {
  if (!g_connected.load()) return false;
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (g_input == nullptr) return false;
  BLE2902 *cccd = static_cast<BLE2902 *>(g_input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902)));
  return cccd != nullptr && cccd->getNotifications();
#else
  return g_subscribed.load();
#endif
}

static void clear_inflight() { g_inflight.store(0); }

static void release_inflight() {
  uint8_t cur = g_inflight.load();
  while (cur > 0 && !g_inflight.compare_exchange_weak(cur, static_cast<uint8_t>(cur - 1))) {
  }
}

static void note_subscription(bool now) {
  const bool was = g_subscribed.exchange(now);
  if (was == now) return;
  clear_inflight();
  if (now) console_println("BLE subscribed");
  else g_drop.store(true);
}

class InputCallbacks : public BLECharacteristicCallbacks {
  void onStatus(BLECharacteristic *characteristic, Status status, uint32_t code) override {
    (void)characteristic;
    (void)code;
    if (g_notify_task.load() == xTaskGetCurrentTaskHandle()) {
      g_notify_ok.store(status == BLECharacteristicCallbacks::Status::SUCCESS_NOTIFY);
      return;
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    // Success or failure: the controller is done with this buffer.
    (void)status;
    release_inflight();
#else
    (void)status;
#endif
  }
#if defined(CONFIG_NIMBLE_ENABLED)
  void onSubscribe(BLECharacteristic *characteristic, ble_gap_conn_desc *desc, uint16_t subValue) override {
    (void)characteristic;
    (void)desc;
    note_subscription((subValue & kNotifyBit) != 0);
  }
#endif
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    (void)server;
    g_connected.store(true);
    console_println("BLE connected");
  }
  void onDisconnect(BLEServer *server) override {
    (void)server;
    g_connected.store(false);
    g_subscribed.store(false);
    clear_inflight();
    g_drop.store(true);
    console_println("BLE disconnected");
  }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  uint32_t onPassKeyRequest() override { return 0; }
  bool onConfirmPIN(uint32_t pin) override {
    (void)pin;
    return false;
  }
  bool onAuthorizationRequest(uint16_t connHandle, uint16_t attrHandle, bool isRead) override {
    (void)connHandle;
    (void)attrHandle;
    (void)isRead;
    return true;
  }
#if defined(CONFIG_NIMBLE_ENABLED)
  void onAuthenticationComplete(ble_gap_conn_desc *desc, int status) override {
    (void)desc;
    console_println(status == 0 ? "BLE encrypted" : "BLE auth failed");
  }
#elif defined(CONFIG_BLUEDROID_ENABLED)
  void onAuthenticationComplete(esp_ble_auth_cmpl_t desc) override {
    console_println(desc.success ? "BLE encrypted" : "BLE auth failed");
  }
#endif
};

static InputCallbacks g_input_cb;
static ServerCallbacks g_server_cb;
static SecurityCallbacks g_security_cb;

static bool send_report() {
  if (g_input == nullptr || !notifications_on()) return false;
  g_input->setValue(reinterpret_cast<uint8_t *>(&g_report), sizeof(g_report));
  g_notify_ok.store(false);
#if defined(CONFIG_NIMBLE_ENABLED)
  // Count this attempt before notify(). A completion that arrives inside the
  // call must not be stranded after a later increment.
  g_inflight.fetch_add(1);
#endif
  g_notify_task.store(xTaskGetCurrentTaskHandle());
  g_input->notify();
  g_notify_task.store(nullptr);
  if (g_notify_ok.load()) return true;
#if defined(CONFIG_NIMBLE_ENABLED)
  release_inflight();
#endif
  return false;
}

static bool press_raw(uint8_t k) {
  if (k >= 0xE0 && k < 0xE8) {
    g_report.modifiers = static_cast<uint8_t>(g_report.modifiers | (1u << (k - 0xE0)));
  } else if (k != 0 && k < 0xA5) {
    bool found = false;
    for (uint8_t slot : g_report.keys) {
      if (slot == k) {
        found = true;
        break;
      }
    }
    if (!found) {
      bool placed = false;
      for (uint8_t &slot : g_report.keys) {
        if (slot == 0) {
          slot = k;
          placed = true;
          break;
        }
      }
      // Six slots are full. Retrying this event would stall the queue.
      if (!placed) return true;
    }
  } else if (g_report.modifiers == 0) {
    // No key usage and no modifier. Nothing to put on the air.
    return true;
  }
  return send_report();
}

static bool press_ascii(uint8_t k) {
  if (k >= 0x88) {
    k = static_cast<uint8_t>(k - 0x88);
  } else if (k >= 0x80) {
    g_report.modifiers = static_cast<uint8_t>(g_report.modifiers | (1u << (k - 0x80)));
    k = 0;
  } else {
    k = kLayoutJis.ascii[k];
    if (k == 0) return true;
    if ((k & kShiftBit) != 0) {
      g_report.modifiers = static_cast<uint8_t>(g_report.modifiers | kLeftShiftMod);
      k = static_cast<uint8_t>(k & static_cast<uint8_t>(~kShiftBit));
    }
    if ((k & kAltGrBit) != 0) {
      g_report.modifiers = static_cast<uint8_t>(g_report.modifiers | kRightAltMod);
      k = static_cast<uint8_t>(k & static_cast<uint8_t>(~kAltGrBit));
    }
    if (k == kIsoReplacement) k = kIsoKey;
  }
  return press_raw(k);
}

bool hid_ble_ready() { return g_started && notifications_on(); }

bool hid_ble_can_send() {
  if (!notifications_on()) return false;
#if defined(CONFIG_NIMBLE_ENABLED)
  return g_inflight.load() == 0;
#else
  return true;
#endif
}

bool hid_ble_take_drop() { return g_drop.exchange(false); }

bool hid_ble_press(uint8_t code) { return press_ascii(code); }

bool hid_ble_press_raw(uint8_t code) { return press_raw(code); }

bool hid_ble_release_all() {
  memset(&g_report, 0, sizeof(g_report));
  return send_report();
}

bool hid_ble_begin() {
  if (g_started) return true;
#if !defined(CONFIG_NIMBLE_ENABLED) && !defined(CONFIG_BLUEDROID_ENABLED)
  console_println("BLE stack missing");
  return false;
#else
  if (!BLEDevice::init("Kanade")) {
    console_println("BLE init failed");
    return false;
  }
  // init() leaves the bonding flag off. Turn on Just Works bonding after it,
  // and distribute the identity key so a later boot can reconnect a bonded
  // host. Nothing here erases the stack's bond store.
  BLESecurity security;
  security.setCapability(ESP_IO_CAP_NONE);
  security.setAuthenticationMode(true, false, true);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEDevice::setSecurityCallbacks(&g_security_cb);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&g_server_cb);
  BLEHIDDevice *hid = new BLEHIDDevice(server);
  hid->manufacturer()->setValue(String("Kanade"));
  hid->pnp(0x02, 0x303A, 0x4004, 0x0100);
  hid->hidInfo(0x00, 0x02);
  hid->reportMap(g_report_map, sizeof(g_report_map));
  hid->setBatteryLevel(100);
  g_input = hid->inputReport(kReportId);
  g_input->setCallbacks(&g_input_cb);
  hid->startServices();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLEUUID((uint16_t)0x1812));
  advertising->setAppearance(HID_KEYBOARD);
  advertising->setScanResponse(true);
  advertising->setMaxPreferred(0x0C);
  advertising->setMinPreferred(0x06);
  if (!advertising->start()) {
    console_println("BLE advertising failed");
    return false;
  }
  g_started = true;
  console_println("BLE advertising");
  return true;
#endif
}
