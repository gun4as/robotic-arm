#include "storage.h"
#include "config.h"
#include "hal.h"
#include "motion.h"
#include "gripper.h"
#include "tmc2209.h"
#include "ui.h"
#include "sdcard.h"
#include <string.h>

namespace storage {
namespace {

constexpr uint32_t kMagic = 0x314B5252UL;   // "RRK1"
constexpr uint16_t kVersion = 3;          // 2: + ekrāns/SD,  3: + motora soļi/mikrosoļi/reduktors

struct __attribute__((packed)) JointRec {
  float minDeg, maxDeg, maxSpeed, accel, homePos, homeSpeed, backoff;
  uint16_t currentMa;
  uint8_t invert, esEnabled, esInvert;
  int8_t homeDir;
  uint8_t homeOrder;
};

struct __attribute__((packed)) Header {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
};

struct __attribute__((packed)) Body {
  JointRec j[NUM_JOINTS];
  uint16_t gripOpenUs, gripClosedUs;
};

// 1. versija (vecāks firmware) - ielasām, lai saglabātie iestatījumi nepazūd
struct __attribute__((packed)) RecordV1 {
  Header h;
  Body b;
  uint16_t crc;
};

// 2. versija
struct __attribute__((packed)) RecordV2 {
  Header h;
  Body b;
  uint8_t lcdOn, sdOn;
  uint16_t crc;
};

// Piedziņa katrai locītavai (no 3. versijas)
struct __attribute__((packed)) DriveRec {
  uint16_t motorSteps, microsteps;
  float gear;
};

struct __attribute__((packed)) Record {
  Header h;
  Body b;
  uint8_t lcdOn, sdOn;
  DriveRec d[NUM_JOINTS];
  uint16_t crc;
};

uint16_t crc16(const uint8_t *d, uint16_t n);

template <typename T>
bool crcOk(const T &r) {
  return r.h.size == sizeof(T) && r.crc == crc16(reinterpret_cast<const uint8_t *>(&r), sizeof(T) - 2);
}

uint16_t crc16(const uint8_t *d, uint16_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*d++) << 8;
    for (uint8_t k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

}  // namespace

void apply(const Body &b) {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    motion::JointConfig &c = motion::joint(j);
    const JointRec &s = b.j[j];
    c.minDeg = s.minDeg;
    c.maxDeg = s.maxDeg;
    c.maxSpeed = s.maxSpeed > 0 ? s.maxSpeed : c.maxSpeed;
    c.accel = s.accel > 0 ? s.accel : c.accel;
    c.homePos = s.homePos;
    c.homeSpeed = s.homeSpeed > 0 ? s.homeSpeed : c.homeSpeed;
    c.backoff = s.backoff;
    c.invert = s.invert;
    c.esEnabled = s.esEnabled;
    c.esInvert = s.esInvert;
    c.homeDir = s.homeDir < 0 ? -1 : 1;
    c.homeOrder = s.homeOrder;
    tmc::presetCurrent(j, s.currentMa);
  }
  gripper::setLimits(b.gripOpenUs, b.gripClosedUs);
  motion::configChanged();
}

bool load() {
  union {
    Record v3;
    RecordV2 v2;
    RecordV1 v1;
  } r;
  hal::storageRead(&r, sizeof r);
  const Header &h = r.v3.h;
  if (h.magic != kMagic) return false;
  if (h.version == 3 && crcOk(r.v3)) {
    for (uint8_t j = 0; j < NUM_JOINTS; j++)   // vispirms piedziņa (soļi/°), tad pārējais
      motion::setDrive(j, r.v3.d[j].motorSteps, r.v3.d[j].microsteps, r.v3.d[j].gear);
    apply(r.v3.b);
    ui::setEnabled(r.v3.lcdOn);
    sdcard::setEnabled(r.v3.sdOn);
    return true;
  }
  if (h.version == 2 && crcOk(r.v2)) {   // piedziņa paliek no config.h
    apply(r.v2.b);
    ui::setEnabled(r.v2.lcdOn);
    sdcard::setEnabled(r.v2.sdOn);
    return true;
  }
  if (h.version == 1 && crcOk(r.v1)) {   // ekrāns/SD un piedziņa paliek noklusējuma
    apply(r.v1.b);
    return true;
  }
  return false;
}
bool save() {
  Record r;
  memset(&r, 0, sizeof r);
  r.h.magic = kMagic;
  r.h.version = kVersion;
  r.h.size = sizeof r;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const motion::JointConfig &c = motion::joint(j);
    JointRec &s = r.b.j[j];
    s.minDeg = c.minDeg;
    s.maxDeg = c.maxDeg;
    s.maxSpeed = c.maxSpeed;
    s.accel = c.accel;
    s.homePos = c.homePos;
    s.homeSpeed = c.homeSpeed;
    s.backoff = c.backoff;
    s.currentMa = tmc::current(j);
    s.invert = c.invert;
    s.esEnabled = c.esEnabled;
    s.esInvert = c.esInvert;
    s.homeDir = c.homeDir;
    s.homeOrder = c.homeOrder;
  }
  r.b.gripOpenUs = gripper::openUs();
  r.b.gripClosedUs = gripper::closedUs();
  r.lcdOn = ui::enabled();
  r.sdOn = sdcard::enabled();
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const motion::JointConfig &c = motion::joint(j);
    r.d[j].motorSteps = c.motorSteps;
    r.d[j].microsteps = c.microsteps;
    r.d[j].gear = c.gear;
  }
  r.crc = crc16(reinterpret_cast<const uint8_t *>(&r), sizeof r - 2);
  if (!hal::storageWrite(&r, sizeof r)) return false;

  Record check;
  hal::storageRead(&check, sizeof check);
  return memcmp(&r, &check, sizeof r) == 0;
}

void factory() {
  motion::loadDefaults();
  const uint16_t cur[NUM_JOINTS] = TMC_RUN_CURRENT_MA;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) tmc::presetCurrent(j, cur[j]);
  gripper::setLimits(GRIPPER_OPEN_US, GRIPPER_CLOSED_US);
  motion::configChanged();
  ui::setEnabled(LCD_DEFAULT_ON);
  sdcard::setEnabled(SD_DEFAULT_ON);
  if (tmc::available()) tmc::init();
}

}  // namespace storage
