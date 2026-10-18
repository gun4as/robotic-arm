#pragma once
// =============================================================================
//  Teksta komandu protokols (USB un ESP32 UART). Pilns apraksts: docs/protokols.md
// =============================================================================

#include <stdint.h>

namespace protocol {
  void init();
  void poll();                    // nolasa portus, izpilda komandas, auto statuss
  void banner();
  void broadcast(const char *msg);  // rinda visiem portiem (un ekrānam)
  // Izpilda komandu iekšēji (programma no SD). false -> errOut = kļūdas kods.
  bool executeInternal(const char *line, char *errOut, uint8_t errSize);
}
