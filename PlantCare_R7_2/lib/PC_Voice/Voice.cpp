#include "Voice.h"
#include "config.h"

// 卡上正式录音的实际台词 (10-31 试听记下)。方案第 2 章原定的三句见 README / sd_card 说明。
const char* voiceLine(uint8_t clip) {
  if (clip == CLIP_DRY) return "I'm thirsty! A little water, please?";
  if (clip == CLIP_WAIT) return "I'm still soaking it up! Let's wait a little.";
  if (clip == CLIP_OK) return "I've had enough water. Thank you!";
  return "";
}

bool Voice::begin(HardwareSerial& ser, int rxPin, int txPin, uint8_t volume) {
  volume_ = volume;
#if NO_TF
  (void)ser; (void)rxPin; (void)txPin;
  online_ = true;
  files_ = 3;
  return true;
#else
  ser_ = &ser;
  ser.begin(9600, SERIAL_8N1, rxPin, txPin);
  // 与 R7_Main 里已验证能放音的 t7 相同: 先等模块就绪, 带应答方式复位; 不行再用无应答方式。
  // (有的兼容模块在无应答方式下不回查询; t7 的 d 命令会分别用两种方式查询一次。)
  delay(1500);
  noAck_ = !df_.begin(ser, /*isACK=*/true, /*doReset=*/true);
  if (noAck_) {
    delay(500);
    df_.begin(ser, /*isACK=*/false, /*doReset=*/true);
  }
  delay(1000);
  df_.setTimeOut(600);
  return probe();
#endif
}

bool Voice::retry() {
#if NO_TF
  return online_;
#else
  if (!ser_) return false;
  noAck_ = (retryCount_++ % 2) == 1;                       // 轮流试应答方式和无应答方式
  df_.begin(*ser_, /*isACK=*/!noAck_, /*doReset=*/false);   // 不复位, 不等待
  df_.setTimeOut(600);
  return probe();
#endif
}

bool Voice::probe() {
#if NO_TF
  return online_;
#else
  df_.volume(volume_);
  delay(100);
  files_ = df_.readFileCounts();
  if (files_ < 0) {  // 第一次常被模块之前发来的消息占住, 再查一次
    delay(200);
    files_ = df_.readFileCounts();
  }
  // 文件数 >= 3 才算在线; 带应答方式下模块有回应、只是文件数查询失败 (-1) 时也先按在线处理。
  // 模块一个字节都不回 (无应答方式 + 查询失败) 时, 按 VOICE_TX_ONLY_FALLBACK 决定是否"只发不收"。
  txOnly_ = VOICE_TX_ONLY_FALLBACK && noAck_ && files_ < 0;
  online_ = files_ >= 3 || (!noAck_ && files_ < 0) || txOnly_;
  return online_;
#endif
}

bool Voice::play(uint8_t clip) {
  if (clip < 1 || clip > 3) return false;
  lastPlayMs_ = millis();
#if NO_TF
  Serial.printf("[voice %u] %s\n", clip, voiceLine(clip));
  return true;
#else
  if (!online_) return false;
#if VOICE_USE_MP3_FOLDER
  df_.playMp3Folder(clip);
#else
  df_.play(clip);
#endif
  return true;
#endif
}

void Voice::stop() {
#if !NO_TF
  if (online_) df_.stop();
#endif
}

void Voice::setVolume(uint8_t v) {
  if (v > 30) v = 30;
  volume_ = v;
#if !NO_TF
  if (online_) df_.volume(v);
#endif
}

void Voice::poll() {
#if !NO_TF
  if (!ser_ || !df_.available()) return;
  const uint8_t type = df_.readType();
  const int value = df_.read();
  if (type == DFPlayerError) {
    errors_++;
    lastErr_ = value;
  } else if (type == DFPlayerPlayFinished) {
    finished_++;
    lastFinished_ = value;
  } else if (type == DFPlayerCardRemoved) {
    online_ = false;
  } else if (type == DFPlayerCardInserted || type == DFPlayerCardOnline) {
    online_ = files_ >= 3 || txOnly_;
  }
#endif
}

const char* Voice::status() const {
#if NO_TF
  return "NO_TF build (lines printed to serial)";
#else
  if (online_ && txOnly_) return "TX-only: module never answers, audio not confirmed (listen)";
  if (online_) return "online";
  if (files_ >= 0 && files_ < 3) return "TF card has <3 files";
  return "not responding";
#endif
}
