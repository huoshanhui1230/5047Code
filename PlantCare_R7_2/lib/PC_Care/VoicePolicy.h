#pragma once
// fw1.3 走近播报规则 (talk normal)。纯 C++, 不依赖 Arduino, 可在电脑上单元测试。
// talk test (展示模式) 不经过这里: 每次走近都说当前状态那一句。
//
//   缺水        走近时按原规则播 0001: 同一句至少隔 10 min, 每天主动播报最多 6 次 (fw1.3 起只有缺水计入)
//   缺水 (等待后未确认恢复)
//               等待结束后先静默一段时间 (默认一个等待时长), 期间读数回升会直接变绿; 之后才按缺水规则播
//   等待水渗透  按键时播一次 0003 (在 handleStep); 等待中走近, 距上一次等待语音 (或等待开始) 已过
//               半个等待时长才再播一次; 不计每日上限
//   不缺水      每次补水后恢复 (wait_to_ok / dry_to_ok) 只播一次 0002: 当时 60 s 内有人走近就当场播,
//               否则下一次走近时补播; 之后走近不再说, 直到状态改变; 不计每日上限。
//               开机首次确定的不缺水不播 (那不是补水后的恢复)
#include <cstdint>
#include "CareState.h"

enum class ApproachVoice : uint8_t {
  Proactive,  // 主动播报: 受同一句冷却和每日上限限制 (缺水)
  Event,      // 事件播报: 不受冷却和每日上限限制, 仍遵守安静时段 (等待再说一次 / 恢复补播)
  Silent      // 不说话; 屏幕和灯照常
};

struct ApproachCtx {
  CareMode mode = CareMode::Boot;
  bool notRecovered = false;       // 缺水来自"等待后未确认恢复"
  uint32_t msSinceWaitEnd = 0;     // 距那次等待结束
  uint32_t recheckQuietMs = 0;     // 未确认恢复后的静默时长
  uint32_t msSinceWaitVoice = 0;   // 距上一次等待语音 (或等待开始)
  uint32_t waitAgainMs = 0;        // 等待中再说一次的最短间隔
  bool okPending = false;          // 这次恢复为不缺水, 0002 还没播过
};

struct ApproachDecision {
  ApproachVoice voice;
  const char* why;                 // 写进串口和日志的原因
};

inline ApproachDecision approachVoice(const ApproachCtx& c) {
  switch (c.mode) {
    case CareMode::Dry:
      if (c.notRecovered && c.msSinceWaitEnd < c.recheckQuietMs) return {ApproachVoice::Silent, "recheck_quiet"};
      return {ApproachVoice::Proactive, "approach"};
    case CareMode::Waiting:
      if (c.msSinceWaitVoice >= c.waitAgainMs) return {ApproachVoice::Event, "waiting_again"};
      return {ApproachVoice::Silent, "waiting_said_recently"};
    case CareMode::Ok:
      if (c.okPending) return {ApproachVoice::Event, "recovered_once"};
      return {ApproachVoice::Silent, "ok_already_said"};
    default:
      return {ApproachVoice::Silent, "no_plant_state"};
  }
}
