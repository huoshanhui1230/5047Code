// 主机联调: 在电脑上真正运行 src/app/main.cpp (模拟时钟、探头、PIR、按钮、Wi-Fi、Gemini 返回),
// 按 README 第 8 节台架流程 + AI 解释 + 日志 + 安静时段逐条检查。
// 运行: bash test_host/run_sim.sh
#include <Arduino.h>
#include <Preferences.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "sim.h"
#include "config.h"
#include "Display.h"

void setup();
void loop();

static int g_fail = 0, g_pass = 0;
#define EXPECT(cond, what)                                                       \
  do {                                                                           \
    if (cond) g_pass++;                                                          \
    else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, what); }             \
  } while (0)

static size_t mark() {
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  return sim.serialOut.size();
}
static bool seen(size_t from, const std::string& s) {
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  return sim.serialOut.find(s, from) != std::string::npos;
}
// fw1.2 起屏幕只画图形: 用 Display::lastView() 看画的是什么, sim.screen (画过的文字) 必须一直是空的
static std::string view() { return Display::lastView(); }

// 模拟运行 ms 毫秒 (AI 任务线程与主循环按模拟时间轮流运行, 见 sim_stubs.cpp 的 delay)
static void runFor(uint32_t ms) {
  const uint32_t end = sim.ms + ms;
  while (static_cast<int32_t>(sim.ms - end) < 0) loop();
}
static void cmd(const std::string& c) {
  {
    std::lock_guard<std::recursive_mutex> g(sim.mu);
    for (char ch : c) sim.serialIn.push_back(ch);
    sim.serialIn.push_back('\n');
  }
  runFor(60);
}
static void setR(float R) { sim.probeMv = static_cast<int>(lround(2100 - 10 * R)); }   // 对应 cal set 2100 1100
#ifndef NO_TF
#define NO_TF 0
#endif
// 无 TF 卡版本: 说话时屏幕先显示 3 s 台词页, 之后才回到表情
static void afterSpeech() { if (NO_TF) runFor(3500); }
static void walkBy() { sim.pirHigh = true; runFor(300); sim.pirHigh = false; runFor(300); afterSpeech(); }
static void press() { sim.buttonDown = true; runFor(200); sim.buttonDown = false; runFor(200); afterSpeech(); }
static std::string gemini(const std::string& headline, const std::string& body) {
  return "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"{\\\"headline\\\":\\\"" + headline + "\\\",\\\"body\\\":\\\"" + body +
         "\\\"}\"}],\"role\":\"model\"},\"finishReason\":\"STOP\"}],\"modelVersion\":\"gemini-3.5-flash-lite-sim\"}";
}
// 最近播放的片段: 有卡版本看 DFPlayer, 无卡版本看串口打印的 "[voice N]"
static std::string clipStr(const char* prefix, int clip, const char* suffix = "") {
  return std::string(prefix) + std::to_string(clip) + suffix;
}
static int lastClip() {
  if (!NO_TF) return sim.clips.empty() ? 0 : sim.clips.back();
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  const size_t p = sim.serialOut.rfind("[voice ");
  return p == std::string::npos ? 0 : atoi(sim.serialOut.c_str() + p + 7);
}
static size_t clipCount() {
  if (!NO_TF) return sim.clips.size();
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  size_t n = 0;
  for (size_t p = sim.serialOut.find("[voice "); p != std::string::npos; p = sim.serialOut.find("[voice ", p + 1)) n++;
  return n;
}

static const uint32_t YELLOW = (255u << 16) | (150u << 8), BLUE = (70u << 8) | 255u, GREEN = (220u << 8) | 40u;

// 24 h 连续运行 (正常参数): 检查 5 min 采样日志的节拍和 logstat 的 PASS 判定
static int longRun() {
  sim.epochAtMs0 = 1793491200LL - 2 * 3600;   // 2026-11-01 09:00 AEDT 左右
  sim.wifiAvailable = true;
  setup();
  runFor(2000);
  cmd("wifi SimAP pw");
  runFor(5000);
  cmd("apikey SIMKEY");
  cmd("cal set 2100 1100");
  cmd("set LH 40 45");
  setR(70);
  cmd("clear");
  sim.httpResp = gemini("Soil drying", "R is below 40. Check the top soil.");
  // R 从 70 慢慢降到 30: 中途应出现一次 OK -> DRY
  const uint32_t start = sim.ms;
  for (int h = 0; h < 24; h++) {
    setR(70 - h * (40.0f / 23));
    runFor(3600000UL);
  }
  runFor(10 * 60000UL);
  const size_t m = mark();
  cmd("logstat");
  std::lock_guard<std::recursive_mutex> g(sim.mu);
  const std::string out = sim.serialOut.substr(m);
  printf("%s", out.c_str());
  EXPECT(out.find("first 24 h: 288 rows, 288 valid") != std::string::npos, "24 h: 288 valid rows in the first 24 h");
  EXPECT(out.find("-> PASS") != std::string::npos, "24 h: PASS");
  EXPECT(out.find("FAST/SIM rows: 0") != std::string::npos, "24 h: no bench rows");
  EXPECT(out.find("OK->DRY ok_to_dry") != std::string::npos, "24 h: the drying event is listed");
  printf("\nsim long run: %d passed, %d failed (simulated %.1f h)\n", g_pass, g_fail, (sim.ms - start) / 3600000.0);
  fflush(stdout);
  std::_Exit(g_fail ? 1 : 0);
}

// 日志分区里留着别的固件的 LittleFS (真机: R7_Main 的 240 块文件系统 -> 挂载时 lfs_fs_grow 断言、死机循环)。
// 按 littlefs 规范写一个超级块: 偏移 8 是 "littlefs", 偏移 20 起是 version / block_size / block_count。
static void putSuperblock(size_t block, uint32_t blocks) {
  uint8_t* h = sim.logArea.data() + block * 4096;
  const uint8_t head[8] = {3, 0, 0, 0, 0x34, 0xf0, 0x00, 0x00};   // revision count + 名字标签 (与规范示例相同)
  const uint8_t tag[4] = {0x2f, 0xe0, 0x00, 0x10};
  memcpy(h, head, 8);
  memcpy(h + 8, "littlefs", 8);
  memcpy(h + 16, tag, 4);
  const uint32_t v[6] = {0x00020001u, 4096u, blocks, 255u, 0x7fffffffu, 1022u};
  for (int i = 0; i < 6; i++)
    for (int k = 0; k < 4; k++) h[20 + 4 * i + k] = static_cast<uint8_t>((v[i] >> (8 * k)) & 0xff);
}
static int fsRun(const std::string& mode) {
  if (mode == "larger") {             // R7_Main 的旧文件系统: 240 块 > 本包分区 224 块
    putSuperblock(0, 240);
    putSuperblock(1, 240);
  } else if (mode == "same") {        // 本固件自己格式化的: 224 块, 必须保留
    putSuperblock(0, 224);
    putSuperblock(1, 224);
  } else if (mode == "crash") {       // 兜底: 上次开机在挂载时死机 (复位原因 PANIC, 标记还在)
    sim.resetReason = 4;
    Preferences p;
    p.begin("pc72fs", false);
    p.putBool("mounting", true);
    p.end();
  }
  setup();
  runFor(3000);
  const size_t m = mark();
  cmd("dump");
  if (mode == "larger") {
    EXPECT(sim.logAreaErases == 1, "fs larger: log area erased once before mounting");
    EXPECT(seen(0, "larger file system from another firmware (240 blocks, room for 224): erasing it once"), "fs larger: serial message");
    EXPECT(seen(m, "log_area_erased (larger file system from another firmware)"), "fs larger: event in the log");
  } else if (mode == "same") {
    EXPECT(sim.logAreaErases == 0, "fs same size: file system kept");
    EXPECT(!seen(0, "log_area_erased"), "fs same size: no erase event");
  } else if (mode == "crash") {
    EXPECT(sim.logAreaErases == 1, "fs crash guard: erased once");
    EXPECT(seen(m, "log_area_erased (previous boot crashed while mounting the log area)"), "fs crash guard: event in the log");
  } else {
    EXPECT(sim.logAreaErases == 0, "fs blank: nothing erased");
  }
  EXPECT(seen(m, "boot fw="), "fs: logging works after the check");
  Preferences p;
  p.begin("pc72fs", false);
  EXPECT(!p.getBool("mounting", true), "fs: mounting flag cleared after a good mount");
  cmd("wifi SimAP pw");                // 设置仍写进主程序自己的命名空间
  Preferences q;
  q.begin(NVS_NAMESPACE, true);
  EXPECT(q.getString("ssid1", "") == "SimAP", "fs: settings still saved in the app namespace");
  printf("sim fs %s: %d passed, %d failed\n", mode.c_str(), g_pass, g_fail);
  fflush(stdout);
  std::_Exit(g_fail ? 1 : 0);
}

// 补水历史 (fw1.1): sim 按键不写入闪存; 改标定端点和 clear 时清掉; [round] 打印的连续轮数包含本轮
static int histRun() {
  sim.epochAtMs0 = 1793491200LL - 2 * 3600;
  sim.wifiAvailable = true;
  setup();
  runFor(2000);
  cmd("wifi SimAP pw");
  runFor(5000);
  cmd("apikey SIMKEY");
  sim.httpResp = gemini("Waiting", "R was 25 before watering. Re-checking soon.");
  cmd("cal set 2100 1100");
  cmd("set LH 30 38");
  cmd("fast on");
  cmd("quiet off");
  setR(20);
  size_t m = mark();
  runFor(20000);
  EXPECT(seen(m, "mode=READING below=3 above=0\n[event] state READING->DRY first_state"),
         "hist: the round that changes state prints below=3");
  press();                                   // 真实读数下按键: 写入补水历史
  Preferences q;
  q.begin(NVS_NAMESPACE, true);
  const uint32_t ep1 = q.getUInt("waterep", 0);
  EXPECT(ep1 != 0, "hist: real press saves the watering time");
  EXPECT(std::fabs(q.getFloat("waterR", NAN) - 20.0f) < 0.6f, "hist: real press saves R before watering");
  runFor(65000);                             // 等待到期, 没恢复 -> 缺水
  cmd("sim 25");
  runFor(16000);
  press();                                   // sim 下按键: 只留在内存
  EXPECT(q.getUInt("waterep", 0) == ep1, "hist: sim press does not change the saved watering time");
  EXPECT(std::fabs(q.getFloat("waterR", NAN) - 20.0f) < 0.6f, "hist: sim press does not change the saved R");
  runFor(12000);
  m = mark();
  cmd("explain");
  runFor(3000);
  EXPECT(seen(m, "\"mode\":\"SIM\"") && seen(m, "\"R_before_water\":25"), "hist: sim AI context uses the sim press");
  cmd("sim off");
  runFor(70000);
  m = mark();
  cmd("explain");
  runFor(3000);
  EXPECT(seen(m, "\"R_before_water\":20"), "hist: after sim off the AI context uses the real record again");
  m = mark();
  cmd("cal set 2200 1100");
  EXPECT(seen(m, "water_history_cleared (calibration changed)"), "hist: calibration change logs the clear");
  EXPECT(q.getUInt("waterep", 1) == 0 && std::isnan(q.getFloat("waterR", 0)), "hist: calibration change clears the record");
  m = mark();
  cmd("cal id C2");
  EXPECT(!seen(m, "water_history_cleared"), "hist: changing only the cal id keeps the record");
  runFor(16000);
  press();
  EXPECT(q.getUInt("waterep", 0) != 0, "hist: next real press saved again");
  m = mark();
  cmd("clear");
  EXPECT(seen(m, "logs cleared, watering history cleared"), "hist: clear message");
  EXPECT(q.getUInt("waterep", 1) == 0 && std::isnan(q.getFloat("prevcyc", 0)), "hist: clear removes the record");
  printf("sim hist: %d passed, %d failed\n", g_pass, g_fail);
  fflush(stdout);
  std::_Exit(g_fail ? 1 : 0);
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "long") return longRun();
  if (argc > 1 && std::string(argv[1]) == "hist") return histRun();
  if (argc > 2 && std::string(argv[1]) == "fs") return fsRun(argv[2]);
  const bool noReply = getenv("SIM_DF_NO_REPLY") != nullptr;   // 模拟只收不回的 DFPlayer
  if (noReply) sim.dfReplies = false;
  size_t m;
  setup();
  runFor(3000);
  if (noReply && !NO_TF) EXPECT(seen(0, "voice_tx_only"), "DFPlayer without replies -> TX-only, logged at boot");
  EXPECT(seen(0, NO_TF ? "=== PlantCare R7.2-fw1.4 (NO_TF) ===" : "=== PlantCare R7.2-fw1.4 (TF) ==="), "boot banner");
  EXPECT(view() == "icon:warning" && sim.screen.empty(), "uncalibrated: warning icon, no text");
  EXPECT(sim.ringColor == 0, "ring off while not calibrated");

  // ---- 台架设置 (README 第 8 节) ----
  setR(50);
  cmd("cal set 2100 1100");
  cmd("cal id C1");
  cmd("set L 30");
  m = mark();
  cmd("set H 38");
  EXPECT(seen(m, "state UNCAL->READING enter_reading"), "calibrated -> Reading");
  runFor(300);
  EXPECT(view() == "icon:hourglass", "Reading: hourglass icon, no face");
  cmd("fast on");
  cmd("quiet off");
  cmd("talk normal");

  // 1. R 50 -> 约 15 s 后不缺水 (3 轮 >= H)
  m = mark();
  runFor(16000);
  EXPECT(seen(m, "state READING->OK first_state"), "1: Reading -> OK after 3 rounds");
  EXPECT(sim.ringColor == GREEN, "1: ring green");
  // 还没存密钥、没联网: 5 s 后显示模板解释页 (TPL) 30 s
  runFor(6000);
  EXPECT(seen(m, "ai_result TPL first_state no_api_key"), "1: no key -> template explanation");
  EXPECT(view() == "face:smile" && sim.screen.empty(), "1: smile face only (the explanation stays in the serial log)");
  runFor(32000);

  // 2. 走近 (PIR 预热 60 s 之后): talk normal 下开机首次确定的不缺水不说 (fw1.3); talk test 每次走近都说
  runFor(5000);
  m = mark();
  const size_t clipsAtStep2 = clipCount();
  walkBy();
  EXPECT(clipCount() == clipsAtStep2 && seen(m, "silent (ok_already_said)"), "2: talk normal: OK after first_state stays silent");
  cmd("talk test");
  m = mark();
  walkBy();
  EXPECT(lastClip() == CLIP_OK, "2: talk test: approach in OK plays the OK clip");
  if (NO_TF) EXPECT(seen(m, clipStr("[voice ", CLIP_OK, "] I've had enough water.")), "2: NO_TF prints the line");
  EXPECT(seen(m, clipStr("speak ", CLIP_OK, " (approach)")), "2: speak logged");
  cmd("talk normal");

  // 3. 不缺水时按键被忽略
  m = mark();
  press();
  EXPECT(seen(m, "button_ignored_in_OK"), "3: button ignored in OK");

  // 4. R 25 -> 缺水
  setR(25);
  m = mark();
  runFor(16000);
  EXPECT(seen(m, "state OK->DRY ok_to_dry"), "4: OK -> DRY after 3 rounds < L");
  EXPECT(sim.ringColor == YELLOW, "4: ring yellow");
  runFor(40000);
  EXPECT(view() == "face:worried" && sim.screen.empty(), "4: worried face, no text");

  // 5. 走近 -> 0001; 6. 马上再走过 -> 冷却
  walkBy();
  EXPECT(lastClip() == CLIP_DRY, "5: approach in DRY plays the dry clip");
  m = mark();
  const size_t clipsBefore = clipCount();
  walkBy();
  EXPECT(clipCount() == clipsBefore, "6: no second clip within cooldown");
  EXPECT(seen(m, clipStr("speak_skipped_cooldown ", CLIP_DRY)), "6: cooldown logged");

  // 7. 按键 -> 等待, 立刻播 0002
  m = mark();
  press();
  EXPECT(seen(m, "state DRY->WAITING dry_to_wait"), "7: DRY -> WAITING on button");
  EXPECT(sim.ringColor == BLUE, "7: ring blue");
  EXPECT(lastClip() == CLIP_WAIT, "7: button plays the waiting clip");
  EXPECT(view() == "face:waiting", "7: waiting face");

  // 8. 等待中变湿: 不立刻变绿; 等待中走近 (fw1.3): 不到半个等待时长 (fast: 30 s) 不再说, 过半后再说一次
  // 9. 到期后变绿, 60 s 内有人走近过则当场播恢复那句 (不受同一句冷却限制)
  setR(50);
  runFor(10000);
  const size_t clipsInWait = clipCount();
  walkBy();
  EXPECT(clipCount() == clipsInWait && seen(m, "silent (waiting_said_recently)"), "8: waiting: no repeat within half the wait");
  runFor(20000);
  EXPECT(!seen(m, "WAITING->OK"), "8: no recovery before the wait ends");
  runFor(10000);
  walkBy();                                   // 等待已过半: 再说一次等待那句
  EXPECT(lastClip() == CLIP_WAIT && seen(m, clipStr("speak ", CLIP_WAIT, " (waiting_again)")), "8: waiting: says it again after half the wait");
  runFor(25000);
  EXPECT(seen(m, "state WAITING->OK wait_to_ok"), "9: recovered after wait expiry");
  EXPECT(sim.ringColor == GREEN, "9: ring green");
  EXPECT(seen(m, clipStr("speak ", CLIP_OK, " (recovered)")) && lastClip() == CLIP_OK,
         "9: recovery line plays although the OK clip played minutes ago");
  // 9b. 恢复那句已经说过: 之后走近不再说, 直到状态改变 (fw1.3)
  runFor(15000);
  m = mark();
  const size_t clipsAfterOk = clipCount();
  walkBy();
  EXPECT(clipCount() == clipsAfterOk && seen(m, "silent (ok_already_said)"), "9: OK line is said once per recovery");

  // 10. 再缺水、按键、一直干 -> 到期后回到缺水 "Not yet recovered"
  setR(25);
  runFor(16000);
  m = mark();
  press();
  EXPECT(seen(m, "DRY->WAITING"), "10: waiting again");
  runFor(70000);
  EXPECT(seen(m, "state WAITING->DRY wait_to_dry"), "10: not recovered -> DRY");
  // 10b. 未确认恢复 (fw1.3): 等待结束后一个等待时长 (fast: 60 s) 内走近不说"口渴"
  const size_t clipsNotRecovered = clipCount();
  walkBy();
  EXPECT(clipCount() == clipsNotRecovered && seen(m, "silent (recheck_quiet)"), "10: not recovered: no thirsty line right after the wait");
  runFor(40000);
  EXPECT(view() == "face:worried" && sim.screen.empty(), "10: not recovered -> worried face");
  EXPECT(sim.ringColor == YELLOW, "10: ring yellow");
  runFor(15000);                              // 静默期已过: 回到缺水的原规则 (这里 0001 还在 10 min 冷却内)
  m = mark();
  walkBy();
  EXPECT(seen(m, clipStr("speak_skipped_cooldown ", CLIP_DRY)) && !seen(m, "recheck_quiet"), "10: after the quiet period the dry rule applies again");

  // 11. 停在滞回区 R 34: 不来回跳
  setR(34);
  m = mark();
  runFor(60000);
  EXPECT(!seen(m, "state "), "11: no state change inside the hysteresis band");

  // 13. dump
  m = mark();
  cmd("dump");
  // 5 min 采样行落在哪个状态取决于台架步骤的时长 (无卡版本每次说话多等 3.5 s), 只要求有采样行和缺水事件行
  EXPECT(seen(m, "----- BEGIN LOG") && seen(m, "epoch,boot,ms,type,state,mv,R,valid,motion,detail") && seen(m, ",S,") &&
         seen(m, ",E,DRY,"),
         "13: dump prints the CSV log");

  // ---- AI 解释: 连上热点 (2026-11-01 21:50 悉尼时间) ----
  sim.epochAtMs0 = 1793491200LL + 10 * 3600 + 50 * 60 - sim.ms / 1000;   // 10:50 UTC = 21:50 AEDT
  sim.wifiAvailable = true;
  m = mark();
  cmd("wifi SimAP secretpass");
  EXPECT(seen(m, "> wifi SimAP ****") && !seen(m, "secretpass"), "wifi password is masked in the echo");
  runFor(3000);
  EXPECT(seen(m, "time_known 21:5") && seen(m, "(ntp)"), "time synced via NTP after Wi-Fi");
  EXPECT(!seen(m, "new_day"), "first NTP sync does not reset the daily counters");
  m = mark();
  cmd("apikey SIMKEY123");
  EXPECT(seen(m, "> apikey ****") && !seen(m, "SIMKEY123"), "api key is never echoed");

  // 14. R 50 -> DRY 变 OK, AI 返回合规文字 -> 屏幕 AI 页
  sim.httpResp = gemini("In range", "R 50 is above 38. No water needed now.");
  setR(50);
  m = mark();
  runFor(16000);
  EXPECT(seen(m, "state DRY->OK dry_to_ok"), "14: DRY -> OK");
  runFor(8000);
  EXPECT(sim.httpCalls == 1, "14: one HTTP call");
  EXPECT(sim.lastHttpBody.find("dry_to_ok") != std::string::npos, "14: request carries the event");
  EXPECT(seen(m, "ai_result AI dry_to_ok ok"), "14: AI result accepted");
  EXPECT(seen(m, "AI: In range | R 50 is above 38. No water needed now."), "14: AI text printed on the serial port");
  EXPECT(view() == "face:smile" && sim.screen.empty(), "14: screen keeps only the face");

  // 走近: 恢复后第一次走近补说一次 (fw1.3), 屏幕不出现任何文字
  runFor(40000);
  m = mark();
  walkBy();
  EXPECT(seen(m, clipStr("speak ", CLIP_OK, " (recovered_once)")) && lastClip() == CLIP_OK, "14: first approach after dry_to_ok says the OK line once");
  runFor(6000);
  EXPECT(view() == "face:smile" && sim.screen.empty(), "14: approach brings up no text");

  // 手动解释: 结果只在串口
  m = mark();
  cmd("explain");                              // 并开始 60 s 自动调用间隔
  runFor(3000);
  EXPECT(seen(m, "ai_result AI manual ok"), "manual explain: result in the serial log");
  EXPECT(view() == "face:smile" && sim.screen.empty(), "manual explain: screen unchanged");
  sim.httpResp = gemini("Back in range", "R 50 is above 38 again.");
  setR(25);
  runFor(16000);
  setR(50);
  runFor(16000);

  // 校验不过 -> 重试一次 -> 模板
  sim.httpResp = gemini("Soil check", "R 99 is very low.");
  const int before = sim.httpCalls;
  m = mark();
  cmd("explain");
  runFor(6000);
  EXPECT(sim.httpCalls == before + 2, "invalid output: retried once (2 HTTP calls)");
  EXPECT(seen(m, "ai_result TPL manual invalid: number not in input: 99"), "invalid output -> template with reason");

  // HTTP 429 -> 模板
  sim.httpCode = 429;
  sim.httpResp = "{\"error\":{\"code\":429,\"message\":\"Resource has been exhausted\",\"status\":\"RESOURCE_EXHAUSTED\"}}";
  runFor(11000);
  m = mark();
  cmd("explain");
  runFor(4000);
  EXPECT(seen(m, "ai_result TPL manual http_429: Resource has been exhausted"), "HTTP 429 -> template with API message");
  sim.httpCode = 200;

  // 15. 关掉热点 -> 离线模板, 状态照常
  sim.wifiAvailable = false;
  runFor(2000);
  runFor(11000);
  m = mark();
  cmd("explain");
  runFor(3000);
  EXPECT(seen(m, "ai_result TPL manual offline"), "15: hotspot off -> template (offline)");

  // ai off: 与 R7 相同, 不调用、不显示; 已经排队的请求结果只记日志
  sim.wifiAvailable = true;
  runFor(25000);
  setR(25);
  m = mark();
  for (int i = 0; i < 400 && !seen(m, "state OK->DRY"); i++) runFor(50);   // 等到状态变化的那一刻
  EXPECT(seen(m, "state OK->DRY"), "OK -> DRY before ai off");
  cmd("ai off");                               // 解释请求还在排队 (5 s 后才执行)
  runFor(15000);
  EXPECT(seen(m, "ai_result TPL ok_to_dry ai_off"), "queued request finishes as ai_off");
  cmd("explain");
  EXPECT(seen(m, "ai is off"), "ai off blocks explain");
  cmd("ai on");
  setR(50);
  runFor(16000);

  // 日志: logstat / aidump
  m = mark();
  cmd("logstat");
  EXPECT(seen(m, "sample rows:") && seen(m, "FAST/SIM rows:"), "logstat prints the 24 h summary");
  m = mark();
  cmd("aidump");
  EXPECT(seen(m, "\"source\":\"AI\"") && seen(m, "\"input\":{\"event\":\"dry_to_ok\""), "aidump has AI rows with input context");
  EXPECT(seen(m, "\"model_version\":\"gemini-3.5-flash-lite-sim\""), "aidump records the returned model version");

  // 安静时段: 21:50 + 运行时间 -> 过 22:00 后走近不说话
  runFor(9 * 60000);
  m = mark();
  cmd("quiet on");
  cmd("time");
  EXPECT(seen(m, "time 22:"), "local time is after 22:00");
  const size_t c0 = clipCount();
  walkBy();
  EXPECT(clipCount() == c0 && seen(m, clipStr("speak_skipped_quiet ", CLIP_OK)), "quiet hours: approach does not speak");

  // 探头断开 -> 读数停在 ADC 底值约 142 mV -> 3 轮无效 -> 暂停判断, 灯熄 (不能显示成"很湿、不缺水")
  sim.probeMv = 0;
  m = mark();
  runFor(20000);
  EXPECT(seen(m, "state OK->PAUSED paused"), "probe unplugged -> PAUSED after 3 invalid rounds");
  EXPECT(view() == "icon:warning" && sim.ringColor == 0, "paused: warning icon, ring off");
  setR(50);
  runFor(20000);
  EXPECT(seen(m, "PAUSED->READING") && seen(m, "READING->OK"), "probe back -> re-confirm -> OK");

  // fw1.4 接触不良: 有效 有效 无效 ... 交替, 既凑不齐 3 轮无效也凑不齐 3 轮有效 -> 15 轮后警示三角, 连续 3 轮有效才恢复
  m = mark();
  for (int i = 0; i < 8; i++) {               // 24 个 5 s 窗口, 每个窗口最多一轮
    setR(50); runFor(5000);
    setR(50); runFor(5000);
    sim.probeMv = 0; runFor(5000);
  }
  EXPECT(seen(m, "state OK->PAUSED unstable"), "flaky probe: valid/invalid mix -> PAUSED (unstable) after 15 rounds");
  EXPECT(view() == "icon:warning" && sim.ringColor == 0, "unstable: warning icon, ring off");
  const size_t mStable = mark();
  setR(50);
  runFor(9000);                               // 不到 3 轮有效: 还在暂停
  EXPECT(!seen(mStable, "PAUSED->READING"), "unstable pause holds until 3 valid rounds in a row");
  runFor(16000);
  EXPECT(seen(mStable, "PAUSED->READING") && seen(mStable, "READING->OK"), "stable again -> 3 valid rounds -> re-confirm -> OK");

  // 阈值一次改两个 (从台架值换成实盆值), 单独改出 H <= L 时拒绝并提示
  m = mark();
  cmd("set LH 47 52");
  EXPECT(seen(m, "set L=47") && !seen(m, "rejected"), "set LH changes both thresholds");
  m = mark();
  cmd("set L 60");
  EXPECT(seen(m, "rejected: H must be greater than L") && seen(m, "set LH <L> <H>"), "set L above H is rejected with a hint");
  cmd("set LH 30 38");

  m = mark();
  cmd("status");
  EXPECT(seen(m, "calls today") && seen(m, "cal dry 2100 wet 1100 id C1"), "status shows AI and calibration");

  printf("\nsim (%s%s): %d passed, %d failed (simulated %.1f min)\n", NO_TF ? "app_noTF" : "app", noReply ? ", DFPlayer no replies" : "", g_pass, g_fail, sim.ms / 60000.0);
  if (g_fail) {
    std::lock_guard<std::recursive_mutex> g(sim.mu);
    const char* path = NO_TF ? ".host_build/sim_serial_noTF.log" : ".host_build/sim_serial.log";
    FILE* f = fopen(path, "w");
    if (f) { fputs(sim.serialOut.c_str(), f); fclose(f); printf("serial output saved to %s\n", path); }
  }
  fflush(stdout);
  std::_Exit(g_fail ? 1 : 0);   // AI 任务线程还在运行, 直接退出
}
