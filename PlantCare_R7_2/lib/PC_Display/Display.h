#pragma once
// 2.42" SSD1309 128x64 I2C OLED。fw1.2 起主程序屏幕只画图形、不写字:
//   植物状态 -> 一个动态表情, 风格沿用组员的 HAPPY 表情 (弯眼、腮红、U 形嘴, 上下轻晃、定时眨眼):
//     不缺水 = 开心脸; 缺水 (含补水后未恢复) = 担忧脸 (八字眉、下弯嘴、汗滴下滑); 等待 = 期待脸 (亮眼睛、小圆嘴、三个点依次出现)
//   设备状态 -> 中性图标, 不出表情 (开机与读数确认中: 沙漏; 未标定与探头异常: 警示三角)
// AI 解释、台架标记 (FAST / SIM)、R 值与等待倒计时只出现在串口和日志里。
#include <Arduino.h>
#include <U8g2lib.h>
#include "pins.h"

enum class Face : uint8_t { Smile, Worried, Waiting };
enum class Icon : uint8_t { Hourglass, Warning };

class Display {
 public:
  bool begin();                // 先探测 I2C 地址; 不在线也能安全调用其他函数
  bool online() const { return online_; }
  void face(Face f, uint32_t now);   // 动态: 每圈调用即可, 画面没变时不刷新屏幕
  void icon(Icon i);
  // 只给分模块测试程序用 (主程序屏幕不写字)
  void text(const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "");
  void setShift(uint8_t dx, uint8_t dy) { dx_ = dx; dy_ = dy; }   // 防烧屏: 整体平移 0-1 像素
  void setPower(bool on);
  // 最近一次要显示的内容, 例 "face:worried"、"icon:hourglass" (status 打印, 主机测试用; 屏幕不在线时也记录)
  static const char* lastView();

 private:
  void happyEye(int x, int y);
  void closedEyes(int ox, int y);
  void drawHappy(int ox, int y, bool blink);
  void drawWorried(int ox, int y, bool blink, int drop);
  void drawWaiting(int ox, int y, bool blink, bool breath, int dots);
  void drawIcon(Icon i, int cx, int cy);
#if OLED_VARIANT == 2
  U8G2_SSD1309_128X64_NONAME2_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE};
#else
  U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE};
#endif
  bool online_ = false;
  uint8_t dx_ = 0, dy_ = 0;
  uint32_t frameKey_ = 0xFFFFFFFFu;   // 上一帧的内容摘要; 相同就不再刷新 (I2C 一帧约 25 ms)
};

const char* faceName(Face f);
const char* iconName(Icon i);
