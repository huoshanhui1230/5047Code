// t10_ai: Wi-Fi + 校时 + Gemini 运行时解释 (不需要探头和语音)
// 与主程序共用 NVS 设置: 在这里保存的 wifi / apikey / model, 主程序直接可用。
// 命令:
//   wifi <ssid> <pass> | wifi2 <ssid> <pass> | apikey <KEY> | model <id> | status
//   run <event>   事件: first_ok first_dry ok_to_dry dry_to_wait wait_to_ok wait_to_dry dry_to_ok manual_ok
//   runall        依次运行全部事件 (每次间隔 3 s)
//   tpl           打印全部模板
//   bad           用几条故意违规的输出演示本地校验 (不联网)
//   body <event>  只打印请求体 (可配合 tools/gemini_post.py 在电脑上调试)
//   insecure on|off  临时跳过证书校验 (只用于排查证书问题)
#include <Arduino.h>
#include <vector>
#include <string>
#include "pins.h"
#include "config.h"
#include "root_ca.h"
#include "Store.h"
#include "Net.h"
#include "AiClient.h"
#include "Explain.h"
#include "Console.h"

Store store;
Net net;
AiClient ai;
Console console;

struct Sample { const char* name; ExplainEvent ev; const char* state; float R; float rBefore; float rCheck; };
const Sample samples[] = {
    {"first_ok", ExplainEvent::FirstState, "OK", 66, NAN, NAN},
    {"first_dry", ExplainEvent::FirstState, "DRY", 36, NAN, NAN},
    {"ok_to_dry", ExplainEvent::OkToDry, "DRY", 38, 35, NAN},
    {"dry_to_wait", ExplainEvent::DryToWait, "WAITING", 37, 37, NAN},
    {"wait_to_ok", ExplainEvent::WaitToOk, "OK", 71, 37, 71},
    {"wait_to_dry", ExplainEvent::WaitToDry, "DRY", 41, 37, 41},
    {"dry_to_ok", ExplainEvent::DryToOk, "OK", 47, NAN, NAN},
    {"manual_ok", ExplainEvent::Manual, "OK", 58, 37, NAN},
};

ExplainContext makeCtx(const Sample& s) {
  ExplainContext c;
  c.event = s.ev;
  c.state = s.state;
  c.R = s.R;
  c.L = 40;
  c.H = 45;
  c.localTime = net.timeKnown() ? net.hhmm().c_str() : "14:20";
  c.hoursSinceWater = s.ev == ExplainEvent::DryToWait ? 0.0f : 51.0f;
  c.rBeforeWater = s.rBefore;
  c.waitMin = 10;
  c.rAtCheck = s.rCheck;
  c.dropPerHour12h = s.ev == ExplainEvent::OkToDry ? 0.6f : NAN;
  c.prevCycleHours = 46;
  c.hourlyR = {68, 66, 64, 61, 58, 55, 52, 49, 46, 43, 41, 39};
  c.plant = PROFILE_PLANT;
  c.waterNeed = PROFILE_WATER;
  c.source = PROFILE_SOURCE;
  return c;
}

const Sample* find(const std::string& n) {
  for (const auto& s : samples)
    if (n == s.name) return &s;
  return nullptr;
}

void runOne(const Sample& s) {
  Serial.printf("\n[t10] ===== %s =====\n", s.name);
  if (!net.connected()) Serial.println("[t10] Wi-Fi not connected -> expect TPL (offline)");
  ai.setNetworkOk(net.connected());
  const AiResult r = ai.runNow(makeCtx(s), true);
  Serial.printf("[t10] result %s | reason %s | http %d | attempts %u | %lu ms | model %s (%s)\n", r.fromAi ? "AI" : "TPL",
                r.reason.c_str(), r.httpCode, r.attempts, static_cast<unsigned long>(r.latencyMs), r.model.c_str(),
                r.modelVersion.c_str());
  Serial.printf("[t10] headline: %s\n[t10] body:     %s\n", r.text.headline.c_str(), r.text.body.c_str());
}

void badDemo() {
  const ExplainContext c = makeCtx(samples[2]);   // ok_to_dry, DRY, R 38
  const std::string cj = buildContextJson(c);
  const ExplainText cases[] = {
      {"Soil drying today!!!!", "R 38 is below 40."},
      {"Soil drying", "Check the top 2-3 cm of soil before watering."},
      {"Soil drying", "Roots may be dry. R 38 is below 40."},
      {"Hot day", "R 38 fell fast in the heat."},
      {"Soil drying", "R 38. No water needed now."},
      {"Soil drying", "R fell from 68 to 38 in 51 h. Check the top soil."},
  };
  for (const auto& t : cases) {
    std::string why;
    const bool ok = validateExplain(t, c, cj, why);
    Serial.printf("[t10] %-4s %s | %s  %s\n", ok ? "PASS" : "REJ", t.headline.c_str(), t.body.c_str(), ok ? "" : ("<- " + why).c_str());
  }
}

void status() {
  Serial.printf("[t10] wifi %s %s | time %s | key %s | model %s | calls %u\n", net.connected() ? "connected" : "not connected",
                net.ssid().c_str(), net.timeKnown() ? net.hhmm().c_str() : "unknown", ai.hasKey() ? "set" : "MISSING",
                ai.model().c_str(), ai.callsToday());
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[t10] AI explanation test (Wi-Fi + Gemini). Type: status | run ok_to_dry | runall | bad | tpl");
  store.begin(NVS_NAMESPACE);
  net.begin(store.s.ssid1, store.s.pass1, store.s.ssid2, store.s.pass2, TZ_SYDNEY);
  ai.begin(GOOGLE_ROOT_CA, false);
  ai.configure(store.s.apiKey, store.s.model, true);
  ai.setVerbose(true);
  status();
}

void loop() {
  net.loop(millis());
  if (net.justSynced()) Serial.printf("[t10] time synced %s\n", net.hhmm().c_str());
  std::vector<std::string> t;
  if (!console.poll(Serial, t)) { delay(10); return; }
  const std::string c = t[0];
  auto arg = [&](size_t i) { return i < t.size() ? t[i] : std::string(); };
  if (c == "status") status();
  else if ((c == "wifi" || c == "wifi2") && t.size() >= 2) {
    if (c == "wifi") { store.s.ssid1 = arg(1).c_str(); store.s.pass1 = arg(2).c_str(); }
    else { store.s.ssid2 = arg(1).c_str(); store.s.pass2 = arg(2).c_str(); }
    store.saveWifi();
    net.setCredentials(store.s.ssid1, store.s.pass1, store.s.ssid2, store.s.pass2);
    Serial.println("[t10] wifi saved");
  } else if (c == "apikey" && t.size() >= 2) {
    store.s.apiKey = arg(1).c_str();
    store.saveAi();
    ai.configure(store.s.apiKey, store.s.model, true);
    Serial.printf("[t10] API key saved (%u chars)\n", static_cast<unsigned>(store.s.apiKey.length()));
  } else if (c == "model") {
    store.s.model = arg(1).c_str();
    store.saveAi();
    ai.configure(store.s.apiKey, store.s.model, true);
    Serial.printf("[t10] model %s\n", ai.model().c_str());
  } else if (c == "run" && t.size() >= 2) {
    const Sample* s = find(arg(1));
    if (s) runOne(*s); else Serial.println("[t10] unknown event");
  } else if (c == "runall") {
    for (const auto& s : samples) { runOne(s); delay(3000); }
  } else if (c == "tpl") {
    for (const auto& s : samples) {
      const ExplainText tt = templateExplain(makeCtx(s));
      Serial.printf("[t10] %-12s %-14s %s\n", s.name, tt.headline.c_str(), tt.body.c_str());
    }
  } else if (c == "bad") badDemo();
  else if (c == "body" && t.size() >= 2) {
    const Sample* s = find(arg(1));
    if (s) Serial.println(buildRequestBody(buildContextJson(makeCtx(*s)), "").c_str());
  } else if (c == "insecure") {
    ai.setInsecureTls(arg(1) == "on");
    Serial.printf("[t10] TLS certificate check %s\n", arg(1) == "on" ? "DISABLED (debug only)" : "enabled");
  } else {
    Serial.println("[t10] commands: status | wifi | wifi2 | apikey | model | run <event> | runall | tpl | bad | body <event> | insecure on|off");
  }
}
