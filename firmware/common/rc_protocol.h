#pragma once
// =============================================================================
//  Pults <-> roka radio pakešu formāts (kopīgs ESP-NOW un LoRa).
//  Izmanto gan firmware/rc_remote, gan firmware/esp32_bridge.
//  Versija 2: pogas (12 trimi + nefiksējošā), 3 pozīciju slēdži, rokas pozīcija atbildē.
// =============================================================================
#include <stdint.h>
#include <stddef.h>

#define RC_MAGIC          0x52   // 'R'
#define RC_PROTO_VERSION  2
#define RC_NUM_CHANNELS   6
#define RC_NUM_JOINTS     5

enum : uint8_t {
  RC_PKT_CONTROL   = 1,   // pults -> roka
  RC_PKT_TELEMETRY = 2,   // roka -> pults (tikai ESP-NOW)
};

// flags bitmaska
enum : uint8_t {
  RC_FLAG_ARMED = 0x01,   // ARM slēdzis ieslēgts (un bija izslēgts pēc starta)
  RC_FLAG_ESTOP = 0x02,   // avārijas slēdzis
  RC_FLAG_CAL   = 0x04,   // pults kalibrējas - kanāli nederīgi
};

// Kanāli ch[]: -1000..1000 pēc kalibrācijas
enum : uint8_t {
  RC_CH_RX = 0,   // labais stiks ↔
  RC_CH_RY = 1,   // labais stiks ↕
  RC_CH_LY = 2,   // kreisais stiks ↕
  RC_CH_LX = 3,   // kreisais stiks ↔
  RC_CH_POT = 4,  // grozāmā poga (potenciometrs)
  RC_CH_AUX = 5,  // brīvs
};

// buttons biti: 0..11 trima pogas (0..7 = BU4051 X0..X7, 8..11 = tiešās pogas), 12 = nefiksējošā poga
#define RC_BTN_TRIMS   12
#define RC_BTN_MOMENT  12

struct __attribute__((packed)) RcControlPacket {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;          // RC_PKT_CONTROL
  uint16_t netId;         // pāra ID: pults un roka ar vienādu ID
  uint16_t seq;
  int16_t  ch[RC_NUM_CHANNELS];
  uint16_t buttons;       // nospiestās pogas (bits = 1)
  uint8_t  switches;      // 2 pozīciju slēdži SW1..SW5 (bits 0..4 = 1 kad ieslēgts)
  uint8_t  sw3;           // 3 pozīciju slēdži: biti 0-1 = A (0/1/2), biti 2-3 = B (0/1/2)
  uint8_t  flags;
  uint16_t battMv;
  uint16_t crc;           // CRC16 visam iepriekšējam
};

struct __attribute__((packed)) RcTelemetryPacket {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;          // RC_PKT_TELEMETRY
  uint16_t netId;
  uint16_t ackSeq;        // pēdējā saņemtā seq
  uint8_t  armState;      // 1 = rokas puse pieņēma vadību
  uint8_t  ctrlState;     // 0 Nezināms 1 Idle 2 Jog 3 Move 4 Alarm 5 Home
  int16_t  pos[RC_NUM_JOINTS];   // locītavu leņķi, 0.1°
  uint8_t  zeroMask;      // locītavas ar nulli
  uint8_t  esMask;        // nospiestie gala slēdži
  uint8_t  grip;          // satvērējs 0..100 %
  uint8_t  speedPct;      // pults ātrums, ko lieto tilts
  uint16_t crc;
};

// CRC-16/CCITT-FALSE
static inline uint16_t rc_crc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (uint8_t k = 0; k < 8; k++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

template <typename T>
static inline void rc_seal(T &p) {
  p.crc = rc_crc16(reinterpret_cast<const uint8_t *>(&p), sizeof(T) - 2);
}

template <typename T>
static inline bool rc_valid(const T &p, uint8_t type, uint16_t netId) {
  return p.magic == RC_MAGIC && p.version == RC_PROTO_VERSION && p.type == type && p.netId == netId &&
         p.crc == rc_crc16(reinterpret_cast<const uint8_t *>(&p), sizeof(T) - 2);
}
