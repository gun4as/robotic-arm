#include "endstops.h"
#include "config.h"
#include "hal.h"
#include "motion.h"

namespace endstops {
namespace {
const pin_id_t kPins[NUM_JOINTS] = ENDSTOP_PINS;
FastPin pins[NUM_JOINTS];
uint8_t stable = 0;     // atfiltrētais līmenis (1 = pins augstā līmenī)
uint8_t last = 0;       // iepriekšējais nolasījums
}

void init() {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    pins[j].beginInput(kPins[j], true);
    if (pins[j].read()) stable |= (uint8_t)(1u << j);
  }
  last = stable;
}

void update() {
  uint8_t now = 0;
  for (uint8_t j = 0; j < NUM_JOINTS; j++)
    if (pins[j].read()) now |= (uint8_t)(1u << j);
  // bits mainās tikai, ja divi nolasījumi pēc kārtas sakrīt
  const uint8_t agree = (uint8_t)~(now ^ last);
  stable = (uint8_t)((stable & ~agree) | (now & agree));
  last = now;
}

bool triggered(uint8_t j) {
  const motion::JointConfig &c = motion::joint(j);
  if (!c.esEnabled) return false;
  const bool high = stable & (1u << j);
  // NO slēdzis uz GND: nospiests = zems līmenis.  NC: nospiests = augsts.
  return high == c.esInvert;
}

char stateChar(uint8_t j) {
  if (!motion::joint(j).esEnabled) return '-';
  return triggered(j) ? '1' : '0';
}

}  // namespace endstops
