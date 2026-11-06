#include "Net.h"

void Net::begin(const String& ssid1, const String& pass1, const String& ssid2, const String& pass2, const char* tz) {
  tz_ = tz;
  setenv("TZ", tz_, 1);
  tzset();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(true);   // 调制解调器休眠, 降低平均电流
  setCredentials(ssid1, pass1, ssid2, pass2);
  lastAttempt_ = 0;
}

void Net::setCredentials(const String& ssid1, const String& pass1, const String& ssid2, const String& pass2) {
  ssid1_ = ssid1; pass1_ = pass1; ssid2_ = ssid2; pass2_ = pass2;
  slot_ = 1;   // 下一次尝试先轮到第 1 组
  lastAttempt_ = 0;
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
}

void Net::loop(uint32_t now) {
  const bool c = connected();
  if (c && !wasConnected_) {
    // 连上后启动 NTP; 校时后即使断网, 时钟也继续走
    configTzTime(tz_, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpStarted_ = true;
  }
  wasConnected_ = c;
  if (c || !hasCredentials()) return;
  if (lastAttempt_ != 0 && now - lastAttempt_ < 20000) return;
  lastAttempt_ = now ? now : 1;
  // 轮流尝试两组网络
  for (int k = 0; k < 2; k++) {
    slot_ = (slot_ + 1) % 2;
    const String& s = slot_ == 0 ? ssid1_ : ssid2_;
    const String& p = slot_ == 0 ? pass1_ : pass2_;
    if (s.length()) {
      WiFi.disconnect();
      WiFi.begin(s.c_str(), p.c_str());
      return;
    }
  }
}

bool Net::timeKnown() const { return time(nullptr) > 1700000000; }

uint32_t Net::epoch() const {
  const time_t t = time(nullptr);
  return t > 1700000000 ? static_cast<uint32_t>(t) : 0;
}

bool Net::localTm(struct tm& out) const {
  if (!timeKnown()) return false;
  const time_t t = time(nullptr);
  localtime_r(&t, &out);
  return true;
}

String Net::hhmm() const {
  struct tm tmv;
  if (!localTm(tmv)) return String("");
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return String(b);
}

int Net::minutesOfDay() const {
  struct tm tmv;
  if (!localTm(tmv)) return -1;
  return tmv.tm_hour * 60 + tmv.tm_min;
}

int Net::dayKey() const {
  struct tm tmv;
  if (!localTm(tmv)) return -1;
  return (tmv.tm_year + 1900) * 1000 + tmv.tm_yday;
}

bool Net::justSynced() {
  if (syncedReported_ || !timeKnown()) return false;
  syncedReported_ = true;
  return true;
}

bool Net::quietNow(int startMin, int endMin) const {
  const int m = minutesOfDay();
  if (m < 0) return true;   // 时间未知: 保守静音
  if (startMin <= endMin) return m >= startMin && m < endMin;
  return m >= startMin || m < endMin;   // 跨午夜, 例如 21:00-07:30
}
