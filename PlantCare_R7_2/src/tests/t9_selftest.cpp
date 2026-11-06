// t9_selftest: 全部模块一起自检
// 串口应显示 OLED、探头、DFPlayer 都是 PASS; 之后按按钮切换表情、灯色并播放对应语音, 在 PIR 前走过板载灯闪一下
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "root_ca.h"
#include "Display.h"
#include "Lights.h"
#include "Voice.h"
#include "Presence.h"
#include "Button.h"
#include "Probe.h"
#include "Store.h"
#include "Net.h"

Display display;
Lights lights;
Voice voice;
Presence pir;
Button button;
Probe probe;
Store store;
Net net;
int view = 0;

void result(const char* name, const char* res, const String& note) {
  Serial.printf("  %-10s %s  %s\n", name, res, note.c_str());
}

const Face kFaces[3] = {Face::Smile, Face::Worried, Face::Waiting};

void showView() {
  const LightColor colors[3] = {LightColor::Green, LightColor::Yellow, LightColor::Blue};
  display.face(kFaces[view], millis());
  lights.set(colors[view]);
  const uint8_t clip = view == 0 ? CLIP_OK : (view == 1 ? CLIP_DRY : CLIP_WAIT);
  voice.play(clip);
  Serial.printf("[t9] view %d, clip %u\n", view, clip);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[t9] self test");
  store.begin(NVS_NAMESPACE);

  const bool oled = display.begin();
  if (oled) display.icon(Icon::Hourglass);
  result("OLED", oled ? "PASS" : "FAIL", oled ? "0x3C" : "not found - run t1_i2c_scan");

  probe.begin(PIN_PROBE);
  const ProbeRound r = probe.readRound(SAMPLES_PER_ROUND, SAMPLE_GAP_MS, VALID_MIN_MV, VALID_MAX_MV, ROUND_SPREAD_MAX_MV);
  String note = String(r.mv) + " mV, spread " + r.spread;
  if (!r.valid) { note += " ("; note += r.why; note += ")"; }
  result("probe", r.valid ? "PASS" : "FAIL", note);

  const bool dfOk = voice.begin(Serial2, PIN_DF_RX, PIN_DF_TX, store.s.volume);
  if (voice.txOnly()) result("DFPlayer", "LISTEN", "module never answers: press the button and listen for each clip");
  else result("DFPlayer", dfOk ? "PASS" : "FAIL", String(voice.status()) + ", files " + voice.files());

  button.begin(PIN_BUTTON, BUTTON_DEBOUNCE_MS);
  result("button", button.pressedNow() ? "FAIL" : "PASS", button.pressedNow() ? "reads LOW (shorted?)" : "idle HIGH");

  pir.begin(PIN_PIR, 10000, PIR_STUCK_MS);
  result("PIR", "PASS", "walk past after 10 s: board LED flashes");

  lights.begin(RING_BRIGHTNESS, BOARD_RGB_BRIGHTNESS);
  result("ring", "CHECK", "look: 12 LEDs green/yellow/blue when you press the button");

  net.begin(store.s.ssid1, store.s.pass1, store.s.ssid2, store.s.pass2, TZ_SYDNEY);
  if (!net.hasCredentials()) {
    result("wifi", "SKIP", "not configured (app: wifi <ssid> <pass>)");
  } else {
    uint32_t t = millis();
    while (!net.connected() && millis() - t < 20000) { net.loop(millis()); delay(100); }
    result("wifi", net.connected() ? "PASS" : "FAIL", net.connected() ? net.ssid() : String("check 2.4 GHz hotspot / password"));
    if (net.connected()) {
      t = millis();
      while (!net.timeKnown() && millis() - t < 15000) { net.loop(millis()); delay(100); }
      result("time", net.timeKnown() ? "PASS" : "FAIL", net.timeKnown() ? net.hhmm() : String("NTP not reached"));
    }
  }
  result("api key", store.s.apiKey.length() ? "SET" : "MISSING", store.s.apiKey.length() ? "" : "app/t10: apikey <KEY>");
  Serial.println("[t9] press the button to cycle OK / DRY / WAITING (face + colour + voice)");
  showView();
}

void loop() {
  const uint32_t now = millis();
  if (button.update(now)) { view = (view + 1) % 3; showView(); }
  display.face(kFaces[view], now);   // 表情是动态的
  if (pir.update(now)) { lights.flashBoard(LightColor::White, 600); Serial.println("[t9] motion"); }
  lights.update(now);
  voice.poll();
  net.loop(now);
  delay(5);
}
