// t6_ring: 灯环 (焊后) + 板载 RGB
// 自动轮换 黄 -> 蓝 -> 绿; 串口: y b g o(关) w(白) c(跑马灯) + / - (亮度) a(自动开关)
// 合格: 12 颗都亮, 黄蓝绿正确, 第一颗不闪
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "Lights.h"

Lights lights;
bool autoplay = true, chase = false;
uint8_t brightness = RING_BRIGHTNESS;
int idx = 0;
uint32_t last = 0;
const LightColor seq[3] = {LightColor::Yellow, LightColor::Blue, LightColor::Green};
const char* names[3] = {"yellow (DRY)", "blue (WAITING)", "green (OK)"};

void setup() {
  Serial.begin(115200);
  delay(500);
  lights.begin(brightness, BOARD_RGB_BRIGHTNESS);
  Serial.printf("\n[t6] ring on GPIO%d (%d LEDs), board RGB GPIO%d, brightness %u\n", PIN_RING, RING_COUNT, PIN_BOARD_RGB, brightness);
  Serial.println("[t6] y b g o w c + - a");
}

void loop() {
  if (Serial.available()) {
    const int c = Serial.read();
    chase = false;
    switch (c) {
      case 'y': autoplay = false; lights.set(LightColor::Yellow); Serial.println("yellow"); break;
      case 'b': autoplay = false; lights.set(LightColor::Blue); Serial.println("blue"); break;
      case 'g': autoplay = false; lights.set(LightColor::Green); Serial.println("green"); break;
      case 'o': autoplay = false; lights.set(LightColor::Off); Serial.println("off"); break;
      case 'w': autoplay = false; lights.set(LightColor::White); Serial.println("white"); break;
      case 'c': autoplay = false; chase = true; Serial.println("chase"); break;
      case '+': brightness = brightness > 245 ? 255 : brightness + 10; lights.setRingBrightness(brightness); Serial.printf("brightness %u\n", brightness); break;
      case '-': brightness = brightness < 10 ? 0 : brightness - 10; lights.setRingBrightness(brightness); Serial.printf("brightness %u\n", brightness); break;
      case 'a': autoplay = !autoplay; Serial.printf("auto %s\n", autoplay ? "on" : "off"); break;
      default: break;
    }
  }
  if (autoplay && millis() - last > 2000) {
    last = millis();
    lights.set(seq[idx]);
    Serial.printf("[t6] %s\n", names[idx]);
    idx = (idx + 1) % 3;
  }
  if (chase && millis() - last > 120) {
    last = millis();
    lights.chaseStep(Adafruit_NeoPixel::Color(0, 200, 40));
  }
}
