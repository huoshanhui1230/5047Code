#pragma once
// 运行时 AI 照护解释: ESP32 经 Wi-Fi (手机热点) 直接调用 Gemini generateContent
// 在后台 FreeRTOS 任务中执行, 不阻塞采样/灯光/按键/语音; 失败或校验不过时回退模板。
#include <Arduino.h>
#include <string>
#include "Explain.h"

struct AiResult {
  uint32_t id = 0;
  ExplainEvent event = ExplainEvent::Manual;
  std::string state;
  ExplainText text;
  bool fromAi = false;          // false = 模板 (TPL)
  std::string reason;           // "ok" 或回退原因
  std::string model;            // 请求使用的模型 ID
  std::string modelVersion;     // 返回中的实际模型版本
  int httpCode = 0;
  uint32_t latencyMs = 0;       // 最后一次 HTTP 耗时
  uint8_t attempts = 0;         // HTTP 次数 (0 = 未联网)
  bool manual = false;
  std::string contextJson;
};

class AiClient {
 public:
  // startTask=false 时只能用 runNow (测试程序)
  void begin(const char* rootCa, bool startTask);
  void configure(const String& apiKey, const String& model, bool enabled);
  void setNetworkOk(bool ok) { networkOk_ = ok; }
  void setVerbose(bool v) { verbose_ = v; }
  void setInsecureTls(bool v) { insecure_ = v; }   // 只用于排查证书问题, 默认校验证书
  bool enabled() const { return enabled_; }
  bool hasKey() const;
  String model() const;

  // 主循环提交请求: 最新请求覆盖尚未开始的旧请求; delayMs 后执行 (等语音播完)
  void submit(const ExplainContext& ctx, bool manual, uint32_t delayMs);
  bool poll(AiResult& out);     // 有新结果时返回 true
  bool busy() const { return working_ || pendingValid_; }
  uint16_t callsToday() const { return callsToday_; }
  void resetDaily() { callsToday_ = 0; }

  // 同步执行一次 (t8_power / t10_ai 使用)
  AiResult runNow(const ExplainContext& ctx, bool manual);

 private:
  static void taskThunk(void* self);
  void taskLoop();
  AiResult process(const ExplainContext& ctx, bool manual, uint32_t id);
  bool httpPost(const std::string& url, const std::string& key, const std::string& body, std::string& resp,
                int& code, uint32_t& ms, std::string& err);
  bool wouldCallNetwork(const std::string& key, std::string& why) const;

  const char* rootCa_ = nullptr;
  void* mutex_ = nullptr;       // SemaphoreHandle_t
  void* task_ = nullptr;        // TaskHandle_t
  std::string apiKey_, model_;
  volatile bool enabled_ = true, networkOk_ = false, verbose_ = true, insecure_ = false;
  // 待处理请求与结果 (受 mutex 保护)
  ExplainContext pendingCtx_;
  volatile bool pendingValid_ = false;
  bool pendingManual_ = false;
  uint32_t pendingDue_ = 0, nextId_ = 1, pendingId_ = 0;
  AiResult result_;
  volatile bool resultReady_ = false, working_ = false;
  volatile uint16_t callsToday_ = 0;
  uint32_t lastCallMs_ = 0;
};
