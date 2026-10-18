#include "protocol.h"
#include "config.h"
#include "boards.h"
#include "motion.h"
#include "gripper.h"
#include "tmc2209.h"
#include "endstops.h"
#include "storage.h"
#include "runner.h"
#include "sdcard.h"
#include "ui.h"
#include "lcd.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#if defined(BOARD_SKR14_TURBO)
  #include <CDCSerial.h>
#endif

// AVR: teksta konstantes glabājam flash atmiņā (RAM ir tikai 8 KB)
#if defined(__AVR__)
  #define FS(x) F(x)
#else
  #define FS(x) (x)
#endif

namespace protocol {
namespace {

constexpr uint8_t kLineMax = 96;
constexpr uint8_t kMaxTokens = 12;

struct Port {
  Stream *s;
  char buf[kLineMax];
  uint8_t len;
  bool overflow;
  bool requireChecksum;   // ESP ports: rindām jābeidzas ar *HH
  // diagnostika (komanda PORTS)
  uint32_t rxBytes, rxLines, csErrors;
  uint16_t autoMs;
  uint32_t lastAuto;
};

Port ports[2];
constexpr uint8_t kNumPorts = 2;

// Iekšējais "ports" programmu izpildei no SD: atbildi ("ok"/"err x") noķer, pārējo izmet.
class CaptureStream : public Stream {
public:
  char last[24];
  char cur[24];
  uint8_t len = 0;
  size_t write(uint8_t c) override {
    if (c == '\n') { cur[len] = 0; memcpy(last, cur, len + 1); len = 0; }
    else if (c != '\r' && len < sizeof cur - 1) cur[len++] = (char)c;
    return 1;
  }
#if defined(__AVR__) || defined(SIM_BUILD)
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
#else
  size_t available() override { return 0; }
  int16_t read() override { return -1; }
  int16_t peek() override { return -1; }
  void flush() override {}
#endif
};
CaptureStream capture;
Port internal;

const char *const kJointNames[NUM_JOINTS] = JOINT_NAMES;

// --------------------------------------------------------------- palīgi
bool parseFloat(const char *s, float &f) {
  char *e;
  f = (float)strtod(s, &e);
  return e != s && *e == 0;
}

bool parseJoint(const char *s, uint8_t &j) {
  if (*s == 'J' || *s == 'j') s++;
  char *e;
  long v = strtol(s, &e, 10);
  if (e == s || *e != 0 || v < 1 || v > NUM_JOINTS) return false;
  j = (uint8_t)(v - 1);
  return true;
}

// "S50" vai "S=50" -> ātrums %
bool parseSpeed(const char *s, float &pct) {
  if (*s != 'S' && *s != 's') return false;
  s++;
  if (*s == '=') s++;
  return parseFloat(s, pct);
}

void upcase(char *s) {
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

// Katra rindas komanda (izņemot JOG) beidzas ar tieši vienu "ok" vai "err ...".
// Tas ļauj PC programmai sūtīt nākamo komandu tikai pēc atbildes.
void ok(Port &p) { p.s->println(FS("ok")); }

void err(Port &p, const char *code) {
  p.s->print(FS("err "));
  p.s->println(code);
}

void reply(Port &p, motion::Result r) {
  if (r == motion::Result::Ok) ok(p);
  else err(p, motion::resultName(r));
}

void status(Print &o) {
  o.print('<');
  o.print(motion::stateName());
  o.print(FS("|P:"));
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    if (j) o.print(',');
    o.print(motion::positionDeg(j), 2);
  }
  o.print(FS("|E:"));
  o.print(motion::enabled() ? 1 : 0);
  o.print(FS("|Z:"));
  for (uint8_t j = 0; j < NUM_JOINTS; j++) o.print(motion::isZeroed(j) ? '1' : '0');
  o.print(FS("|S:"));
  for (uint8_t j = 0; j < NUM_JOINTS; j++) o.print(endstops::stateChar(j));
  o.print(FS("|Q:"));
  o.print(motion::queued());
  o.print(FS("|G:"));
  o.print((int)(gripper::percent() + 0.5f));
  if (runner::active()) {
    o.print(FS("|R:"));
    if (runner::state() == runner::State::Paused) o.print('P');
    o.print(runner::line());
    o.print('/');
    o.print(runner::cycle());
  }
  o.println('>');
}

void printConfig(Print &o) {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const motion::JointConfig &c = motion::joint(j);
    o.print(FS("[CFG] J"));
    o.print(j + 1);
    o.print(FS(" name="));       o.print(kJointNames[j]);
    o.print(FS(" motor_steps=")); o.print(c.motorSteps);
    o.print(FS(" microsteps=")); o.print(c.microsteps);
    o.print(FS(" gear="));       o.print(c.gear, 3);
    o.print(FS(" min="));        o.print(c.minDeg, 2);
    o.print(FS(" max="));        o.print(c.maxDeg, 2);
    o.print(FS(" speed="));      o.print(c.maxSpeed, 2);
    o.print(FS(" accel="));      o.print(c.accel, 2);
    o.print(FS(" invert="));     o.print(c.invert ? 1 : 0);
    o.print(FS(" spd="));        o.print(c.stepsPerDeg, 3);
    o.print(FS(" current="));    o.print(tmc::current(j));
    o.print(FS(" es="));         o.print(c.esEnabled ? 1 : 0);
    o.print(FS(" es_inv="));     o.print(c.esInvert ? 1 : 0);
    o.print(FS(" home_dir="));   o.print((int)c.homeDir);
    o.print(FS(" home_pos="));   o.print(c.homePos, 2);
    o.print(FS(" home_speed=")); o.print(c.homeSpeed, 2);
    o.print(FS(" backoff="));    o.print(c.backoff, 2);
    o.print(FS(" home_order=")); o.println(c.homeOrder);
  }
  o.print(FS("[CFG] grip_open="));
  o.print(gripper::openUs());
  o.print(FS(" grip_closed="));
  o.print(gripper::closedUs());
  o.print(FS(" tmc="));
  o.print(tmc::available() ? 1 : 0);
  o.print(FS(" lcd="));
  o.print(ui::enabled() ? 1 : 0);
  o.print(FS(" sd="));
  o.println(sdcard::enabled() ? 1 : 0);
  o.println(FS("[CFG] end"));
}

void help(Print &o) {
  o.println(FS("[HELP] ? STATUS INFO HELP | JOG v1..v5 (%) | MOVE a1..a5 [S%] | MOVEJ n deg [S%]"));
  o.println(FS("[HELP] REL d1..d5 [S%] | NUDGE n deg [S%] | STOP | ESTOP (!) | RESET | ENABLE | DISABLE"));
  o.println(FS("[HELP] HOME [n] | ZERO [n] | SETPOS n deg | ES | GRIP % | SERVO us | AUTO ms"));
  o.println(FS("[HELP] CFG | CFG key n val | CFG GRIP_OPEN|GRIP_CLOSED us | SAVE | LOAD | FACTORY | TMC [INIT]"));
  o.println(FS("[HELP] CFG keys: MOTOR_STEPS MICROSTEPS GEAR MIN MAX SPEED ACCEL INVERT CURRENT ES ES_INV HOME_DIR HOME_POS HOME_SPEED BACKOFF HOME_ORDER"));
}

bool motionIdle() {
  motion::State s = motion::state();
  return s == motion::State::Idle || s == motion::State::Alarm;
}

// Pozicionālās vērtības locītavām: skaitlis, "_" vai "*" = neaiztikt, "S50" = ātrums
bool parseJointValues(char **tok, uint8_t n, float *val, bool *use, float &speed) {
  uint8_t j = 0;
  for (uint8_t i = 0; i < n; i++) {
    const char *t = tok[i];
    float f;
    if (parseSpeed(t, f)) { speed = f; continue; }
    if (j >= NUM_JOINTS) return false;
    if ((t[0] == '_' || t[0] == '*') && t[1] == 0) { use[j++] = false; continue; }
    if (!parseFloat(t, f)) return false;
    val[j] = f;
    use[j++] = true;
  }
  return true;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// NMEA stila kontrolsumma: "MOVE 10 20*12" (XOR visiem simboliem pirms '*').
// Aizsargā pret pazaudētiem/sabojātiem baitiem UART līnijā (troksnis no motoru
// vadiem, FIFO pārpilde) - citādi "MOVE 45 -20 30" varētu kļūt par "MOVE 45 30".
// Atgriež false, ja rinda jānoraida.
bool verifyChecksum(Port &p, char *line) {
  char *star = strrchr(line, '*');
  if (!star) {
    if (p.requireChecksum) { err(p, "checksum"); return false; }
    return true;
  }
  const int hi = hexDigit(star[1]), lo = hi < 0 ? -1 : hexDigit(star[2]);
  if (hi < 0 || lo < 0 || star[3] != 0) { err(p, "checksum"); return false; }
  uint8_t x = 0;
  for (const char *q = line; q < star; q++) x ^= (uint8_t)*q;
  if (x != (uint8_t)(hi * 16 + lo)) { err(p, "checksum"); return false; }
  *star = 0;
  return true;
}

// CFG <atslēga> <n> <vērtība>  vai  CFG GRIP_OPEN|GRIP_CLOSED <us>
void cfgCommand(Port &p, char **args, uint8_t na) {
  if (na == 0) { printConfig(*p.s); return ok(p); }
  if (!motionIdle()) return err(p, "busy");
  upcase(args[0]);
  const char *k = args[0];
  float v;

  // Vai ekrāns / SD karte ir pieslēgti (saglabā ar SAVE)
  if (na == 2 && (!strcmp(k, "LCD") || !strcmp(k, "SD"))) {
    if (runner::active()) return err(p, "busy");
    if (!parseFloat(args[1], v)) return err(p, "bad_arg");
    if (k[0] == 'L') ui::setEnabled(v != 0);
    else sdcard::setEnabled(v != 0);
    return ok(p);
  }

  if (na == 2 && (!strcmp(k, "GRIP_OPEN") || !strcmp(k, "GRIP_CLOSED"))) {
    if (!parseFloat(args[1], v) || v < 500 || v > 2500) return err(p, "bad_arg");
    if (k[5] == 'O') gripper::setLimits((uint16_t)v, gripper::closedUs());
    else gripper::setLimits(gripper::openUs(), (uint16_t)v);
    gripper::setPercent(gripper::percent());
    return ok(p);
  }

  uint8_t j;
  if (na != 3 || !parseJoint(args[1], j) || !parseFloat(args[2], v)) return err(p, "bad_arg");
  motion::JointConfig &c = motion::joint(j);
  if (!strcmp(k, "MOTOR_STEPS") || !strcmp(k, "MICROSTEPS") || !strcmp(k, "GEAR")) {
    uint16_t ms = c.motorSteps, mi = c.microsteps;
    float g = c.gear;
    if (k[0] == 'G') g = v;
    else if (k[1] == 'O') ms = (uint16_t)v;
    else mi = (uint16_t)v;
    const motion::Result r = motion::setDrive(j, ms, mi, g);
    if (r == motion::Result::Ok && k[1] == 'I' && tmc::available()) tmc::setCurrent(j, tmc::current(j));   // ieraksta jaunos mikrosoļus TMC
    return reply(p, r);
  }
  if (!strcmp(k, "MIN")) { if (v >= c.maxDeg) return err(p, "bad_arg"); c.minDeg = v; }
  else if (!strcmp(k, "MAX")) { if (v <= c.minDeg) return err(p, "bad_arg"); c.maxDeg = v; }
  else if (!strcmp(k, "SPEED")) { if (v <= 0) return err(p, "bad_arg"); c.maxSpeed = v; }
  else if (!strcmp(k, "ACCEL")) { if (v <= 0) return err(p, "bad_arg"); c.accel = v; }
  else if (!strcmp(k, "INVERT")) c.invert = v != 0;
  else if (!strcmp(k, "ES")) c.esEnabled = v != 0;
  else if (!strcmp(k, "ES_INV")) c.esInvert = v != 0;
  else if (!strcmp(k, "HOME_DIR")) c.homeDir = v < 0 ? -1 : 1;
  else if (!strcmp(k, "HOME_POS")) c.homePos = v;
  else if (!strcmp(k, "HOME_SPEED")) { if (v <= 0) return err(p, "bad_arg"); c.homeSpeed = v; }
  else if (!strcmp(k, "BACKOFF")) { if (v < 0) return err(p, "bad_arg"); c.backoff = v; }
  else if (!strcmp(k, "HOME_ORDER")) { if (v < 0 || v > 255) return err(p, "bad_arg"); c.homeOrder = (uint8_t)v; }
  else if (!strcmp(k, "CURRENT")) {
    if (!tmc::available()) return err(p, "no_tmc");
    return tmc::setCurrent(j, (uint16_t)v) ? ok(p) : err(p, "tmc");
  } else return err(p, "bad_arg");
  return reply(p, motion::configChanged());
}

// --------------------------------------------------------------- izpilde
void execute(Port &p, char *line) {
  p.rxLines++;
  if (!verifyChecksum(p, line)) { p.csErrors++; return; }
  char *c = strchr(line, ';');   // komentārs
  if (c) *c = 0;

  char *tok[kMaxTokens];
  uint8_t n = 0;
  for (char *t = strtok(line, " \t,"); t && n < kMaxTokens; t = strtok(nullptr, " \t,")) tok[n++] = t;
  if (!n) return ok(p);
  upcase(tok[0]);
  const char *cmd = tok[0];
  char **args = tok + 1;
  const uint8_t na = (uint8_t)(n - 1);

  if (!strcmp(cmd, "JOG")) {
    // JOG nekad neatbild ar "ok"/"err" (to sūta līdz 25x sekundē bez atbildes gaidīšanas).
    // Kļūdas ziņo kā notikumu "[JOG] kods", lai tās nesajaucas ar citu komandu atbildēm.
    float v[NUM_JOINTS] = {0};
    const char *e = nullptr;
    if (na > NUM_JOINTS) e = "bad_arg";
    for (uint8_t i = 0; i < na && !e; i++)
      if (!parseFloat(args[i], v[i])) e = "bad_arg";
    if (!e) {
      motion::Result r = motion::jog(v);
      if (r != motion::Result::Ok) e = motion::resultName(r);
    }
    if (e) { p.s->print(FS("[JOG] ")); p.s->println(e); }
    return;
  }

  if (!strcmp(cmd, "MOVE") || !strcmp(cmd, "REL")) {
    float val[NUM_JOINTS] = {0};
    bool use[NUM_JOINTS] = {false};
    float speed = 50;
    if (!parseJointValues(args, na, val, use, speed)) return err(p, "bad_arg");
    if (cmd[0] == 'M') return reply(p, motion::queueMove(val, use, speed));
    return reply(p, motion::queueRelative(val, use, speed));
  }

  if (!strcmp(cmd, "MOVEJ")) {
    float val[NUM_JOINTS] = {0};
    bool use[NUM_JOINTS] = {false};
    float speed = 50;
    uint8_t j;
    if (na < 2 || !parseJoint(args[0], j) || !parseFloat(args[1], val[j])) return err(p, "bad_arg");
    if (na > 2 && !parseSpeed(args[2], speed)) return err(p, "bad_arg");
    use[j] = true;
    return reply(p, motion::queueMove(val, use, speed));
  }

  // Viena locītava par deg grādiem (pults trima pogas). Strādā arī bez nulles (lēni).
  if (!strcmp(cmd, "NUDGE")) {
    uint8_t j;
    float d, speed = 30;
    if (na < 2 || !parseJoint(args[0], j) || !parseFloat(args[1], d)) return err(p, "bad_arg");
    if (na > 2 && !parseSpeed(args[2], speed)) return err(p, "bad_arg");
    return reply(p, motion::nudge(j, d, speed));
  }

  if (!strcmp(cmd, "STOP"))    { if (&p != &internal) runner::stop("STOP"); motion::stop(); return ok(p); }
  if (!strcmp(cmd, "ESTOP"))   { runner::stop("ESTOP"); motion::estop(); broadcast("[ESTOP]"); return ok(p); }
  if (!strcmp(cmd, "RESET"))   return reply(p, motion::reset());
  if (!strcmp(cmd, "ENABLE"))  return reply(p, motion::setEnabled(true));
  if (!strcmp(cmd, "DISABLE")) return reply(p, motion::setEnabled(false));
  if (!strcmp(cmd, "STATUS"))  { status(*p.s); return ok(p); }
  if (!strcmp(cmd, "HELP"))    { help(*p.s); return ok(p); }

  if (!strcmp(cmd, "INFO")) {
    p.s->print(FS("[INFO] " FIRMWARE_NAME " " FIRMWARE_VERSION " board=" BOARD_NAME " joints="));
    p.s->print(NUM_JOINTS);
    p.s->print(FS(" tmc="));
    p.s->println(tmc::available() ? 1 : 0);
    return ok(p);
  }

  if (!strcmp(cmd, "HOME") || !strcmp(cmd, "ZERO")) {
    int8_t j = -1;
    uint8_t jj;
    if (na >= 1) {
      if (!parseJoint(args[0], jj)) return err(p, "bad_arg");
      j = (int8_t)jj;
    }
    if (cmd[0] == 'H') return reply(p, motion::home(j));
    const motion::Result r = motion::zero(j);
    if (motion::takeHomeRefChanged()) storage::save();   // HOME turpmāk brauks uz šo vietu
    return reply(p, r);
  }

  if (!strcmp(cmd, "SETPOS")) {
    uint8_t j;
    float d;
    if (na != 2 || !parseJoint(args[0], j) || !parseFloat(args[1], d)) return err(p, "bad_arg");
    const motion::Result r = motion::setPositionDeg(j, d);
    if (motion::takeHomeRefChanged()) storage::save();
    return reply(p, r);
  }

  if (!strcmp(cmd, "ES")) {
    p.s->print(FS("[ES]"));
    for (uint8_t j = 0; j < NUM_JOINTS; j++) {
      p.s->print(FS(" J"));
      p.s->print(j + 1);
      p.s->print('=');
      p.s->print(endstops::stateChar(j));
    }
    p.s->println();
    return ok(p);
  }

  if (!strcmp(cmd, "GRIP")) {
    float f;
    if (na != 1 || !parseFloat(args[0], f)) return err(p, "bad_arg");
    gripper::setPercent(f);
    return ok(p);
  }

  if (!strcmp(cmd, "SERVO")) {
    float f;
    if (na != 1 || !parseFloat(args[0], f)) return err(p, "bad_arg");
    gripper::setMicros((uint16_t)f);
    return ok(p);
  }

  if (!strcmp(cmd, "AUTO")) {
    float f;
    if (na != 1 || !parseFloat(args[0], f) || f < 0) return err(p, "bad_arg");
    p.autoMs = f == 0 ? 0 : (uint16_t)(f < 50 ? 50 : (f > 10000 ? 10000 : f));
    return ok(p);
  }

  if (!strcmp(cmd, "CFG")) return cfgCommand(p, args, na);

  if (!strcmp(cmd, "SAVE")) {
    if (!motionIdle()) return err(p, "busy");
    return storage::save() ? ok(p) : err(p, "storage");
  }
  if (!strcmp(cmd, "LOAD")) {
    if (!motionIdle()) return err(p, "busy");
    if (!storage::load()) return err(p, "storage");
    if (tmc::available()) tmc::init();
    return ok(p);
  }
  if (!strcmp(cmd, "FACTORY")) {
    if (!motionIdle()) return err(p, "busy");
    storage::factory();
    return ok(p);
  }

  if (!strcmp(cmd, "SD")) {
    if (!sdcard::available()) return err(p, "no_sd");
    const uint8_t n = sdcard::count();
    char name[13];
    for (uint8_t i = 0; i < n; i++)
      if (sdcard::nameAt(i, name)) { p.s->print(FS("[SD] ")); p.s->println(name); }
    return ok(p);
  }

  if (!strcmp(cmd, "RUN")) {
    if (na == 0) {
      p.s->print(FS("[RUN] "));
      p.s->print(runner::active() ? runner::file() : "-");
      p.s->print(FS(" L"));
      p.s->print(runner::line());
      p.s->print(FS(" c"));
      p.s->println(runner::cycle());
      return ok(p);
    }
    if (&p == &internal) return err(p, "busy");
    upcase(args[0]);
    bool loop = false;
    if (na > 1) { upcase(args[1]); loop = args[1][0] == 'L'; }
    char e[12];
    return runner::start(args[0], loop, e, sizeof e) ? ok(p) : err(p, e);
  }
  if (!strcmp(cmd, "PAUSE"))  { runner::pause(); return ok(p); }
  if (!strcmp(cmd, "RESUME")) { runner::resume(); return ok(p); }

  // Savienojuma diagnostika: cik baitu/rindu saņemts katrā portā
  if (!strcmp(cmd, "PORTS")) {
    for (uint8_t i = 0; i < kNumPorts; i++) {
      p.s->print(i == 0 ? FS("[PORT] usb") : FS("[PORT] esp"));
      p.s->print(FS(" rx_bytes="));   p.s->print(ports[i].rxBytes);
      p.s->print(FS(" lines="));      p.s->print(ports[i].rxLines);
      p.s->print(FS(" checksum_err=")); p.s->print(ports[i].csErrors);
      p.s->print(FS(" auto_ms="));    p.s->println(ports[i].autoMs);
    }
    return ok(p);
  }

  if (!strcmp(cmd, "LCD")) {
    for (uint8_t r = 0; r < lcd::ROWS; r++) {
      p.s->print(FS("[LCD] |"));
      p.s->print(lcd::row(r));
      p.s->println('|');
    }
    return ok(p);
  }

  // Ekrāna pogas no attāluma: KEY +3 / KEY -1 / KEY C (klikšķis) / KEY K (STOP poga)
  if (!strcmp(cmd, "KEY")) {
    if (na != 1) return err(p, "bad_arg");
    upcase(args[0]);
    if (args[0][0] == 'C') ui::inject(0, true, false);
    else if (args[0][0] == 'K') ui::inject(0, false, true);
    else ui::inject((int8_t)atoi(args[0]), false, false);
    return ok(p);
  }

  if (!strcmp(cmd, "TMC")) {
    if (!motionIdle()) return err(p, "busy");
    if (na == 1) {
      upcase(args[0]);
      if (!strcmp(args[0], "INIT")) tmc::init();
    }
    tmc::report(*p.s);
    return ok(p);
  }

  err(p, "unknown_cmd");
}

void pollPort(Port &p) {
  while (p.s->available() > 0) {
    const int ch = p.s->read();
    if (ch < 0) break;
    const char c = (char)ch;
    p.rxBytes++;
    // Reāllaika simboli: darbojas arī rindas vidū
    if (c == '!') { runner::stop("ESTOP"); motion::estop(); broadcast("[ESTOP]"); continue; }
    if (c == '?') { status(*p.s); continue; }
    if (c == '\n' || c == '\r') {
      if (p.overflow) err(p, "line_too_long");
      else if (p.len) { p.buf[p.len] = 0; execute(p, p.buf); }
      p.len = 0;
      p.overflow = false;
      continue;
    }
    if (p.len < kLineMax - 1) p.buf[p.len++] = c;
    else p.overflow = true;
  }

  if (p.autoMs && (uint32_t)(millis() - p.lastAuto) >= p.autoMs) {
    p.lastAuto = millis();
    status(*p.s);
  }
}

}  // namespace

void init() {
  USB_PORT.begin(USB_BAUD);
  ESP_PORT.begin(ESP_BAUD);
  ports[0].s = &USB_PORT;
  ports[1].s = &ESP_PORT;
  for (uint8_t i = 0; i < kNumPorts; i++) {
    ports[i].len = 0;
    ports[i].overflow = false;
    ports[i].autoMs = 0;
    ports[i].lastAuto = 0;
    ports[i].requireChecksum = false;
  }
#if ESP_REQUIRE_CHECKSUM
  ports[1].requireChecksum = true;
#endif
}

void poll() {
  for (uint8_t i = 0; i < kNumPorts; i++) pollPort(ports[i]);
  char ev[48];
  while (motion::popEvent(ev, sizeof ev)) broadcast(ev);
}

void banner() {
  for (uint8_t i = 0; i < kNumPorts; i++) {
    Print &o = *ports[i].s;
    o.print(FS("[READY] " FIRMWARE_NAME " " FIRMWARE_VERSION " board=" BOARD_NAME " joints="));
    o.println(NUM_JOINTS);
  }
}

void broadcast(const char *msg) {
  for (uint8_t i = 0; i < kNumPorts; i++) ports[i].s->println(msg);
  ui::message(msg);
}

bool executeInternal(const char *line, char *errOut, uint8_t errSize) {
  internal.s = &capture;
  internal.requireChecksum = false;
  internal.autoMs = 0;
  strncpy(internal.buf, line, kLineMax - 1);
  internal.buf[kLineMax - 1] = 0;
  capture.last[0] = 0;
  execute(internal, internal.buf);
  if (!strncmp(capture.last, "err ", 4)) {
    strncpy(errOut, capture.last + 4, errSize - 1);
    errOut[errSize - 1] = 0;
    return false;
  }
  return true;   // "ok" vai JOG (bez atbildes)
}

}  // namespace protocol
