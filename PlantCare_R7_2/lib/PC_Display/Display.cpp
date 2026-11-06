#include "Display.h"
#include <Wire.h>
#include <string.h>
#include "config.h"

namespace {
char g_view[24] = "none";

void setView(const char* kind, const char* name) {
  snprintf(g_view, sizeof(g_view), "%s:%s", kind, name);
}
}  // namespace

const char* faceName(Face f) {
  switch (f) {
    case Face::Smile:   return "smile";
    case Face::Worried: return "worried";
    case Face::Waiting: return "waiting";
  }
  return "?";
}

const char* iconName(Icon i) {
  return i == Icon::Hourglass ? "hourglass" : "warning";
}

const char* Display::lastView() { return g_view; }

bool Display::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(OLED_I2C_ADDR);
  online_ = (Wire.endTransmission() == 0);
  if (!online_) return false;
  u8g2_.setI2CAddress(OLED_I2C_ADDR * 2);
  u8g2_.setBusClock(400000);
  u8g2_.begin();
  u8g2_.setContrast(OLED_CONTRAST);
  u8g2_.setFontMode(1);
  return true;
}

void Display::setPower(bool on) {
  if (online_) u8g2_.setPowerSave(on ? 0 : 1);
}

// ---------- 动态表情: 风格和尺寸沿用组员的 HAPPY 表情 (128x64, 原代码不带外框) ----------
// 开心的弯眼 ^ (3 像素粗)
void Display::happyEye(int x, int y) {
  for (int i = 0; i < 3; i++) {
    u8g2_.drawLine(x, y + 8 + i, x + 8, y + i);
    u8g2_.drawLine(x + 8, y + i, x + 16, y + 8 + i);
  }
}

// 眨眼: 两条圆角短横
void Display::closedEyes(int ox, int y) {
  u8g2_.drawRBox(ox + 26, y + 29, 22, 3, 1);
  u8g2_.drawRBox(ox + 80, y + 29, 22, 3, 1);
}

// 不缺水: 组员的 HAPPY 表情 (去掉底下的文字)
void Display::drawHappy(int ox, int y, bool blink) {
  if (blink) {
    closedEyes(ox, y);
  } else {
    happyEye(ox + 28, y + 22);
    happyEye(ox + 82, y + 22);
  }
  u8g2_.drawLine(ox + 20, y + 37, ox + 23, y + 40);     // 两边的小脸颊
  u8g2_.drawLine(ox + 26, y + 37, ox + 29, y + 40);
  u8g2_.drawLine(ox + 99, y + 37, ox + 102, y + 40);
  u8g2_.drawLine(ox + 105, y + 37, ox + 108, y + 40);
  for (int i = 0; i < 2; i++) {                         // U 形微笑
    u8g2_.drawLine(ox + 53, y + 40 + i, ox + 58, y + 46 + i);
    u8g2_.drawLine(ox + 58, y + 46 + i, ox + 69, y + 46 + i);
    u8g2_.drawLine(ox + 69, y + 46 + i, ox + 75, y + 40 + i);
  }
}

// 缺水: 睁眼 + 八字眉 (内侧高, 担忧) + 倒 U 形的嘴 + 一滴汗慢慢滑下 (drop < 0 时不画)
void Display::drawWorried(int ox, int y, bool blink, int drop) {
  if (blink) {
    closedEyes(ox, y);
  } else {
    u8g2_.drawFilledEllipse(ox + 36, y + 29, 5, 6);
    u8g2_.drawFilledEllipse(ox + 90, y + 29, 5, 6);
  }
  for (int i = 0; i < 3; i++) {
    u8g2_.drawLine(ox + 25, y + 19 + i, ox + 43, y + 13 + i);
    u8g2_.drawLine(ox + 101, y + 19 + i, ox + 83, y + 13 + i);
  }
  for (int i = 0; i < 2; i++) {
    u8g2_.drawLine(ox + 53, y + 47 + i, ox + 58, y + 41 + i);
    u8g2_.drawLine(ox + 58, y + 41 + i, ox + 69, y + 41 + i);
    u8g2_.drawLine(ox + 69, y + 41 + i, ox + 75, y + 47 + i);
  }
  if (drop >= 0) {
    const int d = y + 12 + drop;
    u8g2_.drawDisc(ox + 112, d, 3);
    u8g2_.drawTriangle(ox + 109, d - 1, ox + 115, d - 1, ox + 112, d - 7);
  }
}

// 等待: 带高光的亮眼睛 + 跟着呼吸变化的小圆嘴 + 右上角三个点依次出现
void Display::drawWaiting(int ox, int y, bool blink, bool breath, int dots) {
  if (blink) {
    closedEyes(ox, y);
  } else {
    u8g2_.drawFilledEllipse(ox + 36, y + 28, 6, 7);
    u8g2_.drawFilledEllipse(ox + 90, y + 28, 6, 7);
    u8g2_.setDrawColor(0);                              // 眼睛里的高光
    u8g2_.drawDisc(ox + 38, y + 25, 2);
    u8g2_.drawDisc(ox + 92, y + 25, 2);
    u8g2_.setDrawColor(1);
  }
  u8g2_.drawEllipse(ox + 64, y + 45, 4, breath ? 4 : 3);
  u8g2_.drawEllipse(ox + 64, y + 45, 3, breath ? 3 : 2);
  for (int k = 0; k < dots; k++) u8g2_.drawDisc(ox + 104 + 7 * k, y + 14 - 3 * k, 2);
}

// 设备状态图标: 不是表情, 也不是文字
void Display::drawIcon(Icon i, int cx, int cy) {
  if (i == Icon::Hourglass) {
    u8g2_.drawBox(cx - 15, cy - 24, 31, 3);       // 上下横梁
    u8g2_.drawBox(cx - 15, cy + 21, 31, 3);
    for (int k = 0; k < 2; k++) {                 // 两个三角形的外框, 2 像素粗
      u8g2_.drawLine(cx - 12 + k, cy - 21, cx + k, cy - 1);
      u8g2_.drawLine(cx + 12 - k, cy - 21, cx - k, cy - 1);
      u8g2_.drawLine(cx - 12 + k, cy + 20, cx + k, cy);
      u8g2_.drawLine(cx + 12 - k, cy + 20, cx - k, cy);
    }
    u8g2_.drawTriangle(cx - 6, cy - 11, cx + 6, cy - 11, cx, cy - 2);   // 上面剩下的沙
    u8g2_.drawTriangle(cx - 10, cy + 20, cx + 10, cy + 20, cx, cy + 11);  // 下面堆起来的沙
  } else {
    for (int k = 0; k < 3; k++) {                 // 三个嵌套三角形 -> 约 3 像素粗的外框
      const int top = cy - 26 + 2 * k, base = cy + 24 - k, half = 28 - 2 * k;
      u8g2_.drawLine(cx, top, cx - half, base);
      u8g2_.drawLine(cx, top, cx + half, base);
      u8g2_.drawLine(cx - half, base, cx + half, base);
    }
    u8g2_.drawBox(cx - 2, cy - 12, 4, 21);        // 感叹号
    u8g2_.drawBox(cx - 2, cy + 13, 4, 4);
  }
}

void Display::face(Face f, uint32_t now) {
  setView("face", faceName(f));
  if (!online_) return;
  // 动画参数 (和组员的 HAPPY 一样: 每 650 ms 上下晃 1 像素, 每 4.5 s 眨一次眼 0.2 s)
  int bob = static_cast<int>((now / 650UL) % 2UL);
  bool blink = (now % 4500UL) < 200UL;
  int phase = 0;     // 担忧: 汗滴位置 0-9, 15 = 不画; 等待: 点数 0-3
  bool breath = false;
  if (f == Face::Worried) {
    bob = static_cast<int>((now / 1000UL) % 2UL);       // 担忧时晃得慢一点
    blink = (now % 5200UL) < 200UL;
    const uint32_t t = now % 3000UL;                    // 1.5 s 滑下, 1.5 s 不显示
    phase = t < 1500UL ? static_cast<int>(t / 150UL) : 15;
  } else if (f == Face::Waiting) {
    breath = ((now / 900UL) % 2UL) != 0;
    phase = static_cast<int>((now / 400UL) % 4UL);
  }
  const uint32_t key = (1u << 24) | (static_cast<uint32_t>(f) << 16) | (static_cast<uint32_t>(bob) << 12) |
                       (blink ? 1u << 11 : 0u) | (breath ? 1u << 10 : 0u) | (static_cast<uint32_t>(phase) << 4) |
                       (static_cast<uint32_t>(dx_) << 1) | dy_;
  if (key == frameKey_) return;                         // 画面没变: 不占用 I2C
  frameKey_ = key;
  const int ox = dx_, y = dy_ - 3 + bob;                // 原图没有底下的文字, 整体上移 3 像素居中
  u8g2_.clearBuffer();
  switch (f) {
    case Face::Smile:   drawHappy(ox, y, blink); break;
    case Face::Worried: drawWorried(ox, y, blink, phase == 15 ? -1 : phase); break;
    case Face::Waiting: drawWaiting(ox, y, blink, breath, phase); break;
  }
  u8g2_.sendBuffer();
}

void Display::icon(Icon i) {
  setView("icon", iconName(i));
  if (!online_) return;
  const uint32_t key = (2u << 24) | (static_cast<uint32_t>(i) << 16) | (static_cast<uint32_t>(dx_) << 1) | dy_;
  if (key == frameKey_) return;
  frameKey_ = key;
  u8g2_.clearBuffer();
  drawIcon(i, 64 + dx_, 32 + dy_);
  u8g2_.sendBuffer();
}

void Display::text(const char* l1, const char* l2, const char* l3, const char* l4) {
  setView("text", l1);
  if (!online_) return;
  frameKey_ = 0xFFFFFFFFu;
  u8g2_.clearBuffer();
  u8g2_.setFont(u8g2_font_6x10_tf);
  u8g2_.drawStr(0, 12, l1);
  u8g2_.drawStr(0, 26, l2);
  u8g2_.drawStr(0, 40, l3);
  u8g2_.drawStr(0, 54, l4);
  u8g2_.sendBuffer();
}
