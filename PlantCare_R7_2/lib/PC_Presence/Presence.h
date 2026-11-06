#pragma once
// AM312 PIR: 高电平上升沿 = 一次移动事件 (不代表持续在场); 上电预热期内忽略
#include <Arduino.h>

class Presence {
 public:
  void begin(uint8_t pin, uint32_t warmupMs, uint32_t stuckMs);
  bool update(uint32_t now);                 // 有新的移动事件时返回 true
  bool warmedUp(uint32_t now) const { return static_cast<int32_t>(now - warmEnd_) >= 0; }
  uint32_t warmRemainingMs(uint32_t now) const { return warmedUp(now) ? 0 : warmEnd_ - now; }
  bool inWindow(uint32_t now, uint32_t windowMs) const { return lastMotion_ && now - lastMotion_ < windowMs; }
  uint32_t lastMotion() const { return lastMotion_; }
  uint32_t count() const { return count_; }
  bool stuck() const { return stuck_; }      // 持续高电平过久, 视为故障
  bool level() const { return high_; }
  void inject(uint32_t now) { lastMotion_ = now; count_++; }   // motion 命令

 private:
  uint8_t pin_ = 14;
  uint32_t warmEnd_ = 0, stuckMs_ = 300000;
  bool high_ = false, stuck_ = false;
  uint32_t highSince_ = 0, lastMotion_ = 0, count_ = 0;
};
