#include "controller_link.h"
#include "config.h"

namespace ctrl {
namespace {

LineHandler handler = nullptr;
String rx;
String status;
State st = State::Unknown;
uint32_t lastStatusMs = 0;
uint32_t lastAutoReqMs = 0;
// diagnostika (#diag): ko tilts sūtījis/saņēmis no kontroliera
uint32_t txBytes = 0, rxBytes = 0, rxLines = 0, rxOdd = 0;
String lastRx;

// Gaidošo atbilžu rinda: kurš sūtīja komandu (secībā)
constexpr uint8_t kPendMax = 16;
struct Pending { int16_t source; uint32_t ms; };
Pending pend[kPendMax];
uint8_t pendHead = 0, pendCount = 0;

void pushPending(int16_t source) {
  if (pendCount == kPendMax) { pendHead = (uint8_t)((pendHead + 1) % kPendMax); pendCount--; }
  pend[(pendHead + pendCount) % kPendMax] = {source, millis()};
  pendCount++;
}

int16_t popPending() {
  // Ļoti veci ieraksti (komanda pazuda, piem. kontrolieris pārstartējās) - izmetam
  while (pendCount && millis() - pend[pendHead].ms > 8000) {
    pendHead = (uint8_t)((pendHead + 1) % kPendMax);
    pendCount--;
  }
  if (!pendCount) return TO_ALL;
  const int16_t s = pend[pendHead].source;
  pendHead = (uint8_t)((pendHead + 1) % kPendMax);
  pendCount--;
  return s;
}

bool isJog(const String &line) {
  return line.length() >= 3 && (line[0] == 'J' || line[0] == 'j') && (line[1] == 'O' || line[1] == 'o') &&
         (line[2] == 'G' || line[2] == 'g') && (line.length() == 3 || line[3] == ' ');
}

void requestAutoStatus() {
  lastAutoReqMs = millis();
  send("AUTO " + String(CTRL_STATUS_MS));
}

void parseState(const String &line) {
  int bar = line.indexOf('|');
  String s = line.substring(1, bar < 0 ? line.length() - 1 : bar);
  if (s == "Idle") st = State::Idle;
  else if (s == "Jog") st = State::Jog;
  else if (s == "Move") st = State::Move;
  else if (s == "Alarm") st = State::Alarm;
  else if (s == "Home") st = State::Home;
  else st = State::Unknown;
}

void handleLine(const String &line) {
  rxLines++;
  lastRx = line.substring(0, 40);
  int16_t target = TO_ALL;
  if (line.startsWith("<")) {
    status = line;
    lastStatusMs = millis();
    parseState(line);
  } else if (line == "ok" || line.startsWith("err ")) {
    target = popPending();
  } else if (line.startsWith("[READY]")) {
    pendCount = 0;               // kontrolieris pārstartējās - vecās komandas pazuda
    requestAutoStatus();
  }
  if (handler && target != SRC_BRIDGE) handler(line, target);
}

}  // namespace

void begin(LineHandler onLine) {
  handler = onLine;
#if defined(ESP8266)
  CTRL_SERIAL.begin(CTRL_BAUD);
  CTRL_SERIAL.swap();          // RX = D7 (GPIO13), TX = D8 (GPIO15)
#else
  CTRL_SERIAL.begin(CTRL_BAUD, SERIAL_8N1, CTRL_RX_PIN, CTRL_TX_PIN);
#endif
  rx.reserve(160);
  requestAutoStatus();
}

void loop() {
  while (CTRL_SERIAL.available()) {
    char c = (char)CTRL_SERIAL.read();
    rxBytes++;
    if ((c < 32 || c > 126) && c != '\n' && c != '\r') rxOdd++;   // nesalasāmi baiti = trokšņi / nepareizs ātrums
    if (c == '\n' || c == '\r') {
      if (rx.length()) handleLine(rx);
      rx = "";
    } else if (rx.length() < 320) {   // [CFG] rindas ir ~200 simbolu
      rx += c;
    }
  }
  // Kontrolieris pārstartējās vai tikko pieslēgts -> atkal ieslēdzam statusu.
  if (!online() && millis() - lastAutoReqMs > 2000) requestAutoStatus();
  if (!online()) st = State::Unknown;
}

// Katrai rindai pievienojam kontrolsummu "*HH" - kontrolieris noraida bojātas rindas.
void send(const String &line, int16_t source) {
  if (!isJog(line)) pushPending(source);   // JOG nekad neatbild
  uint8_t x = 0;
  for (size_t i = 0; i < line.length(); i++) x ^= (uint8_t)line[i];
  char tail[5];
  snprintf(tail, sizeof tail, "*%02X\n", x);
  CTRL_SERIAL.print(line);
  CTRL_SERIAL.print(tail);
  txBytes += line.length() + 4;
}

void realtime(char c) { CTRL_SERIAL.write((uint8_t)c); }

State state() { return st; }
bool online() { return lastStatusMs && millis() - lastStatusMs < 1000; }
const String &lastStatus() { return status; }

String diagLine() {
  String s = "#diag tx=" + String(txBytes) + " rx=" + String(rxBytes) + " rindas=" + String(rxLines) +
             " nesalasami=" + String(rxOdd) + " pedeja='";
  for (size_t i = 0; i < lastRx.length(); i++) s += (lastRx[i] >= 32 && lastRx[i] <= 126) ? lastRx[i] : '?';
  return s + "'";
}

}  // namespace ctrl
