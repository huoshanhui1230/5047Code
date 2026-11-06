// 主机模拟: 给 test_host/stubs 里声明的 Arduino / 库 API 提供简单实现,
// 让 src/app/main.cpp 和全部 lib 在电脑上真正跑起来 (模拟时钟、传感器、Wi-Fi、Gemini 返回、闪存)。
// 只用于逻辑联调, 不模拟真实硬件的时序与电气特性。
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobotDFPlayerMini.h>
#include <freertos/semphr.h>
#include <esp_partition.h>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include "sim.h"

SimState sim;

// ---------------- 时间 ----------------
time_t sim_time(time_t* t) {
  const time_t v = sim.ntpSynced ? static_cast<time_t>(sim.epochAtMs0 + sim.ms / 1000) : static_cast<time_t>(sim.ms / 1000);
  if (t) *t = v;
  return v;
}
unsigned long millis() { return sim.ms; }

// 主循环与 AI 任务线程按模拟时间轮流运行 (确定性): 主循环每次 delay() 推进时钟后,
// 等 AI 线程把到期的这一轮跑完、重新进入 vTaskDelay, 再继续。
static std::mutex g_tm;
static std::condition_variable g_tcv;
static bool g_aiThread = false, g_aiIdle = false;
static uint32_t g_aiWakeAt = 0;
void delay(uint32_t ms) {
  std::unique_lock<std::mutex> lk(g_tm);
  sim.ms += ms;
  g_tcv.notify_all();
  g_tcv.wait(lk, [] { return !g_aiThread || (g_aiIdle && static_cast<int32_t>(g_aiWakeAt - sim.ms) > 0); });
}
void configTzTime(const char* tz, const char*, const char*, const char*) {
  setenv("TZ", tz, 1);
  tzset();
  if (WiFi.status() == WL_CONNECTED) sim.ntpSynced = true;
}
bool getLocalTime(struct tm* info, uint32_t) {
  const time_t t = sim_time(nullptr);
  if (t < 1700000000) return false;
  localtime_r(&t, info);
  return true;
}

// ---------------- GPIO / ADC ----------------
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
  if (pin == 14) return sim.pirHigh ? HIGH : LOW;
  if (pin == 13) return sim.buttonDown ? LOW : HIGH;
  return LOW;
}
uint32_t analogReadMilliVolts(uint8_t) {
  static uint32_t lcg = 12345;
  lcg = lcg * 1103515245u + 12345u;
  const int n = sim.probeNoise ? static_cast<int>((lcg >> 16) % (2 * sim.probeNoise + 1)) - sim.probeNoise : 0;
  const int v = sim.probeMv + n;
  return v < 150 ? 142u : static_cast<uint32_t>(v);   // 和真机一样: 11 dB 衰减下能测的最低约 142 mV
}
void analogReadResolution(uint8_t) {}
void analogSetPinAttenuation(uint8_t, adc_attenuation_t) {}

// ---------------- String 拼接 ----------------
#define SUM_OP(T)                                                        \
  StringSumHelper& operator+(const StringSumHelper& lhs, T v) {          \
    StringSumHelper& a = const_cast<StringSumHelper&>(lhs);              \
    a.concat(v);                                                         \
    return a;                                                            \
  }
SUM_OP(const String&)
SUM_OP(const char*)
SUM_OP(char)
SUM_OP(unsigned char)
SUM_OP(int)
SUM_OP(unsigned int)
SUM_OP(long)
SUM_OP(unsigned long)
SUM_OP(float)
SUM_OP(double)

// ---------------- Print / Stream / Serial ----------------
size_t Print::write(const uint8_t* buffer, size_t size) {
  size_t n = 0;
  for (size_t i = 0; i < size; i++) n += write(buffer[i]);
  return n;
}
size_t Print::printf(const char* format, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, format);
  const int n = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  if (n <= 0) return 0;
  return write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n) < sizeof(buf) ? n : sizeof(buf) - 1);
}
static std::string numStr(long long v, int base) {
  char b[40];
  snprintf(b, sizeof(b), base == 16 ? "%llX" : "%lld", v);
  return b;
}
size_t Print::print(const String& s) { return write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }
size_t Print::print(const char s[]) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
size_t Print::print(char c) { return write(static_cast<uint8_t>(c)); }
size_t Print::print(int v, int b) { return print(numStr(v, b).c_str()); }
size_t Print::print(unsigned int v, int b) { return print(numStr(v, b).c_str()); }
size_t Print::print(long v, int b) { return print(numStr(v, b).c_str()); }
size_t Print::print(unsigned long v, int b) { return print(numStr(static_cast<long long>(v), b).c_str()); }
size_t Print::print(double v, int d) {
  char b[48];
  snprintf(b, sizeof(b), "%.*f", d, v);
  return print(b);
}
size_t Print::println(void) { return print("\r\n"); }
size_t Print::println(const String& s) { return print(s) + println(); }
size_t Print::println(const char s[]) { return print(s) + println(); }
size_t Print::println(char c) { return print(c) + println(); }
size_t Print::println(int v, int b) { return print(v, b) + println(); }
size_t Print::println(unsigned int v, int b) { return print(v, b) + println(); }
size_t Print::println(long v, int b) { return print(v, b) + println(); }
size_t Print::println(unsigned long v, int b) { return print(v, b) + println(); }
size_t Print::println(double v, int d) { return print(v, d) + println(); }

void Stream::setTimeout(unsigned long) {}
String Stream::readStringUntil(char terminator) {
  std::string s;
  while (available() > 0) {
    const int c = read();
    if (c < 0 || c == terminator) break;
    s += static_cast<char>(c);
  }
  return String(s.c_str());
}

HardwareSerial::HardwareSerial(int uart_nr) : Stream() { (void)uart_nr; }
static int uartOf(const HardwareSerial* s);
void HardwareSerial::begin(unsigned long, uint32_t, int8_t, int8_t, bool, unsigned long, uint8_t) {}
int HardwareSerial::available() {
  if (uartOf(this) != 0) return 0;
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  return static_cast<int>(sim.serialIn.size());
}
int HardwareSerial::read() {
  if (uartOf(this) != 0) return -1;
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  if (sim.serialIn.empty()) return -1;
  const char c = sim.serialIn.front();
  sim.serialIn.pop_front();
  return static_cast<unsigned char>(c);
}
int HardwareSerial::peek() {
  if (uartOf(this) != 0) return -1;
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  return sim.serialIn.empty() ? -1 : static_cast<unsigned char>(sim.serialIn.front());
}
size_t HardwareSerial::write(uint8_t c) {
  if (uartOf(this) != 0) return 1;
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  if (c != '\r') sim.serialOut += static_cast<char>(c);
  return 1;
}
HardwareSerial Serial(0);
HardwareSerial Serial2(2);
static int uartOf(const HardwareSerial* s) { return s == &Serial ? 0 : 2; }

uint32_t EspClass::getFreeHeap() { return 150000; }
uint32_t EspClass::getMinFreeHeap() { return 120000; }
uint32_t EspClass::getMaxAllocHeap() { return 110000; }
void EspClass::restart() { throw std::runtime_error("ESP.restart"); }
EspClass ESP;
esp_reset_reason_t esp_reset_reason(void) { return static_cast<esp_reset_reason_t>(sim.resetReason); }

// ---------------- FreeRTOS ----------------
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* const, const uint32_t, void* const param, UBaseType_t,
                                   TaskHandle_t* const handle, const BaseType_t) {
  {
    std::lock_guard<std::mutex> lk(g_tm);
    g_aiThread = true;
    g_aiIdle = false;
  }
  std::thread(fn, param).detach();
  if (handle) *handle = reinterpret_cast<TaskHandle_t>(1);
  return 1;
}
void vTaskDelay(const TickType_t ticks) {
  std::unique_lock<std::mutex> lk(g_tm);
  g_aiWakeAt = sim.ms + (ticks ? ticks : 1);
  g_aiIdle = true;
  g_tcv.notify_all();
  g_tcv.wait(lk, [] { return static_cast<int32_t>(sim.ms - g_aiWakeAt) >= 0; });
  g_aiIdle = false;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return reinterpret_cast<SemaphoreHandle_t>(new std::recursive_mutex()); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t) {
  reinterpret_cast<std::recursive_mutex*>(m)->lock();
  return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {
  reinterpret_cast<std::recursive_mutex*>(m)->unlock();
  return 1;
}

// ---------------- I2C ----------------
static uint16_t g_i2cAddr = 0;
bool TwoWire::begin(int, int, uint32_t) { return true; }
void TwoWire::beginTransmission(uint16_t address) { g_i2cAddr = address; }
uint8_t TwoWire::endTransmission(bool) { return g_i2cAddr == 0x3C ? 0 : 2; }
TwoWire Wire;

// ---------------- Wi-Fi / HTTPS ----------------
static bool g_wifiJoined = false;
static std::string g_wifiSsid;
wl_status_t WiFiClass::begin(const char* ssid, const char*, int32_t, const uint8_t*, bool) {
  g_wifiSsid = ssid ? ssid : "";
  g_wifiJoined = sim.wifiAvailable && g_wifiSsid == sim.apSsid;
  return status();
}
wl_status_t WiFiClass::status() {
  if (g_wifiJoined && !sim.wifiAvailable) g_wifiJoined = false;   // 热点关掉 -> 断线
  return g_wifiJoined ? WL_CONNECTED : WL_DISCONNECTED;
}
bool WiFiClass::disconnect(bool, bool) { g_wifiJoined = false; return true; }
bool WiFiClass::mode(wifi_mode_t) { return true; }
void WiFiClass::persistent(bool) {}
bool WiFiClass::setAutoReconnect(bool) { return true; }
bool WiFiClass::setSleep(bool) { return true; }
String WiFiClass::SSID() const { return String(g_wifiJoined ? g_wifiSsid.c_str() : ""); }
int8_t WiFiClass::RSSI() { return g_wifiJoined ? -55 : 0; }
WiFiClass WiFi;

void WiFiClientSecure::setInsecure() {}
void WiFiClientSecure::setCACert(const char*) {}
void WiFiClientSecure::setHandshakeTimeout(unsigned long) {}

bool HTTPClient::begin(WiFiClient&, String) { return true; }
void HTTPClient::end(void) {}
void HTTPClient::setReuse(bool) {}
void HTTPClient::setConnectTimeout(int32_t) {}
void HTTPClient::setTimeout(uint16_t) {}
void HTTPClient::addHeader(const String&, const String&, bool, bool) {}
int HTTPClient::GET() { return sim.wifiAvailable ? 204 : -1; }
int HTTPClient::POST(uint8_t* payload, size_t size) {
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  sim.httpCalls++;
  sim.lastHttpBody.assign(reinterpret_cast<const char*>(payload), size);
  return sim.wifiAvailable ? sim.httpCode : -1;
}
int HTTPClient::POST(String payload) {
  return POST(reinterpret_cast<uint8_t*>(const_cast<char*>(payload.c_str())), payload.length());
}
String HTTPClient::getString(void) {
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  return String(sim.httpResp.c_str());
}
String HTTPClient::errorToString(int) { return String("connection refused"); }

// ---------------- NVS ----------------
static std::map<std::string, std::map<std::string, std::string>> g_nvs;
bool Preferences::begin(const char* name, bool, const char*) {
  strncpy(ns_, name, sizeof(ns_) - 1);
  return true;
}
void Preferences::end() {}
bool Preferences::clear() { g_nvs[ns_].clear(); return true; }
bool Preferences::remove(const char* key) { return g_nvs[ns_].erase(key) > 0; }
static size_t putRaw(const char* ns, const char* key, const std::string& v) { g_nvs[ns][key] = v; return v.size() ? v.size() : 1; }
static bool getRaw(const char* ns, const char* key, std::string& v) {
  auto& m = g_nvs[ns];
  auto it = m.find(key);
  if (it == m.end()) return false;
  v = it->second;
  return true;
}
size_t Preferences::putUChar(const char* k, uint8_t v) { return putRaw(ns_, k, std::to_string(v)); }
size_t Preferences::putUShort(const char* k, uint16_t v) { return putRaw(ns_, k, std::to_string(v)); }
size_t Preferences::putInt(const char* k, int32_t v) { return putRaw(ns_, k, std::to_string(v)); }
size_t Preferences::putUInt(const char* k, uint32_t v) { return putRaw(ns_, k, std::to_string(v)); }
size_t Preferences::putFloat(const char* k, float v) {
  char b[40];
  snprintf(b, sizeof(b), "%.9g", static_cast<double>(v));
  return putRaw(ns_, k, b);
}
size_t Preferences::putBool(const char* k, bool v) { return putRaw(ns_, k, v ? "1" : "0"); }
size_t Preferences::putString(const char* k, const char* v) { return putRaw(ns_, k, v); }
size_t Preferences::putString(const char* k, String v) { return putRaw(ns_, k, v.c_str()); }
uint8_t Preferences::getUChar(const char* k, uint8_t d) { std::string v; return getRaw(ns_, k, v) ? static_cast<uint8_t>(atoi(v.c_str())) : d; }
uint16_t Preferences::getUShort(const char* k, uint16_t d) { std::string v; return getRaw(ns_, k, v) ? static_cast<uint16_t>(atoi(v.c_str())) : d; }
int32_t Preferences::getInt(const char* k, int32_t d) { std::string v; return getRaw(ns_, k, v) ? atoi(v.c_str()) : d; }
uint32_t Preferences::getUInt(const char* k, uint32_t d) { std::string v; return getRaw(ns_, k, v) ? static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 10)) : d; }
float Preferences::getFloat(const char* k, float d) { std::string v; return getRaw(ns_, k, v) ? strtof(v.c_str(), nullptr) : d; }
bool Preferences::getBool(const char* k, bool d) { std::string v; return getRaw(ns_, k, v) ? v == "1" : d; }
String Preferences::getString(const char* k, String d) { std::string v; return getRaw(ns_, k, v) ? String(v.c_str()) : d; }

// ---------------- LittleFS (内存文件) ----------------
static std::map<std::string, std::string> g_files;
size_t fs::File::write(uint8_t c) {
  if (!ok_ || mode_ == 'r') return 0;
  g_files[path_] += static_cast<char>(c);
  return 1;
}
int fs::File::available() {
  if (!ok_ || mode_ != 'r') return 0;
  const std::string& d = g_files[path_];
  return pos_ < d.size() ? static_cast<int>(d.size() - pos_) : 0;
}
int fs::File::read() {
  if (available() <= 0) return -1;
  return static_cast<unsigned char>(g_files[path_][pos_++]);
}
int fs::File::peek() {
  if (available() <= 0) return -1;
  return static_cast<unsigned char>(g_files[path_][pos_]);
}
size_t fs::File::read(uint8_t* buf, size_t size) {
  size_t n = 0;
  while (n < size && available() > 0) buf[n++] = static_cast<uint8_t>(read());
  return n;
}
size_t fs::File::size() const { return ok_ && g_files.count(path_) ? g_files.at(path_).size() : 0; }
void fs::File::close() { ok_ = false; }
fs::File::operator bool() const { return ok_; }
fs::File fs::FS::open(const char* path, const char* mode, const bool) {
  const char m = mode[0];
  if (m == 'r') return g_files.count(path) ? File(path, 'r') : File();
  if (m == 'w') g_files[path].clear();
  else g_files[path];   // append: 不存在则创建
  return File(path, m);
}
bool fs::FS::exists(const char* path) { return g_files.count(path) > 0; }
bool fs::FS::remove(const char* path) { return g_files.erase(path) > 0; }
bool fs::FS::rename(const char* from, const char* to) {
  if (!g_files.count(from)) return false;
  g_files[to] = g_files[from];
  g_files.erase(from);
  return true;
}
bool fs::LittleFSFS::begin(bool, const char*, uint8_t, const char*) { return true; }
size_t fs::LittleFSFS::totalBytes() { return 917504; }
size_t fs::LittleFSFS::usedBytes() {
  size_t n = 0;
  for (const auto& kv : g_files) n += kv.second.size();
  return n;
}
fs::LittleFSFS LittleFS;
static const esp_partition_t g_spiffs = {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0x310000, 0xE0000, "spiffs", false};
const esp_partition_t* esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char*) { return &g_spiffs; }
esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t, size_t) {
  g_files.clear();
  sim.logArea.assign(sim.logArea.size(), 0xFF);
  sim.logAreaErases++;
  return 0;
}
esp_err_t esp_partition_read(const esp_partition_t*, size_t off, void* dst, size_t n) {
  uint8_t* d = static_cast<uint8_t*>(dst);
  for (size_t i = 0; i < n; ++i) d[i] = off + i < sim.logArea.size() ? sim.logArea[off + i] : 0xFF;
  return 0;
}

// ---------------- OLED: 记录屏幕上的文字 ----------------
const u8g2_cb_t u8g2_cb_r0 = {0};
const uint8_t u8g2_font_6x10_tf[] = {0};
const uint8_t u8g2_font_5x8_tf[] = {0};
const uint8_t u8g2_font_6x13B_tf[] = {0};
bool U8G2::begin(void) { return true; }
void U8G2::setI2CAddress(uint8_t) {}
void U8G2::setBusClock(uint32_t) {}
void U8G2::setContrast(uint8_t) {}
void U8G2::setPowerSave(uint8_t) {}
void U8G2::setFontMode(uint8_t) {}
void U8G2::clearBuffer(void) { sim.screenBuf.clear(); }
void U8G2::sendBuffer(void) { sim.screen = sim.screenBuf; }
void U8G2::setFont(const uint8_t*) {}
u8g2_uint_t U8G2::drawStr(u8g2_uint_t, u8g2_uint_t, const char* s) {
  sim.screenBuf += s;
  sim.screenBuf += " | ";
  return static_cast<u8g2_uint_t>(strlen(s) * 6);
}
u8g2_uint_t U8G2::getStrWidth(const char* s) { return static_cast<u8g2_uint_t>(strlen(s) * 6); }
void U8G2::drawCircle(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, uint8_t) {}
void U8G2::drawDisc(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, uint8_t) {}
void U8G2::drawLine(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t) {}
void U8G2::drawHLine(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t) {}
void U8G2::drawBox(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t) {}
void U8G2::drawRBox(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t) {}
void U8G2::drawEllipse(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, uint8_t) {}
void U8G2::drawFilledEllipse(u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, u8g2_uint_t, uint8_t) {}
void U8G2::setDrawColor(uint8_t) {}
void U8G2::drawTriangle(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t) {}
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C::U8G2_SSD1309_128X64_NONAME0_F_HW_I2C(const u8g2_cb_t*, uint8_t, uint8_t, uint8_t) {}
U8G2_SSD1309_128X64_NONAME2_F_HW_I2C::U8G2_SSD1309_128X64_NONAME2_F_HW_I2C(const u8g2_cb_t*, uint8_t, uint8_t, uint8_t) {}

// ---------------- 灯 ----------------
static std::map<const Adafruit_NeoPixel*, int>& pixPin() {   // 函数内静态: 全局对象构造顺序无关
  static std::map<const Adafruit_NeoPixel*, int> m;
  return m;
}
Adafruit_NeoPixel::Adafruit_NeoPixel(uint16_t, int16_t pin, neoPixelType) { pixPin()[this] = pin; }
void Adafruit_NeoPixel::begin(void) {}
void Adafruit_NeoPixel::show(void) {}
void Adafruit_NeoPixel::clear(void) { if (pixPin()[this] == 25) sim.ringColor = 0; }
void Adafruit_NeoPixel::setBrightness(uint8_t) {}
void Adafruit_NeoPixel::fill(uint32_t c, uint16_t, uint16_t) { if (pixPin()[this] == 25) sim.ringColor = c; }
void Adafruit_NeoPixel::setPixelColor(uint16_t, uint32_t) {}
void Adafruit_NeoPixel::setPixelColor(uint16_t, uint8_t, uint8_t, uint8_t) {}
uint16_t Adafruit_NeoPixel::numPixels(void) const { return 12; }

// ---------------- DFPlayer ----------------
bool DFRobotDFPlayerMini::begin(Stream&, bool isACK, bool) { return sim.dfReplies || !isACK; }
void DFRobotDFPlayerMini::setTimeOut(unsigned long) {}
void DFRobotDFPlayerMini::volume(uint8_t) {}
void DFRobotDFPlayerMini::play(int n) { sim.clips.push_back(n); }
void DFRobotDFPlayerMini::playMp3Folder(int n) { sim.clips.push_back(n); }
void DFRobotDFPlayerMini::stop() {}
int DFRobotDFPlayerMini::readFileCounts() { return sim.dfReplies ? sim.dfFiles : -1; }
bool DFRobotDFPlayerMini::available() { return false; }
uint8_t DFRobotDFPlayerMini::readType() { return 0; }
uint16_t DFRobotDFPlayerMini::read() { return 0; }
