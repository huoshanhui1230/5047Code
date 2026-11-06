#pragma once
// NVS 持久化设置: 标定、阈值、版本绑定、Wi-Fi (两组)、API 密钥、开关, 以及断电恢复所需的运行状态
#include <Arduino.h>
#include <Preferences.h>

struct Settings {
  // 标定与阈值
  int32_t dryMv = -1, wetMv = -1;
  String calId = "C0";
  float L = NAN, H = NAN;
  uint16_t waitMin = 10;
  // 版本绑定 (第 5 章第 7 步)
  String profileVer = "P1", voiceVer = "V1", paramVer = "v0-temp";
  String potId = "POT1", probeId = "SEN0308-1";
  // 网络与 AI
  String ssid1, pass1, ssid2, pass2;
  String apiKey, model;
  bool aiOn = true;
  // 语音与静音
  bool quietOn = true;
  uint8_t talkMode = 0;      // 0 = talk normal, 1 = talk test
  uint8_t volume = 20;
  // 断电恢复
  uint8_t lastPlant = 0;     // CareMode 数值 (Dry/Ok), 0 = 无
  uint32_t lastPlantEpoch = 0;
  bool waitMarker = false;
  uint32_t waterEpoch = 0;   // 最近一次按"已补水"的时间 (校时后)
  float waterR = NAN;
  float prevCycleH = NAN;    // 上一周期: 补水到转缺水的小时数
  uint32_t boots = 0;
};

class Store {
 public:
  bool begin(const char* ns);
  Settings s;
  void saveCal();             // dryMv, wetMv, calId
  void saveThresholds();      // L, H, waitMin
  void saveVersions();        // profile/voice/param/pot/probe
  void saveWifi();
  void saveAi();              // apiKey, model, aiOn
  void saveVoice();           // quietOn, talkMode, volume
  void saveLastPlant();
  void saveWait();
  void saveWater();
  void savePrevCycle();
  void bumpBoots();
  void factoryReset();        // 清空本命名空间

 private:
  Preferences p_;
  bool ok_ = false;
};
