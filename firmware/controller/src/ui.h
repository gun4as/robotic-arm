#pragma once
// =============================================================================
//  Ekrāna lietotāja saskarne (20x4 LCD + grozāmā poga + poga "X"/KILL + pīkstulis).
//  Statusa ekrāns -> klikšķis -> izvēlne: kustināt locītavu, solis, ātrums,
//  satvērējs, programma no SD, HOME, nulle, motori, RESET, info.
// =============================================================================
#include <stdint.h>

namespace ui {
  // Ekrāns pieslēgts / nav (CFG LCD). Ieslēdzot inicializē pinus un LCD (~60 ms).
  void setEnabled(bool on);
  bool enabled();
  void poll();                       // no loop()
  void encoderIsr();                 // no soļu ISR ~1 kHz
  void message(const char *msg);     // notikumi ("[HOME] J1 ok" u.c.) uz 3 s
  // Attālināta / testa ievade (protokola komanda KEY)
  void inject(int8_t detents, bool click, bool kill);
}
