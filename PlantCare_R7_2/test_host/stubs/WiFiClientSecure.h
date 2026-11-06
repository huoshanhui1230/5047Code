#pragma once
#include "WiFi.h"
class WiFiClient {
 public:
  virtual ~WiFiClient() {}
};
class WiFiClientSecure : public WiFiClient {
 public:
  void setInsecure();
  void setCACert(const char* rootCA);
  void setHandshakeTimeout(unsigned long handshake_timeout);
};
