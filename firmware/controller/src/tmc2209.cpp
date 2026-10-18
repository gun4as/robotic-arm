#include "tmc2209.h"
#include "config.h"
#include "hal.h"
#include "protocol.h"
#include "motion.h"

#if defined(TMC_UART_PINS)

namespace tmc {
namespace {

// Reģistri
constexpr uint8_t REG_GCONF = 0x00, REG_IFCNT = 0x02, REG_IOIN = 0x06,
                  REG_IHOLD_IRUN = 0x10, REG_TPOWERDOWN = 0x11, REG_TPWMTHRS = 0x13,
                  REG_CHOPCONF = 0x6C;

constexpr uint32_t GCONF_VALUE =
    (TMC_STEALTHCHOP ? 0UL : (1UL << 2))  // en_SpreadCycle
    | (1UL << 6)                          // pdn_disable: PDN_UART pins tikai UART
    | (1UL << 7)                          // mstep_reg_select: mikrosoļi no reģistra
    | (1UL << 8);                         // multistep_filt

const pin_id_t kPins[NUM_JOINTS] = TMC_UART_PINS;
uint16_t currentMa[NUM_JOINTS] = TMC_RUN_CURRENT_MA;
FastPin pins[NUM_JOINTS];
bool online[NUM_JOINTS];
uint32_t bitCycles = 0;
uint32_t lastPollMs = 0;
uint8_t pollIdx = 0;

uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t b = d[i];
    for (uint8_t k = 0; k < 8; k++) {
      if ((crc >> 7) ^ (b & 1)) crc = (uint8_t)((crc << 1) ^ 0x07);
      else crc = (uint8_t)(crc << 1);
      b >>= 1;
    }
  }
  return crc;
}

inline void waitUntil(uint32_t t) {
  while ((int32_t)(hal::cycles() - t) < 0) {}
}

// Jāsauc kritiskajā sekcijā.
void txBytes(FastPin &p, const uint8_t *d, uint8_t n) {
  p.high();
  p.setOutput();
  uint32_t t = hal::cycles() + bitCycles;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t b = d[i];
    waitUntil(t); p.low(); t += bitCycles;              // starta bits
    for (uint8_t k = 0; k < 8; k++) {
      waitUntil(t); p.write(b & 1); b >>= 1; t += bitCycles;
    }
    waitUntil(t); p.high(); t += bitCycles;             // stopa bits
  }
  waitUntil(t);
}

// Jāsauc kritiskajā sekcijā. Pins jau ir ieeja.
bool rxBytes(FastPin &p, uint8_t *d, uint8_t n, uint32_t firstTimeout) {
  for (uint8_t i = 0; i < n; i++) {
    const uint32_t start = hal::cycles();
    const uint32_t timeout = i == 0 ? firstTimeout : bitCycles * 4;
    while (p.read()) {
      if (hal::cycles() - start > timeout) return false;
    }
    uint32_t t = hal::cycles() + bitCycles + bitCycles / 2;   // 0. bita vidus
    uint8_t b = 0;
    for (uint8_t k = 0; k < 8; k++) {
      waitUntil(t);
      if (p.read()) b |= (uint8_t)(1u << k);
      t += bitCycles;
    }
    waitUntil(t - bitCycles / 2);   // stopa bita sākums
    d[i] = b;
  }
  return true;
}

void writeReg(uint8_t j, uint8_t reg, uint32_t val) {
  uint8_t d[8] = { 0x05, 0x00, (uint8_t)(reg | 0x80),
                   (uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val, 0 };
  d[7] = crc8(d, 7);
  CriticalSection cs;
  txBytes(pins[j], d, 8);
}

bool readReg(uint8_t j, uint8_t reg, uint32_t &val) {
  uint8_t req[4] = { 0x05, 0x00, reg, 0 };
  req[3] = crc8(req, 3);
  uint8_t r[8];
  bool ok;
  {
    CriticalSection cs;
    txBytes(pins[j], req, 4);
    pins[j].setInput();
    ok = rxBytes(pins[j], r, 8, bitCycles * 40);
    pins[j].high();
    pins[j].setOutput();
  }
  if (!ok || r[0] != 0x05 || r[1] != 0xFF || r[2] != reg || r[7] != crc8(r, 7)) return false;
  val = ((uint32_t)r[3] << 24) | ((uint32_t)r[4] << 16) | ((uint32_t)r[5] << 8) | r[6];
  return true;
}

uint8_t mresFor(uint16_t micro) {
  uint8_t m = 8;   // 1 mikrosolis
  for (uint16_t v = 1; v < micro && m > 0; v <<= 1) m--;
  return m;
}

// TMC2209 strāvas skala (kā TMCStepper rms_current)
void currentRegs(uint16_t mA, uint8_t &irun, uint8_t &ihold, bool &vsense) {
  const float k = 32.0f * 1.41421356f * (TMC_RSENSE + 0.02f) * (mA / 1000.0f);
  float cs = k / 0.325f - 1.0f;
  vsense = false;
  if (cs < 16.0f) { vsense = true; cs = k / 0.180f - 1.0f; }
  if (cs > 31.0f) cs = 31.0f;
  if (cs < 0.0f) cs = 0.0f;
  irun = (uint8_t)cs;
  ihold = (uint8_t)(irun * TMC_HOLD_PCT / 100);
}

uint32_t chopconf(uint8_t j, bool vsense) {
  return 4UL                          // TOFF = 4
       | (0UL << 4)                   // HSTRT = 1
       | (2UL << 7)                   // HEND = -1
       | (1UL << 15)                  // TBL = 24 takti
       | ((vsense ? 1UL : 0UL) << 17)
       | ((uint32_t)mresFor(motion::joint(j).microsteps) << 24)
       | (1UL << 28);                 // intpol: interpolācija līdz 256
}

bool configure(uint8_t j) {
  uint8_t irun, ihold;
  bool vsense;
  currentRegs(currentMa[j], irun, ihold, vsense);
  uint32_t before = 0, after = 0;
  if (!readReg(j, REG_IFCNT, before)) return false;
  writeReg(j, REG_GCONF, GCONF_VALUE);
  writeReg(j, REG_CHOPCONF, chopconf(j, vsense));
  writeReg(j, REG_IHOLD_IRUN, (uint32_t)ihold | ((uint32_t)irun << 8) | (8UL << 16));
  writeReg(j, REG_TPOWERDOWN, 20);
  writeReg(j, REG_TPWMTHRS, 0);
  if (!readReg(j, REG_IFCNT, after)) return false;
  return ((after - before) & 0xFF) == 5;
}

}  // namespace

bool available() { return true; }

void init() {
  hal::cycleCounterInit();
  bitCycles = hal::cyclesPerSecond() / TMC_UART_BAUD;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    pins[j].beginOutput(kPins[j], true);
    online[j] = false;
  }
  delay(2);
  for (uint8_t j = 0; j < NUM_JOINTS; j++) online[j] = configure(j);
}

void poll(bool idle) {
  if (!idle) return;
  const uint32_t now = millis();
  if (now - lastPollMs < 1000) return;
  lastPollMs = now;

  // Katru sekundi pārbaudām vienu draiveri. Ja GCONF neatbilst (draiveris
  // pārstartējies vai tikko ieslēgta 12/24V barošana) - konfigurējam no jauna.
  const uint8_t j = pollIdx;
  pollIdx = (uint8_t)((pollIdx + 1) % NUM_JOINTS);
  uint32_t g;
  const bool ok = readReg(j, REG_GCONF, g);
  char msg[48];
  if (!ok) {
    if (online[j]) {
      online[j] = false;
      snprintf(msg, sizeof msg, "[TMC] J%u: nav atbildes (VM barosana?)", j + 1);
      protocol::broadcast(msg);
    }
    return;
  }
  if ((g & 0x1C4) != (GCONF_VALUE & 0x1C4) || !online[j]) {
    online[j] = configure(j);
    snprintf(msg, sizeof msg, "[TMC] J%u: %s", j + 1, online[j] ? "konfigurets" : "kluda");
    protocol::broadcast(msg);
  }
}

bool setCurrent(uint8_t j, uint16_t mA) {
  if (j >= NUM_JOINTS || mA < 100 || mA > 2000) return false;
  currentMa[j] = mA;
  online[j] = configure(j);
  return online[j];
}

void presetCurrent(uint8_t j, uint16_t mA) {
  if (j < NUM_JOINTS && mA >= 100 && mA <= 2000) currentMa[j] = mA;
}

uint16_t current(uint8_t j) { return currentMa[j]; }

void report(Print &out) {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    uint32_t ioin = 0, gconf = 0, chop = 0;
    const bool ok = readReg(j, REG_IOIN, ioin) && readReg(j, REG_GCONF, gconf) &&
                    readReg(j, REG_CHOPCONF, chop);
    out.print("[TMC] J");
    out.print(j + 1);
    if (!ok) { out.println(" nav atbildes"); continue; }
    out.print(" ver=0x");
    out.print((unsigned)(ioin >> 24), HEX);
    out.print(" gconf=0x");
    out.print((unsigned long)gconf, HEX);
    out.print(" mres=");
    out.print((unsigned)((chop >> 24) & 0xF));
    out.print(" cur=");
    out.print(currentMa[j]);
    out.println(online[j] ? " ok" : " nekonfigurets");
  }
}

}  // namespace tmc

#else  // nav TMC UART (RAMPS)

namespace tmc {
bool available() { return false; }
void init() {}
void poll(bool) {}
bool setCurrent(uint8_t, uint16_t) { return false; }
void presetCurrent(uint8_t, uint16_t) {}
uint16_t current(uint8_t) { return 0; }
void report(Print &out) { out.println("[TMC] sai platei nav TMC UART"); }
}

#endif
