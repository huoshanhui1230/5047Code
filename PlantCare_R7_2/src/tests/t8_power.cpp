// t8_power: 供电压力测试 (方案 R7.2 第 3、7 章)
// 用展示时的实际供电方式 (电脑 USB-C 口或 5 V 2 A 电源) 运行 10 分钟, 人别在 PIR 前走动:
//   灯环全部点亮 + 三句语音循环播放 + Wi-Fi 联网做 HTTPS 请求; 已保存 API 密钥时每 60 s 真实调用一次 Gemini
// 合格: 不重启; 复位原因不是 BROWNOUT; PIR 误触发 <= 1 次。
// 若测试中途重启, 下次上电会打印 FAIL 和复位原因。输入 s 重新开始, f 切换满亮度 (最坏情况)。
// Wi-Fi 与密钥与主程序共用 (先在 app 或 t10_ai 里用 wifi / apikey 命令保存)。
#include <Arduino.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "pins.h"
#include "config.h"
#include "root_ca.h"
#include "sysutil.h"
#include "Lights.h"
#include "Voice.h"
#include "Presence.h"
#include "Store.h"
#include "Net.h"
#include "AiClient.h"

Lights lights;
Voice voice;
Presence pir;
Store store;
Net net;
AiClient ai;
Preferences flag;

const uint32_t TEST_MS = 10UL * 60UL * 1000UL;
bool running = false, fullBright = false;
uint32_t t0 = 0, lastClip = 0, lastHttps = 0, lastAi = 0, lastReport = 0;
uint8_t clip = 1;
uint32_t httpsOk = 0, httpsFail = 0, aiOk = 0, aiFail = 0, falsePir = 0;

bool httpsProbe() {
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, "https://www.google.com/generate_204")) return false;
  const int code = http.GET();
  http.end();
  return code == 204;
}

ExplainContext sampleCtx() {
  ExplainContext c;
  c.event = ExplainEvent::Manual;
  c.state = "OK";
  c.R = 66; c.L = 40; c.H = 45;
  c.localTime = net.hhmm().c_str();
  c.waitMin = 10;
  c.hourlyR = {70, 69, 68, 67, 66};
  c.plant = PROFILE_PLANT; c.waterNeed = PROFILE_WATER; c.source = PROFILE_SOURCE;
  return c;
}

void startTest() {
  flag.putBool("running", true);
  running = true;
  t0 = millis();
  httpsOk = httpsFail = aiOk = aiFail = falsePir = 0;
  lights.set(LightColor::White);
  Serial.printf("[t8] START 10 min stress: ring white (brightness %u), audio loop, Wi-Fi%s\n",
                fullBright ? 255 : RING_BRIGHTNESS, ai.hasKey() ? " + Gemini every 60 s" : " (no API key: HTTPS only)");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n[t8] power stress test | reset reason: %s\n", resetReasonName());
  flag.begin("pc72t8", false);
  const bool interrupted = flag.getBool("running", false);
  store.begin(NVS_NAMESPACE);
  lights.begin(RING_BRIGHTNESS, BOARD_RGB_BRIGHTNESS);
  voice.begin(Serial2, PIN_DF_RX, PIN_DF_TX, store.s.volume);
  Serial.printf("[t8] voice: %s\n", voice.status());
  pir.begin(PIN_PIR, PIR_WARMUP_MS, PIR_STUCK_MS);
  net.begin(store.s.ssid1, store.s.pass1, store.s.ssid2, store.s.pass2, TZ_SYDNEY);
  ai.begin(GOOGLE_ROOT_CA, false);
  ai.configure(store.s.apiKey, store.s.model, true);
  ai.setVerbose(false);
  if (interrupted) {
    flag.putBool("running", false);
    Serial.printf("[t8] FAIL: the previous run rebooted before finishing (reset reason now: %s)\n", resetReasonName());
    Serial.println("[t8] fix power (better USB supply / powered hub, lower volume) and type s to run again");
    return;
  }
  startTest();
}

void loop() {
  const uint32_t now = millis();
  if (Serial.available()) {
    const int c = Serial.read();
    if (c == 's') startTest();
    if (c == 'f') {
      fullBright = !fullBright;
      lights.setRingBrightness(fullBright ? 255 : RING_BRIGHTNESS);
      Serial.printf("[t8] ring brightness %u\n", fullBright ? 255 : RING_BRIGHTNESS);
    }
  }
  net.loop(now);
  ai.setNetworkOk(net.connected());
  if (pir.update(now) && running) { falsePir++; Serial.printf("[t8] PIR trigger #%lu (nobody should move)\n", static_cast<unsigned long>(falsePir)); }
  voice.poll();
  if (!running) { delay(10); return; }

  if (now - lastClip >= 6000) {      // 语音循环
    lastClip = now;
    voice.play(clip);
    clip = clip % 3 + 1;
  }
  if (net.connected() && now - lastHttps >= 20000) {   // TLS 握手 + 发射
    lastHttps = now;
    if (httpsProbe()) httpsOk++; else httpsFail++;
  }
  if (net.connected() && ai.hasKey() && now - lastAi >= 60000) {   // 语音播放期间真实调用 Gemini
    lastAi = now;
    voice.play(clip);
    const AiResult r = ai.runNow(sampleCtx(), true);
    if (r.fromAi) aiOk++; else aiFail++;
    Serial.printf("[t8] gemini: %s (%s, %lu ms)\n", r.fromAi ? "AI ok" : "TPL", r.reason.c_str(), static_cast<unsigned long>(r.latencyMs));
  }
  if (now - lastReport >= 30000) {
    lastReport = now;
    Serial.printf("[t8] %lu s | wifi %s | https ok %lu fail %lu | ai ok %lu fail %lu | PIR %lu | heap %u | voice err %d\n",
                  static_cast<unsigned long>((now - t0) / 1000), net.connected() ? "up" : "down", static_cast<unsigned long>(httpsOk),
                  static_cast<unsigned long>(httpsFail), static_cast<unsigned long>(aiOk), static_cast<unsigned long>(aiFail),
                  static_cast<unsigned long>(falsePir), ESP.getFreeHeap(), voice.errors());
  }
  if (now - t0 >= TEST_MS) {
    running = false;
    flag.putBool("running", false);
    lights.set(LightColor::Off);
    voice.stop();
    const bool pass = falsePir <= 1;
    Serial.printf("[t8] DONE 10 min without reboot. PIR triggers %lu (<=1 %s). HTTPS ok %lu/%lu, AI ok %lu/%lu -> %s\n",
                  static_cast<unsigned long>(falsePir), pass ? "ok" : "NO", static_cast<unsigned long>(httpsOk),
                  static_cast<unsigned long>(httpsOk + httpsFail), static_cast<unsigned long>(aiOk),
                  static_cast<unsigned long>(aiOk + aiFail), pass ? "PASS" : "FAIL (PIR: move it away from the antenna)");
    if (!net.hasCredentials()) Serial.println("[t8] note: Wi-Fi not configured, radio load was not tested");
  }
  delay(5);
}
