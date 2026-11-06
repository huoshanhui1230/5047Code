#pragma once
// 日志统计 (纯 C++, 电脑上可测试). 逐行读入 /log.csv, 给出 24 h 连续运行测试要看的指标:
//   采样行数 / 有效行数 / FAST、SIM 行数, 时间跨度与覆盖率, 最长间隔,
//   前 24 h 有效记录数 (方案: 288 条中不少于 274 条), 重启次数与复位原因, 状态变化列表。
// 行格式: epoch,boot,ms,type,state,mv,R,valid,motion,detail   (type: S 采样 / E 事件)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

struct LogRow {
  uint32_t epoch = 0, boot = 0, ms = 0;
  char type = 0;              // 'S' 或 'E'
  std::string state;
  bool valid = false;
  std::string last;           // S: 标记 (FAST/SIM); E: 事件内容
};

// 拆一行; 表头和坏行返回 false。第 10 列之后的逗号保留在 last 里。
inline bool parseLogRow(const std::string& line, LogRow& r) {
  std::vector<std::string> f;
  std::string cur;
  for (char ch : line) {
    if (ch == '\r' || ch == '\n') continue;
    if (ch == ',' && f.size() < 9) { f.push_back(cur); cur.clear(); }
    else cur += ch;
  }
  f.push_back(cur);
  if (f.size() < 10) return false;
  if (f[3] != "S" && f[3] != "E") return false;
  for (int i = 0; i < 3; i++) {
    if (f[i].empty() || f[i].find_first_not_of("0123456789") != std::string::npos) return false;
  }
  r.epoch = static_cast<uint32_t>(strtoul(f[0].c_str(), nullptr, 10));
  r.boot = static_cast<uint32_t>(strtoul(f[1].c_str(), nullptr, 10));
  r.ms = static_cast<uint32_t>(strtoul(f[2].c_str(), nullptr, 10));
  r.type = f[3][0];
  r.state = f[4];
  r.valid = (f[7] == "1");
  r.last = f[9];
  return true;
}

class LogStats {
 public:
  explicit LogStats(uint32_t periodS = 300, uint32_t windowS = 86400, uint32_t needValid = 274)
      : periodS_(periodS), windowS_(windowS), needValid_(needValid) {}

  void add(const std::string& line) {
    LogRow r;
    if (!parseLogRow(line, r)) return;
    if (r.type == 'S') addSample(r);
    else addEvent(r);
  }

  // ---- 结果 ----
  uint32_t rows() const { return rows_; }
  uint32_t validRows() const { return valid_; }
  uint32_t flaggedRows() const { return flagged_; }
  uint32_t bootEvents() const { return boots_; }
  uint32_t abnormalResets() const { return abnormal_; }
  uint32_t stateChanges() const { return stateCount_; }
  bool epochAxis() const { return rows_ > 0 && allEpoch_; }
  bool uptimeAxis() const { return rows_ > 0 && !allEpoch_ && singleBoot_; }
  bool spanKnown() const { return epochAxis() || uptimeAxis(); }
  double spanS() const {
    if (epochAxis()) return static_cast<double>(lastEp_ - firstEp_);
    if (uptimeAxis()) return (lastMs_ - firstMs_) / 1000.0;
    return -1;
  }
  double expectedRows() const { return spanKnown() ? spanS() / periodS_ + 1.0 : 0; }
  uint32_t windowRows() const { return epochAxis() ? winRowsEp_ : winRowsMs_; }
  uint32_t windowValid() const { return epochAxis() ? winValidEp_ : winValidMs_; }
  bool windowComplete() const { return spanKnown() && spanS() + periodS_ >= windowS_; }
  bool windowPass() const { return windowComplete() && windowValid() >= needValid_; }
  double maxGapS() const { return maxGapS_; }
  const std::string& resetSummary() const { return resets_; }

  // 生成报告文本 (英文, 与串口其他输出一致)
  std::string report() const {
    std::string o;
    char b[200];
    snprintf(b, sizeof(b), "sample rows: %lu | valid: %lu | FAST/SIM rows: %lu%s\n", ul(rows_), ul(valid_), ul(flagged_),
             flagged_ ? "  (bench rows: not normal parameters)" : "");
    o += b;
    if (rows_ < 2) return o + "not enough rows yet\n";
    if (!spanKnown()) {
      o += "span unknown: rows without NTP time across several boots (connect Wi-Fi so rows carry time)\n";
    } else {
      const double ex = expectedRows();
      snprintf(b, sizeof(b), "time axis: %s | span %.2f h | expected %.0f rows | coverage %.1f %% (valid %.1f %%)\n",
               epochAxis() ? "NTP clock" : "uptime of one boot", spanS() / 3600.0, ex, 100.0 * rows_ / ex,
               100.0 * valid_ / ex);
      o += b;
      snprintf(b, sizeof(b), "longest gap between sample rows: %.1f min%s\n", maxGapS_ / 60.0,
               maxGapS_ > 2.0 * periodS_ ? "  (rows missing around a reboot or freeze)" : "");
      o += b;
      if (windowComplete()) {
        snprintf(b, sizeof(b), "first %.0f h: %lu rows, %lu valid (need >= %lu of %lu) -> %s\n", windowS_ / 3600.0,
                 ul(windowRows()), ul(windowValid()), ul(needValid_), ul(windowS_ / periodS_), windowPass() ? "PASS" : "FAIL");
      } else {
        snprintf(b, sizeof(b), "first %.0f h: still running (%.2f h so far)\n", windowS_ / 3600.0, spanS() / 3600.0);
      }
      o += b;
    }
    snprintf(b, sizeof(b), "boots logged: %lu (%s) | abnormal resets (BROWNOUT/PANIC/WDT): %lu\n", ul(boots_),
             resets_.empty() ? "none" : resets_.c_str(), ul(abnormal_));
    o += b;
    snprintf(b, sizeof(b), "state changes: %lu%s\n", ul(stateCount_), stateCount_ > states_.size() ? " (last ones shown)" : "");
    o += b;
    for (const auto& s : states_) o += "  " + s + "\n";
    return o;
  }

 private:
  static unsigned long ul(uint32_t v) { return static_cast<unsigned long>(v); }

  static std::string when(const LogRow& r) {
    char b[40];
    if (r.epoch) {
      const time_t t = static_cast<time_t>(r.epoch);
      struct tm tmv;
      localtime_r(&t, &tmv);
      strftime(b, sizeof(b), "%m-%d %H:%M", &tmv);
    } else {
      snprintf(b, sizeof(b), "boot%lu+%lus", ul(r.boot), ul(r.ms / 1000));
    }
    return b;
  }

  void addSample(const LogRow& r) {
    rows_++;
    if (r.valid) valid_++;
    if (r.last.find("FAST") != std::string::npos || r.last.find("SIM") != std::string::npos) flagged_++;
    if (rows_ == 1) {
      firstEp_ = r.epoch;
      firstMs_ = r.ms;
      firstBoot_ = r.boot;
    } else {
      // 相邻两行的间隔: 都有 NTP 时间用时间, 同一次开机用开机毫秒
      double gap = -1;
      if (r.epoch && prevEp_ && r.epoch >= prevEp_) gap = r.epoch - prevEp_;
      else if (r.boot == prevBoot_ && r.ms >= prevMs_) gap = (r.ms - prevMs_) / 1000.0;
      if (gap > maxGapS_) maxGapS_ = gap;
    }
    if (r.epoch == 0) allEpoch_ = false;
    if (r.boot != firstBoot_) singleBoot_ = false;
    // 前 24 h 窗口: 两种时间轴各算一份, 最后按可用的那一种报告
    if (allEpoch_ && r.epoch >= firstEp_ && r.epoch - firstEp_ < windowS_) {
      winRowsEp_++;
      if (r.valid) winValidEp_++;
    }
    if (singleBoot_ && r.ms >= firstMs_ && (r.ms - firstMs_) / 1000 < windowS_) {
      winRowsMs_++;
      if (r.valid) winValidMs_++;
    }
    lastEp_ = r.epoch;
    lastMs_ = r.ms;
    prevEp_ = r.epoch;
    prevMs_ = r.ms;
    prevBoot_ = r.boot;
  }

  void addEvent(const LogRow& r) {
    const std::string& d = r.last;
    if (d.compare(0, 5, "boot ") == 0) {
      boots_++;
      std::string reason = "?";
      const size_t p = d.find("reset=");
      if (p != std::string::npos) {
        const size_t e = d.find(' ', p);
        reason = d.substr(p + 6, e == std::string::npos ? std::string::npos : e - p - 6);
      }
      static const char* const kBad[] = {"BROWNOUT", "PANIC", "INT_WDT", "TASK_WDT", "WDT"};
      for (const char* k : kBad)
        if (reason == k) abnormal_++;
      bumpReason(reason);
    } else if (d.compare(0, 6, "state ") == 0) {
      stateCount_++;
      states_.push_back(when(r) + "  " + d.substr(6));
      if (states_.size() > kMaxStates) states_.erase(states_.begin());
    }
  }

  void bumpReason(const std::string& reason) {
    // resets_ 形如 "POWERON x1, SOFTWARE x2"
    std::vector<std::pair<std::string, int>> list;
    size_t pos = 0;
    while (pos < resets_.size()) {
      size_t end = resets_.find(", ", pos);
      if (end == std::string::npos) end = resets_.size();
      const std::string item = resets_.substr(pos, end - pos);
      const size_t x = item.rfind(" x");
      if (x != std::string::npos) list.push_back({item.substr(0, x), atoi(item.c_str() + x + 2)});
      pos = end + 2;
    }
    bool found = false;
    for (auto& it : list)
      if (it.first == reason) { it.second++; found = true; }
    if (!found) list.push_back({reason, 1});
    resets_.clear();
    for (const auto& it : list) {
      if (!resets_.empty()) resets_ += ", ";
      resets_ += it.first + " x" + std::to_string(it.second);
    }
  }

  static const size_t kMaxStates = 40;
  uint32_t periodS_, windowS_, needValid_;
  uint32_t rows_ = 0, valid_ = 0, flagged_ = 0;
  uint32_t firstEp_ = 0, lastEp_ = 0, firstMs_ = 0, lastMs_ = 0, firstBoot_ = 0;
  uint32_t prevEp_ = 0, prevMs_ = 0, prevBoot_ = 0;
  bool allEpoch_ = true, singleBoot_ = true;
  uint32_t winRowsEp_ = 0, winValidEp_ = 0, winRowsMs_ = 0, winValidMs_ = 0;
  double maxGapS_ = 0;
  uint32_t boots_ = 0, abnormal_ = 0, stateCount_ = 0;
  std::string resets_;
  std::vector<std::string> states_;
};
