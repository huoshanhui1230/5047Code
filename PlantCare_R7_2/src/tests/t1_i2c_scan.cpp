// t1_i2c_scan: 扫描 I2C 总线, 确认 OLED 在 0x3C
// 合格: 打印 found 0x3C OLED
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.printf("\n[t1] I2C scan, SDA=GPIO%d SCL=GPIO%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
}

void loop() {
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("found 0x%02X%s\n", a, a == OLED_I2C_ADDR ? " OLED" : "");
      found++;
    }
  }
  if (!found) Serial.println("no I2C device: check OLED VCC->3V3 (j8), SCL->j10, SDA->j11, GND");
  Serial.println("---");
  delay(3000);
}
