#pragma once
// =============================================================================
//  Aparatūras abstrakcija: ātrie pini, soļu taimeris, kritiskās sekcijas.
//  Realizācija: hal_avr.cpp (Mega2560) un hal_lpc176x.cpp (LPC1769).
// =============================================================================
#include <Arduino.h>
#include "boards.h"

#if defined(__AVR__)
  typedef uint8_t pin_id_t;
#else
  typedef int16_t pin_id_t;
#endif

// Soļu ISR ķermenis (stepper.cpp). HAL taimera pārtraukums to izsauc.
void stepper_isr();

// Pins ar tiešu reģistru piekļuvi (digitalWrite ir par lēnu ISR vajadzībām).
// AVR: high()/low() ir read-modify-write. Ja to sauc ārpus ISR uz porta, kuru
// izmanto arī ISR, izmanto writeAtomic().
class FastPin {
public:
  void beginOutput(pin_id_t pin, bool level);
  void beginInput(pin_id_t pin, bool pullup);
  void setOutput();
  void setInput();
  bool valid() const { return mask_ != 0; }

#if defined(__AVR__)
  inline void high() { *out_ |= mask_; }
  inline void low()  { *out_ &= (uint8_t)~mask_; }
  inline bool read() const { return (*in_ & mask_) != 0; }
#else
  inline void high() { gpio_->FIOSET = mask_; }
  inline void low()  { gpio_->FIOCLR = mask_; }
  inline bool read() const { return (gpio_->FIOPIN & mask_) != 0; }
#endif
  inline void write(bool v) { if (v) high(); else low(); }
  void writeAtomic(bool v);

private:
#if defined(__AVR__)
  volatile uint8_t *out_ = nullptr;
  volatile uint8_t *in_ = nullptr;
  volatile uint8_t *ddr_ = nullptr;
  uint8_t mask_ = 0;
#else
  LPC_GPIO_TypeDef *gpio_ = nullptr;
  uint32_t mask_ = 0;
#endif
  pin_id_t pin_ = 0;
};

// RAII kritiskā sekcija (atslēdz pārtraukumus un atjauno iepriekšējo stāvokli).
class CriticalSection {
public:
#if defined(__AVR__)
  CriticalSection() : sreg_(SREG) { cli(); }
  ~CriticalSection() { SREG = sreg_; }
private:
  uint8_t sreg_;
#else
  CriticalSection() : primask_(__get_PRIMASK()) { __disable_irq(); }
  ~CriticalSection() { if (!primask_) __enable_irq(); }
private:
  uint32_t primask_;
#endif
};

namespace hal {
  void init();                       // plates specifiskā inicializācija (USB u.c.)
  void startStepTimer(uint32_t hz);  // palaiž periodisku stepper_isr()
  void idle();                       // izsauc no loop()

  // Pastāvīgā atmiņa iestatījumiem (Mega EEPROM / LPC flash sektors)
  void storageRead(void *buf, uint16_t len);
  bool storageWrite(const void *buf, uint16_t len);

  // Precīza laika skaitītājs bit-bang UART vajadzībām (TMC2209).
  void cycleCounterInit();
  uint32_t cycles();
  uint32_t cyclesPerSecond();
}
