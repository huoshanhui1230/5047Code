// PlantCare R7.2 整机联通版本 (env: app / app_noTF)
// 方案 R7.2: 单盆、两类传感器 (SEN0308 + PIR)、三状态 (缺水/等待水渗透/不缺水)、
// 本地规则判断; 状态变化时 ESP32 经手机热点直接调用 Gemini 生成只读解释, 失败回退模板。
#include <Arduino.h>
#include <string>
#include <vector>
#include "pins.h"
#include "config.h"
#include "root_ca.h"
#include "sysutil.h"
#include "Probe.h"
#include "CalMath.h"
#include "Display.h"
#include "Lights.h"
#include "Voice.h"
#include "Presence.h"
#include "Button.h"
#include "Store.h"
#include "Logger.h"
#include "Net.h"
#include "CareState.h"
#include "VoicePolicy.h"
#include "Explain.h"
#include "MiniJson.h"
#include "AiClient.h"
#include "Console.h"
#include "Tokens.h"

namespace {

Display display;
Lights lights;
Voice voice;
Presence pir;
Button button;
Probe probe;
Store store;
Logger logger;
Net net;
CareMachine care;
AiClient ai;
Console console;

// ---------- 运行时状态 ----------
bool fastMode = false;              // 不保存: 重启后自动关闭
bool simOn = false;
float simR = 50;
bool talkTest = false;              // talk test / talk normal
uint32_t bootId = 0;

uint32_t nextSampleAt = 0, nextLogAt = 0;
ProbeRound lastRound;
float lastR = NAN;
bool lastValid = false, haveRound = false;
uint16_t motionsSinceLog = 0;

std::vector<int> hourly;            // 每小时平均 R (最多 48 个)
double hourSum = 0;
int hourCount = 0;
uint32_t hourStart = 0;

uint32_t lastSpeakMs = 0;
uint32_t lastClipMs[4] = {0, 0, 0, 0};
uint8_t speaksToday = 0;
// fw1.3 走近播报 (talk normal), 规则见 VoicePolicy.h
bool okPending = false;              // 补水后恢复为不缺水, 0002 还没播过
uint32_t waitVoiceMs = 0;            // 等待: 上一次等待语音 (或等待开始) 的时刻
uint32_t waitEndMs = 0;              // 等待后未恢复: 那次等待结束的时刻
int dayKey = -1;
uint32_t voiceRetryAt = 0;
bool pirFaultLogged = false;

std::string lastAiSummary = "none";   // AI 解释只在串口和日志里: fw1.2 起屏幕只显示表情, 不显示文字

uint32_t waterMs = 0;
bool waterMsValid = false;
float rAtCheck = NAN;
// 模拟读数 (sim) 时按"已补水"只是台架操作: 记录只留在内存里给 AI 上下文用, 不写入闪存; 进入或退出 sim 时丢弃
bool simWaterValid = false;
uint32_t simWaterMs = 0;
float simWaterR = NAN;

uint32_t lastDraw = 0, lastShift = 0;
uint8_t shiftIdx = 0;
bool dirty = true;

// ---------- 小工具 ----------
bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

const char* modeName() { return careModeName(care.mode()); }

String flagsStr() {
  String f;
  if (fastMode) f += "FAST";
  if (simOn) f += f.length() ? "|SIM" : "SIM";
  return f;
}

void logEv(const String& detail) {
  logger.event(net.epoch(), bootId, millis(), modeName(), detail);
  Serial.printf("[event] %s\n", detail.c_str());
}

uint8_t clipFor(CareMode m) {
  switch (m) {
    case CareMode::Dry: return CLIP_DRY;
    case CareMode::Waiting: return CLIP_WAIT;
    case CareMode::Ok: return CLIP_OK;
    default: return 0;
  }
}

uint32_t waitMsNow() { return fastMode ? WAIT_FAST_MS : static_cast<uint32_t>(store.s.waitMin) * 60000UL; }

CareConfig makeConfig() {
  const Settings& s = store.s;
  CareConfig c;
  c.calibrated = calibrationValid(s.dryMv, s.wetMv, CAL_MIN_SPAN_MV) && !isnan(s.L) && !isnan(s.H) && s.H > s.L;
  c.L = s.L;
  c.H = s.H;
  c.waitMs = fastMode ? WAIT_FAST_MS : static_cast<uint32_t>(s.waitMin) * 60000UL;
  c.confirmRounds = CONFIRM_ROUNDS;
  c.invalidToPause = INVALID_ROUNDS_TO_PAUSE;
  c.unconfirmedToPause = UNCONFIRMED_ROUNDS_TO_PAUSE;
  return c;
}

CareMode trustedPrev() {
  const Settings& s = store.s;
  const CareMode m = static_cast<CareMode>(s.lastPlant);
  if (m != CareMode::Dry && m != CareMode::Ok) return CareMode::Boot;
  const uint32_t now = net.epoch();
  if (now && s.lastPlantEpoch && now >= s.lastPlantEpoch && now - s.lastPlantEpoch <= PREV_STATE_TRUST_S) return m;
  return CareMode::Boot;
}

float hoursSinceWater() {
  if (simOn) return simWaterValid ? (millis() - simWaterMs) / 3600000.0f : NAN;
  const uint32_t ep = net.epoch();
  if (ep && store.s.waterEpoch && ep >= store.s.waterEpoch) return (ep - store.s.waterEpoch) / 3600.0f;
  if (waterMsValid) return (millis() - waterMs) / 3600000.0f;
  return NAN;
}

float drop12h() {
  if (hourly.size() < 12 || isnan(lastR)) return NAN;
  const float d = (hourly[hourly.size() - 12] - lastR) / 12.0f;
  return d >= 0 ? d : NAN;   // 期间补过水时不给下降速度
}

// 补水历史 (AI 上下文: 距补水小时数、补水前 R、上一周期、每小时 R)。换标定后 R 换了刻度不再可比,
// 台架上的按键也不是真的补水; 所以 cal 改端点和 clear (开始 24 h 测试前) 时清掉。
void clearWaterHistory() {
  store.s.waterEpoch = 0;
  store.s.waterR = NAN;
  store.s.prevCycleH = NAN;
  store.saveWater();
  store.savePrevCycle();
  waterMsValid = false;
  simWaterValid = false;
  rAtCheck = NAN;
  hourly.clear();
  hourSum = 0;
  hourCount = 0;
  hourStart = 0;
}

void addHourly(float R, uint32_t now) {
  if (hourStart == 0) hourStart = now;
  hourSum += R;
  hourCount++;
  if (now - hourStart >= 3600000UL) {
    hourly.push_back(static_cast<int>(lround(hourSum / hourCount)));
    if (hourly.size() > 48) hourly.erase(hourly.begin());
    hourSum = 0;
    hourCount = 0;
    hourStart = now;
  }
}

void applyLights() {
  switch (care.mode()) {
    case CareMode::Dry: lights.set(LightColor::Yellow); break;
    case CareMode::Waiting: lights.set(LightColor::Blue); break;
    case CareMode::Ok: lights.set(LightColor::Green); break;
    default: lights.set(LightColor::Off); break;   // 设备状态: 灯环暂熄
  }
}

bool quietNow() { return store.s.quietOn && net.quietNow(QUIET_START_MIN, QUIET_END_MIN); }

// proactive = 走近时主动播报 (受冷却、间隔和每日上限限制); 按键反馈和恢复播报不受这些限制, 但仍遵守安静时段
bool speak(uint8_t clip, bool proactive, const char* why) {
  if (clip < 1 || clip > 3) return false;
  const uint32_t now = millis();
  if (!voice.online()) { logEv(String("speak_skipped_no_voice ") + clip); return false; }
  if (quietNow()) {
    logEv(String("speak_skipped_quiet ") + clip + (net.timeKnown() ? "" : " (time unknown)"));
    return false;
  }
  if (proactive) {
    if (talkTest) {
      if (lastSpeakMs && now - lastSpeakMs < VOICE_TEST_GAP_MS) { logEv(String("speak_skipped_gap ") + clip); return false; }
    } else {
      if (lastClipMs[clip] && now - lastClipMs[clip] < VOICE_COOLDOWN_MS) { logEv(String("speak_skipped_cooldown ") + clip); return false; }
      if (speaksToday >= VOICE_DAILY_MAX) { logEv(String("speak_skipped_daily_cap ") + clip); return false; }
    }
  }
  voice.play(clip);
  lastSpeakMs = now;
  lastClipMs[clip] = now;
  if (proactive && !talkTest) speaksToday++;
#if NO_TF
  lights.flash(lights.current(), 3000);   // 无 TF 卡: 台词打印在串口 ([voice N]), 灯闪 3 s 表示正在说话
#endif
  logEv(String("speak ") + clip + " (" + why + ")");
  return true;
}

ExplainContext buildContext(ExplainEvent e) {
  ExplainContext c;
  c.event = e;
  c.state = modeName();
  c.R = care.lastR();
  c.L = store.s.L;
  c.H = store.s.H;
  c.mode = simOn ? "SIM" : (fastMode ? "FAST" : "normal");
  c.localTime = net.hhmm().c_str();
  c.hoursSinceWater = hoursSinceWater();
  c.rBeforeWater = simOn ? (simWaterValid ? simWaterR : NAN) : ((store.s.waterEpoch || waterMsValid) ? store.s.waterR : NAN);
  c.waitMin = fastMode ? 1 : store.s.waitMin;
  c.rAtCheck = (e == ExplainEvent::WaitToOk || e == ExplainEvent::WaitToDry) ? rAtCheck : NAN;
  c.dropPerHour12h = drop12h();
  c.prevCycleHours = simOn ? NAN : store.s.prevCycleH;
  c.confirmRounds = CONFIRM_ROUNDS;
  c.aboveHRounds = care.consecAboveH();      // 最近连续达到 H 的有效读数轮数: 解释"最后一轮已达 H 却未确认恢复"
  const size_t from = hourly.size() > 24 ? hourly.size() - 24 : 0;
  c.hourlyR.assign(hourly.begin() + from, hourly.end());
  c.plant = PROFILE_PLANT;
  c.waterNeed = PROFILE_WATER;
  c.source = PROFILE_SOURCE;
  return c;
}

void requestExplain(ExplainEvent e, bool manual) {
  if (!store.s.aiOn) return;                     // ai off: 不调用
  if (!isPlantMode(care.mode())) return;         // 设备状态不调用
  ai.setNetworkOk(net.connected());
  ai.submit(buildContext(e), manual, manual ? 0 : AI_DELAY_AFTER_EVENT_MS);
  logEv(String("ai_request ") + explainEventName(e) + (manual ? " manual" : ""));
}

void handleStep(const CareStep& st, uint32_t now) {
  if (st.event == CareEvent::None) return;
  char rb[24];
  snprintf(rb, sizeof(rb), " R=%.1f", static_cast<double>(care.lastR()));
  logEv(String("state ") + careModeName(st.from) + "->" + careModeName(st.to) + " " + careEventName(st.event) + rb);
  dirty = true;
  applyLights();
  if ((st.to == CareMode::Dry || st.to == CareMode::Ok) && !simOn) {   // 模拟读数不作为可信原状态
    store.s.lastPlant = static_cast<uint8_t>(st.to);
    store.s.lastPlantEpoch = net.epoch();
    store.saveLastPlant();
  }
  if (st.from == CareMode::Ok && st.to != CareMode::Ok) okPending = false;   // 离开不缺水: 这次恢复那句作废
  switch (st.event) {
    case CareEvent::DryToWait:
      store.s.waitMarker = true;
      store.saveWait();
      if (simOn) {                               // 台架模拟: 不写入补水历史
        simWaterValid = true;
        simWaterMs = now;
        simWaterR = care.lastR();
      } else {
        waterMs = now;
        waterMsValid = true;
        store.s.waterEpoch = net.epoch();
        store.s.waterR = care.lastR();
        store.saveWater();
      }
      waitVoiceMs = now;                         // 等待中"再说一次"的间隔从按键算起
      speak(CLIP_WAIT, false, "button");         // 按键进入等待: 立即播放一次
      requestExplain(ExplainEvent::DryToWait, false);
      break;
    case CareEvent::WaitToOk:
      store.s.waitMarker = false;
      store.saveWait();
      rAtCheck = care.lastR();
      okPending = true;                          // 恢复那句只说一次: 当时有人就当场说, 否则下一次走近时补说
      if (pir.inWindow(now, INTERACTION_WINDOW_MS) && speak(CLIP_OK, false, "recovered")) okPending = false;   // 事件播报: 不受同一句冷却限制
      requestExplain(ExplainEvent::WaitToOk, false);
      break;
    case CareEvent::WaitToDry:
      store.s.waitMarker = false;
      store.saveWait();
      rAtCheck = care.lastR();
      waitEndMs = now;                           // 未确认恢复: 先静默一段时间, 不马上请人再浇水
      requestExplain(ExplainEvent::WaitToDry, false);
      break;
    case CareEvent::OkToDry: {
      requestExplain(ExplainEvent::OkToDry, false);   // 先用旧的"上一周期"组装上下文
      const float h = simOn ? NAN : hoursSinceWater();
      if (!isnan(h)) { store.s.prevCycleH = h; store.savePrevCycle(); }
      break;
    }
    case CareEvent::DryToOk:                     // 未按键但读数恢复 (例如等待后才渗到探头处): 同样只说一次
      okPending = true;
      if (pir.inWindow(now, INTERACTION_WINDOW_MS) && speak(CLIP_OK, false, "recovered")) okPending = false;
      requestExplain(ExplainEvent::DryToOk, false);
      break;
    case CareEvent::FirstState: requestExplain(ExplainEvent::FirstState, false); break;
    case CareEvent::ResumeWaiting:
      store.s.waitMarker = true;
      store.saveWait();
      waitVoiceMs = now;                         // 重新等待完整周期: 间隔从恢复时算起
      break;
    default: break;
  }
}

void handleButton(uint32_t now) {
  const CareStep st = care.onButton(now);
  if (st.event == CareEvent::ButtonIgnored) {
    logEv(String("button_ignored_in_") + careModeName(st.from));
    return;
  }
  handleStep(st, now);
}

void handleMotion(uint32_t now) {
  motionsSinceLog++;
  const CareMode m = care.mode();
  if (!isPlantMode(m)) { logEv("motion (no plant state)"); return; }
  if (talkTest) { speak(clipFor(m), true, "approach"); return; }   // 展示模式: 每次走近都说 (两次至少隔 8 s)
  ApproachCtx c;                                                   // talk normal: fw1.3 规则, 见 VoicePolicy.h
  c.mode = m;
  c.notRecovered = care.notRecovered();
  c.msSinceWaitEnd = now - waitEndMs;
  c.recheckQuietMs = waitMsNow() * VOICE_RECHECK_QUIET_WAITS;
  c.msSinceWaitVoice = now - waitVoiceMs;
  c.waitAgainMs = waitMsNow() / VOICE_WAIT_AGAIN_DIV;
  c.okPending = okPending;
  const ApproachDecision d = approachVoice(c);
  if (d.voice == ApproachVoice::Silent) {                          // 只打印在串口, 不写日志 (桌边有人时走近很频繁)
    Serial.printf("[voice] approach in %s: silent (%s)\n", modeName(), d.why);
    return;
  }
  const bool spoke = speak(clipFor(m), d.voice == ApproachVoice::Proactive, d.why);
  if (spoke && m == CareMode::Waiting) waitVoiceMs = now;
  if (spoke && m == CareMode::Ok) okPending = false;
}

void doRound(uint32_t now) {
  ProbeRound r;
  if (simOn) {
    r.valid = true;
    r.why = "";
  } else {
    r = probe.readRound(SAMPLES_PER_ROUND, SAMPLE_GAP_MS, VALID_MIN_MV, VALID_MAX_MV, ROUND_SPREAD_MAX_MV);
  }
  lastRound = r;
  haveRound = true;
  const float R = simOn ? simR : computeR(r.mv, store.s.dryMv, store.s.wetMv);
  const bool valid = r.valid && !isnan(R);
  lastR = R;
  lastValid = valid;
  if (valid && !simOn) addHourly(R, now);   // 模拟读数不进每小时趋势
  const CareStep st = care.onRound(valid, R, now);   // 先计入本轮, 打印的连续轮数包含本轮
  Serial.printf("[round] mv=%d spread=%d R=%.1f%s valid=%d%s%s mode=%s below=%u above=%u\n", r.mv, r.spread,
                static_cast<double>(R), (!isnan(R) && (R < 0 || R > 100)) ? "(out of 0-100)" : "", valid ? 1 : 0,
                valid ? "" : " why=", valid ? "" : (r.valid ? "uncal" : r.why), careModeName(st.from),
                care.consecBelowL(), care.consecAboveH());
  handleStep(st, now);
  dirty = true;
}

void reconfigure() {
  handleStep(care.reconfigure(makeConfig(), millis()), millis());
  applyLights();
  dirty = true;
}

void onAiResult(const AiResult& res) {
  const char* src = res.fromAi ? "AI" : "TPL";
  String line = "{\"t\":";
  line += net.epoch();
  line += ",\"boot\":";
  line += bootId;
  line += ",\"id\":";
  line += res.id;
  line += ",\"event\":\"";
  line += explainEventName(res.event);
  line += "\",\"state\":\"";
  line += res.state.c_str();
  line += "\",\"model\":";
  line += mj::quote(res.model).c_str();
  line += ",\"model_version\":";
  line += mj::quote(res.modelVersion).c_str();
  line += ",\"prompt\":\"" AI_PROMPT_VERSION "\",\"schema\":\"" AI_SCHEMA_VERSION "\",\"http\":";
  line += res.httpCode;
  line += ",\"attempts\":";
  line += res.attempts;
  line += ",\"latency_ms\":";
  line += res.latencyMs;
  line += ",\"source\":\"";
  line += src;
  line += "\",\"reason\":";
  line += mj::quote(res.reason).c_str();
  line += ",\"headline\":";
  line += mj::quote(res.text.headline).c_str();
  line += ",\"body\":";
  line += mj::quote(res.text.body).c_str();
  line += ",\"manual\":";
  line += res.manual ? "true" : "false";
  line += ",\"input\":";                             // 送给模型的上下文, 供两人核对"输入之外的数字或断言"
  line += res.contextJson.empty() ? "null" : res.contextJson.c_str();
  line += "}";
  logger.ai(line);
  logEv(String("ai_result ") + src + " " + explainEventName(res.event) + " " + res.reason.c_str() + " " + res.latencyMs + "ms");
  char sum[96];
  snprintf(sum, sizeof(sum), "%s %s (%s, %lu ms)", src, explainEventName(res.event), res.reason.c_str(),
           static_cast<unsigned long>(res.latencyMs));
  lastAiSummary = sum;
}

// 屏幕只画图形 (fw1.2): 植物状态一个动态表情, 设备状态一个中性图标; 文字信息都在串口和日志里。
// 每 50 ms 调用一次; 动画帧没变时 Display 不刷新屏幕。
void draw(uint32_t now) {
  if (!display.online()) return;
  if (now - lastShift >= OLED_SHIFT_PERIOD_MS) {
    lastShift = now;
    shiftIdx = (shiftIdx + 1) % 4;
    display.setShift(shiftIdx & 1, (shiftIdx >> 1) & 1);
  }
  switch (care.mode()) {
    case CareMode::Dry:     display.face(Face::Worried, now); break;   // 含"补水后未恢复"
    case CareMode::Waiting: display.face(Face::Waiting, now); break;
    case CareMode::Ok:      display.face(Face::Smile, now); break;
    case CareMode::Boot:
    case CareMode::Reading: display.icon(Icon::Hourglass); break;  // 开机 / 读数确认中: 不出表情
    case CareMode::Uncal:
    case CareMode::Paused:  display.icon(Icon::Warning); break;    // 未标定 / 探头异常: 不出表情
  }
}

void dailyReset() {
  int dk = net.dayKey();
  if (dk < 0) dk = 100000 + static_cast<int>(millis() / 86400000UL);   // 未校时: 按开机天数
  if (dk != dayKey) {
    const bool clockArrived = dayKey >= 100000 && dayKey < 1000000 && dk >= 1000000;   // 开机天数 -> 真实日期
    if (dayKey != -1 && !clockArrived) {
      speaksToday = 0;
      ai.resetDaily();
      logEv("new_day");
    }
    dayKey = dk;
  }
}

// ---------- 串口命令 ----------
void printHelp() {
  Serial.println(
      "commands:\n"
      "  status | help | time | reboot\n"
      "  cal dry | cal wet | cal set <dry_mV> <wet_mV> | cal id <ID>\n"
      "  set L <R> | set H <R> | set LH <L> <H> | set wait <min>\n"
      "  ver profile <P> | ver voice <V> | ver param <X> | id pot <P> | id probe <X>\n"
      "  sim <R> | sim off | fast on|off | quiet on|off | talk test|normal | volume <0-30>\n"
      "  press | motion | explain | ai on|off | apikey <KEY> | model <id>\n"
      "  wifi <ssid> <pass> | wifi2 <ssid> <pass> | wifi clear   (use \"quotes\" for spaces)\n"
      "  dump | aidump | logstat | clear | factory yes");
}

void cmdStatus() {
  const Settings& s = store.s;
  const uint32_t now = millis();
  Serial.printf("PlantCare %s %s | boot #%lu | reset %s | uptime %lu s\n", FW_VERSION, NO_TF ? "NO_TF" : "TF",
                static_cast<unsigned long>(bootId), resetReasonName(), static_cast<unsigned long>(now / 1000));
  Serial.printf("state %s | R %.1f (mv %d, %s) | L %.1f H %.1f | wait %u min%s\n", modeName(), static_cast<double>(lastR),
                lastRound.mv, lastValid ? "valid" : "invalid", static_cast<double>(s.L), static_cast<double>(s.H),
                s.waitMin, fastMode ? " (FAST: 1 min, 5 s sampling)" : "");
  if (simOn) Serial.printf("SIM on: R %.1f comes from the sim command, not the probe (sim off to stop)\n", static_cast<double>(simR));
  if (care.mode() == CareMode::Waiting)
    Serial.printf("waiting: %lu s left\n", static_cast<unsigned long>(care.waitRemainingMs(now) / 1000));
  Serial.printf("cal dry %ld wet %ld id %s | calibrated %s\n", static_cast<long>(s.dryMv), static_cast<long>(s.wetMv),
                s.calId.c_str(), makeConfig().calibrated ? "yes" : "no");
  Serial.printf("versions profile %s voice %s param %s | pot %s probe %s\n", s.profileVer.c_str(), s.voiceVer.c_str(),
                s.paramVer.c_str(), s.potId.c_str(), s.probeId.c_str());
  Serial.printf("voice %s, files %d, vol %u | talk %s | quiet %s (now %s)\n", voice.status(), voice.files(), voice.volume(),
                talkTest ? "test" : "normal", s.quietOn ? "on" : "off", quietNow() ? "quiet" : "speaking allowed");
  if (talkTest)
    Serial.println("approach voice (talk test): every approach, at least 8 s apart");
  else
    Serial.printf("approach voice (talk normal): DRY every %lu min, max %d/day | WAITING again after %lu s | OK once per recovery (%s) | spoken today %u\n",
                  static_cast<unsigned long>(VOICE_COOLDOWN_MS / 60000UL), VOICE_DAILY_MAX,
                  static_cast<unsigned long>(waitMsNow() / VOICE_WAIT_AGAIN_DIV / 1000UL), okPending ? "pending" : "not pending",
                  static_cast<unsigned>(speaksToday));
  Serial.printf("pir warmed %s, motions %lu, stuck %s | button presses %lu | oled %s, screen %s\n", pir.warmedUp(now) ? "yes" : "no",
                static_cast<unsigned long>(pir.count()), pir.stuck() ? "YES" : "no",
                static_cast<unsigned long>(button.presses()), display.online() ? "ok" : "NOT FOUND", Display::lastView());
  Serial.printf("wifi %s %s rssi %d | time %s\n", net.connected() ? "connected" : "not connected", net.ssid().c_str(),
                net.rssi(), net.timeKnown() ? net.hhmm().c_str() : "unknown (quiet by default)");
  Serial.printf("ai %s | key %s | model %s | calls today %u | busy %s | last %s\n", s.aiOn ? "on" : "off",
                ai.hasKey() ? "set" : "MISSING", ai.model().c_str(), ai.callsToday(), ai.busy() ? "yes" : "no",
                lastAiSummary.c_str());
  Serial.printf("log %u / %u bytes | heap free %u, min %u\n", static_cast<unsigned>(logger.usedBytes()),
                static_cast<unsigned>(logger.totalBytes()), ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

void suggestThresholdConversion(int oldDry, int oldWet) {
  const Settings& s = store.s;
  if (isnan(s.L) || isnan(s.H) || !calibrationValid(oldDry, oldWet, CAL_MIN_SPAN_MV) ||
      !calibrationValid(s.dryMv, s.wetMv, CAL_MIN_SPAN_MV))
    return;
  const float nL = convertThreshold(s.L, oldDry, oldWet, s.dryMv, s.wetMv);
  const float nH = convertThreshold(s.H, oldDry, oldWet, s.dryMv, s.wetMv);
  Serial.printf("NOTE: endpoints changed after L/H were set. To keep the same decision boundary (mV), apply:\n"
                "  set LH %.1f %.1f\n", static_cast<double>(nL), static_cast<double>(nH));
}

void handleCommand(const std::vector<std::string>& t) {
  const std::string c = lowerStr(t[0]);
  auto arg = [&](size_t i) { return i < t.size() ? t[i] : std::string(); };
  const std::string a1 = lowerStr(arg(1));
  Settings& s = store.s;

  if (c == "help") { printHelp(); return; }
  if (c == "status") { cmdStatus(); return; }
  if (c == "time") {
    Serial.printf("time %s, wifi %s\n", net.timeKnown() ? net.hhmm().c_str() : "unknown", net.connected() ? "connected" : "not connected");
    return;
  }
  if (c == "reboot") { Serial.println("rebooting..."); delay(200); ESP.restart(); return; }
  if (c == "cal") {
    const int oldDry = s.dryMv, oldWet = s.wetMv;
    if (a1 == "dry" || a1 == "wet") {
      Serial.println("measuring 5 rounds...");
      const ProbeRound r = probe.readStable(5, SAMPLES_PER_ROUND, SAMPLE_GAP_MS, VALID_MIN_MV, VALID_MAX_MV, ROUND_SPREAD_MAX_MV);
      if (!r.valid) { Serial.printf("reading invalid (%s, mv %d, spread %d) - check wiring\n", r.why, r.mv, r.spread); return; }
      if (a1 == "dry") s.dryMv = r.mv; else s.wetMv = r.mv;
      Serial.printf("%s end = %d mV (spread %d mV)\n", a1.c_str(), r.mv, r.spread);
    } else if (a1 == "set" && t.size() >= 4) {
      const int dry = atoi(arg(2).c_str()), wet = atoi(arg(3).c_str());
      if (!calibrationValid(dry, wet, CAL_MIN_SPAN_MV)) { Serial.printf("rejected: |wet-dry| must be >= %d mV\n", CAL_MIN_SPAN_MV); return; }
      s.dryMv = dry;
      s.wetMv = wet;
    } else if (a1 == "id" && t.size() >= 3) {
      s.calId = arg(2).c_str();
    } else {
      Serial.println("usage: cal dry | cal wet | cal set <dry> <wet> | cal id <ID>");
      return;
    }
    store.saveCal();
    logEv(String("cal dry=") + s.dryMv + " wet=" + s.wetMv + " id=" + s.calId);
    if (oldDry != s.dryMv || oldWet != s.wetMv) {
      suggestThresholdConversion(oldDry, oldWet);
      clearWaterHistory();
      logEv("water_history_cleared (calibration changed)");
    }
    reconfigure();
    return;
  }
  if (c == "set" && t.size() >= 3) {
    const float v = atof(arg(2).c_str());
    if (a1 == "lh") {                                  // 一次改两个, 不受先后顺序影响
      if (t.size() < 4) { Serial.println("usage: set LH <L> <H>"); return; }
      const float L = v, H = atof(arg(3).c_str());
      if (L < 0 || L > 100 || H < 0 || H > 100) { Serial.println("rejected: use 0-100"); return; }
      if (H <= L) { Serial.println("rejected: H must be greater than L"); return; }
      s.L = L;
      s.H = H;
    } else if (a1 == "l" || a1 == "h") {
      if (v < 0 || v > 100) { Serial.println("rejected: use 0-100"); return; }
      const float L = a1 == "l" ? v : s.L, H = a1 == "h" ? v : s.H;
      if (!isnan(L) && !isnan(H) && H <= L) {
        Serial.printf("rejected: H must be greater than L (would be L %.1f, H %.1f) - change both with  set LH <L> <H>\n",
                      static_cast<double>(L), static_cast<double>(H));
        return;
      }
      if (a1 == "l") s.L = v; else s.H = v;
    } else if (a1 == "wait") {
      if (v < 1 || v > 120) { Serial.println("rejected: 1-120 min"); return; }
      s.waitMin = static_cast<uint16_t>(v);
    } else {
      Serial.println("usage: set L <R> | set H <R> | set LH <L> <H> | set wait <min>");
      return;
    }
    store.saveThresholds();
    logEv(String("set L=") + s.L + " H=" + s.H + " wait=" + s.waitMin);
    reconfigure();
    return;
  }
  if ((c == "ver" || c == "id") && t.size() >= 3) {
    const String v = arg(2).c_str();
    if (c == "ver" && a1 == "profile") s.profileVer = v;
    else if (c == "ver" && a1 == "voice") s.voiceVer = v;
    else if (c == "ver" && a1 == "param") s.paramVer = v;
    else if (c == "id" && a1 == "pot") s.potId = v;
    else if (c == "id" && a1 == "probe") s.probeId = v;
    else { Serial.println("usage: ver profile|voice|param <X> | id pot|probe <X>"); return; }
    store.saveVersions();
    logEv(String("version ") + c.c_str() + " " + a1.c_str() + "=" + v);
    return;
  }
  if (c == "sim") {
    const bool wasSim = simOn;
    if (a1 == "off") simOn = false;
    else if (t.size() >= 2) { simOn = true; simR = atof(arg(1).c_str()); }
    if (simOn != wasSim) simWaterValid = false;
    if (simOn) logEv(String("sim on R=") + simR);
    else logEv("sim off");
    nextSampleAt = millis();
    return;
  }
  if (c == "fast") {
    fastMode = (a1 == "on");
    logEv(fastMode ? "fast on" : "fast off");
    reconfigure();
    nextSampleAt = millis();
    return;
  }
  if (c == "quiet") { s.quietOn = (a1 != "off"); store.saveVoice(); logEv(s.quietOn ? "quiet on" : "quiet off"); return; }
  if (c == "talk") {
    talkTest = (a1 == "test");
    s.talkMode = talkTest ? 1 : 0;
    store.saveVoice();
    logEv(talkTest ? "talk test" : "talk normal");
    return;
  }
  if (c == "volume" && t.size() >= 2) {
    const int v = constrain(atoi(arg(1).c_str()), 0, 30);
    voice.setVolume(static_cast<uint8_t>(v));
    s.volume = static_cast<uint8_t>(v);
    store.saveVoice();
    Serial.printf("volume %d\n", v);
    return;
  }
  if (c == "press") { handleButton(millis()); return; }
  if (c == "motion") { pir.inject(millis()); handleMotion(millis()); return; }
  if (c == "explain") {
    if (!s.aiOn) { Serial.println("ai is off (ai on)"); return; }
    if (!isPlantMode(care.mode())) { Serial.println("no care state yet"); return; }
    requestExplain(ExplainEvent::Manual, true);
    return;
  }
  if (c == "ai") {
    s.aiOn = (a1 != "off");
    store.saveAi();
    ai.configure(s.apiKey, s.model, s.aiOn);
    logEv(s.aiOn ? "ai on" : "ai off");
    return;
  }
  if (c == "apikey") {
    if (t.size() < 2) { Serial.println("usage: apikey <KEY>   (stored on the device, never printed)"); return; }
    s.apiKey = arg(1).c_str();
    store.saveAi();
    ai.configure(s.apiKey, s.model, s.aiOn);
    Serial.printf("API key saved (%u chars)\n", static_cast<unsigned>(s.apiKey.length()));
    logEv("apikey updated");
    return;
  }
  if (c == "model") {
    s.model = t.size() >= 2 ? String(arg(1).c_str()) : String("");
    store.saveAi();
    ai.configure(s.apiKey, s.model, s.aiOn);
    Serial.printf("model %s\n", ai.model().c_str());
    logEv(String("model ") + ai.model());
    return;
  }
  if (c == "wifi" || c == "wifi2") {
    if (a1 == "clear") { s.ssid1 = s.pass1 = s.ssid2 = s.pass2 = ""; }
    else if (t.size() >= 2) {
      if (c == "wifi") { s.ssid1 = arg(1).c_str(); s.pass1 = arg(2).c_str(); }
      else { s.ssid2 = arg(1).c_str(); s.pass2 = arg(2).c_str(); }
    } else { Serial.println("usage: wifi <ssid> <pass> | wifi2 <ssid> <pass> | wifi clear"); return; }
    store.saveWifi();
    net.setCredentials(s.ssid1, s.pass1, s.ssid2, s.pass2);
    Serial.printf("wifi1 \"%s\" wifi2 \"%s\" saved (2.4 GHz only)\n", s.ssid1.c_str(), s.ssid2.c_str());
    logEv("wifi updated");
    return;
  }
  if (c == "dump") { logger.dump(Serial); return; }
  if (c == "aidump") { logger.dumpAi(Serial); return; }
  if (c == "logstat") { logger.stats(Serial, LOG_PERIOD_MS); return; }
  if (c == "clear") {
    logger.clear();
    clearWaterHistory();
    Serial.println("logs cleared, watering history cleared");
    return;
  }
  if (c == "factory") {
    if (a1 != "yes") { Serial.println("erases ALL settings and logs: type  factory yes"); return; }
    store.factoryReset();
    logger.clear();
    Serial.println("factory reset, rebooting");
    delay(200);
    ESP.restart();
    return;
  }
  Serial.printf("unknown command: %s (type help)\n", t[0].c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== PlantCare %s (%s) ===\nreset reason: %s\n", FW_VERSION, NO_TF ? "NO_TF" : "TF", resetReasonName());
  store.begin(NVS_NAMESPACE);
  store.bumpBoots();
  bootId = store.s.boots;
  talkTest = NO_TF ? true : (store.s.talkMode == 1);   // 无 TF 卡版本默认 talk test
  logger.begin(LOG_MAX_BYTES, AI_LOG_MAX_BYTES);
  // 开机记录尽早写: 上电后几秒内断电或掉电重启 (BROWNOUT) 也能在日志里看到
  logEv(String("boot fw=" FW_VERSION " reset=") + resetReasonName() + " cal=" + store.s.calId + " param=" + store.s.paramVer);
  if (logger.recovered()) logEv(String("log_area_erased (") + logger.recoveredWhy() + ")");
  display.begin();
  display.icon(Icon::Hourglass);
  lights.begin(RING_BRIGHTNESS, BOARD_RGB_BRIGHTNESS);
  lights.set(LightColor::Off);
  probe.begin(PIN_PROBE);
  button.begin(PIN_BUTTON, BUTTON_DEBOUNCE_MS);
  pir.begin(PIN_PIR, PIR_WARMUP_MS, PIR_STUCK_MS);
  voice.begin(Serial2, PIN_DF_RX, PIN_DF_TX, store.s.volume);
  voiceRetryAt = millis() + 600000UL;
  if (voice.txOnly()) logEv("voice_tx_only (DFPlayer sends no replies: commands are sent, audio not confirmed)");
  net.begin(store.s.ssid1, store.s.pass1, store.s.ssid2, store.s.pass2, TZ_SYDNEY);
  ai.begin(GOOGLE_ROOT_CA, true);
  ai.configure(store.s.apiKey, store.s.model, store.s.aiOn);
  Serial.printf("oled %s | voice %s (files %d) | wifi %s | ai %s, key %s\n", display.online() ? "ok" : "NOT FOUND",
                voice.status(), voice.files(), net.hasCredentials() ? "configured" : "not set",
                store.s.aiOn ? "on" : "off", ai.hasKey() ? "set" : "missing");
  const CareStep st = care.begin(makeConfig(), trustedPrev(), store.s.waitMarker, millis());
  handleStep(st, millis());
  applyLights();
  nextSampleAt = millis() + 2000;
  nextLogAt = millis() + 5000;
  printHelp();
}

void loop() {
  const uint32_t now = millis();
  std::vector<std::string> toks;
  if (console.poll(Serial, toks)) {
    // 回显命令, 让串口记录 (logs/ 文件夹) 能看出输入了什么; 密钥和密码不回显
    const std::string c0 = lowerStr(toks[0]);
    if (c0 == "apikey") Serial.println("> apikey ****");
    else if ((c0 == "wifi" || c0 == "wifi2") && toks.size() >= 3) Serial.printf("> %s %s ****\n", toks[0].c_str(), toks[1].c_str());
    else Serial.printf("> %s\n", console.lastLine().c_str());
    handleCommand(toks);
  }

  net.loop(now);
  ai.setNetworkOk(net.connected());
  if (net.justSynced()) {
    logEv(String("time_known ") + net.hhmm() + (net.ntpStarted() ? " (ntp)" : " (kept across software reset)"));
    care.setTrustedPrev(trustedPrev());
  }
  dailyReset();

  if (button.update(now)) handleButton(now);
  if (pir.update(now)) handleMotion(now);
  if (pir.stuck() && !pirFaultLogged) { pirFaultLogged = true; logEv("pir_fault (stuck high) - approach trigger paused"); }
  if (!pir.stuck()) pirFaultLogged = false;
  voice.poll();
#if !NO_TF
  if (!voice.online() && due(now, voiceRetryAt)) {   // 语音模块离线时每 10 min 重试一次
    voiceRetryAt = now + 600000UL;
    if (voice.retry()) logEv("voice back online");
  }
#endif

  if (due(now, nextSampleAt)) {
    nextSampleAt = now + (fastMode ? SAMPLE_PERIOD_FAST_MS : SAMPLE_PERIOD_MS);
    doRound(now);
  }
  handleStep(care.onTick(millis()), millis());

  if (haveRound && due(now, nextLogAt)) {
    nextLogAt += LOG_PERIOD_MS;                                // 固定节拍, 不累积漂移
    if (due(now, nextLogAt)) nextLogAt = now + LOG_PERIOD_MS;   // 长时间阻塞后重新对齐
    logger.sample(net.epoch(), bootId, now, modeName(), lastRound.mv, lastR, lastValid, motionsSinceLog, flagsStr().c_str());
    motionsSinceLog = 0;
    // 每 5 min 刷新"可信原状态"的时间: 断电或重启后 6 h 内, R 在滞回区可沿用这个状态
    if ((care.mode() == CareMode::Dry || care.mode() == CareMode::Ok) && net.epoch() && !simOn) {
      store.s.lastPlant = static_cast<uint8_t>(care.mode());
      store.s.lastPlantEpoch = net.epoch();
      store.saveLastPlant();
    }
  }

  AiResult res;
  if (ai.poll(res)) onAiResult(res);

  lights.update(now);
  if (dirty || now - lastDraw >= 50) {          // 动态表情: 约 20 次/秒检查, 实际只在画面变化时刷新
    draw(now);
    lastDraw = now;
    dirty = false;
  }
  delay(5);
}
