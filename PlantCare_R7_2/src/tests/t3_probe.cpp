// t3_probe: 探头读数 + 三级标定助手
//  - 持续输出: 每秒一轮 (7 次取中位数), 每 10 轮打印一行统计
//  - 器件检查 (指南第 3 步): 探头放空气里输入 a, 插进一杯水输入 w; 各 2 次以上后输入 report
//    合格: 空气与水差值 > 600 mV, 同一状态极差 < 20 mV, 全部 < 2450 mV
//  - 三级标定 (方案 R7.2 第 5 章): 每次插好探头后输入大写 D (干端) / M (中间级) / W (湿端, 浇透沥干)
//    每级 3 次 (每次拔出、擦干、按同一深度重插), 然后输入 report:
//    跨度 >= 10 x 合并标准差、三级单调、同级极差 < 跨度 10% -> 给出 cal set 和临时 L/H
//  - reset 清空记录, s 开关持续输出
#include <Arduino.h>
#include <map>
#include <vector>
#include <string>
#include "pins.h"
#include "config.h"
#include "Probe.h"
#include "CalMath.h"
#include "Console.h"

Probe probe;
Console console;
bool streaming = true;
std::vector<int> win;
std::map<char, std::vector<int>> rec;
uint32_t lastRound = 0;

void help() {
  Serial.println("[t3] a = air, w = water cup (device check) | D / M / W = dry / mid / wet level (calibration)");
  Serial.println("[t3] device check: keep the probe still, wait for two '10 rounds' lines, then press a (or w) 2-3 times without moving it");
  Serial.println("[t3] report | reset | s (toggle stream)");
}

void labelled(char label) {
  Serial.printf("[t3] %c: settling 3 s, keep the probe still...\n", label);
  delay(3000);
  const ProbeRound r = probe.readStable(5, SAMPLES_PER_ROUND, SAMPLE_GAP_MS, 0, 4000, 100000);
  rec[label].push_back(r.mv);
  const LevelStats s = statsOf(rec[label]);
  Serial.printf("[t3] %c #%d = %d mV (spread %d) | so far n=%d mean=%.0f range=%.0f\n", label, s.n, r.mv, r.spread,
                s.n, s.mean, s.range);
}

void report() {
  Serial.println("[t3] ---- report ----");
  for (auto& kv : rec) {
    const LevelStats s = statsOf(kv.second);
    Serial.printf("  %c: n=%d mean=%.0f sd=%.1f min=%.0f max=%.0f range=%.0f\n", kv.first, s.n, s.mean, s.sd, s.minv, s.maxv, s.range);
  }
  for (auto& kv : rec) {
    if (statsOf(kv.second).minv < VALID_MIN_MV)
      Serial.printf("  note: %c reads below %d mV (ESP32 ADC floor is about 142 mV): %s\n", kv.first, VALID_MIN_MV,
                    kv.first == 'w' ? "normal for a probe in plain water, fine for this check"
                                    : "this wetness is below what the ADC can measure; the app treats it as invalid");
  }
  if (rec.count('a') && rec.count('w')) {
    const LevelStats a = statsOf(rec['a']), w = statsOf(rec['w']);
    const double diff = fabs(a.mean - w.mean);
    const bool rangeOk = a.range < 20 && w.range < 20;
    const bool below = a.maxv < 2450 && w.maxv < 2450;
    Serial.printf("  device check: diff %.0f mV (>600 %s), ranges <20 mV %s, all <2450 mV %s -> %s\n", diff,
                  diff > 600 ? "ok" : "NO", rangeOk ? "ok" : "NO", below ? "ok" : "NO",
                  (diff > 600 && rangeOk && below) ? "PASS" : "FAIL");
    if (!below) Serial.println("  readings >= 2450 mV: add the 10k/20k divider BEFORE calibration, then run this check again");
  }
  if (rec.count('D') && rec.count('M') && rec.count('W')) {
    const CalReport r = calibrationReport(rec['D'], rec['M'], rec['W']);
    Serial.printf("  calibration: span %.0f mV, pooled sd %.1f, span/sd %.1f (>=10 %s)\n", r.span, r.pooledSd, r.ratio,
                  r.spanOk ? "ok" : "NO");
    Serial.printf("               monotonic %s, max range %.1f%% of span (<%.0f%% %s) -> %s\n", r.monotonic ? "ok" : "NO",
                  r.maxRangePct, CAL_MAX_RANGE_PCT, r.repeatOk ? "ok" : "NO", r.ok ? "PASS" : "FAIL");
    if (r.wetMv < VALID_MIN_MV + 50)
      Serial.printf("  WARNING: wet level %d mV is at the ADC floor; drain the wet sample longer and measure W again\n", r.wetMv);
    if (r.ok) {
      const float L = static_cast<float>(lround(r.rMid));
      Serial.printf("  write to the app:\n    cal set %d %d\n    cal id C1\n", r.dryMv, r.wetMv);
      Serial.printf("  provisional thresholds v0 (mid level R = %.1f):\n    set LH %.0f %.0f\n    ver param v0-temp\n",
                    static_cast<double>(r.rMid), static_cast<double>(L), static_cast<double>(L + 5));
      Serial.println("  check: H - L must be larger than the reading jitter seen in the real pot.");
    } else {
      Serial.println("  not passed: check depth/compaction or replace the probe; do not enable the states.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  probe.begin(PIN_PROBE);
  Serial.printf("\n[t3] probe on GPIO%d\n", PIN_PROBE);
  help();
}

void loop() {
  std::vector<std::string> t;
  if (console.poll(Serial, t)) {
    const std::string c = t[0];
    if (c == "report") report();
    else if (c == "reset") { rec.clear(); Serial.println("[t3] cleared"); }
    else if (c == "s") { streaming = !streaming; Serial.printf("[t3] stream %s\n", streaming ? "on" : "off"); }
    else if (c.size() == 1 && isalpha(static_cast<unsigned char>(c[0]))) labelled(c[0]);
    else help();
  }
  if (streaming && millis() - lastRound >= 1000) {
    lastRound = millis();
    const ProbeRound r = probe.readRound(SAMPLES_PER_ROUND, SAMPLE_GAP_MS, VALID_MIN_MV, VALID_MAX_MV, ROUND_SPREAD_MAX_MV);
    win.push_back(r.mv);
    if (win.size() >= 10) {
      const LevelStats s = statsOf(win);
      Serial.printf("[t3] 10 rounds: mean %.0f mV, min %.0f, max %.0f, range %.0f%s\n", s.mean, s.minv, s.maxv, s.range,
                    r.valid ? "" : (String("  (last round invalid: ") + r.why + ")").c_str());
      win.clear();
    }
  }
}
