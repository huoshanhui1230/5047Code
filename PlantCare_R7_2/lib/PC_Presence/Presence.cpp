#include "Presence.h"
#include "config.h"

void Presence::begin(uint8_t pin, uint32_t warmupMs, uint32_t stuckMs) {
  pin_ = pin;
  stuckMs_ = stuckMs;
  pinMode(pin_, PIR_PIN_MODE);     // 见 config.h: 不加内部下拉
  warmEnd_ = millis() + warmupMs;
  high_ = digitalRead(pin_) == HIGH;
  highSince_ = millis();
}

bool Presence::update(uint32_t now) {
  const bool h = digitalRead(pin_) == HIGH;
  if (h && !high_) {
    high_ = true;
    highSince_ = now;
    if (warmedUp(now) && !stuck_) {
      lastMotion_ = now;
      count_++;
      return true;
    }
  } else if (h && high_) {
    if (!stuck_ && now - highSince_ > stuckMs_) stuck_ = true;
  } else if (!h) {
    high_ = false;
    stuck_ = false;
  }
  return false;
}
