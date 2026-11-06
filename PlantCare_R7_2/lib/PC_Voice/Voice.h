#pragma once
// DFPlayer Mini 三段固定语音 (/mp3/0001..0003); 编译时定义 NO_TF=1 则改为串口打印台词
#include <Arduino.h>
#include "config.h"

#ifndef NO_TF
#define NO_TF 0
#endif

#if !NO_TF
#include <DFRobotDFPlayerMini.h>
#endif

// 片段编号 -> 台词 (与方案第 2 章一致; 编号对应关系见 config.h 的 CLIP_DRY / CLIP_OK / CLIP_WAIT)
const char* voiceLine(uint8_t clip);

class Voice {
 public:
  // 返回是否在线 (模块应答且卡内至少 3 个文件)
  bool begin(HardwareSerial& ser, int rxPin, int txPin, uint8_t volume);
  bool retry();                       // 离线时的轻量探测 (不复位, 最多约 1.5 s; 应答方式和无应答方式轮流试)
  bool online() const { return online_; }
  int files() const { return files_; }
  bool noAck() const { return noAck_; }
  bool txOnly() const { return txOnly_; }   // 模块不回任何字节, 只发命令 (见 config.h VOICE_TX_ONLY_FALLBACK)
  bool play(uint8_t clip);            // 1..3
  void stop();
  void setVolume(uint8_t v);
  uint8_t volume() const { return volume_; }
  bool recentlyStarted(uint32_t now, uint32_t windowMs) const { return lastPlayMs_ && now - lastPlayMs_ < windowMs; }
  void poll();                        // 读取模块上报的错误/插拔卡
  int errors() const { return errors_; }
  uint16_t finished() const { return finished_; }   // 模块报告"播放完毕"的次数
  int lastFinishedTrack() const { return lastFinished_; }
  const char* status() const;

 private:
  bool probe();
  uint8_t retryCount_ = 0;
#if !NO_TF
  DFRobotDFPlayerMini df_;
  HardwareSerial* ser_ = nullptr;
#endif
  bool online_ = false;
  bool noAck_ = false;
  bool txOnly_ = false;
  int files_ = -1;
  uint8_t volume_ = VOICE_VOLUME_DEFAULT;
  uint32_t lastPlayMs_ = 0;
  int errors_ = 0;
  uint16_t finished_ = 0;
  int lastFinished_ = 0;
  int lastErr_ = 0;
};
