// t4_pir: 走近触发
// 上电后先等 60 s (人离开视野), 然后在 1-3 m 外走过 10 次; 合格: >= 9/10 次触发, 人不动时不触发
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "Presence.h"

Presence pir;
uint32_t lastInfo = 0;
bool lastLevel = false;
bool warmDone = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  pir.begin(PIN_PIR, PIR_WARMUP_MS, PIR_STUCK_MS);
  Serial.printf("\n[t4] PIR on GPIO%d, warm-up %lu s - stay out of view\n", PIN_PIR, PIR_WARMUP_MS / 1000);
}

void loop() {
  const uint32_t now = millis();
  if (pir.update(now)) Serial.printf("[t4] motion #%lu at %lu s\n", static_cast<unsigned long>(pir.count()), static_cast<unsigned long>(now / 1000));
  if (pir.level() != lastLevel) {
    lastLevel = pir.level();
    Serial.printf("[t4] pin %s%s\n", lastLevel ? "HIGH" : "LOW", pir.warmedUp(now) ? "" : " (warm-up, ignored)");
  }
  if (!pir.warmedUp(now) && now - lastInfo > 10000) {
    lastInfo = now;
    Serial.printf("[t4] warm-up... %lu s left\n", static_cast<unsigned long>((pir.warmRemainingMs(now) + 999) / 1000));
  }
  if (!warmDone && pir.warmedUp(now)) {
    warmDone = true;
    Serial.println("[t4] warm-up done: walk past 10 times at 1-3 m; each detection prints 'motion #N'");
  }
  if (pir.stuck()) {
    Serial.println("[t4] PIR stuck HIGH > 5 min: check wiring / move away from antenna");
    delay(5000);
  }
  delay(10);
}
