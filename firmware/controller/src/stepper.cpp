#include "stepper.h"
#include "hal.h"
#include "ui.h"

namespace {

// AVR ir 8 bitu: 16 bitu akumulators ir 2x ātrāks un izšķirtspēja
// (STEP_ISR_HZ / 65536 = 0.24 soļi/s) ir pilnīgi pietiekama.
#if defined(__AVR__) || defined(STEPPER_ACC16)
typedef uint16_t acc_t;
constexpr acc_t kMaxInc = 0x8000;
constexpr float kIncPerHz = 65536.0f / (float)STEP_ISR_HZ;
#else
typedef uint32_t acc_t;
constexpr acc_t kMaxInc = 0x80000000UL;
constexpr float kIncPerHz = 4294967296.0f / (float)STEP_ISR_HZ;
#endif
// Maksimālais pieaugums = puse no akumulatora diapazona garantē, ka soļi nekad
// nenotiek divos taktos pēc kārtas: impulss ir augsts vienu takti un zems vismaz vienu.

struct Axis {
  // Raksta galvenā cilpa, lasa ISR
  volatile acc_t inc;        // akumulatora pieaugums vienā ISR taktī
  volatile int8_t dirReq;    // +1 / -1
  volatile int32_t lo, hi;   // cietās robežas (soļi)
  volatile bool invert;
  // Raksta ISR, lasa galvenā cilpa (kritiskajā sekcijā)
  volatile int32_t pos;
  // Tikai ISR
  acc_t acc;
  int8_t dirCur;
  bool stepHigh;
  FastPin step, dir;
};

Axis axes[NUM_JOINTS];
FastPin enablePins[NUM_JOINTS];
bool driversEnabled = false;

const pin_id_t kStepPins[NUM_JOINTS] = STEP_PINS;
const pin_id_t kDirPins[NUM_JOINTS] = DIR_PINS;
const pin_id_t kEnablePins[NUM_JOINTS] = ENABLE_PINS;

constexpr int32_t kNoLimitLo = INT32_MIN + 1;
constexpr int32_t kNoLimitHi = INT32_MAX - 1;

}  // namespace

// Karstākais kods visā firmware: uz AVR izsaukts 16000x sekundē.
void stepper_isr() {
  for (Axis *a = axes; a != axes + NUM_JOINTS; ++a) {
    // iepriekšējā takts impulsu nolaižam (impulsa garums = 1 takts)
    if (a->stepHigh) { a->step.low(); a->stepHigh = false; }

    const acc_t inc = a->inc;
    if (!inc) continue;

    const int8_t want = a->dirReq;
    if (want != a->dirCur) {
      // Virziena maiņa: uzstādam DIR un šajā taktī soli nesperam
      // (draiverim vajag DIR iestatīšanās laiku pirms STEP fronta).
      a->dirCur = want;
      a->dir.write((want > 0) != a->invert);
      continue;
    }

    const acc_t prev = a->acc;
    const acc_t next = (acc_t)(prev + inc);
    a->acc = next;
    if (next >= prev) continue;                  // nav pārpildes -> nav soļa

    if (want > 0) {
      if (a->pos >= a->hi) { a->acc = prev; continue; }   // bloķēts ar robežu
      a->pos = a->pos + 1;
    } else {
      if (a->pos <= a->lo) { a->acc = prev; continue; }
      a->pos = a->pos - 1;
    }
    a->step.high();
    a->stepHigh = true;
  }

#if LCD_ENABLED
  // Ekrāna grozāmā poga: nolasām ~1000x sekundē (galvenā cilpa var būt par lēnu)
  static uint8_t encDiv = 0;
  if (++encDiv >= STEP_ISR_HZ / 1000) {
    encDiv = 0;
    ui::encoderIsr();
  }
#endif
}
namespace stepper {

void init() {
  for (uint8_t i = 0; i < NUM_JOINTS; i++) {
    Axis &a = axes[i];
    a.inc = 0;
    a.dirReq = 1;
    a.dirCur = 0;   // pirmais solis vienmēr vispirms ieraksta DIR
    a.lo = kNoLimitLo;
    a.hi = kNoLimitHi;
    a.pos = 0;
    a.acc = 0;
    a.stepHigh = false;
    a.step.beginOutput(kStepPins[i], false);
    a.dir.beginOutput(kDirPins[i], !a.invert);
    enablePins[i].beginOutput(kEnablePins[i], ENABLE_ACTIVE_LOW ? true : false);  // izslēgts
  }
  driversEnabled = false;
  hal::startStepTimer(STEP_ISR_HZ);
}

float maxRate() { return (float)STEP_ISR_HZ * 0.5f; }

void setRate(uint8_t axis, float rate) {
  if (axis >= NUM_JOINTS) return;
  float r = rate < 0 ? -rate : rate;
  acc_t inc;
  if (r >= maxRate()) inc = kMaxInc;
  else inc = (acc_t)(r * kIncPerHz);
  Axis &a = axes[axis];
  CriticalSection cs;
  a.inc = inc;
  if (inc) a.dirReq = rate < 0 ? -1 : 1;
}

void stopAll() {
  CriticalSection cs;
  for (uint8_t i = 0; i < NUM_JOINTS; i++) axes[i].inc = 0;
}

int32_t position(uint8_t axis) {
  CriticalSection cs;
  return axes[axis].pos;
}

void setPosition(uint8_t axis, int32_t steps) {
  CriticalSection cs;
  axes[axis].pos = steps;
}

void setLimits(uint8_t axis, int32_t lo, int32_t hi) {
  CriticalSection cs;
  axes[axis].lo = lo;
  axes[axis].hi = hi;
}

void clearLimits(uint8_t axis) { setLimits(axis, kNoLimitLo, kNoLimitHi); }

void setInvert(uint8_t axis, bool invert) {
  CriticalSection cs;
  Axis &a = axes[axis];
  a.invert = invert;
  a.dirCur = 0;   // piespiež DIR pina pārrakstīšanu nākamajā solī
}

void enable(bool on) {
  for (uint8_t i = 0; i < NUM_JOINTS; i++)
    enablePins[i].writeAtomic(ENABLE_ACTIVE_LOW ? !on : on);
  driversEnabled = on;
}

bool enabled() { return driversEnabled; }

}  // namespace stepper
