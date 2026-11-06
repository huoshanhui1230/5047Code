#include "Logger.h"
#include <LittleFS.h>
#include <string.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <string>
#include "LogStats.h"

namespace {
const char* const kLog = "/log.csv";
const char* const kLogOld = "/log1.csv";
const char* const kAi = "/ai.jsonl";
const char* const kAiOld = "/ai1.jsonl";
const char* const kHeader = "epoch,boot,ms,type,state,mv,R,valid,motion,detail";

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// littlefs 规范 (SPEC.md): 有效超级块的魔数 "littlefs" 总在块内偏移 8, 偏移 20 起依次是 version、block_size、
// block_count (小端)。读超级块对 (块 0 和块 1), 返回分区里现有文件系统的块数; 不是 littlefs v2 或块大小不是 4096 时返回 0。
uint32_t existingFsBlocks(const esp_partition_t* part) {
  uint32_t most = 0;
  for (uint32_t b = 0; b < 2; ++b) {
    uint8_t h[32];
    if (esp_partition_read(part, b * 4096, h, sizeof(h)) != 0) continue;
    if (memcmp(h + 8, "littlefs", 8) != 0) continue;
    const uint32_t version = le32(h + 20), blockSize = le32(h + 24), blocks = le32(h + 28);
    if ((version >> 16) != 2 || blockSize != 4096 || blocks == 0 || blocks > 4096) continue;
    if (blocks > most) most = blocks;
  }
  return most;
}

String clean(const String& s) {
  String d = s;
  d.replace(',', ';');
  d.replace('\n', ' ');
  d.replace('\r', ' ');
  return d;
}
}  // namespace

bool Logger::begin(size_t maxBytes, size_t aiMaxBytes) {
  max_ = maxBytes;
  aiMax_ = aiMaxBytes;
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  // 1) 日志分区里留着别的固件用更大分区格式化的 LittleFS (例如 R7_Main: 0xF0000 = 240 块; 本包 0xE0000 = 224 块)。
  //    库不支持缩小文件系统, 挂载时断言 lfs_fs_grow (block_count >= lfs->block_count) 失败、死机重启。
  //    所以挂载前先读超级块: 块数比分区大就擦掉日志分区, 挂载时自动格式化。
  if (part) {
    const uint32_t found = existingFsBlocks(part), room = part->size / 4096;
    if (found > room) {
      Serial.printf("[log] the log area holds a larger file system from another firmware (%lu blocks, room for %lu): erasing it once\n",
                    static_cast<unsigned long>(found), static_cast<unsigned long>(room));
      eraseArea(part, "larger file system from another firmware");
    }
  }
  // 2) 兜底: 挂载前记一个标记, 挂载完清掉; 若上次开机在挂载中死机 (其它旧数据), 这次先擦掉日志分区再挂载。
  Preferences guard;
  const bool guardOk = guard.begin("pc72fs", false);
  const esp_reset_reason_t why = esp_reset_reason();
  const bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  if (guardOk && crashed && guard.getBool("mounting", false) && !recovered_ && part) {
    Serial.println("[log] last boot crashed while mounting the log area: erasing it once");
    eraseArea(part, "previous boot crashed while mounting the log area");
  }
  if (guardOk) guard.putBool("mounting", true);
  ok_ = LittleFS.begin(true);   // 首次使用或擦除后自动格式化
  if (guardOk) {
    guard.putBool("mounting", false);
    guard.end();
  }
  if (ok_ && !LittleFS.exists(kLog)) {
    File f = LittleFS.open(kLog, FILE_WRITE);
    if (f) { f.println(kHeader); f.close(); }
  }
  return ok_;
}

void Logger::eraseArea(const esp_partition_t* part, const char* why) {
  if (esp_partition_erase_range(part, 0, part->size) != 0) return;
  recovered_ = true;
  recoveredWhy_ = why;
}

void Logger::append(const char* path, const char* old, size_t maxBytes, const String& line) {
  if (!ok_) return;
  size_t size = 0;
  if (LittleFS.exists(path)) {
    File r = LittleFS.open(path, FILE_READ);
    if (r) { size = r.size(); r.close(); }
  }
  if (size + line.length() > maxBytes) {   // 轮换: 只保留上一份
    LittleFS.remove(old);
    LittleFS.rename(path, old);
    if (strcmp(path, kLog) == 0) {
      File h = LittleFS.open(path, FILE_WRITE);
      if (h) { h.println(kHeader); h.close(); }
    }
  }
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return;
  f.print(line);
  f.close();
}

void Logger::sample(uint32_t epoch, uint32_t boot, uint32_t ms, const char* state, int mv, float R, bool valid,
                    uint16_t motions, const char* flags) {
  char rbuf[16];
  if (isnan(R)) rbuf[0] = 0;
  else snprintf(rbuf, sizeof(rbuf), "%.1f", R);
  char buf[160];
  snprintf(buf, sizeof(buf), "%lu,%lu,%lu,S,%s,%d,%s,%d,%u,%s\n", static_cast<unsigned long>(epoch),
           static_cast<unsigned long>(boot), static_cast<unsigned long>(ms), state, mv, rbuf, valid ? 1 : 0,
           static_cast<unsigned>(motions), flags);
  append(kLog, kLogOld, max_, String(buf));
}

void Logger::event(uint32_t epoch, uint32_t boot, uint32_t ms, const char* state, const String& detail) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%lu,%lu,%lu,E,%s,,,,,", static_cast<unsigned long>(epoch),
           static_cast<unsigned long>(boot), static_cast<unsigned long>(ms), state);
  String line(buf);
  line += clean(detail);
  line += "\n";
  append(kLog, kLogOld, max_, line);
}

void Logger::ai(const String& jsonLine) {
  String line = jsonLine;
  line.replace('\n', ' ');
  line += "\n";
  append(kAi, kAiOld, aiMax_, line);
}

void Logger::printFile(Stream& out, const char* path) {
  if (!ok_ || !LittleFS.exists(path)) return;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return;
  uint8_t buf[256];
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    out.write(buf, n);
  }
  f.close();
}

void Logger::dump(Stream& out) {
  out.println("----- BEGIN LOG (older file first) -----");
  printFile(out, kLogOld);
  printFile(out, kLog);
  out.println("----- END LOG -----");
}

void Logger::dumpAi(Stream& out) {
  out.println("----- BEGIN AI LOG -----");
  printFile(out, kAiOld);
  printFile(out, kAi);
  out.println("----- END AI LOG -----");
}

void Logger::stats(Stream& out, uint32_t periodMs) {
  // 逐行交给 LogStats (纯 C++, 已在电脑上单元测试): 覆盖率、前 24 h 有效条数、重启原因、状态变化
  LogStats st(periodMs / 1000);
  const char* files[2] = {kLogOld, kLog};
  for (const char* path : files) {
    if (!ok_ || !LittleFS.exists(path)) continue;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) continue;
    while (f.available()) {
      const String line = f.readStringUntil('\n');
      st.add(std::string(line.c_str()));
    }
    f.close();
  }
  out.print(st.report().c_str());
}

void Logger::clear() {
  if (!ok_) return;
  LittleFS.remove(kLog);
  LittleFS.remove(kLogOld);
  LittleFS.remove(kAi);
  LittleFS.remove(kAiOld);
  File f = LittleFS.open(kLog, FILE_WRITE);
  if (f) { f.println(kHeader); f.close(); }
}

size_t Logger::usedBytes() const { return ok_ ? LittleFS.usedBytes() : 0; }
size_t Logger::totalBytes() const { return ok_ ? LittleFS.totalBytes() : 0; }
