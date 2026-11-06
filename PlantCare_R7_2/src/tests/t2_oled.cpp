// t2_oled: 依次显示主程序会用到的全部画面。fw1.2 起屏幕只画图形、不写字:
//   三种表情 (不缺水 / 缺水 / 等待) + 两个设备图标 (沙漏 = 开机与读数确认中, 警示三角 = 未标定与探头异常)
// 表情是动态的: 上下轻晃、定时眨眼; 担忧脸有汗滴下滑, 期待脸的三个点依次出现。
// 串口输入 n 切换下一页, a 开关自动轮播; 合格: 清楚、不花屏、不偏移 (花屏改 pins.h 的 OLED_VARIANT)
#include <Arduino.h>
#include "Display.h"

Display d;
int page = 0;
bool autoplay = true;
uint32_t last = 0;

void render(int p, uint32_t now) {
  switch (p % 5) {
    case 0: d.face(Face::Smile, now); break;
    case 1: d.face(Face::Worried, now); break;
    case 2: d.face(Face::Waiting, now); break;
    case 3: d.icon(Icon::Hourglass); break;
    case 4: d.icon(Icon::Warning); break;
  }
}

void show(int p) {
  static const char* const kMeaning[5] = {"OK: no water needed now", "DRY: check soil (also: not recovered after watering)",
                                          "WAITING: water soaking in", "device: starting / confirming readings",
                                          "device: not calibrated / probe problem"};
  render(p, millis());
  Serial.printf("[t2] page %d  %-15s %s\n", p % 5, Display::lastView(), kMeaning[p % 5]);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[t2] OLED test: n = next page, a = toggle auto");
  if (!d.begin()) Serial.println("OLED not found at 0x3C - run t1_i2c_scan");
  show(page);
  last = millis();
}

void loop() {
  if (Serial.available()) {
    const int c = Serial.read();
    if (c == 'n') { show(++page); last = millis(); }
    if (c == 'a') { autoplay = !autoplay; Serial.printf("[t2] auto %s\n", autoplay ? "on" : "off"); }
  }
  if (autoplay && millis() - last > 6000) { show(++page); last = millis(); }   // 每页 6 s, 看得到眨眼和动画
  render(page, millis());
  delay(20);
}
