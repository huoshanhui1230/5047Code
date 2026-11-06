// t7_dfplayer: 三句语音 (TF 卡 /mp3/0001-0003)
// 合格: 打印 DFPlayer ready 与 files on card: 3; 输入 1 2 3 分别听到三句, 1 m 外听得清
// 其他命令: + / - 音量, s 停止, r 重新初始化, d 不经过库直接查询 (看模块有没有回任何字节)
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "Voice.h"

Voice voice;
uint8_t vol = VOICE_VOLUME_DEFAULT;

// 不经过 DFPlayer 库: 直接发"查询 TF 卡文件数"命令, 把模块回的原始字节打印出来。
// ack = 1 时命令里要求模块回应答 (库的"带应答方式"), 0 时不要求。返回收到的字节数。
int rawQuery(uint8_t ack) {
  while (Serial2.available()) Serial2.read();
  uint8_t q[10] = {0x7E, 0xFF, 0x06, 0x48, ack, 0x00, 0x00, 0x00, 0x00, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += q[i];
  sum = static_cast<uint16_t>(0 - sum);
  q[7] = static_cast<uint8_t>(sum >> 8);
  q[8] = static_cast<uint8_t>(sum & 0xFF);
  Serial2.write(q, sizeof(q));
  uint8_t b[64];
  int n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < 800) {
    while (Serial2.available() && n < 64) b[n++] = static_cast<uint8_t>(Serial2.read());
    delay(5);
  }
  Serial.printf("[t7] raw reply (%s):", ack ? "ACK requested" : "no ACK");
  if (n == 0) Serial.print(" none");
  for (int i = 0; i < n; i++) Serial.printf(" %02X", b[i]);
  Serial.println();
  for (int i = 0; i + 9 < n; i++) {
    if (b[i] != 0x7E || b[i + 9] != 0xEF) continue;
    const uint8_t cmd = b[i + 3];
    const int param = (b[i + 5] << 8) | b[i + 6];
    if (cmd == 0x48) Serial.printf("[t7] -> module answers: %d file(s) on the TF card\n", param);
    else if (cmd == 0x41) Serial.println("[t7] -> module acknowledged the command");
    else if (cmd == 0x40) Serial.printf("[t7] -> module answers with error %d (1 = still starting, wait and press r; 8 = cannot read the card: reseat it, FAT32, /mp3 folder)\n", param);
    else if (cmd == 0x3F) Serial.println("[t7] -> module reports 'card online'");
    else Serial.printf("[t7] -> module message 0x%02X, param %d\n", cmd, param);
  }
  return n;
}

void rawProbe() {
  const int a = rawQuery(0);
  delay(200);
  const int b = rawQuery(1);
  if (a == 0 && b == 0) {
    Serial.println("[t7] -> no bytes at all from the DFPlayer (its TX -> GPIO16 path).");
    Serial.println("[t7]    If 1 2 3 still play audio, commands get through and only the reply path is broken");
    Serial.println("[t7]    (R7_Main recorded the same: tracks=-1, events=0). The firmware then sends without replies.");
    Serial.println("[t7]    If nothing plays either: check DFPlayer power and GPIO17 -> 1k -> RX.");
  }
  else if (a == 0)
    Serial.println("[t7] -> this module only answers when an ACK is requested: the firmware uses ACK mode for it");
}

void init_() {
  Serial.println("[t7] initialising DFPlayer (about 4 s, up to ~9 s if it does not answer)...");
  const bool ok = voice.begin(Serial2, PIN_DF_RX, PIN_DF_TX, vol);
  Serial.printf("[t7] %s | files on card: %d\n", ok ? "DFPlayer ready" : "DFPlayer NOT ready", voice.files());
  Serial.printf("[t7] status: %s, %s mode\n", voice.status(), voice.noAck() ? "no-ACK" : "ACK");
  if (voice.txOnly()) {
    Serial.println("[t7] the module sends no replies: commands are still sent. Press 1 2 3 and LISTEN.");
    rawProbe();
  } else if (!ok) {
    Serial.println("[t7] check: GPIO17 -> 1k -> RX, TX -> GPIO16, VCC/GND, card FAT, files /mp3/0001..0003");
    rawProbe();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n[t7] DFPlayer on RX=GPIO%d TX=GPIO%d\n", PIN_DF_RX, PIN_DF_TX);
  init_();
  Serial.println("[t7] 1 2 3 = play, + - = volume, s = stop, r = re-init, d = raw query");
}

uint16_t seenFinished = 0;

void loop() {
  voice.poll();
  if (voice.finished() != seenFinished) {
    seenFinished = voice.finished();
    Serial.printf("[t7] finished %d\n", voice.lastFinishedTrack());
  }
  if (!Serial.available()) { delay(5); return; }
  const int c = Serial.read();
  if (c >= '1' && c <= '3') {
    const uint8_t clip = static_cast<uint8_t>(c - '0');
    Serial.printf("[t7] play %u: %s -> %s\n", clip, voiceLine(clip), voice.play(clip) ? "sent" : "voice offline");
  } else if (c == '+' || c == '-') {
    vol = c == '+' ? (vol >= 30 ? 30 : vol + 1) : (vol == 0 ? 0 : vol - 1);
    voice.setVolume(vol);
    Serial.printf("[t7] volume %u (mini speaker: keep <= 20)\n", vol);
  } else if (c == 's') {
    voice.stop();
    Serial.println("[t7] stop");
  } else if (c == 'r') {
    init_();
  } else if (c == 'd') {
    rawProbe();
  }
  if (voice.errors()) Serial.printf("[t7] module reported %d error(s)\n", voice.errors());
}
