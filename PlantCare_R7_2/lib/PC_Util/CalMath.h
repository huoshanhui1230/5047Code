#pragma once
// 标定计算 (纯 C++): 相对湿润指数 R 与三级标定验收 (方案 R7.2 第 5 章)
#include <vector>
#include <string>
#include <cmath>

// R = 100 × (当前 − 干端) / (湿端 − 干端); 湿端低于干端时同样成立。跨度过小返回 NAN
inline float computeR(int mv, int dryMv, int wetMv) {
  const int span = wetMv - dryMv;
  if (dryMv <= 0 || wetMv <= 0 || span == 0) return NAN;
  return 100.0f * static_cast<float>(mv - dryMv) / static_cast<float>(span);
}

inline bool calibrationValid(int dryMv, int wetMv, int minSpan) {
  return dryMv > 0 && wetMv > 0 && std::abs(wetMv - dryMv) >= minSpan;
}

// 阈值设定后端点改变: 按毫伏换算, 使判断边界不变
inline float convertThreshold(float oldT, int oldDry, int oldWet, int newDry, int newWet) {
  const float mv = oldDry + oldT * (oldWet - oldDry) / 100.0f;
  return computeR(static_cast<int>(std::lround(mv)), newDry, newWet);
}

struct LevelStats {
  int n = 0;
  double mean = 0, sd = 0, minv = 0, maxv = 0, range = 0;
};

inline LevelStats statsOf(const std::vector<int>& v) {
  LevelStats s;
  s.n = static_cast<int>(v.size());
  if (v.empty()) return s;
  s.minv = s.maxv = v[0];
  double sum = 0;
  for (int x : v) { sum += x; if (x < s.minv) s.minv = x; if (x > s.maxv) s.maxv = x; }
  s.mean = sum / s.n;
  double ss = 0;
  for (int x : v) ss += (x - s.mean) * (x - s.mean);
  s.sd = s.n > 1 ? std::sqrt(ss / (s.n - 1)) : 0;
  s.range = s.maxv - s.minv;
  return s;
}

struct CalReport {
  bool ok = false;
  double span = 0, pooledSd = 0, ratio = 0, maxRangePct = 0, rMid = NAN;
  bool spanOk = false, monotonic = false, repeatOk = false;
  int dryMv = 0, wetMv = 0;
};

// 三级验收: 跨度 ≥ 10 × 合并标准差; 三级单调; 同级极差 < 跨度 5%
inline CalReport calibrationReport(const std::vector<int>& dry, const std::vector<int>& mid,
                                   const std::vector<int>& wet) {
  CalReport r;
  const LevelStats d = statsOf(dry), m = statsOf(mid), w = statsOf(wet);
  if (d.n < 2 || m.n < 2 || w.n < 2) return r;
  r.span = std::fabs(d.mean - w.mean);
  const double num = (d.n - 1) * d.sd * d.sd + (m.n - 1) * m.sd * m.sd + (w.n - 1) * w.sd * w.sd;
  const double den = (d.n - 1) + (m.n - 1) + (w.n - 1);
  r.pooledSd = std::sqrt(num / den);
  r.ratio = r.pooledSd > 0 ? r.span / r.pooledSd : 1e9;
  r.spanOk = r.ratio >= 10.0;
  r.monotonic = (d.mean - m.mean) * (m.mean - w.mean) > 0;
  const double maxRange = std::fmax(d.range, std::fmax(m.range, w.range));
  r.maxRangePct = r.span > 0 ? 100.0 * maxRange / r.span : 100.0;
  r.repeatOk = r.maxRangePct < 5.0;
  r.dryMv = static_cast<int>(std::lround(d.mean));
  r.wetMv = static_cast<int>(std::lround(w.mean));
  r.rMid = computeR(static_cast<int>(std::lround(m.mean)), r.dryMv, r.wetMv);
  r.ok = r.spanOk && r.monotonic && r.repeatOk;
  return r;
}
