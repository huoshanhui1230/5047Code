#include "Probe.h"
#include <algorithm>

void Probe::begin(uint8_t pin) {
  pin_ = pin;
  analogReadResolution(12);
  analogSetPinAttenuation(pin_, ADC_11db);   // 量程约 150-2450 mV 线性, 最高约 3.1 V
}

ProbeRound Probe::readRound(uint8_t samples, uint16_t gapMs, int validMin, int validMax, int spreadMax) {
  ProbeRound r;
  if (samples == 0) samples = 1;
  if (samples > 15) samples = 15;
  int v[15];
  for (uint8_t i = 0; i < samples; i++) {
    v[i] = static_cast<int>(analogReadMilliVolts(pin_));
    if (gapMs && i + 1 < samples) delay(gapMs);
  }
  std::sort(v, v + samples);
  r.mv = v[samples / 2];
  r.minMv = v[0];
  r.maxMv = v[samples - 1];
  r.spread = r.maxMv - r.minMv;
  if (r.mv < validMin) r.why = "too low: probe off?";      // 停在 ADC 底值 (约 142 mV) 也算
  else if (r.mv > validMax) r.why = "too high";
  else if (r.spread > spreadMax) r.why = "unstable";
  else r.valid = true;
  return r;
}

ProbeRound Probe::readStable(uint8_t rounds, uint8_t samples, uint16_t gapMs, int validMin, int validMax, int spreadMax) {
  ProbeRound out;
  if (rounds == 0) rounds = 1;
  if (rounds > 15) rounds = 15;
  int med[15];
  int minAll = 100000, maxAll = 0;
  for (uint8_t i = 0; i < rounds; i++) {
    ProbeRound r = readRound(samples, gapMs, validMin, validMax, spreadMax);
    if (!r.valid) return r;
    med[i] = r.mv;
    minAll = std::min(minAll, r.minMv);
    maxAll = std::max(maxAll, r.maxMv);
    delay(150);
  }
  std::sort(med, med + rounds);
  out.valid = true;
  out.mv = med[rounds / 2];
  out.minMv = minAll;
  out.maxMv = maxAll;
  out.spread = maxAll - minAll;
  return out;
}
