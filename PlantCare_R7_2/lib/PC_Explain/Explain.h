#pragma once
// AI 运行时照护解释的纯逻辑部分 (不依赖 Arduino, 可在电脑上单元测试):
// 组装输入 JSON 与请求体、解析 Gemini 返回、本地校验、固定模板。
// 联网发送在 PC_Ai/AiClient 中完成。
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>

enum class ExplainEvent : uint8_t {
  FirstState,   // 开机后首次确定状态
  OkToDry,      // 不缺水 -> 缺水
  DryToWait,    // 缺水 -> 等待 (人按了已补水)
  WaitToOk,     // 等待到期, 恢复
  WaitToDry,    // 等待到期, 未恢复
  DryToOk,      // 缺水期间读数自行回到 H 以上 (未按键)
  Manual        // explain 命令, 解释当前状态
};
const char* explainEventName(ExplainEvent e);

struct ExplainContext {
  ExplainEvent event = ExplainEvent::Manual;
  std::string state;              // "DRY" | "WAITING" | "OK"
  float R = NAN, L = NAN, H = NAN;
  std::string mode = "normal";    // normal | FAST | SIM
  std::string localTime;          // "HH:MM", 未校时则为空
  float hoursSinceWater = NAN;    // 距上次按"已补水"的小时数
  float rBeforeWater = NAN;       // 按键时的 R
  int waitMin = 10;               // 等待时长 (分钟)
  float rAtCheck = NAN;           // 等待到期复核时的 R
  float dropPerHour12h = NAN;     // 近 12 h 每小时下降量 (正数 = 变干)
  float prevCycleHours = NAN;     // 上一周期: 补水到转缺水的小时数
  int confirmRounds = 3;
  int aboveHRounds = -1;          // 最近连续 R >= H 的有效读数轮数 (状态机计数), -1 = 不知道
  std::vector<int> hourlyR;       // 近 24 h 每小时 R, 旧 -> 新
  std::string plant, waterNeed, source;
};

struct ExplainText {
  std::string headline;
  std::string body;
};

extern const char* const kExplainSystemPrompt;

// 输入 JSON (确定性顺序, 数字已取整); 校验时允许的数字也来自这里
std::string buildContextJson(const ExplainContext& c);
// generateContent 请求体; retryHint 非空时附在用户消息末尾
std::string buildRequestBody(const std::string& contextJson, const std::string& retryHint);
// 从 HTTP 返回中取出模型文本 (跳过 thought 部分)、modelVersion; 失败时 err 说明原因
bool extractModelText(const std::string& httpBody, std::string& text, std::string& modelVersion, std::string& err);
// API 错误信息 (error.message), 没有则空
std::string apiErrorMessage(const std::string& httpBody);
// 解析模型输出的 {"headline","body"}; 容忍 ``` 代码块包裹
bool parseExplainJson(const std::string& text, ExplainText& out, std::string& err);
// 本地校验 (第 4 章): 长度、ASCII、数字必须来自输入、禁用诊断用词、不得与状态矛盾
bool validateExplain(const ExplainText& t, const ExplainContext& c, const std::string& contextJson, std::string& reason);
// 固定模板 (也是对照基线); 输出同样能通过 validateExplain
ExplainText templateExplain(const ExplainContext& c);
// 按单词换行, 供 OLED 使用
std::vector<std::string> wrapWords(const std::string& text, size_t width);
