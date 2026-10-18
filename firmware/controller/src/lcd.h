#pragma once
// =============================================================================
//  HD44780 20x4 LCD (4 bitu režīms) bez gaidīšanas.
//  Programma raksta ekrāna "buferī"; poll() katrā loop() izsaukumā nosūta uz
//  ekrānu ne vairāk kā vienu mainīto simbolu (~50 µs starplaiks), tāpēc kustību
//  vadība netiek aizturēta.
// =============================================================================
#include <stdint.h>

namespace lcd {
  constexpr uint8_t COLS = 20, ROWS = 4;
  constexpr char DEG = (char)0xDF;   // grāda zīme HD44780 A00 ROM

  void init();                                   // ~60 ms (tikai setup laikā)
  void clear();
  void setRow(uint8_t row, const char *text);   // rinda, papildināta ar atstarpēm
  void put(uint8_t row, uint8_t col, const char *text);
  const char *row(uint8_t r);                    // bufera saturs (testiem, LCD komandai)
  void poll();
}
