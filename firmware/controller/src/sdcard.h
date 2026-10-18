#pragma once
// =============================================================================
//  SD karte ekrānā: programmu faili *.TXT (8.3 nosaukumi, saknes mapē).
//  Mega/RAMPS: Arduino SD bibliotēka. SKR: vēl nav (available() = false).
//  Datora simulācijā: sim/mock/mock_sd.h
// =============================================================================
#include <stdint.h>

namespace sdcard {
  void setEnabled(bool on);                   // CFG SD: lasītājs pieslēgts / nav
  bool enabled();
  void poll();                                // kartes ielikšana/izņemšana
  bool available();                           // karte ir un nolasāma
  uint8_t count();                            // .TXT failu skaits
  bool nameAt(uint8_t index, char *out13);    // n-tā faila nosaukums
  bool open(const char *name);
  int readLine(char *buf, uint8_t size);      // garums, -1 = faila beigas
  void rewind();
  void close();
}
