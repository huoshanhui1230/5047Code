#pragma once
// 串口命令行: 非阻塞读取一行, 分词交给 Tokens.h
#include <Arduino.h>
#include <string>
#include <vector>
#include "Tokens.h"

class Console {
 public:
  // 读到完整一行时返回 true, 并把分词结果写入 out
  bool poll(Stream& in, std::vector<std::string>& out) {
    while (in.available()) {
      const int c = in.read();
      if (c < 0) break;
      if (c == '\r' || c == '\n') {
        if (buf_.empty()) continue;
        out = tokenize(buf_);
        line_ = buf_;
        buf_.clear();
        return !out.empty();
      }
      if (c == 8 || c == 127) {   // 退格
        if (!buf_.empty()) buf_.pop_back();
        continue;
      }
      if (buf_.size() < 240) buf_ += static_cast<char>(c);
    }
    return false;
  }
  const std::string& lastLine() const { return line_; }

 private:
  std::string buf_, line_;
};
