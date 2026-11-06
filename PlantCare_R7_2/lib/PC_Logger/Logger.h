#pragma once
// 板载闪存日志 (LittleFS):
//   /log.csv   每 5 min 一条采样记录 (S) + 所有事件 (E); 超过上限轮换为 /log1.csv
//   /ai.jsonl  每次 AI 解释一行 (时间、事件、模型、版本、延迟、校验结果、显示文本、AI/TPL、输入上下文)
// 用 dump / aidump 导出, logstat 统计 24 h 测试 (覆盖率、前 24 h 有效条数、复位原因、状态变化)
#include <Arduino.h>
#include <esp_partition.h>

class Logger {
 public:
  bool begin(size_t maxBytes, size_t aiMaxBytes);
  bool ok() const { return ok_; }
  bool recovered() const { return recovered_; }   // 这次开机擦除过日志区 (别的固件留下的旧文件系统)
  const char* recoveredWhy() const { return recoveredWhy_; }
  void sample(uint32_t epoch, uint32_t boot, uint32_t ms, const char* state, int mv, float R, bool valid,
              uint16_t motions, const char* flags);
  void event(uint32_t epoch, uint32_t boot, uint32_t ms, const char* state, const String& detail);
  void ai(const String& jsonLine);
  void dump(Stream& out);
  void dumpAi(Stream& out);
  void stats(Stream& out, uint32_t periodMs);
  void clear();
  size_t usedBytes() const;
  size_t totalBytes() const;

 private:
  void append(const char* path, const char* old, size_t maxBytes, const String& line);
  void printFile(Stream& out, const char* path);
  void eraseArea(const esp_partition_t* part, const char* why);
  bool ok_ = false;
  bool recovered_ = false;
  const char* recoveredWhy_ = "";
  size_t max_ = 128 * 1024, aiMax_ = 64 * 1024;
};
