#include "CareState.h"

const char* careModeName(CareMode m) {
  switch (m) {
    case CareMode::Boot:    return "BOOT";
    case CareMode::Uncal:   return "UNCAL";
    case CareMode::Reading: return "READING";
    case CareMode::Dry:     return "DRY";
    case CareMode::Waiting: return "WAITING";
    case CareMode::Ok:      return "OK";
    case CareMode::Paused:  return "PAUSED";
  }
  return "?";
}

bool isPlantMode(CareMode m) {
  return m == CareMode::Dry || m == CareMode::Waiting || m == CareMode::Ok;
}

const char* careEventName(CareEvent e) {
  switch (e) {
    case CareEvent::None:          return "none";
    case CareEvent::FirstState:    return "first_state";
    case CareEvent::OkToDry:       return "ok_to_dry";
    case CareEvent::DryToWait:     return "dry_to_wait";
    case CareEvent::WaitToOk:      return "wait_to_ok";
    case CareEvent::WaitToDry:     return "wait_to_dry";
    case CareEvent::DryToOk:       return "dry_to_ok";
    case CareEvent::ResumeWaiting: return "resume_waiting";
    case CareEvent::EnterReading:  return "enter_reading";
    case CareEvent::Paused:        return "paused";
    case CareEvent::Uncalibrated:  return "uncalibrated";
    case CareEvent::ButtonIgnored: return "button_ignored";
    case CareEvent::Unstable:      return "unstable";
  }
  return "?";
}

static uint8_t inc(uint8_t v) { return v < 250 ? static_cast<uint8_t>(v + 1) : v; }

CareStep CareMachine::begin(const CareConfig& cfg, CareMode trustedPrev, bool waitMarker, uint32_t nowMs) {
  cfg_ = cfg;
  resetCounters();
  notRecovered_ = false;
  pauseNeedsConfirm_ = false;
  lastR_ = NAN;
  lastValid_ = false;
  prev_ = (trustedPrev == CareMode::Dry || trustedPrev == CareMode::Ok) ? trustedPrev : CareMode::Boot;
  CareStep st;
  st.from = CareMode::Boot;
  if (!cfg_.calibrated) {
    mode_ = CareMode::Uncal;
    st.event = CareEvent::Uncalibrated;
  } else if (waitMarker) {
    // 重启前处于等待: 重新等待完整周期, 不直接变绿
    mode_ = CareMode::Waiting;
    waitStart_ = nowMs;
    st.event = CareEvent::ResumeWaiting;
  } else {
    mode_ = CareMode::Boot;
  }
  st.to = mode_;
  return st;
}

CareStep CareMachine::reconfigure(const CareConfig& cfg, uint32_t nowMs) {
  (void)nowMs;
  CareStep st;
  st.from = mode_;
  const bool wasUncal = (mode_ == CareMode::Uncal);
  cfg_ = cfg;
  valid_ = below_ = above_ = unconfirmed_ = 0;   // 阈值变了, 连续确认重新累计
  if (!cfg_.calibrated) {
    if (mode_ != CareMode::Uncal) {
      mode_ = CareMode::Uncal;
      st.event = CareEvent::Uncalibrated;
    }
  } else if (wasUncal) {
    mode_ = CareMode::Reading;
    prev_ = CareMode::Boot;
    st.event = CareEvent::EnterReading;
  }
  st.to = mode_;
  return st;
}

CareStep CareMachine::onRound(bool valid, float R, uint32_t nowMs) {
  CareStep st;
  st.from = st.to = mode_;
  if (mode_ == CareMode::Uncal) {
    lastValid_ = valid;
    if (valid) lastR_ = R;
    return st;
  }
  if (!valid) {
    lastValid_ = false;
    invalid_ = inc(invalid_);
    valid_ = below_ = above_ = 0;   // 异常后连续确认重新累计
    if (invalid_ >= cfg_.invalidToPause && mode_ != CareMode::Paused) {
      pausedFrom_ = mode_;
      mode_ = CareMode::Paused;
      pauseNeedsConfirm_ = false;
      unconfirmed_ = 0;
      st.event = CareEvent::Paused;
      st.to = mode_;
      return st;
    }
    return checkUnconfirmed(st);
  }

  invalid_ = 0;
  lastValid_ = true;
  lastR_ = R;
  valid_ = inc(valid_);
  below_ = (R < cfg_.L) ? inc(below_) : 0;
  above_ = (R >= cfg_.H) ? inc(above_) : 0;
  if (isPlantMode(mode_)) {
    const CareStep u = checkUnconfirmed(st);
    if (u.event != CareEvent::None) return u;
  }

  switch (mode_) {
    case CareMode::Paused:
      if (pauseNeedsConfirm_ && valid_ < cfg_.confirmRounds) break;   // 因读数不稳暂停: 连续 3 轮有效才恢复
      pauseNeedsConfirm_ = false;
      unconfirmed_ = 0;
      if (pausedFrom_ == CareMode::Waiting) {
        mode_ = CareMode::Waiting;
        waitStart_ = nowMs;
        st.event = CareEvent::ResumeWaiting;
      } else {
        if (pausedFrom_ == CareMode::Dry || pausedFrom_ == CareMode::Ok) prev_ = pausedFrom_;
        mode_ = CareMode::Reading;
        st.event = CareEvent::EnterReading;
      }
      st.to = mode_;
      return st;
    case CareMode::Boot:
    case CareMode::Reading:
      return establish(st);
    case CareMode::Ok:
      if (below_ >= cfg_.confirmRounds) {
        mode_ = CareMode::Dry;
        notRecovered_ = false;
        st.event = CareEvent::OkToDry;
      }
      break;
    case CareMode::Dry:
      if (above_ >= cfg_.confirmRounds) {
        mode_ = CareMode::Ok;
        notRecovered_ = false;
        st.event = CareEvent::DryToOk;
      }
      break;
    case CareMode::Waiting:
      return checkWait(st, nowMs);
    default:
      break;
  }
  st.to = mode_;
  return st;
}

CareStep CareMachine::establish(CareStep st) {
  const uint8_t n = cfg_.confirmRounds;
  if (below_ >= n) {
    mode_ = CareMode::Dry;
    notRecovered_ = false;
    st.event = CareEvent::FirstState;
  } else if (above_ >= n) {
    mode_ = CareMode::Ok;
    st.event = CareEvent::FirstState;
  } else if (valid_ >= n) {
    const bool inBand = (lastR_ >= cfg_.L && lastR_ < cfg_.H);
    if (inBand && (prev_ == CareMode::Dry || prev_ == CareMode::Ok)) {
      mode_ = prev_;                     // 有可信原状态, 滞回区沿用
      notRecovered_ = false;
      st.event = CareEvent::FirstState;
    } else if (mode_ == CareMode::Boot) {
      mode_ = CareMode::Reading;         // 原状态未知: 读数待确认, 不默认变绿
      st.event = CareEvent::EnterReading;
    }
  }
  st.to = mode_;
  return st;
}

// fw1.4 活性保护: 有效与无效读数交替出现时 (如 有效 有效 无效 ...), 既凑不齐连续 3 轮无效 (不会暂停),
// 也凑不齐连续 3 轮有效 (不能判断), 植物表情会一直停在旧状态, 等待也不会结束。
// 植物状态下连续 unconfirmedToPause 轮都没有"连续 3 轮有效", 就改为警示三角, 连续 3 轮有效后才恢复。
CareStep CareMachine::checkUnconfirmed(CareStep st) {
  st.to = mode_;
  if (!isPlantMode(mode_) || cfg_.unconfirmedToPause == 0) return st;
  if (lastValid_ && valid_ >= cfg_.confirmRounds) unconfirmed_ = 0;
  else unconfirmed_ = inc(unconfirmed_);
  if (unconfirmed_ >= cfg_.unconfirmedToPause) {
    pausedFrom_ = mode_;
    mode_ = CareMode::Paused;
    pauseNeedsConfirm_ = true;
    unconfirmed_ = 0;
    valid_ = below_ = above_ = 0;   // 恢复要看暂停之后的连续 3 轮有效读数
    st.event = CareEvent::Unstable;
    st.to = mode_;
  }
  return st;
}

CareStep CareMachine::checkWait(CareStep st, uint32_t nowMs) {
  if (mode_ != CareMode::Waiting || !waitExpired(nowMs)) {
    st.to = mode_;
    return st;
  }
  // 到期后复核最近连续 3 轮有效数据; 不足 3 轮时继续等待新读数
  if (valid_ >= cfg_.confirmRounds) {
    if (above_ >= cfg_.confirmRounds) {
      mode_ = CareMode::Ok;
      notRecovered_ = false;
      st.event = CareEvent::WaitToOk;
    } else {
      mode_ = CareMode::Dry;
      notRecovered_ = true;
      st.event = CareEvent::WaitToDry;
    }
  }
  st.to = mode_;
  return st;
}

CareStep CareMachine::onButton(uint32_t nowMs) {
  CareStep st;
  st.from = mode_;
  if (mode_ == CareMode::Dry) {
    mode_ = CareMode::Waiting;
    waitStart_ = nowMs;
    notRecovered_ = false;
    st.event = CareEvent::DryToWait;
  } else {
    st.event = CareEvent::ButtonIgnored;   // 不缺水/等待/设备状态: 忽略, 不重置计时
  }
  st.to = mode_;
  return st;
}

CareStep CareMachine::onTick(uint32_t nowMs) {
  CareStep st;
  st.from = st.to = mode_;
  if (mode_ == CareMode::Waiting) return checkWait(st, nowMs);
  return st;
}

bool CareMachine::waitExpired(uint32_t nowMs) const {
  return mode_ == CareMode::Waiting && (nowMs - waitStart_) >= cfg_.waitMs;
}

uint32_t CareMachine::waitRemainingMs(uint32_t nowMs) const {
  if (mode_ != CareMode::Waiting) return 0;
  const uint32_t el = nowMs - waitStart_;
  return el >= cfg_.waitMs ? 0 : cfg_.waitMs - el;
}
