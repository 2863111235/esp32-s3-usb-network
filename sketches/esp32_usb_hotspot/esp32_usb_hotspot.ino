// Windows ICS -> USB CDC-NCM -> Wi-Fi SoftAP.
// EspUsbDevice (MIT) owns USB, including its DMA-enabled TinyUSB stack.
// Raw Wi-Fi forwarding follows Espressif's CC0/Unlicense tusb_ncm example.
#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"

static const char *SSID = "ESP32-S3-PC";
static const char *PASSWORD = "S3Net8264";
static const uint8_t USB_MAC[6] = {0x02, 0xa3, 0x53, 0x19, 0x00, 0x01};
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "Hotspot Console");
EspUsbDeviceNet net(device);
static std::atomic<bool> apReady{false};
static std::atomic<uint32_t> wifiRx{0}, usbQueued{0}, usbRx{0}, wifiTx{0}, drops{0};

static esp_err_t wifiReceive(void *buffer, uint16_t len, void *eb) {
  ++wifiRx;
  // sendFrame copies into the library's bounded TX queue before returning.
  if (len >= 14 && len <= 1514 && net.sendFrame(static_cast<uint8_t *>(buffer), len)) ++usbQueued;
  else ++drops;
  esp_wifi_internal_free_rx_buffer(eb);
  return ESP_OK;
}

static void usbReceive(const uint8_t *data, size_t len) {
  ++usbRx;
  if (apReady && len >= 14 && len <= 1514 &&
      esp_wifi_internal_tx(WIFI_IF_AP, const_cast<uint8_t *>(data), len) == ESP_OK) ++wifiTx;
  else ++drops;
}

static void printStatus() {
  wifi_sta_list_t clients = {};
  esp_wifi_ap_get_sta_list(&clients);
  console.printf("HOTSPOT_V2 ssid=%s clients=%d usb=%d ap=%d wifi_rx=%lu usb_queued=%lu usb_rx=%lu wifi_tx=%lu drop=%lu heap=%lu\n",
    SSID, clients.num, net.linkUp(), apReady.load(),
    (unsigned long)wifiRx.load(), (unsigned long)usbQueued.load(),
    (unsigned long)usbRx.load(), (unsigned long)wifiTx.load(),
    (unsigned long)drops.load(), (unsigned long)ESP.getFreeHeap());
  for (unsigned i = 0; i < clients.num; ++i) {
    const uint8_t *m = clients.sta[i].mac;
    console.printf("CLIENT %02x:%02x:%02x:%02x:%02x:%02x rssi=%d\n",
      m[0],m[1],m[2],m[3],m[4],m[5],clients.sta[i].rssi);
  }
}

void setup() {
  // Clear any color retained by the onboard WS2812 across a CPU reset.
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
  net.macAddress(USB_MAC);
  net.onFrame(usbReceive);
  EspUsbDeviceConfig usb;
  usb.vid = 0x303a;
  usb.pid = 0x1001;
  usb.deviceVersion = 0x0200;
  usb.manufacturer = "Espressif Systems";
  usb.product = "ESP32-S3 USB Network";
  usb.serialNumber = "14C19FC73EEC";
  if (!device.begin(usb)) return;
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t eventResult = esp_event_loop_create_default();
  if (eventResult != ESP_OK && eventResult != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(eventResult);
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  wifi_config_t config = {};
  strlcpy(reinterpret_cast<char *>(config.ap.ssid), SSID, sizeof(config.ap.ssid));
  strlcpy(reinterpret_cast<char *>(config.ap.password), PASSWORD, sizeof(config.ap.password));
  config.ap.ssid_len = strlen(SSID);
  config.ap.channel = 6;
  config.ap.max_connection = 4;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_AP, wifiReceive));
  apReady = true;
  // No ESP DHCP server or USB netif: Windows ICS handles DHCP, DNS and NAT.
}

void loop() {
  static uint32_t lastStatus;
  if (console.available()) {
    char command = console.read();
    if (command == 's') printStatus();
    if (command == 'b') device.rebootToBootloader();
    if (command == 'r') ESP.restart();
  }
  if (apReady && millis() - lastStatus >= 5000) { lastStatus = millis(); printStatus(); }
  delay(1);
}
