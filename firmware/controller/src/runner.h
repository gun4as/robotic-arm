#pragma once
// =============================================================================
//  Programmas izpilde no SD kartes bez datora.
//  Faila formāts = PC programmas "Eksportēt tekstu": katra rinda ir kontroliera
//  komanda (MOVE, MOVEJ, REL, GRIP, HOME ...), "WAIT ms" = pauze, ';' = komentārs.
//  Pēc kustības komandas gaida, kamēr roka apstājas; kļūda -> izpilde apstājas.
// =============================================================================
#include <stdint.h>

namespace runner {
  enum class State : uint8_t { Idle, Running, Paused };

  bool start(const char *file, bool loop, char *err, uint8_t errSize);  // false + err kods
  void stop(const char *reason);    // nullptr = bez ziņas
  void pause();                     // apstājas pēc pašreizējā soļa
  void resume();
  void poll();

  State state();
  bool active();                    // Running vai Paused
  uint16_t line();
  uint16_t cycle();
  const char *file();
}
