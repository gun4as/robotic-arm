#pragma once
// Satvērēja servo (0% = atvērts, 100% = aizvērts)
#include <stdint.h>

namespace gripper {
  void init();
  void setPercent(float pct);
  void setMicros(uint16_t us);
  float percent();
  // Impulsa robežas (iestatāmas ar CFG GRIP_OPEN / GRIP_CLOSED, saglabājas ar SAVE)
  void setLimits(uint16_t openUs, uint16_t closedUs);
  uint16_t openUs();
  uint16_t closedUs();
}
