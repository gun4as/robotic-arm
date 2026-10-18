#include "settings.h"
#include "config.h"
#include "rc_protocol.h"

namespace settings {
namespace {
constexpr uint32_t kRcMagic = 0x52434D31;   // "RCM1"
RcMap rc;
}

void rcMapDefaults(RcMap &m) {
  struct E { int8_t ch; int8_t dir; };
  const E trim[RC_BTN_TRIMS] = RC_TRIM_MAP;
  const E a[NUM_JOINTS] = RC_MAP_BANK_A, b[NUM_JOINTS] = RC_MAP_BANK_B;
  const uint8_t sp[3] = RC_SPEED_LEVELS;
  memset(&m, 0, sizeof m);
  for (uint8_t i = 0; i < 12; i++) { m.trimJoint[i] = trim[i].ch; m.trimDir[i] = trim[i].dir < 0 ? -1 : 1; }
  for (uint8_t j = 0; j < 5; j++) {
    m.bank[0][j][0] = a[j].ch; m.bank[0][j][1] = a[j].dir < 0 ? -1 : 1;
    m.bank[1][j][0] = b[j].ch; m.bank[1][j][1] = b[j].dir < 0 ? -1 : 1;
  }
  for (uint8_t i = 0; i < 3; i++) m.speed[i] = sp[i];
  m.trimStep10 = (uint8_t)lroundf(RC_TRIM_STEP_DEG * 10.0f);
  m.gripStep = RC_GRIP_STEP;
}

const RcMap &rcMap() { return rc; }

}  // namespace settings

#if defined(ESP8266)
// ESP8266: nav Preferences bibliotēkas -> EEPROM emulācija flash atmiņā
#include <EEPROM.h>

namespace settings {
namespace {
struct Stored {
  uint32_t magic;
  char ssid[33];
  char pass[65];
  uint32_t rcMagic;     // pievienots vēlāk: vecā atmiņā tur ir kaut kas cits -> noklusējumi
  RcMap rc;
};
constexpr uint32_t kMagic = 0x52524231;   // "RRB1"
Stored data;

void commit() {
  EEPROM.put(0, data);
  EEPROM.commit();
}
}

void begin() {
  EEPROM.begin(sizeof(Stored));
  EEPROM.get(0, data);
  if (data.magic != kMagic) {
    memset(&data, 0, sizeof data);
    data.magic = kMagic;
  }
  data.ssid[32] = 0;
  data.pass[64] = 0;
  if (data.rcMagic != kRcMagic) { rcMapDefaults(data.rc); data.rcMagic = kRcMagic; }
  rc = data.rc;
}

String wifiSsid() { return data.ssid[0] ? String(data.ssid) : String(WIFI_STA_SSID); }
String wifiPass() { return data.ssid[0] ? String(data.pass) : String(WIFI_STA_PASS); }

void setWifi(const String &ssid, const String &pass) {
  strncpy(data.ssid, ssid.c_str(), 32);
  strncpy(data.pass, pass.c_str(), 64);
  data.ssid[32] = 0;
  data.pass[64] = 0;
  commit();
}

void setRcMap(const RcMap &m) {
  rc = m;
  data.rc = m;
  data.rcMagic = kRcMagic;
  commit();
}

}  // namespace settings

#else
#include <Preferences.h>

namespace settings {
namespace {
Preferences prefs;
}

void begin() {
  prefs.begin("robotroka", false);
  if (prefs.getUInt("rcmagic", 0) == kRcMagic && prefs.getBytesLength("rcmap") == sizeof(RcMap))
    prefs.getBytes("rcmap", &rc, sizeof rc);
  else
    rcMapDefaults(rc);
}

String wifiSsid() {
  String s = prefs.getString("ssid", "");
  return s.length() ? s : String(WIFI_STA_SSID);
}
String wifiPass() {
  return prefs.getString("ssid", "").length() ? prefs.getString("pass", "") : String(WIFI_STA_PASS);
}

void setWifi(const String &ssid, const String &pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
}

void setRcMap(const RcMap &m) {
  rc = m;
  prefs.putBytes("rcmap", &rc, sizeof rc);
  prefs.putUInt("rcmagic", kRcMagic);
}

}  // namespace settings
#endif
