#pragma once
// Pults krāsu ekrāns (ST7789 240x280): saite, ARM, baterija, režīms, locītavu joslas.
#include <stdint.h>
#include "rc_protocol.h"

struct ScreenData {
  bool link, armed, estop, battLow;
  uint8_t charge;         // 0 = nav USB, 1 = lādējas, 2 = pilna
  uint8_t channel;        // ESP-NOW kanāls (meklējot roku)
  uint16_t battMv;
  uint8_t speedPos, mode; // 3 pozīciju slēdži A, B (0/1/2)
  uint8_t switches;       // SW1..SW5 (biti 0..4)
  uint8_t calStep;        // 0 = nav kalibrācijas, 1 = centrs, 2 = gali
  int8_t ota;             // -1 = parasts darbs, 0..100 = atjaunināšanas režīms (progress %)
  const char *otaIp;      // IP mājas tīklā (atjaunināšanas režīmā), nullptr = nav pieslēgts
  int8_t pressed;         // nesen nospiestā poga (-1 = nav)
  uint8_t swWarn;         // ieslēdzot: slēdži nav sākuma pozīcijā (biti 0..4 = SW1..SW5, 5 = E, 6 = C)
  RcTelemetryPacket tel;
};

namespace screen {
void begin();
void reinit();          // pēc barošanas lēciena: ekrāna kontrolieris var būt pārstartējies
void draw(const ScreenData &d);
}
