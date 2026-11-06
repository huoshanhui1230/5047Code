// t5_button: "已补水"按钮 (或碰线替身), 碰 10 次计数应正好是 10
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "Button.h"

Button button;

void setup() {
  Serial.begin(115200);
  delay(500);
  button.begin(PIN_BUTTON, BUTTON_DEBOUNCE_MS);
  Serial.printf("\n[t5] button on GPIO%d (to GND, internal pull-up), debounce %d ms\n", PIN_BUTTON, BUTTON_DEBOUNCE_MS);
  if (button.pressedNow()) Serial.println("[t5] WARNING: reads LOW at start - wire shorted to GND?");
}

void loop() {
  if (button.update(millis())) Serial.printf("[t5] press #%lu\n", static_cast<unsigned long>(button.presses()));
  delay(2);
}
