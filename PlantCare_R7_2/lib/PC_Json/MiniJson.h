#pragma once
// 极简 JSON 工具 (纯 C++, 不依赖 Arduino): 生成请求时转义字符串, 解析 Gemini 返回。
// 只实现本项目需要的部分; 深度上限 40。
#include <string>
#include <vector>
#include <utility>

namespace mj {

// 转义为 JSON 字符串内容 (不含两侧引号)
std::string escape(const std::string& s);
// 带引号的 JSON 字符串
inline std::string quote(const std::string& s) { return "\"" + escape(s) + "\""; }

struct Value {
  enum Kind { T_NULL, T_BOOL, T_NUM, T_STR, T_ARR, T_OBJ };
  Kind kind = T_NULL;
  bool b = false;
  double num = 0;
  std::string str;                                   // T_STR: 解码后的文本
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> obj;

  const Value* get(const char* key) const;          // 对象成员, 没有则 nullptr
  const Value* at(size_t i) const;                   // 数组元素, 越界则 nullptr
  bool isStr() const { return kind == T_STR; }
  bool isObj() const { return kind == T_OBJ; }
  bool isArr() const { return kind == T_ARR; }
  bool isTrue() const { return kind == T_BOOL && b; }
};

// 解析整段文本; 失败时 err 写明位置
bool parse(const std::string& text, Value& out, std::string& err);

}  // namespace mj
