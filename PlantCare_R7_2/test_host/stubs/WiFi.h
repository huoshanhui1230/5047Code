#pragma once
#include "Arduino.h"
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED,
               WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;
typedef enum { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
class WiFiClass {
 public:
  wl_status_t begin(const char* ssid, const char* passphrase = nullptr, int32_t channel = 0,
                    const uint8_t* bssid = nullptr, bool connect = true);
  wl_status_t status();
  bool disconnect(bool wifioff = false, bool eraseap = false);
  bool mode(wifi_mode_t);
  void persistent(bool persistent);
  bool setAutoReconnect(bool autoReconnect);
  bool setSleep(bool enabled);
  String SSID() const;
  int8_t RSSI();
};
extern WiFiClass WiFi;
