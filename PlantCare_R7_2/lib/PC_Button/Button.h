#pragma once
// "已补水"按钮: 内部上拉, 按下为 LOW; 软件消抖; 只有短按语义 (按下即一次)
#include <Arduino.h>

class Button {
 public:
  void begin(uint8_t pin, uint16_t debounceMs) {
    pin_ = pin;
    debounce_ = debounceMs;
    pinMode(pin_, INPUT_PULLUP);
    raw_ = stable_ = digitalRead(pin_);
    changedAt_ = millis();
  }
  // 消抖后检测到一次按下时返回 true
  bool update(uint32_t now) {
    const int r = digitalRead(pin_);
    if (r != raw_) { raw_ = r; changedAt_ = now; }
    if (now - changedAt_ >= debounce_ && raw_ != stable_) {
      stable_ = raw_;
      if (stable_ == LOW) { presses_++; return true; }
    }
    return false;
  }
  bool pressedNow() const { return stable_ == LOW; }
  uint32_t presses() const { return presses_; }

 private:
  uint8_t pin_ = 13;
  uint16_t debounce_ = 40;
  int raw_ = HIGH, stable_ = HIGH;
  uint32_t changedAt_ = 0, presses_ = 0;
};
