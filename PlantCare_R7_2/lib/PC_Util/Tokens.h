#pragma once
// 串口命令分词 (纯 C++): 空格分隔, 支持双引号, 例如  wifi "Zihao Phone" pass123
#include <string>
#include <vector>
#include <cctype>

inline std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuote = false, have = false;
  for (char ch : line) {
    if (ch == '"') { inQuote = !inQuote; have = true; continue; }
    if (!inQuote && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
      if (have) { out.push_back(cur); cur.clear(); have = false; }
      continue;
    }
    cur += ch;
    have = true;
  }
  if (have) out.push_back(cur);
  return out;
}

inline std::string lowerStr(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
