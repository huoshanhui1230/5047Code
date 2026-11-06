#pragma once
#include "Arduino.h"
#define TimeOut 0
#define WrongStack 1
#define DFPlayerCardInserted 2
#define DFPlayerCardRemoved 3
#define DFPlayerCardOnline 4
#define DFPlayerPlayFinished 5
#define DFPlayerError 6
class DFRobotDFPlayerMini {
 public:
  bool begin(Stream& stream, bool isACK = true, bool doReset = true);
  void setTimeOut(unsigned long timeOutDuration);
  void volume(uint8_t volume);
  void play(int fileNumber = 1);
  void playMp3Folder(int fileNumber);
  void stop();
  int readFileCounts();
  bool available();
  uint8_t readType();
  uint16_t read();
};
