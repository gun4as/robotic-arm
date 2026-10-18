#include "gripper.h"
#include "config.h"
#include "boards.h"

namespace gripper {
namespace {
uint16_t usOpen = GRIPPER_OPEN_US;
uint16_t usClosed = GRIPPER_CLOSED_US;
float pct = GRIPPER_START_PCT;
}

void setLimits(uint16_t o, uint16_t c) {
  if (o < 500) o = 500;
  if (o > 2500) o = 2500;
  if (c < 500) c = 500;
  if (c > 2500) c = 2500;
  usOpen = o;
  usClosed = c;
}
uint16_t openUs() { return usOpen; }
uint16_t closedUs() { return usClosed; }

}  // namespace gripper

#if GRIPPER_ENABLED
#include <Servo.h>

namespace gripper {
namespace {
Servo servo;
}

void init() {
  servo.attach(GRIPPER_SERVO_PIN);
  setPercent(pct);
}

void setPercent(float p) {
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  pct = p;
  servo.writeMicroseconds((int)(usOpen + ((float)usClosed - (float)usOpen) * p / 100.0f));
}

void setMicros(uint16_t us) {
  if (us < 500) us = 500;
  if (us > 2500) us = 2500;
  servo.writeMicroseconds(us);
  if (usClosed != usOpen) pct = ((float)us - (float)usOpen) * 100.0f / ((float)usClosed - (float)usOpen);
}

float percent() { return pct; }

}  // namespace gripper

#else
namespace gripper {
void init() {}
void setPercent(float) {}
void setMicros(uint16_t) {}
float percent() { return 0; }
}
#endif
