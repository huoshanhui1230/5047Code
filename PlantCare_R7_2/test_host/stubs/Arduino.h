// Host-side stand-in for the arduino-esp32 core API (syntax/type checking only; not a simulator).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <string>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef HOST_SIM
// 主机模拟 (test_host/sim): 用模拟时钟代替系统时间
time_t sim_time(time_t* t);
#define time(x) sim_time(x)
#endif

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define SERIAL_8N1 0x800001c
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

typedef enum { ADC_0db, ADC_2_5db, ADC_6db, ADC_11db } adc_attenuation_t;

unsigned long millis();
void delay(uint32_t ms);
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
uint32_t analogReadMilliVolts(uint8_t pin);
void analogReadResolution(uint8_t bits);
void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t attenuation);
void configTzTime(const char* tz, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
bool getLocalTime(struct tm* info, uint32_t ms = 5000);

class StringSumHelper;
class String {
 public:
  String(const char* cstr = "") : s_(cstr ? cstr : "") {}
  String(const String& o) = default;
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(int v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(unsigned int v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(unsigned long v, unsigned char base = 10) : s_(std::to_string(v)) { (void)base; }
  explicit String(float v, unsigned int decimalPlaces = 2) : s_(std::to_string(v)) { (void)decimalPlaces; }
  explicit String(double v, unsigned int decimalPlaces = 2) : s_(std::to_string(v)) { (void)decimalPlaces; }
  String& operator=(const String& o) = default;
  String& operator=(const char* c) { s_ = c ? c : ""; return *this; }
  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }
  bool reserve(unsigned int n) { s_.reserve(n); return true; }
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(const char* c) { s_ += c; return true; }
  bool concat(char c) { s_ += c; return true; }
  bool concat(unsigned char v) { s_ += std::to_string(v); return true; }
  bool concat(int v) { s_ += std::to_string(v); return true; }
  bool concat(unsigned int v) { s_ += std::to_string(v); return true; }
  bool concat(long v) { s_ += std::to_string(v); return true; }
  bool concat(unsigned long v) { s_ += std::to_string(v); return true; }
  bool concat(float v) { s_ += std::to_string(v); return true; }
  bool concat(double v) { s_ += std::to_string(v); return true; }
  template <typename T> String& operator+=(T v) { concat(v); return *this; }
  friend StringSumHelper& operator+(const StringSumHelper& lhs, const String& rhs);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, const char* cstr);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, char c);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, unsigned char num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, int num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, unsigned int num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, long num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, unsigned long num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, float num);
  friend StringSumHelper& operator+(const StringSumHelper& lhs, double num);
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* c) const { return s_ == c; }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* c) const { return s_ != c; }
  char charAt(unsigned int i) const { return s_[i]; }
  int indexOf(char ch) const { return static_cast<int>(s_.find(ch)); }
  int indexOf(char ch, unsigned int from) const { return static_cast<int>(s_.find(ch, from)); }
  int indexOf(const String& str) const { return static_cast<int>(s_.find(str.s_)); }
  String substring(unsigned int beginIndex) const { return String(s_.substr(beginIndex).c_str()); }
  String substring(unsigned int beginIndex, unsigned int endIndex) const { return String(s_.substr(beginIndex, endIndex - beginIndex).c_str()); }
  void replace(char find, char replace) { for (auto& c : s_) if (c == find) c = replace; }
  void replace(const String& find, const String& replace) { (void)find; (void)replace; }
  void trim() {}
  void toLowerCase() {}
  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return static_cast<float>(atof(s_.c_str())); }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
 protected:
  std::string s_;
};
class StringSumHelper : public String {
 public:
  StringSumHelper(const String& s) : String(s) {}
  StringSumHelper(const char* p) : String(p) {}
  StringSumHelper(char c) : String(c) {}
  StringSumHelper(unsigned char num) : String(num) {}
  StringSumHelper(int num) : String(num) {}
  StringSumHelper(unsigned int num) : String(num) {}
  StringSumHelper(long num) : String(num) {}
  StringSumHelper(unsigned long num) : String(num) {}
  StringSumHelper(float num) : String(num) {}
  StringSumHelper(double num) : String(num) {}
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size);
  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
  size_t print(const String&);
  size_t print(const char[]);
  size_t print(char);
  size_t print(int, int = 10);
  size_t print(unsigned int, int = 10);
  size_t print(long, int = 10);
  size_t print(unsigned long, int = 10);
  size_t print(double, int = 2);
  size_t println(const String&);
  size_t println(const char[]);
  size_t println(char);
  size_t println(int, int = 10);
  size_t println(unsigned int, int = 10);
  size_t println(long, int = 10);
  size_t println(unsigned long, int = 10);
  size_t println(double, int = 2);
  size_t println(void);
};
class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  void setTimeout(unsigned long timeout);
  String readStringUntil(char terminator);
};
class HardwareSerial : public Stream {
 public:
  explicit HardwareSerial(int uart_nr);
  void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin = -1,
             bool invert = false, unsigned long timeout_ms = 20000UL, uint8_t rxfifo_full_thrhd = 112);
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t) override;
  using Print::write;
};
extern HardwareSerial Serial;
extern HardwareSerial Serial2;

class EspClass {
 public:
  uint32_t getFreeHeap();
  uint32_t getMinFreeHeap();
  uint32_t getMaxAllocHeap();
  void restart();
};
extern EspClass ESP;
