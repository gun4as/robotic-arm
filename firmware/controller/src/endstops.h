#pragma once
// =============================================================================
//  Gala slēdži: nolasīšana ar atlēcienu filtru (2 vienādi nolasījumi pēc kārtas).
//  Iestatījumi (ieslēgts, NO/NC) ir motion::JointConfig.
// =============================================================================
#include <stdint.h>

namespace endstops {
  void init();
  void update();                 // izsauc kustību vadības takts (200 Hz / 1 kHz)
  bool triggered(uint8_t j);     // false, ja slēdzis atslēgts konfigurācijā
  char stateChar(uint8_t j);     // '-' atslēgts, '0' brīvs, '1' nospiests
}
