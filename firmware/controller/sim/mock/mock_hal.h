#pragma once
// Datora HAL aizstājējs: katram pinam savs "reģistrs", atmiņa = baitu masīvs.
// Iekļauj pēc firmware .cpp failiem (unity build).
#include <map>
#include <vector>

uint64_t g_simUs = 0;

static std::map<int, LPC_GPIO_TypeDef *> g_pinRegs;
static LPC_GPIO_TypeDef *pinReg(int pin) {
  LPC_GPIO_TypeDef *&r = g_pinRegs[pin];
  if (!r) { r = new LPC_GPIO_TypeDef(); r->FIOPIN = 1; }
  return r;
}
// Simulācija uzstāda ieejas līmeni (piem. gala slēdzim)
static void setPinLevel(int pin, bool high) { pinReg(pin)->FIOPIN = high ? 1 : 0; }

void FastPin::beginOutput(pin_id_t p, bool) { pin_ = p; gpio_ = pinReg(p); mask_ = 1; }
void FastPin::beginInput(pin_id_t p, bool) { pin_ = p; gpio_ = pinReg(p); mask_ = 1; }
void FastPin::setOutput() {}
void FastPin::setInput() {}
void FastPin::writeAtomic(bool v) { write(v); }

static std::vector<uint8_t> g_storage(4096, 0xFF);

namespace hal {
void init() {}
void startStepTimer(uint32_t) {}
void idle() {}
void storageRead(void *buf, uint16_t len) { memcpy(buf, g_storage.data(), len); }
bool storageWrite(const void *buf, uint16_t len) { memcpy(g_storage.data(), buf, len); return true; }
void cycleCounterInit() {}
uint32_t cycles() { return 0; }
uint32_t cyclesPerSecond() { return 1; }
}
