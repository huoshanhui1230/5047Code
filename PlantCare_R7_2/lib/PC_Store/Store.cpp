#include "Store.h"
#include "config.h"

bool Store::begin(const char* ns) {
  ok_ = p_.begin(ns, false);
  if (!ok_) return false;
  s.dryMv = p_.getInt("dry", -1);
  s.wetMv = p_.getInt("wet", -1);
  s.calId = p_.getString("calid", "C0");
  s.L = p_.getFloat("L", NAN);
  s.H = p_.getFloat("H", NAN);
  s.waitMin = p_.getUShort("waitmin", WAIT_MIN_DEFAULT);
  s.profileVer = p_.getString("pver", "P1");
  s.voiceVer = p_.getString("vver", "V1");
  s.paramVer = p_.getString("parver", "v0-temp");
  s.potId = p_.getString("pot", "POT1");
  s.probeId = p_.getString("probe", "SEN0308-1");
  s.ssid1 = p_.getString("ssid1", "");
  s.pass1 = p_.getString("pass1", "");
  s.ssid2 = p_.getString("ssid2", "");
  s.pass2 = p_.getString("pass2", "");
  s.apiKey = p_.getString("apikey", "");
  s.model = p_.getString("model", "");
  s.aiOn = p_.getBool("aion", true);
  s.quietOn = p_.getBool("quieton", true);
  s.talkMode = p_.getUChar("talk", 0);
  s.volume = p_.getUChar("vol", VOICE_VOLUME_DEFAULT);
  s.lastPlant = p_.getUChar("lastpl", 0);
  s.lastPlantEpoch = p_.getUInt("lastplep", 0);
  s.waitMarker = p_.getBool("waitmk", false);
  s.waterEpoch = p_.getUInt("waterep", 0);
  s.waterR = p_.getFloat("waterR", NAN);
  s.prevCycleH = p_.getFloat("prevcyc", NAN);
  s.boots = p_.getUInt("boots", 0);
  return true;
}

void Store::saveCal() {
  if (!ok_) return;
  p_.putInt("dry", s.dryMv);
  p_.putInt("wet", s.wetMv);
  p_.putString("calid", s.calId);
}

void Store::saveThresholds() {
  if (!ok_) return;
  p_.putFloat("L", s.L);
  p_.putFloat("H", s.H);
  p_.putUShort("waitmin", s.waitMin);
}

void Store::saveVersions() {
  if (!ok_) return;
  p_.putString("pver", s.profileVer);
  p_.putString("vver", s.voiceVer);
  p_.putString("parver", s.paramVer);
  p_.putString("pot", s.potId);
  p_.putString("probe", s.probeId);
}

void Store::saveWifi() {
  if (!ok_) return;
  p_.putString("ssid1", s.ssid1);
  p_.putString("pass1", s.pass1);
  p_.putString("ssid2", s.ssid2);
  p_.putString("pass2", s.pass2);
}

void Store::saveAi() {
  if (!ok_) return;
  p_.putString("apikey", s.apiKey);
  p_.putString("model", s.model);
  p_.putBool("aion", s.aiOn);
}

void Store::saveVoice() {
  if (!ok_) return;
  p_.putBool("quieton", s.quietOn);
  p_.putUChar("talk", s.talkMode);
  p_.putUChar("vol", s.volume);
}

void Store::saveLastPlant() {
  if (!ok_) return;
  p_.putUChar("lastpl", s.lastPlant);
  p_.putUInt("lastplep", s.lastPlantEpoch);
}

void Store::saveWait() {
  if (!ok_) return;
  p_.putBool("waitmk", s.waitMarker);
}

void Store::saveWater() {
  if (!ok_) return;
  p_.putUInt("waterep", s.waterEpoch);
  p_.putFloat("waterR", s.waterR);
}

void Store::savePrevCycle() {
  if (!ok_) return;
  p_.putFloat("prevcyc", s.prevCycleH);
}

void Store::bumpBoots() {
  s.boots++;
  if (ok_) p_.putUInt("boots", s.boots);
}

void Store::factoryReset() {
  if (!ok_) return;
  p_.clear();
  s = Settings();
}
