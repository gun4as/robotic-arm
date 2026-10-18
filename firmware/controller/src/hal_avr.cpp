// =============================================================================
//  HAL: Arduino Mega 2560 (RAMPS 1.4)
// =============================================================================
#if defined(__AVR__)
#include "hal.h"
#include <avr/interrupt.h>
#include <avr/eeprom.h>

void FastPin::beginOutput(pin_id_t pin, bool level) {
  pin_ = pin;
  uint8_t port = digitalPinToPort(pin);
  out_  = portOutputRegister(port);
  in_   = portInputRegister(port);
  ddr_  = portModeRegister(port);
  mask_ = digitalPinToBitMask(pin);
  writeAtomic(level);
  setOutput();
}

void FastPin::beginInput(pin_id_t pin, bool pullup) {
  pin_ = pin;
  uint8_t port = digitalPinToPort(pin);
  out_  = portOutputRegister(port);
  in_   = portInputRegister(port);
  ddr_  = portModeRegister(port);
  mask_ = digitalPinToBitMask(pin);
  setInput();
  writeAtomic(pullup);  // AVR: PORT bits = pull-up, ja pins ir ieeja
}

void FastPin::setOutput() {
  CriticalSection cs;
  *ddr_ |= mask_;
}

void FastPin::setInput() {
  CriticalSection cs;
  *ddr_ &= (uint8_t)~mask_;
}

void FastPin::writeAtomic(bool v) {
  CriticalSection cs;
  write(v);
}

namespace hal {

void init() {}

// Timer2 (8 bitu, CTC). Timer1/3/4/5 rezervē Servo bibliotēka, Timer0 - millis().
void startStepTimer(uint32_t hz) {
  CriticalSection cs;
  TCCR2A = _BV(WGM21);                 // CTC
  TCCR2B = _BV(CS21);                  // /8 -> 2 MHz
  uint32_t top = (F_CPU / 8UL) / hz - 1;
  if (top > 255) top = 255;
  OCR2A = (uint8_t)top;
  TCNT2 = 0;
  TIMSK2 = _BV(OCIE2A);
}

void idle() {}

void storageRead(void *buf, uint16_t len) { eeprom_read_block(buf, (const void *)0, len); }

bool storageWrite(const void *buf, uint16_t len) {
  eeprom_update_block(buf, (void *)0, len);   // raksta tikai mainītos baitus
  return true;
}

void cycleCounterInit() {}
uint32_t cycles() { return micros() * (F_CPU / 1000000UL); }
uint32_t cyclesPerSecond() { return F_CPU; }

}  // namespace hal

ISR(TIMER2_COMPA_vect) {
  stepper_isr();
}

#endif  // __AVR__
