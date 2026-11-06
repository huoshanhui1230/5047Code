#pragma once
// 主机模拟的共享状态: 测试脚本通过它设置传感器、网络和 AI 返回, 并检查串口、屏幕、灯和语音。
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct SimState {
  uint32_t ms = 0;                 // millis()
  bool ntpSynced = false;
  long long epochAtMs0 = 0;        // 校时后: epoch = epochAtMs0 + ms / 1000
  int probeMv = 1600;              // GPIO34 读数
  int probeNoise = 3;              // 每次读数的抖动 (mV)
  bool pirHigh = false;
  bool buttonDown = false;
  bool wifiAvailable = false;
  std::string apSsid = "SimAP";
  int httpCode = 200;
  std::string httpResp;            // Gemini 返回 (完整 JSON)
  int httpCalls = 0;
  std::string lastHttpBody;
  std::string serialOut;           // Serial 输出累计
  std::deque<char> serialIn;
  std::vector<int> clips;          // DFPlayer 播放的片段
  int dfFiles = 3;
  bool dfReplies = true;           // false: 模拟你们的 DFPlayer (收命令、能放音, 但从不回字节)
  uint32_t ringColor = 0;          // 灯环最近一次 fill 的颜色
  std::string screen;              // 最近一次 sendBuffer 时屏幕上的全部文字
  std::string screenBuf;
  int resetReason = 1;             // esp_reset_reason(): 1 = POWERON, 4 = PANIC
  std::vector<uint8_t> logArea = std::vector<uint8_t>(8192, 0xFF);   // 日志分区前两块 (超级块对)
  int logAreaErases = 0;
  std::recursive_mutex mu;         // 串口输出 (AI 任务线程也会打印)
};
extern SimState sim;
