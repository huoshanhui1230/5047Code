// 电脑上运行的单元测试 (不需要开发板): 状态机、AI 解释校验/模板、JSON、标定计算、分词
// 运行: bash test_host/run_tests.sh
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "CareState.h"
#include "VoicePolicy.h"
#include "Explain.h"
#include "MiniJson.h"
#include "CalMath.h"
#include "Tokens.h"
#include "LogStats.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ_STR(a, b) do { std::string _a = (a), _b = (b); if (_a == _b) g_pass++; else { g_fail++; printf("FAIL %s:%d  [%s] != [%s]\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); } } while (0)

static const uint32_t MIN = 60000;

static CareConfig cfg(float L = 30, float H = 38, uint32_t waitMs = 10 * MIN) {
  CareConfig c; c.calibrated = true; c.L = L; c.H = H; c.waitMs = waitMs; return c;
}

// 让状态机跑 n 轮相同读数, 返回最后一步
static CareStep rounds(CareMachine& m, int n, float R, uint32_t& t, bool valid = true) {
  CareStep st;
  for (int i = 0; i < n; i++) { t += MIN; st = m.onRound(valid, R, t); CareStep tk = m.onTick(t); if (tk.event != CareEvent::None) st = tk; }
  return st;
}

static void test_care_bench13() {
  // 对应操作指南 8.1 台架 13 条 (L=30, H=38)
  CareMachine m; uint32_t t = 0;
  CareStep st = m.begin(cfg(), CareMode::Boot, false, t);
  CHECK(m.mode() == CareMode::Boot);
  // 1. R≈50, 3 轮后不缺水
  st = rounds(m, 2, 50, t); CHECK(m.mode() == CareMode::Boot);
  st = rounds(m, 1, 50, t); CHECK(m.mode() == CareMode::Ok); CHECK(st.event == CareEvent::FirstState);
  // 3. 不缺水时按键被忽略
  st = m.onButton(t); CHECK(st.event == CareEvent::ButtonIgnored); CHECK(m.mode() == CareMode::Ok);
  // 4. R<30 连续 3 轮 -> 缺水
  st = rounds(m, 2, 25, t); CHECK(m.mode() == CareMode::Ok);
  st = rounds(m, 1, 25, t); CHECK(m.mode() == CareMode::Dry); CHECK(st.event == CareEvent::OkToDry);
  // 7. 缺水时按键 -> 等待
  st = m.onButton(t); CHECK(st.event == CareEvent::DryToWait); CHECK(m.mode() == CareMode::Waiting);
  // 8. 等待期间读数回到 45, 不立即变绿
  st = rounds(m, 5, 45, t); CHECK(m.mode() == CareMode::Waiting);
  // 等待期间重复按键不重置计时
  uint32_t remBefore = m.waitRemainingMs(t);
  st = m.onButton(t); CHECK(st.event == CareEvent::ButtonIgnored); CHECK(m.waitRemainingMs(t) == remBefore);
  // 9. 到期且连续 3 轮 ≥ H -> 不缺水
  st = rounds(m, 5, 45, t); CHECK(m.mode() == CareMode::Ok); CHECK(st.event == CareEvent::WaitToOk);
  // 10. 再次变干、按键、一直保持干 -> 到期回到缺水, Not yet recovered
  rounds(m, 3, 25, t); CHECK(m.mode() == CareMode::Dry);
  m.onButton(t); CHECK(m.mode() == CareMode::Waiting);
  st = rounds(m, 10, 25, t); CHECK(m.mode() == CareMode::Dry); CHECK(st.event == CareEvent::WaitToDry);
  CHECK(m.notRecovered());
  // 11. 滞回区 R=34: 缺水保持缺水
  st = rounds(m, 5, 34, t); CHECK(m.mode() == CareMode::Dry);
  // 缺水期间未按键但读数连续 3 轮 ≥ H -> 不缺水 (DryToOk)
  st = rounds(m, 3, 40, t); CHECK(m.mode() == CareMode::Ok); CHECK(st.event == CareEvent::DryToOk);
  CHECK(!m.notRecovered());
  // 滞回区: 不缺水保持不缺水
  st = rounds(m, 5, 34, t); CHECK(m.mode() == CareMode::Ok);
  // 12. 停在 34 重启 (无可信原状态) -> Reading, 不默认变绿
  CareMachine m2; uint32_t t2 = 0;
  m2.begin(cfg(), CareMode::Boot, false, t2);
  rounds(m2, 3, 34, t2); CHECK(m2.mode() == CareMode::Reading);
  rounds(m2, 10, 34, t2); CHECK(m2.mode() == CareMode::Reading);
  rounds(m2, 3, 39, t2); CHECK(m2.mode() == CareMode::Ok);
}

static void test_care_restart_and_faults() {
  // 可信原状态 Ok + 滞回区 -> 沿用 Ok
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Ok, false, t);
    CareStep st = rounds(m, 3, 34, t); CHECK(m.mode() == CareMode::Ok); CHECK(st.event == CareEvent::FirstState); }
  // 可信原状态 Dry + 滞回区 -> 沿用 Dry
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Dry, false, t);
    rounds(m, 3, 34, t); CHECK(m.mode() == CareMode::Dry); }
  // 等待标记 -> 开机即等待完整周期, 到期按读数判断
  { CareMachine m; uint32_t t = 0; CareStep st = m.begin(cfg(), CareMode::Boot, true, t);
    CHECK(st.event == CareEvent::ResumeWaiting); CHECK(m.mode() == CareMode::Waiting);
    CHECK(m.waitRemainingMs(t) == 10 * MIN);
    rounds(m, 9, 40, t); CHECK(m.mode() == CareMode::Waiting);
    st = rounds(m, 1, 40, t); CHECK(m.mode() == CareMode::Ok); CHECK(st.event == CareEvent::WaitToOk); }
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, true, t);
    CareStep st = rounds(m, 10, 25, t); CHECK(m.mode() == CareMode::Dry); CHECK(st.event == CareEvent::WaitToDry); }
  // 无效读数打断连续确认
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 50, t); CHECK(m.mode() == CareMode::Ok);
    rounds(m, 2, 25, t); rounds(m, 1, 0, t, false); rounds(m, 2, 25, t); CHECK(m.mode() == CareMode::Ok);
    rounds(m, 1, 25, t); CHECK(m.mode() == CareMode::Dry); }
  // 连续 3 轮无效 -> Paused; 恢复后以原状态为可信状态重新确认
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 50, t); CHECK(m.mode() == CareMode::Ok);
    CareStep st = rounds(m, 3, 0, t, false); CHECK(m.mode() == CareMode::Paused); CHECK(st.event == CareEvent::Paused);
    st = rounds(m, 1, 34, t); CHECK(m.mode() == CareMode::Reading); CHECK(st.event == CareEvent::EnterReading);
    rounds(m, 2, 34, t); CHECK(m.mode() == CareMode::Ok); }
  // 等待中暂停, 恢复后重新等待完整周期
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 25, t); m.onButton(t); CHECK(m.mode() == CareMode::Waiting);
    rounds(m, 3, 0, t, false); CHECK(m.mode() == CareMode::Paused);
    CareStep st = rounds(m, 1, 45, t); CHECK(st.event == CareEvent::ResumeWaiting); CHECK(m.mode() == CareMode::Waiting);
    CHECK(m.waitRemainingMs(t) == 10 * MIN); }
  // 到期时有效读数不足 3 轮 -> 继续等待新读数
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 25, t); m.onButton(t);
    rounds(m, 8, 45, t); rounds(m, 1, 0, t, false); rounds(m, 1, 45, t);
    CHECK(m.waitExpired(t)); CHECK(m.mode() == CareMode::Waiting);
    rounds(m, 1, 45, t); CHECK(m.mode() == CareMode::Waiting);
    rounds(m, 1, 45, t); CHECK(m.mode() == CareMode::Ok); }
  // 未标定 -> Uncal; 标定后进入 Reading
  { CareMachine m; uint32_t t = 0; CareConfig c; c.calibrated = false;
    CareStep st = m.begin(c, CareMode::Boot, false, t); CHECK(m.mode() == CareMode::Uncal); CHECK(st.event == CareEvent::Uncalibrated);
    rounds(m, 5, 50, t); CHECK(m.mode() == CareMode::Uncal);
    st = m.reconfigure(cfg(), t); CHECK(m.mode() == CareMode::Reading); CHECK(st.event == CareEvent::EnterReading);
    rounds(m, 3, 50, t); CHECK(m.mode() == CareMode::Ok); }
  // 开机后校时才得到可信原状态: Reading 中补认后沿用
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 34, t); CHECK(m.mode() == CareMode::Reading);
    m.setTrustedPrev(CareMode::Dry);
    rounds(m, 1, 34, t); CHECK(m.mode() == CareMode::Dry); }
  // R 正好等于 L 不算低于 L; 正好等于 H 算达到 H
  { CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 38, t); CHECK(m.mode() == CareMode::Ok);
    rounds(m, 5, 30, t); CHECK(m.mode() == CareMode::Ok); }
}

static void test_json() {
  mj::Value v; std::string err;
  CHECK(mj::parse("{\"a\":[1,2.5,{\"b\":\"x\\n\\\"y\\u00e9\"}],\"t\":true,\"n\":null}", v, err));
  CHECK(v.get("a") && v.get("a")->arr.size() == 3);
  CHECK(v.get("a")->at(1)->num == 2.5);
  CHECK_EQ_STR(v.get("a")->at(2)->get("b")->str, std::string("x\n\"y\xc3\xa9"));
  CHECK(v.get("t")->isTrue());
  CHECK(!mj::parse("{\"a\":}", v, err));
  CHECK(!mj::parse("{\"a\":1} x", v, err));
  CHECK_EQ_STR(mj::escape("a\"b\\c\nd"), "a\\\"b\\\\c\\nd");
  mj::Value s; CHECK(mj::parse("\"\\ud83d\\ude00\"", s, err)); CHECK(s.str == "\xF0\x9F\x98\x80");
}

static ExplainContext ctx(ExplainEvent ev, const char* state, float R) {
  ExplainContext c;
  c.event = ev; c.state = state; c.R = R; c.L = 40; c.H = 45; c.localTime = "14:20";
  c.hoursSinceWater = 51.2f; c.rBeforeWater = 36; c.waitMin = 10; c.rAtCheck = NAN;
  c.dropPerHour12h = 0.63f; c.prevCycleHours = 46; c.hourlyR = {68, 66, 60, 52, 45, 39};
  c.plant = "Viola banksii"; c.waterNeed = "prefers moist soil"; c.source = "ANPSA plant profile (accessed 2026-10)";
  return c;
}

static void test_explain() {
  ExplainContext c = ctx(ExplainEvent::OkToDry, "DRY", 38.4f);
  std::string cj = buildContextJson(c);
  mj::Value v; std::string err;
  CHECK(mj::parse(cj, v, err));
  CHECK(v.get("R")->num == 38);
  CHECK(v.get("hours_since_water")->num == 51);
  CHECK(v.get("R_drop_per_hour_12h")->num == 0.6);
  CHECK(v.get("R_at_check")->kind == mj::Value::T_NULL);
  std::string body = buildRequestBody(cj, "");
  CHECK(mj::parse(body, v, err));
  CHECK(v.get("generationConfig") && v.get("generationConfig")->get("responseSchema"));
  CHECK(v.get("contents")->at(0)->get("parts")->at(0)->get("text")->str.find("\"event\":\"ok_to_dry\"") != std::string::npos);

  // 正常返回 (含 thought 部分)
  std::string resp = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"thinking...\",\"thought\":true},"
                     "{\"text\":\"{\\\"headline\\\":\\\"Soil drying\\\",\\\"body\\\":\\\"R fell from 68 to 38 in 51 h. Check the top soil.\\\"}\"}]},"
                     "\"finishReason\":\"STOP\"}],\"modelVersion\":\"gemini-3.5-flash-lite\"}";
  std::string text, model;
  CHECK(extractModelText(resp, text, model, err));
  CHECK_EQ_STR(model, "gemini-3.5-flash-lite");
  ExplainText t;
  CHECK(parseExplainJson(text, t, err));
  CHECK_EQ_STR(t.headline, "Soil drying");
  std::string reason;
  CHECK(validateExplain(t, c, cj, reason));
  // 错误与被拦截
  CHECK(!extractModelText("{\"error\":{\"code\":400,\"message\":\"bad model\"}}", text, model, err));
  CHECK(err.find("bad model") != std::string::npos);
  CHECK_EQ_STR(apiErrorMessage("{\"error\":{\"code\":429,\"message\":\"quota\"}}"), "quota");
  CHECK(!extractModelText("{\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}", text, model, err));
  CHECK(!extractModelText("{\"candidates\":[{\"content\":{\"parts\":[]},\"finishReason\":\"MAX_TOKENS\"}]}", text, model, err));
  CHECK(err.find("MAX_TOKENS") != std::string::npos);
  // 代码块包裹
  CHECK(parseExplainJson("```json\n{\"headline\":\"Waiting\",\"body\":\"Re-checking R after 10 min.\"}\n```", t, err));
  CHECK(!parseExplainJson("{\"headline\":\"x\"}", t, err));

  // 校验规则
  auto bad = [&](const char* h, const char* b, const ExplainContext& cc) {
    ExplainText tt{h, b}; std::string r; return !validateExplain(tt, cc, buildContextJson(cc), r);
  };
  CHECK(bad("This headline is far too long", "R 38.", c));                      // 标题过长
  CHECK(bad("Soil drying", std::string(101, 'a').c_str(), c));                   // 正文过长
  CHECK(bad("Soil drying", "R is 38 \xe2\x80\x94 check soil.", c));             // 非 ASCII
  CHECK(bad("Soil drying", "Check the top 2-3 cm of soil.", c));                 // 编造数字 2
  CHECK(bad("Soil drying", "Roots may be dry. R 38.", c));                       // 诊断用词
  CHECK(bad("Hot day", "R 38 fell fast in the heat.", c));                       // 天气/高温
  CHECK(bad("Soil drying", "I feel thirsty. R 38.", c));                         // 拟人感受
  CHECK(bad("Soil drying", "R 38. No water needed now.", c));                    // DRY 说不用浇
  ExplainContext okc = ctx(ExplainEvent::WaitToOk, "OK", 71);
  CHECK(bad("Recovered", "R 71. Please water now.", okc));                       // OK 要求浇水
  ExplainContext wc = ctx(ExplainEvent::DryToWait, "WAITING", 36);
  CHECK(bad("Waiting", "Add water again if R is low.", wc));                     // WAITING 要求浇水
  // 允许的写法: 输入里的数字、时间、四舍五入
  CHECK(!bad("Soil drying", "R fell from 68 to 38, about 0.6 per hour. Check soil.", c));
  CHECK(!bad("Soil drying", "R fell about 1 per hour since 14:20. Check soil.", c));
  CHECK(!bad("Soil drying", "R 38/100 is below 40. Check the soil.", c));
  CHECK(!bad("Soil drying", "Protect the pot; R 38 is below 40. Check soil.", c));  // "protect" 不是 rot

  // 所有模板都必须通过校验, 且长度合适
  const ExplainEvent evs[] = {ExplainEvent::FirstState, ExplainEvent::OkToDry, ExplainEvent::DryToWait,
                              ExplainEvent::WaitToOk, ExplainEvent::WaitToDry, ExplainEvent::DryToOk, ExplainEvent::Manual};
  const char* states[] = {"DRY", "WAITING", "OK"};
  const float Rs[] = {12.0f, 38.0f, 42.6f, 45.0f, 71.0f, 100.0f};
  for (ExplainEvent ev : evs)
    for (const char* st : states)
      for (float R : Rs)
        for (int withTime = 0; withTime < 2; withTime++) {
          ExplainContext cc = ctx(ev, st, R);
          if (!withTime) cc.localTime.clear();
          cc.waitMin = 30;
          // 只测有意义的组合
          if ((ev == ExplainEvent::OkToDry || ev == ExplainEvent::WaitToDry) && std::string(st) != "DRY") continue;
          if ((ev == ExplainEvent::WaitToOk || ev == ExplainEvent::DryToOk) && std::string(st) != "OK") continue;
          if (ev == ExplainEvent::DryToWait && std::string(st) != "WAITING") continue;
          ExplainText tt = templateExplain(cc);
          std::string r;
          bool okv = validateExplain(tt, cc, buildContextJson(cc), r);
          if (!okv) printf("  template failed: %s/%s R=%.1f: %s | %s (%s)\n", explainEventName(ev), st, R, tt.headline.c_str(), tt.body.c_str(), r.c_str());
          CHECK(okv);
        }
  // 换行
  std::vector<std::string> lines = wrapWords("R reached 71 (>= 45) after watering. No water needed now.", 25);
  CHECK(lines.size() == 3);
  for (auto& l : lines) CHECK(l.size() <= 25);
  CHECK(wrapWords("abcdefghijklmnopqrstuvwxyz0123", 25).size() == 2);
}

static void test_calmath() {
  CHECK(std::fabs(computeR(1700, 2100, 1150) - 42.105f) < 0.01f);
  CHECK(std::isnan(computeR(1700, 2100, 2100)));
  CHECK(std::fabs(convertThreshold(40, 2100, 1150, 2100, 1050) - 36.19f) < 0.05f);
  CHECK(calibrationValid(2100, 1150, 300));
  CHECK(!calibrationValid(2100, 1900, 300));
  CalReport r = calibrationReport({2101, 2098, 2104}, {1650, 1655, 1648}, {1152, 1149, 1155});
  CHECK(r.ok); CHECK(r.monotonic); CHECK(r.spanOk); CHECK(r.repeatOk);
  CHECK(r.dryMv == 2101); CHECK(r.wetMv == 1152);
  CHECK(r.rMid > 45 && r.rMid < 50);
  CalReport bad = calibrationReport({2100, 2000, 2200}, {1650, 1655, 1648}, {1152, 1149, 1155});
  CHECK(!bad.repeatOk); CHECK(!bad.ok);
  // 同级极差判据 10%: 8.6% (与 11/2 实测最大值相当) 通过, 10.5% 不通过
  CalReport r86 = calibrationReport({2101, 2060, 2142}, {1650, 1655, 1648}, {1152, 1149, 1155});
  CHECK(r86.maxRangePct > 8.5 && r86.maxRangePct < 8.8); CHECK(r86.repeatOk); CHECK(r86.ok);
  CalReport r105 = calibrationReport({2101, 2051, 2151}, {1650, 1655, 1648}, {1152, 1149, 1155});
  CHECK(r105.maxRangePct > 10.0); CHECK(!r105.repeatOk); CHECK(!r105.ok);
  CalReport nonmono = calibrationReport({2100, 2101, 2099}, {2300, 2301, 2299}, {1150, 1151, 1149});
  CHECK(!nonmono.monotonic);
}

static void test_tokens() {
  auto t = tokenize("wifi \"Zihao Phone\" pass123");
  CHECK(t.size() == 3); CHECK_EQ_STR(t[1], "Zihao Phone");
  t = tokenize("  cal   set 2100 1150 ");
  CHECK(t.size() == 4); CHECK_EQ_STR(t[3], "1150");
  t = tokenize("wifi \"\" \"\"");
  CHECK(t.size() == 3); CHECK(t[1].empty());
  CHECK_EQ_STR(lowerStr("FaSt ON"), "fast on");
}

static std::string srow(uint32_t ep, uint32_t boot, uint32_t ms, bool valid, const char* flags = "") {
  char b[160];
  snprintf(b, sizeof(b), "%lu,%lu,%lu,S,OK,1500,55.0,%d,0,%s", (unsigned long)ep, (unsigned long)boot, (unsigned long)ms,
           valid ? 1 : 0, flags);
  return b;
}
static std::string erow(uint32_t ep, uint32_t boot, uint32_t ms, const char* detail) {
  char b[200];
  snprintf(b, sizeof(b), "%lu,%lu,%lu,E,OK,,,,,%s", (unsigned long)ep, (unsigned long)boot, (unsigned long)ms, detail);
  return b;
}

static void test_logstats() {
  const uint32_t E0 = 1761955200u;   // 2025-11-01 00:00 UTC (只用于相对时间)
  // 1) 完整 24 h, NTP 时间, 3 条无效 -> 通过
  {
    LogStats st(300);
    st.add("epoch,boot,ms,type,state,mv,R,valid,motion,detail");   // 表头忽略
    st.add("garbage line");
    st.add(erow(E0, 5, 1000, "boot fw=R7.2-fw1.0 reset=POWERON cal=C1 param=v0-temp"));
    for (uint32_t i = 0; i <= 288; i++) st.add(srow(E0 + 5 + i * 300, 5, 5000 + i * 300000u, !(i == 10 || i == 11 || i == 200)));
    st.add(erow(E0 + 40000, 5, 40000000u, "state OK->DRY ok_to_dry R=38.0"));
    st.add(erow(E0 + 41000, 5, 41000000u, "state DRY->WAITING dry_to_wait R=37.5"));
    CHECK(st.rows() == 289); CHECK(st.validRows() == 286); CHECK(st.flaggedRows() == 0);
    CHECK(st.epochAxis()); CHECK(st.spanKnown());
    CHECK(st.spanS() > 86399 && st.spanS() < 86401);
    CHECK(st.windowComplete()); CHECK(st.windowRows() == 288); CHECK(st.windowValid() == 285); CHECK(st.windowPass());
    CHECK(st.maxGapS() > 299 && st.maxGapS() < 301);
    CHECK(st.bootEvents() == 1); CHECK(st.abnormalResets() == 0); CHECK_EQ_STR(st.resetSummary(), "POWERON x1");
    CHECK(st.stateChanges() == 2);
    const std::string rep = st.report();
    CHECK(rep.find("-> PASS") != std::string::npos);
    CHECK(rep.find("OK->DRY ok_to_dry R=38.0") != std::string::npos);
    CHECK(rep.find("DRY->WAITING") != std::string::npos);
  }
  // 2) 中间丢 20 条 + 一次 BROWNOUT 重启 -> 不通过, 最长间隔 > 2 个周期
  {
    LogStats st(300);
    st.add(erow(E0, 7, 900, "boot fw=R7.2-fw1.0 reset=POWERON cal=C1 param=v0-temp"));
    for (uint32_t i = 0; i <= 288; i++) {
      if (i >= 100 && i < 120) continue;
      const uint32_t boot = i < 100 ? 7 : 8;
      st.add(srow(E0 + i * 300, boot, (i < 100 ? i : i - 120) * 300000u + 5000, true));
      if (i == 120) st.add(erow(E0 + i * 300 - 5, 8, 900, "boot fw=R7.2-fw1.0 reset=BROWNOUT cal=C1 param=v0-temp"));
    }
    CHECK(st.rows() == 269); CHECK(st.epochAxis()); CHECK(!st.uptimeAxis());
    CHECK(st.windowComplete()); CHECK(st.windowValid() == 268); CHECK(!st.windowPass());
    CHECK(st.maxGapS() > 20 * 300 && st.maxGapS() < 22 * 300);
    CHECK(st.bootEvents() == 2); CHECK(st.abnormalResets() == 1);
    CHECK_EQ_STR(st.resetSummary(), "POWERON x1, BROWNOUT x1");
    CHECK(st.report().find("FAIL") != std::string::npos);
  }
  // 3) 没有 NTP 时间但只开机一次: 用开机毫秒; 未满 24 h 显示 still running; FAST 行计数
  {
    LogStats st(300);
    for (uint32_t i = 0; i < 50; i++) st.add(srow(0, 3, 5000 + i * 300000u, true, i < 5 ? "FAST" : ""));
    CHECK(!st.epochAxis()); CHECK(st.uptimeAxis()); CHECK(st.flaggedRows() == 5);
    CHECK(st.spanS() > 49 * 300 - 1 && st.spanS() < 49 * 300 + 1);
    CHECK(!st.windowComplete()); CHECK(!st.windowPass());
    CHECK(st.report().find("still running") != std::string::npos);
  }
  // 4) 没有 NTP 时间且重启过: 跨度未知
  {
    LogStats st(300);
    for (uint32_t i = 0; i < 10; i++) st.add(srow(0, 3, 5000 + i * 300000u, true));
    for (uint32_t i = 0; i < 10; i++) st.add(srow(0, 4, 5000 + i * 300000u, true));
    CHECK(!st.spanKnown());
    CHECK(st.report().find("span unknown") != std::string::npos);
  }
  // 5) 解析: 第 10 列里的逗号保留, 非数字时间列视为坏行
  {
    LogRow r;
    CHECK(parseLogRow("1761955200,2,1234,E,DRY,,,,,ai_result AI ok_to_dry ok, 812ms", r));
    CHECK(r.type == 'E'); CHECK_EQ_STR(r.last, "ai_result AI ok_to_dry ok, 812ms");
    CHECK(!parseLogRow("abc,2,1234,S,OK,1,2,1,0,", r));
    CHECK(!parseLogRow("1,2,3,X,OK,1,2,1,0,", r));
  }
}

// fw1.4 活性保护: 有效与无效读数交替时不能一直停在植物状态
static void test_care_liveness() {
  // 1) 不缺水时出现 有效 有效 无效 ... : 15 轮后改为暂停 (unstable), 连续 3 轮有效才恢复
  {
    CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 50, t); CHECK(m.mode() == CareMode::Ok);
    CareStep st; int n = 0;
    while (m.mode() == CareMode::Ok && n < 40) {
      t += MIN;
      const bool valid = (n % 3) != 0;               // 无效 有效 有效 无效 ...
      st = m.onRound(valid, 50, t); m.onTick(t); n++;
    }
    CHECK(m.mode() == CareMode::Paused); CHECK(st.event == CareEvent::Unstable); CHECK(n == 15);
    CHECK_EQ_STR(careEventName(st.event), "unstable");
    rounds(m, 2, 50, t); CHECK(m.mode() == CareMode::Paused);          // 2 轮有效还不够
    st = rounds(m, 1, 50, t); CHECK(m.mode() == CareMode::Reading); CHECK(st.event == CareEvent::EnterReading);
    rounds(m, 1, 50, t); CHECK(m.mode() == CareMode::Ok);                // 可信原状态 + 已有 3 轮以上有效
  }
  // 2) 等待中、到期后仍凑不齐 3 轮有效: 不会一直停在蓝色, 暂停后连续 3 轮有效重新等待完整周期
  {
    CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 25, t); m.onButton(t); CHECK(m.mode() == CareMode::Waiting);
    CareStep st; int n = 0;
    while (m.mode() == CareMode::Waiting && n < 40) {
      t += MIN;
      const bool valid = (n % 3) != 2;               // 有效 有效 无效 ...
      st = m.onRound(valid, 50, t); CareStep tk = m.onTick(t); if (tk.event != CareEvent::None) st = tk; n++;
    }
    CHECK(m.mode() == CareMode::Paused); CHECK(st.event == CareEvent::Unstable);
    CHECK(n > 10);                                    // 先过了 10 min 等待期, 到期后才因凑不齐而暂停
    rounds(m, 2, 50, t); CHECK(m.mode() == CareMode::Paused);
    st = rounds(m, 1, 50, t); CHECK(st.event == CareEvent::ResumeWaiting); CHECK(m.waitRemainingMs(t) == 10 * MIN);
  }
  // 3) 偶尔一轮无效 (每 4 轮 1 次) 不触发
  {
    CareMachine m; uint32_t t = 0; m.begin(cfg(), CareMode::Boot, false, t);
    rounds(m, 3, 50, t);
    for (int n = 0; n < 100; n++) { t += MIN; m.onRound((n % 4) != 3, 50, t); }
    CHECK(m.mode() == CareMode::Ok);
  }
  // 4) 设成 0 时不检查
  {
    CareConfig c = cfg(); c.unconfirmedToPause = 0;
    CareMachine m; uint32_t t = 0; m.begin(c, CareMode::Boot, false, t);
    rounds(m, 3, 50, t);
    for (int n = 0; n < 60; n++) { t += MIN; m.onRound((n % 3) != 0, 50, t); }
    CHECK(m.mode() == CareMode::Ok);
  }
}

// fw1.4 模板与输入: 文字里的大小关系必须和状态机的比较一致
static void test_explain_fw14() {
  auto mk = [](ExplainEvent ev, const char* st, float R, int above) {
    ExplainContext c = ctx(ev, st, R); c.L = 44; c.H = 49; c.aboveHRounds = above; return c;
  };
  // 审核反例: 等待期最后三轮 47、50、51 -> 未确认恢复, 但当前 R = 51 已达 H
  ExplainContext c = mk(ExplainEvent::WaitToDry, "DRY", 51, 2);
  ExplainText t = templateExplain(c);
  CHECK(t.body.find("below") == std::string::npos);
  CHECK(t.body.find("not all >= 49") != std::string::npos);
  std::string r;
  CHECK(validateExplain(t, c, buildContextJson(c), r));
  mj::Value v; std::string err;
  CHECK(mj::parse(buildContextJson(c), v, err));
  CHECK(v.get("consecutive_at_or_above_H") && v.get("consecutive_at_or_above_H")->num == 2);
  CHECK(mj::parse(buildContextJson(mk(ExplainEvent::Manual, "OK", 60, -1)), v, err));
  CHECK(v.get("consecutive_at_or_above_H")->kind == mj::Value::T_NULL);
  CHECK(std::string(kExplainSystemPrompt).find("R is still below H") == std::string::npos);
  // 缺水但 R 已达 H (还没连续 3 轮): 不能说 below 49
  t = templateExplain(mk(ExplainEvent::Manual, "DRY", 50, 1));
  CHECK(t.body.find("below 49") == std::string::npos); CHECK(t.body.find("not yet for 3 readings") != std::string::npos);
  // 不缺水但 R 刚低于 L (还没连续 3 轮): 不能说 above 44
  t = templateExplain(mk(ExplainEvent::Manual, "OK", 42, 0));
  CHECK(t.body.find("above") == std::string::npos); CHECK_EQ_STR(t.headline, "Watching");
  // 没有按键的恢复不说 after watering
  t = templateExplain(mk(ExplainEvent::DryToOk, "OK", 52, 3));
  CHECK(t.body.find("watering") == std::string::npos);
  // 靠近阈值时取一位小数并向下截断
  t = templateExplain(mk(ExplainEvent::OkToDry, "DRY", 43.97f, 0));
  CHECK_EQ_STR(t.body, "R 43.9 is below this pot's check line 44. Please check the soil.");
  CHECK(mj::parse(buildContextJson(mk(ExplainEvent::OkToDry, "DRY", 43.97f, 0)), v, err));
  CHECK(v.get("R")->num == 43.9);
  // 扫一遍 L、H 附近: 模板开头显示的 R 与阈值的大小关系, 必须和实际 R 一致; 所有模板都能通过校验
  const ExplainEvent evs[] = {ExplainEvent::FirstState, ExplainEvent::OkToDry, ExplainEvent::WaitToOk,
                              ExplainEvent::WaitToDry, ExplainEvent::DryToOk, ExplainEvent::Manual};
  const char* states[] = {"DRY", "OK"};
  // 只测状态机实际可能产生的组合 (L = 44, H = 49)
  auto possible = [](ExplainEvent ev, const std::string& st, float R) {
    switch (ev) {
      case ExplainEvent::OkToDry:    return st == "DRY" && R < 44.0f;
      case ExplainEvent::WaitToOk:
      case ExplainEvent::DryToOk:    return st == "OK" && R >= 49.0f;
      case ExplainEvent::WaitToDry:  return st == "DRY";
      case ExplainEvent::FirstState: return (st == "DRY" && R < 49.0f) || (st == "OK" && R >= 44.0f);
      default:                       return true;   // manual: 缺水时 R 可能已达 H, 不缺水时 R 可能刚低于 L
    }
  };
  int bad = 0;
  for (ExplainEvent ev : evs)
    for (const char* st : states)
      for (float R = 40.0f; R <= 53.0f; R += 0.07f) {
        if (!possible(ev, st, R)) continue;
        ExplainContext cc = mk(ev, st, R, 1);
        const ExplainText tt = templateExplain(cc);
        std::string why;
        if (!validateExplain(tt, cc, buildContextJson(cc), why)) {
          bad++;
          printf("  template rejected: %s/%s R=%.3f | %s (%s)\n", explainEventName(ev), st, R, tt.body.c_str(), why.c_str());
          continue;
        }
        const size_t p = tt.body.find_first_of("0123456789");
        if (tt.body.compare(0, 2, "R ") != 0 || p == std::string::npos) continue;
        const double shown = strtod(tt.body.c_str() + p, nullptr);
        if ((shown < 44) != (R < 44.0f) || (shown < 49) != (R < 49.0f)) {
          bad++;
          printf("  order mismatch: %s/%s R=%.3f shown %.2f | %s\n", explainEventName(ev), st, R, shown, tt.body.c_str());
        }
        const bool wrong = (R >= 49.0f && tt.body.find("below 49") != std::string::npos) ||
                           (R < 49.0f && tt.body.find("at or above 49") != std::string::npos) ||
                           (R >= 44.0f && tt.body.find("below 44") != std::string::npos) ||
                           (R >= 44.0f && tt.body.find("is below this pot's check line 44") != std::string::npos) ||
                           (R < 44.0f && tt.body.find("at or above the check line 44") != std::string::npos);
        if (wrong) {
          bad++;
          printf("  wrong claim: %s/%s R=%.3f | %s\n", explainEventName(ev), st, R, tt.body.c_str());
        }
      }
  CHECK(bad == 0);
}

// fw1.3 走近播报规则 (talk normal)
static void test_voice_policy() {
  const uint32_t W = 10 * MIN;                       // 等待 10 min
  auto ctx = [&](CareMode m) {
    ApproachCtx c; c.mode = m; c.recheckQuietMs = W; c.waitAgainMs = W / 2; return c;
  };
  // 缺水: 按原规则主动播报 (冷却与每日上限在 speak() 里)
  ApproachCtx c = ctx(CareMode::Dry);
  ApproachDecision d = approachVoice(c);
  CHECK(d.voice == ApproachVoice::Proactive); CHECK_EQ_STR(d.why, "approach");
  // 等待后未确认恢复: 一个等待时长内不说, 之后恢复缺水规则
  c.notRecovered = true; c.msSinceWaitEnd = 3 * MIN;
  CHECK(approachVoice(c).voice == ApproachVoice::Silent); CHECK_EQ_STR(approachVoice(c).why, "recheck_quiet");
  c.msSinceWaitEnd = W - 1;
  CHECK(approachVoice(c).voice == ApproachVoice::Silent);
  c.msSinceWaitEnd = W;
  CHECK(approachVoice(c).voice == ApproachVoice::Proactive);
  // 等待中: 不到半个等待时长不再说; 过半后再说一次 (事件播报, 不计每日上限)
  c = ctx(CareMode::Waiting);
  c.msSinceWaitVoice = 2 * MIN;
  d = approachVoice(c);
  CHECK(d.voice == ApproachVoice::Silent); CHECK_EQ_STR(d.why, "waiting_said_recently");
  c.msSinceWaitVoice = W / 2;
  d = approachVoice(c);
  CHECK(d.voice == ApproachVoice::Event); CHECK_EQ_STR(d.why, "waiting_again");
  // 不缺水: 恢复那句没说过 -> 说一次; 说过 -> 不说
  c = ctx(CareMode::Ok);
  c.okPending = true;
  d = approachVoice(c);
  CHECK(d.voice == ApproachVoice::Event); CHECK_EQ_STR(d.why, "recovered_once");
  c.okPending = false;
  d = approachVoice(c);
  CHECK(d.voice == ApproachVoice::Silent); CHECK_EQ_STR(d.why, "ok_already_said");
  // 设备状态: 不说
  const CareMode dev[] = {CareMode::Boot, CareMode::Uncal, CareMode::Reading, CareMode::Paused};
  for (CareMode m : dev) CHECK(approachVoice(ctx(m)).voice == ApproachVoice::Silent);
  // 等待改成 30 min 时, 再说一次的间隔随之变为 15 min
  c = ctx(CareMode::Waiting); c.waitAgainMs = 15 * MIN; c.msSinceWaitVoice = 10 * MIN;
  CHECK(approachVoice(c).voice == ApproachVoice::Silent);
  c.msSinceWaitVoice = 15 * MIN;
  CHECK(approachVoice(c).voice == ApproachVoice::Event);
}

int main() {
  test_care_bench13();
  test_care_restart_and_faults();
  test_care_liveness();
  test_voice_policy();
  test_json();
  test_explain();
  test_explain_fw14();
  test_calmath();
  test_tokens();
  test_logstats();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
