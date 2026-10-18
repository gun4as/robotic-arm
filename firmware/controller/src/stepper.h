#pragma once
// =============================================================================
//  Soļu ģenerators.
//  Fiksētas frekvences taimera ISR ar fāzes akumulatoru (DDA) katrai asij.
//  Galvenā cilpa tikai uzstāda ātrumu (soļi/s, ar zīmi); ISR ģenerē impulsus,
//  skaita pozīciju un nekad neiziet ārpus [lo, hi] robežām.
// =============================================================================
#include <stdint.h>
#include "config.h"

namespace stepper {
  void init();

  // Ātrums soļos/s ar zīmi. Tiek ierobežots līdz maxRate().
  void setRate(uint8_t axis, float stepsPerSec);
  void stopAll();
  float maxRate();

  int32_t position(uint8_t axis);
  void setPosition(uint8_t axis, int32_t steps);   // tikai stāvot!

  // Cietās robežas soļos. ISR atsakās spert soli ārpus tām.
  void setLimits(uint8_t axis, int32_t lo, int32_t hi);
  void clearLimits(uint8_t axis);

  void setInvert(uint8_t axis, bool invert);
  void enable(bool on);
  bool enabled();
}
