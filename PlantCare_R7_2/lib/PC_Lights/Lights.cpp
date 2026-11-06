#include "Lights.h"

uint32_t Lights::rgb(LightColor c) {
  switch (c) {
    case LightColor::Yellow: return Adafruit_NeoPixel::Color(255, 150, 0);
    case LightColor::Blue:   return Adafruit_NeoPixel::Color(0, 70, 255);
    case LightColor::Green:  return Adafruit_NeoPixel::Color(0, 220, 40);
    case LightColor::White:  return Adafruit_NeoPixel::Color(255, 255, 255);
    case LightColor::Red:    return Adafruit_NeoPixel::Color(255, 0, 0);
    case LightColor::Off:
    default:                 return 0;
  }
}

void Lights::begin(uint8_t ringBrightness, uint8_t boardBrightness) {
  ring_.begin();
  ring_.setBrightness(ringBrightness);
  ring_.clear();
  ring_.show();
  board_.begin();
  board_.setBrightness(boardBrightness);
  board_.clear();
  board_.show();
}

void Lights::showRing(LightColor c) {
  ring_.fill(rgb(c), 0, RING_COUNT);
  ring_.show();
}

void Lights::showBoard(LightColor c) {
  board_.setPixelColor(0, rgb(c));
  board_.show();
}

void Lights::set(LightColor c) {
  color_ = c;
  if (flashing_) return;   // 闪烁结束后由 update 恢复
  showRing(c);
  showBoard(c);
}

void Lights::flash(LightColor c, uint32_t ms) {
  flashColor_ = c;
  flashStart_ = millis();
  flashUntil_ = flashStart_ + ms;
  flashPeriod_ = 250;
  flashing_ = true;
  boardOnly_ = false;
}

void Lights::flashBoard(LightColor c, uint32_t ms) {
  flash(c, ms);
  boardOnly_ = true;
}

void Lights::update(uint32_t now) {
  if (!flashing_) return;
  if (static_cast<int32_t>(now - flashUntil_) >= 0) {
    flashing_ = false;
    showRing(color_);
    showBoard(color_);
    return;
  }
  const bool on = ((now - flashStart_) / flashPeriod_) % 2 == 0;
  const LightColor c = on ? flashColor_ : LightColor::Off;
  if (!boardOnly_) showRing(c);
  showBoard(c);
}

void Lights::setRingBrightness(uint8_t b) {
  ring_.setBrightness(b);
  showRing(color_);
}

void Lights::fillRing(uint8_t r, uint8_t g, uint8_t b) {
  ring_.fill(Adafruit_NeoPixel::Color(r, g, b), 0, RING_COUNT);
  ring_.show();
}

void Lights::chaseStep(uint32_t color) {
  ring_.clear();
  ring_.setPixelColor(chase_ % RING_COUNT, color);
  ring_.show();
  chase_++;
}
