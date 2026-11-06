#pragma once
#include "Arduino.h"
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
class File : public Stream {
 public:
  File() {}
  File(const std::string& path, char mode) : path_(path), mode_(mode), ok_(true) {}
  size_t write(uint8_t) override;
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  size_t read(uint8_t* buf, size_t size);
  size_t size() const;
  void close();
  operator bool() const;

 private:  // 只在主机模拟 (test_host/sim) 里使用
  std::string path_;
  char mode_ = 'r';
  bool ok_ = false;
  size_t pos_ = 0;
};
class FS {
 public:
  File open(const char* path, const char* mode = FILE_READ, const bool create = false);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* pathFrom, const char* pathTo);
};
}  // namespace fs
using fs::File;
using fs::FS;
