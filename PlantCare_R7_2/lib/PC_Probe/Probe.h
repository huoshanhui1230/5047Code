#pragma once
// SEN0308 探头读取: 每轮 N 次 analogReadMilliVolts 取中位数, 并判断本轮是否有效
#include <Arduino.h>

struct ProbeRound {
  bool valid = false;
  int mv = 0;          // 中位数 (mV)
  int spread = 0;      // 本轮极差 (mV)
  int minMv = 0, maxMv = 0;
  const char* why = "";  // 无效原因: "low" / "high" / "spread"
};

class Probe {
 public:
  void begin(uint8_t pin);
  ProbeRound readRound(uint8_t samples, uint16_t gapMs, int validMin, int validMax, int spreadMax);
  // 连续多轮取中位数 (cal dry / cal wet 用); 有任一轮无效则返回无效
  ProbeRound readStable(uint8_t rounds, uint8_t samples, uint16_t gapMs, int validMin, int validMax, int spreadMax);

 private:
  uint8_t pin_ = 34;
};
