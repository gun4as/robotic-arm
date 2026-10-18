#pragma once
// Pastāvīgie iestatījumi: Wi-Fi un pults funkciju piešķiršana.
// (Kontroliera iestatījumus glabā pats kontrolieris - SAVE.)
#include <Arduino.h>

namespace settings {

// Pults funkcijas: kura poga/stiks ko dara (maina web lapas cilne "Pults")
struct RcMap {
  int8_t trimJoint[12];   // trima pogas P0..P11: 0 = nav, 1..5 = locītava, 6 = satvērējs
  int8_t trimDir[12];     // -1 / +1
  int8_t bank[2][5][2];   // [A/B][locītava] = {kanāls (-1 = nav, 0..4 = RC_CH_*), virziens -1/+1}
  uint8_t speed[3];       // 3 pozīciju slēdža A ātrumi, %
  uint8_t trimStep10;     // trima solis, 0.1° vienībās
  uint8_t gripStep;       // satvērēja pogu solis, %
};

void begin();
String wifiSsid();
String wifiPass();
void setWifi(const String &ssid, const String &pass);

const RcMap &rcMap();
void setRcMap(const RcMap &m);   // saglabā pastāvīgi
void rcMapDefaults(RcMap &m);    // no config.h
}
