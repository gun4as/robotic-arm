#pragma once
// Pults uztvērējs (ESP-NOW vai LoRa) un stiku pārvēršana JOG komandās.
#include <Arduino.h>

namespace rc {
  void begin();
  void loop();
  bool hasPriority();     // pults aktīvi vada -> web jogs tiek ignorēts
  String statusLine();    // "#rc link=1 armed=0 ..." web lapai
  String rawLine();       // "#rcraw ch=... btn=... sw=..." - pults dati tiešraidē (cilne "Pults")
  const char *linkName();
}
