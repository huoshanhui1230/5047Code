#pragma once
// Wi-Fi (两组: 家里 / 展示热点, 轮流尝试) + NTP 校时 (悉尼时区) + 安静时段判断
// ESP32 只支持 2.4 GHz; iPhone 热点需打开"最大兼容性"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

class Net {
 public:
  void begin(const String& ssid1, const String& pass1, const String& ssid2, const String& pass2, const char* tz);
  void setCredentials(const String& ssid1, const String& pass1, const String& ssid2, const String& pass2);
  void loop(uint32_t now);
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  bool hasCredentials() const { return ssid1_.length() || ssid2_.length(); }
  bool timeKnown() const;
  uint32_t epoch() const;                   // 未校时返回 0
  bool localTm(struct tm& out) const;       // 未校时返回 false
  String hhmm() const;                      // "HH:MM", 未校时返回 ""
  int minutesOfDay() const;                 // 未校时返回 -1
  int dayKey() const;                       // 年*1000+一年中的第几天, 未校时返回 -1
  bool justSynced();                        // 时间第一次可用时返回 true (NTP 校时, 或软件重启后 ESP32 保留的时间)
  bool ntpStarted() const { return ntpStarted_; }   // 本次开机已经通过 Wi-Fi 启动 NTP
  String ssid() const { return connected() ? WiFi.SSID() : String(""); }
  int rssi() const { return connected() ? WiFi.RSSI() : 0; }
  // 现在是否处于安静时段; 时间未知时保守返回 true
  bool quietNow(int startMin, int endMin) const;

 private:
  String ssid1_, pass1_, ssid2_, pass2_;
  const char* tz_ = "UTC0";
  uint32_t lastAttempt_ = 0;
  uint8_t slot_ = 1;
  bool wasConnected_ = false, ntpStarted_ = false, syncedReported_ = false;
};
