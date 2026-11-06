#pragma once
#include "WiFiClientSecure.h"
class HTTPClient {
 public:
  bool begin(WiFiClient& client, String url);
  void end(void);
  void setReuse(bool reuse);
  void setConnectTimeout(int32_t connectTimeout);
  void setTimeout(uint16_t timeout);
  void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
  int GET();
  int POST(uint8_t* payload, size_t size);
  int POST(String payload);
  String getString(void);
  static String errorToString(int error);
};
