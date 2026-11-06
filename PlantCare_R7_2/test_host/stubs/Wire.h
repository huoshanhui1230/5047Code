#pragma once
#include "Arduino.h"
class TwoWire {
 public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
  void beginTransmission(uint16_t address);
  uint8_t endTransmission(bool sendStop = true);
};
extern TwoWire Wire;
