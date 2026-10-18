#include "rc_link.h"
#include "config.h"
#include "controller_link.h"
#include "rc_protocol.h"
#include "settings.h"

#if defined(RC_LINK_ESPNOW) && defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #define RC_TIMEOUT_MS ESPNOW_TIMEOUT_MS
#elif defined(RC_LINK_ESPNOW)
  #include <WiFi.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
  #include <esp_arduino_version.h>
  #define RC_TIMEOUT_MS ESPNOW_TIMEOUT_MS
#elif defined(RC_LINK_LORA) && defined(ESP8266)
  #error "LoRa uz D1 mini netiek atbalstīta (par maz pinu) - izmanto ESP-NOW"
#elif defined(RC_LINK_LORA)
  #include <SPI.h>
  #include <LoRa.h>
  #define RC_TIMEOUT_MS LORA_TIMEOUT_MS
#else
  #error "Definē RC_LINK_ESPNOW vai RC_LINK_LORA (platformio.ini vide)"
#endif

namespace rc {
namespace {

// Funkciju piešķiršana: settings::rcMap() (maina web lapas cilne "Pults", noklusējumi config.h)
inline const settings::RcMap &M() { return settings::rcMap(); }

RcControlPacket pkt;
bool havePkt = false;
uint32_t lastPktMs = 0;
uint32_t pktCount = 0;
int16_t rssi = 0;

bool linkUp = false, armed = false, estop = false, bankB = false;
bool jogging = false;
uint32_t lastJogMs = 0, lastActiveMs = 0, lastStopReqMs = 0;
int lastGrip = -1;          // pēdējais nosūtītais satvērējs, %
int lastPot = -1;           // potenciometra vērtība, kad to pēdējo reizi ņēmām vērā
float lastSpeed = 0;
uint16_t prevButtons = 0;
uint32_t trimNextMs[RC_BTN_TRIMS];
uint32_t momentSince = 0;
bool homeSent = false;
bool shiftUsed = false;     // Shift laikā nospiests trims -> HOME visām nesūtām

// Kontroliera statuss pultij (ekrānam): "<Idle|P:1.00,...|E:1|Z:11111|S:0----|Q:0|G:0>"
struct CtrlView { int16_t pos[NUM_JOINTS]; uint8_t zero, es, grip; } view = {};

void parseStatus(const String &l) {
  int i = l.indexOf("|P:");
  if (i >= 0) {
    const char *c = l.c_str() + i + 3;
    for (uint8_t j = 0; j < NUM_JOINTS; j++) {
      char *end;
      const float v = strtof(c, &end);
      if (end == c) break;
      view.pos[j] = (int16_t)lroundf(constrain(v, -3000.0f, 3000.0f) * 10.0f);
      c = (*end == ',') ? end + 1 : end;
    }
  }
  auto mask = [&](const char *key) -> int {
    const int k = l.indexOf(key);
    if (k < 0) return -1;
    uint8_t m = 0;
    for (uint8_t j = 0; j < NUM_JOINTS; j++)
      if (l.charAt(k + 3 + j) == '1') m |= (uint8_t)(1u << j);
    return m;
  };
  int m = mask("|Z:");
  if (m >= 0) view.zero = (uint8_t)m;
  m = mask("|S:");
  if (m >= 0) view.es = (uint8_t)m;
  i = l.indexOf("|G:");
  if (i >= 0) view.grip = (uint8_t)constrain(l.substring(i + 3).toInt(), 0, 100);
}

// ============================================================ radio slānis
#if defined(RC_LINK_ESPNOW)
// ESP32: atzvanīšana Wi-Fi uzdevumā -> vajag slēdzeni. ESP8266: tā izpildās starp
// loop() izsaukumiem (nav vienlaicīga), slēdzene nav vajadzīga.
#if defined(ESP8266)
  #define RX_LOCK()
  #define RX_UNLOCK()
#else
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  #define RX_LOCK() portENTER_CRITICAL(&mux)
  #define RX_UNLOCK() portEXIT_CRITICAL(&mux)
#endif
volatile bool rxNew = false;
RcControlPacket rxBuf;
uint8_t rxMac[6];
uint8_t peerMac[6];
bool peerKnown = false;

void onRecvCommon(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(RcControlPacket)) return;
  RX_LOCK();
  memcpy(&rxBuf, data, sizeof rxBuf);
  memcpy(rxMac, mac, 6);
  rxNew = true;
  RX_UNLOCK();
}

#if defined(ESP8266)
void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) { onRecvCommon(mac, data, len); }

bool radioBegin() {
  if (esp_now_init() != 0) return false;
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onRecv);
  return true;
}
#else
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  onRecvCommon(info->src_addr, data, len);
}
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len) { onRecvCommon(mac, data, len); }
#endif

bool radioBegin() {
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  return true;
}
#endif

bool radioReceive(RcControlPacket &out, uint8_t mac[6]) {
  bool got = false;
  RX_LOCK();
  if (rxNew) {
    out = rxBuf;
    memcpy(mac, rxMac, 6);
    rxNew = false;
    got = true;
  }
  RX_UNLOCK();
  return got;
}

// Atbilde pultij: tā zina, ka saite strādā (un atrod pareizo Wi-Fi kanālu).
void sendTelemetry(const uint8_t mac[6], uint16_t ackSeq) {
  if (!peerKnown || memcmp(mac, peerMac, 6) != 0) {
    if (peerKnown) esp_now_del_peer(peerMac);
    memcpy(peerMac, mac, 6);
#if defined(ESP8266)
    peerKnown = esp_now_add_peer(peerMac, ESP_NOW_ROLE_COMBO, WiFi.channel(), nullptr, 0) == 0;
#else
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;            // pašreizējais kanāls
    p.ifidx = WIFI_IF_AP;
    p.encrypt = false;
    peerKnown = esp_now_add_peer(&p) == ESP_OK;
#endif
  }
  RcTelemetryPacket t = {};
  t.magic = RC_MAGIC;
  t.version = RC_PROTO_VERSION;
  t.type = RC_PKT_TELEMETRY;
  t.netId = RC_NET_ID;
  t.ackSeq = ackSeq;
  t.armState = armed ? 1 : 0;
  t.ctrlState = (uint8_t)ctrl::state();
  parseStatus(ctrl::lastStatus());
  for (uint8_t j = 0; j < RC_NUM_JOINTS && j < NUM_JOINTS; j++) t.pos[j] = view.pos[j];
  t.zeroMask = view.zero;
  t.esMask = view.es;
  t.grip = view.grip;
  t.speedPct = (uint8_t)(lastSpeed * 100.0f);
  rc_seal(t);
#if defined(ESP8266)
  esp_now_send(peerMac, reinterpret_cast<uint8_t *>(&t), sizeof t);
#else
  esp_now_send(peerMac, reinterpret_cast<const uint8_t *>(&t), sizeof t);
#endif
}

#else  // LoRa
bool radioBegin() {
  SPI.begin(LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI, LORA_PIN_SS);
  LoRa.setPins(LORA_PIN_SS, LORA_PIN_RST, LORA_PIN_DIO0);
  if (!LoRa.begin(LORA_FREQ)) return false;
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(LORA_SYNC);
  LoRa.enableCrc();
  return true;
}

bool radioReceive(RcControlPacket &out, uint8_t *) {
  const int sz = LoRa.parsePacket();
  if (sz <= 0) return false;
  if (sz != (int)sizeof(RcControlPacket)) {
    while (LoRa.available()) LoRa.read();
    return false;
  }
  LoRa.readBytes(reinterpret_cast<uint8_t *>(&out), sizeof out);
  rssi = (int16_t)LoRa.packetRssi();
  return true;
}
#endif

// ============================================================ vadības loģika
float shape(float x) {
  float a = fabsf(x);
  if (a < RC_DEADBAND) return 0;
  a = (a - RC_DEADBAND) / (1.0f - RC_DEADBAND);
  if (a > 1) a = 1;
  a = (1.0f - RC_EXPO) * a + RC_EXPO * a * a * a;
  return x < 0 ? -a : a;
}

void stopJog() {
  ctrl::send("JOG 0 0 0 0 0");
  jogging = false;
}

void handleFlags() {
  const bool e = pkt.flags & RC_FLAG_ESTOP;
  if (e && !estop) ctrl::realtime('!');
  if (!e && estop) ctrl::send("RESET");   // avārijas slēdzis atlaists
  estop = e;

  const bool a = (pkt.flags & RC_FLAG_ARMED) && !(pkt.flags & RC_FLAG_CAL) && !estop;
  if (!a && armed) {
    if (jogging) stopJog();
    lastPot = -1;
  }
  armed = a;
  bankB = pkt.switches & (1 << SW_BANK);
  const uint8_t lvl = pkt.sw3 & 3;
  lastSpeed = M().speed[lvl < 3 ? lvl : 1] / 100.0f;
}

void setGrip(int g) {
  g = constrain(g, 0, 100);
  if (g == lastGrip) return;
  ctrl::send("GRIP " + String(g));
  lastGrip = g;
}

// Trima poga: locītava par RC_TRIM_STEP_DEG (turot - atkārto) vai satvērējs par RC_GRIP_STEP
void trimAction(uint8_t i) {
  const int8_t joint = M().trimJoint[i], dir = M().trimDir[i];
  if (joint == 6) {
    setGrip((lastGrip < 0 ? view.grip : lastGrip) + dir * M().gripStep);
  } else if (joint >= 1 && joint <= NUM_JOINTS) {
    if (jogging) return;   // kamēr stiki kustina, soļus nesūtām
    ctrl::send("NUDGE " + String(joint) + " " + String(dir * M().trimStep10 / 10.0f, 2) + " S" +
               String((int)(lastSpeed * 100)));
  }
  lastActiveMs = millis();
}

void doButtons(uint32_t now) {
  const uint16_t b = armed ? pkt.buttons : 0;
  const uint16_t mb = (uint16_t)(1u << RC_BTN_MOMENT);
  const bool shift = b & mb;   // nefiksējošā poga = "Shift"
  if (shift && !(prevButtons & mb)) { momentSince = now; homeSent = false; shiftUsed = false; }

  for (uint8_t i = 0; i < RC_BTN_TRIMS; i++) {
    const uint16_t bit = (uint16_t)(1u << i);
    if (!(b & bit)) continue;
    const bool first = !(prevButtons & bit);
    if (shift) {
      // Shift + locītavas trims: "+" = nulle šeit, "-" = HOME šai locītavai (tikai vienreiz uz spiedienu)
      const int8_t joint = M().trimJoint[i];
      if (first && joint >= 1 && joint <= NUM_JOINTS) {
        if (jogging) stopJog();
        ctrl::send((M().trimDir[i] > 0 ? "ZERO " : "HOME ") + String(joint));
        shiftUsed = true;
      }
      continue;
    }
    if (first) {
      trimAction(i);
      trimNextMs[i] = now + RC_TRIM_DELAY_MS;
    } else if ((int32_t)(now - trimNextMs[i]) >= 0) {
      trimAction(i);
      trimNextMs[i] = now + RC_TRIM_REPEAT_MS;
    }
  }
  // tikai nefiksējošā poga, turēta RC_HOME_HOLD_MS -> HOME visām (avārijas stāvoklī -> RESET)
  if (shift && !shiftUsed && !homeSent && now - momentSince >= RC_HOME_HOLD_MS) {
    if (jogging) stopJog();
    ctrl::send(ctrl::state() == ctrl::State::Alarm ? "RESET" : "HOME");
    homeSent = true;
  }
  prevButtons = b;
}

void doJog(uint32_t now) {
  if (!armed) return;
  const auto &map = M().bank[bankB ? 1 : 0];

  float v[NUM_JOINTS];
  bool any = false;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    v[j] = 0;
    const int8_t ch = map[j][0];
    if (ch < 0 || ch >= RC_NUM_CHANNELS) continue;
    v[j] = shape(pkt.ch[ch] / 1000.0f) * map[j][1] * lastSpeed * 100.0f;
    if (v[j] != 0) any = true;
  }

  if (any) {
    lastActiveMs = now;
    // Pults pārņem vadību: ja web programma kustina roku, vispirms to apturam.
    if (ctrl::state() == ctrl::State::Move) {
      if (now - lastStopReqMs > 500) { ctrl::send("STOP"); lastStopReqMs = now; }
      return;
    }
    String cmd = "JOG";
    for (uint8_t j = 0; j < NUM_JOINTS; j++) { cmd += ' '; cmd += String(v[j], 1); }
    ctrl::send(cmd);
    jogging = true;
  } else if (jogging) {
    stopJog();
  }

#if RC_GRIPPER_CH >= 0
  // Potenciometrs: tikai, kad to pagriež (citādi satvērēja pogas tiktu pārrakstītas)
  const int g = constrain((pkt.ch[RC_GRIPPER_CH] + 1000) / 20, 0, 100);
  if (lastPot < 0) lastPot = g;
  if (abs(g - lastPot) >= 2) {
    setGrip(g);
    lastPot = g;
  }
#endif
}

}  // namespace

void begin() {
  if (radioBegin()) DBG.printf("[rc] %s gatavs, NET_ID=0x%04X\n", linkName(), RC_NET_ID);
  else DBG.printf("[rc] KLUDA: %s neizdevās inicializēt\n", linkName());
}

void loop() {
  const uint32_t now = millis();
  RcControlPacket p;
  uint8_t mac[6];
  while (radioReceive(p, mac)) {
    if (!rc_valid(p, RC_PKT_CONTROL, RC_NET_ID)) continue;
    pkt = p;
    havePkt = true;
    lastPktMs = now;
    pktCount++;
    handleFlags();
#if defined(RC_LINK_ESPNOW)
    if (pktCount % 5 == 0) sendTelemetry(mac, p.seq);
#endif
  }

  const bool up = havePkt && now - lastPktMs < RC_TIMEOUT_MS;
  if (!up && linkUp) {
    // Saite pazuda: apturam. (Kontroliera JOG sargsuns to izdarītu arī pats.)
    if (jogging) stopJog();
    armed = false;
    lastPot = -1;
    prevButtons = 0;
    DBG.println("[rc] saite pazuda");
  }
  if (up && !linkUp) DBG.println("[rc] saite izveidota");
  linkUp = up;
  if (!linkUp) return;

  if (now - lastJogMs >= 1000 / RC_JOG_HZ) {
    lastJogMs = now;
    doJog(now);
    doButtons(now);
  }
}

bool hasPriority() {
  return linkUp && armed && (jogging || millis() - lastActiveMs < RC_PRIORITY_MS);
}

const char *linkName() {
#if defined(RC_LINK_ESPNOW)
  return "espnow";
#else
  return "lora";
#endif
}

// Pults dati tiešraidē web lapas cilnei "Pults"
String rawLine() {
  char b[140];
  if (!havePkt) return "#rcraw link=0";
  snprintf(b, sizeof b, "#rcraw link=%d ch=%d,%d,%d,%d,%d btn=%u sw=%u s3=%u,%u bat=%u armed=%d", linkUp, pkt.ch[0],
           pkt.ch[1], pkt.ch[2], pkt.ch[3], pkt.ch[4], pkt.buttons, pkt.switches, pkt.sw3 & 3, (pkt.sw3 >> 2) & 3,
           pkt.battMv, armed);
  return String(b);
}

String statusLine() {
  char b[200];
  snprintf(b, sizeof b, "#rc type=%s link=%d armed=%d bank=%c estop=%d speed=%d grip=%d rssi=%d batt=%u pkts=%lu "
           "mode=%u btn=%04X",
           linkName(), linkUp, armed, bankB ? 'B' : 'A', estop, (int)(lastSpeed * 100), lastGrip, rssi,
           havePkt ? pkt.battMv : 0, (unsigned long)pktCount, havePkt ? (pkt.sw3 >> 2) & 3 : 0,
           havePkt ? pkt.buttons : 0);
  return String(b);
}

}  // namespace rc
