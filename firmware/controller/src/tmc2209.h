#pragma once
// =============================================================================
//  TMC2209 konfigurācija caur viena vada UART (SKR V1.4 Turbo).
//  Katram draiverim savs pins, adrese 0. Bit-bang ar DWT cikla skaitītāju.
//  Ieraksta: mikrosoļus, strāvu, SpreadCycle/StealthChop.
//  Periodiski pārbauda draiverus: ja motoru barošana ieslēgta vēlāk vai draiveris
//  pārstartējies, konfigurācija tiek ierakstīta no jauna.
// =============================================================================
#include <Arduino.h>

namespace tmc {
  bool available();                    // vai platei ir TMC UART
  void init();                         // konfigurē visus draiverus
  void poll(bool idle);                // izsaukt no loop(); strādā tikai stāvot
  bool setCurrent(uint8_t j, uint16_t mA);   // ieraksta draiverī uzreiz
  void presetCurrent(uint8_t j, uint16_t mA); // tikai atceras (pirms init)
  uint16_t current(uint8_t j);
  void report(Print &out);             // diagnostika: versija, savienojums
}
