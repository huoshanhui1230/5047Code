#pragma once
// 本地照护状态机 (纯 C++, 不依赖 Arduino, 可在电脑上单元测试)
// 对应方案 R7.2 第 6 章: 三状态 + 两种无表情的设备状态; 滞回; 等待复测; 重启规则。
#include <cstdint>
#include <cmath>

enum class CareMode : uint8_t {
  Boot = 0,   // 启动中, 还没有 3 轮有效读数
  Uncal,      // 未标定 / 阈值无效 -> "Device check: Not calibrated"
  Reading,    // 原状态未知且读数在滞回区 -> "Reading..."
  Dry,        // 缺水 (黄)
  Waiting,    // 等待水渗透 (蓝)
  Ok,         // 不缺水 (绿)
  Paused      // 连续无效读数 -> "Device check" 暂停判断
};
const char* careModeName(CareMode m);
bool isPlantMode(CareMode m);   // Dry / Waiting / Ok

enum class CareEvent : uint8_t {
  None = 0,
  FirstState,     // Boot/Reading -> Dry/Ok
  OkToDry,
  DryToWait,      // 缺水时收到有效短按
  WaitToOk,
  WaitToDry,      // 等待到期未恢复
  DryToOk,        // 缺水期间读数连续 3 轮达到 H (未按键)
  ResumeWaiting,  // 开机有等待标记 / 暂停后恢复 -> 重新等待完整周期
  EnterReading,   // -> Reading
  Paused,         // -> Paused
  Uncalibrated,   // -> Uncal
  ButtonIgnored,  // 非缺水状态下的按键
  Unstable        // fw1.4: 植物状态下长时间凑不齐连续 3 轮有效读数 -> 暂停判断
};
const char* careEventName(CareEvent e);

struct CareConfig {
  bool calibrated = false;        // 干湿端有效且 H > L
  float L = NAN;                  // 缺水下限
  float H = NAN;                  // 恢复阈值
  uint32_t waitMs = 600000;       // 等待时长
  uint8_t confirmRounds = 3;
  uint8_t invalidToPause = 3;
  uint8_t unconfirmedToPause = 15;  // fw1.4: 植物状态下连续这么多轮都没有"连续 3 轮有效" -> 暂停; 0 = 不检查
};

struct CareStep {
  CareEvent event = CareEvent::None;
  CareMode from = CareMode::Boot;
  CareMode to = CareMode::Boot;
  bool changed() const { return from != to; }
};

class CareMachine {
 public:
  // trustedPrev: 断电前的可信状态 (Dry/Ok), 没有则传 Boot; waitMarker: 断电前处于等待
  CareStep begin(const CareConfig& cfg, CareMode trustedPrev, bool waitMarker, uint32_t nowMs);
  // 标定/阈值/等待时长变更
  CareStep reconfigure(const CareConfig& cfg, uint32_t nowMs);
  // 每轮采样结束调用一次
  CareStep onRound(bool valid, float R, uint32_t nowMs);
  // 有效短按 (已消抖)
  CareStep onButton(uint32_t nowMs);
  // 每次 loop 调用: 等待到期判断
  CareStep onTick(uint32_t nowMs);
  // 开机时通常尚未校时: 校时后补充可信原状态 (只在 Boot/Reading 时生效)
  void setTrustedPrev(CareMode m) {
    if (mode_ == CareMode::Boot || mode_ == CareMode::Reading)
      prev_ = (m == CareMode::Dry || m == CareMode::Ok) ? m : CareMode::Boot;
  }

  CareMode mode() const { return mode_; }
  bool notRecovered() const { return notRecovered_; }
  bool waitExpired(uint32_t nowMs) const;
  uint32_t waitRemainingMs(uint32_t nowMs) const;
  float lastR() const { return lastR_; }
  bool lastValid() const { return lastValid_; }
  uint8_t consecValid() const { return valid_; }
  uint8_t consecBelowL() const { return below_; }
  uint8_t consecAboveH() const { return above_; }
  uint8_t roundsUnconfirmed() const { return unconfirmed_; }
  const CareConfig& config() const { return cfg_; }

 private:
  CareStep establish(CareStep st);
  CareStep checkWait(CareStep st, uint32_t nowMs);
  CareStep checkUnconfirmed(CareStep st);
  void resetCounters() { valid_ = below_ = above_ = invalid_ = unconfirmed_ = 0; }

  CareConfig cfg_;
  CareMode mode_ = CareMode::Boot;
  CareMode prev_ = CareMode::Boot;        // 可信原状态 (Dry/Ok) 或 Boot
  CareMode pausedFrom_ = CareMode::Boot;
  uint8_t valid_ = 0, below_ = 0, above_ = 0, invalid_ = 0;
  uint8_t unconfirmed_ = 0;               // 植物状态下距上一次"连续 3 轮有效"已过的轮数
  bool pauseNeedsConfirm_ = false;        // 因读数不稳暂停: 要连续 3 轮有效才恢复
  float lastR_ = NAN;
  bool lastValid_ = false;
  bool notRecovered_ = false;
  uint32_t waitStart_ = 0;
};
