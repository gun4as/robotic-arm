#include "ui.h"
#include "config.h"
#include "hal.h"
#include "lcd.h"
#include "motion.h"
#include "gripper.h"
#include "runner.h"
#include "sdcard.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>

#if LCD_ENABLED

namespace ui {
namespace {

// ------------------------------------------------------------------ ievade
FastPin pinA, pinB, pinBtn, beeper;
#ifdef KILL_PIN
FastPin pinKill;
#endif
volatile bool active = false;     // ekrāns pieslēgts un inicializēts
bool pinsReady = false;
volatile int16_t encRaw = 0;
uint8_t encState = 0;
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

int8_t injDetents = 0;
bool injClick = false, injKill = false;

bool btnDown = false;
uint32_t btnMs = 0, beepUntil = 0, lastInputMs = 0, lastRenderMs = 0;
#ifdef KILL_PIN
bool killDown = false;
uint32_t killMs = 0;
#endif

// ------------------------------------------------------------------ stāvoklis
enum class Scr : uint8_t { Status, Menu, JointSel, Jog, Edit, Files, RunOpts, Confirm, Info };
enum Item : uint8_t { I_BACK, I_PAUSE, I_STOP, I_JOG, I_STEP, I_SPEED, I_GRIP, I_SD, I_HOME, I_ZERO,
                      I_MOTORS, I_RESET, I_INFO };
enum class Edit : uint8_t { Speed, Grip };
enum class Ask : uint8_t { Zero, MotorsOff };

Scr scr = Scr::Status;
uint8_t sel = 0, top = 0;
uint8_t jogJoint = 0;
Edit editWhat = Edit::Speed;
Ask askWhat = Ask::Zero;
const float kSteps[] = {0.1f, 1.0f, 5.0f, 10.0f};
uint8_t stepIdx = 1;
uint8_t speedPct = 30;
char fileSel[13] = "";
char msg[lcd::COLS + 1] = "";
uint32_t msgUntil = 0;
uint8_t items[16];
uint8_t itemCount = 0;

void beep(uint16_t ms) {
  beeper.writeAtomic(true);
  beepUntil = millis() + ms;
}

// ------------------------------------------------------------------ formatēšana (AVR printf neprot %f)
// out jābūt vismaz max(width, 13) + 1 baitu garam
void fmtFixed(char *out, uint8_t width, float v, uint8_t dec) {
  if (width > 12) width = 12;
  const bool neg = v < 0;
  if (neg) v = -v;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < dec; i++) scale *= 10;
  uint32_t n = (uint32_t)(v * scale + 0.5f);
  char tmp[16];
  uint8_t len = 0;
  do {
    if (dec && len == dec) tmp[len++] = '.';
    tmp[len++] = (char)('0' + n % 10);
    n /= 10;
  } while (n || len <= dec);
  if (neg) tmp[len++] = '-';
  uint8_t pad = width > len ? width - len : 0;
  uint8_t o = 0;
  while (pad--) out[o++] = ' ';
  while (len) out[o++] = tmp[--len];
  out[o] = 0;
}

const char *errText(motion::Result r) {
  switch (r) {
    case motion::Result::NotZeroed: return "Nav nulles";
    case motion::Result::Limit: return "Arpus limita!";
    case motion::Result::QueueFull: return "Rinda pilna";
    case motion::Result::Alarm: return "Avarija - RESET";
    case motion::Result::Busy: return "Aiznemts";
    case motion::Result::NoEndstop: return "Nav gala sledzu";
    default: return "Kluda";
  }
}

void showMsg(const char *m, uint16_t ms = 3000) {
  strncpy(msg, m, lcd::COLS);
  msg[lcd::COLS] = 0;
  msgUntil = millis() + ms;
}

void check(motion::Result r) {
  if (r != motion::Result::Ok) { showMsg(errText(r)); beep(120); }
}

const char *stateText() {
  if (runner::state() == runner::State::Paused) return "PAUZE";
  switch (motion::state()) {
    case motion::State::Idle: return "GATAVS";
    case motion::State::Jog: return "JOGS";
    case motion::State::Move: return "KUSTIBA";
    case motion::State::Home: return "HOME...";
    case motion::State::Alarm: return "AVARIJA!";
  }
  return "?";
}

// ------------------------------------------------------------------ izvēlnes
void buildMenu() {
  itemCount = 0;
  items[itemCount++] = I_BACK;
  if (runner::active()) { items[itemCount++] = I_PAUSE; items[itemCount++] = I_STOP; }
  const uint8_t rest[] = {I_JOG, I_STEP, I_SPEED, I_GRIP, I_SD, I_HOME, I_ZERO, I_MOTORS, I_RESET, I_INFO};
  for (uint8_t i = 0; i < sizeof rest; i++) items[itemCount++] = rest[i];
}

void itemLabel(uint8_t id, char *o) {
  char v[16];
  switch (id) {
    case I_BACK: strcpy(o, "< Atpakal"); break;
    case I_PAUSE: strcpy(o, runner::state() == runner::State::Paused ? "Progr.: turpinat" : "Progr.: pauze"); break;
    case I_STOP: strcpy(o, "Progr.: apturet"); break;
    case I_JOG: strcpy(o, "Kustinat locitavu"); break;
    case I_STEP:
      fmtFixed(v, 0, kSteps[stepIdx], stepIdx == 0 ? 1 : 0);
      snprintf(o, 21, "Solis: %.6s%c", v, lcd::DEG);
      break;
    case I_SPEED: snprintf(o, 21, "Atrums: %u%%", speedPct); break;
    case I_GRIP: snprintf(o, 21, "Satverejs: %d%%", (int)(gripper::percent() + 0.5f)); break;
    case I_SD: strcpy(o, "Programma no SD"); break;
    case I_HOME: strcpy(o, "HOME visas"); break;
    case I_ZERO: strcpy(o, "Nulle visam"); break;
    case I_MOTORS: strcpy(o, motion::enabled() ? "Motori: atslegt" : "Motori: ieslegt"); break;
    case I_RESET: strcpy(o, "RESET (avarija)"); break;
    case I_INFO: strcpy(o, "Info"); break;
    default: o[0] = 0;
  }
}

uint8_t listCount() {
  switch (scr) {
    case Scr::Menu: return itemCount;
    case Scr::JointSel: return NUM_JOINTS + 1;
    case Scr::Files: return sdcard::available() ? (uint8_t)(sdcard::count() + 1) : 1;
    case Scr::RunOpts: return 3;
    case Scr::Confirm: return 2;
    default: return 0;
  }
}

void listLabel(uint8_t i, char *o) {
  char v[16];
  if (i == 0 && scr != Scr::Menu) { strcpy(o, scr == Scr::Confirm ? "< Ne" : "< Atpakal"); return; }
  switch (scr) {
    case Scr::Menu: itemLabel(items[i], o); break;
    case Scr::JointSel:
      fmtFixed(v, 8, motion::positionDeg(i - 1), 2);
      snprintf(o, 21, "J%u %.9s%c%s", i, v, lcd::DEG, motion::isZeroed(i - 1) ? "" : " ?");
      break;
    case Scr::Files:
      if (!sdcard::nameAt(i - 1, o)) o[0] = 0;
      break;
    case Scr::RunOpts: strcpy(o, i == 1 ? "Palaist vienreiz" : "Atkartot bezgaligi"); break;
    case Scr::Confirm: strcpy(o, askWhat == Ask::Zero ? "Ja, nulle visam" : "Ja, atslegt"); break;
    default: o[0] = 0;
  }
}

void renderList() {
  const uint8_t n = listCount();
  if (sel >= n) sel = n ? n - 1 : 0;
  if (sel < top) top = sel;
  if (sel >= top + lcd::ROWS) top = (uint8_t)(sel - lcd::ROWS + 1);
  for (uint8_t r = 0; r < lcd::ROWS; r++) {
    char line[40] = "";
    const uint8_t i = top + r;
    if (i < n) {
      char lab[lcd::COLS + 1];
      listLabel(i, lab);
      if (scr == Scr::Files && !sdcard::available() && i == 0) strcpy(lab, "< Nav SD kartes");
      snprintf(line, sizeof line, "%c%s", i == sel ? '>' : ' ', lab);
    }
    lcd::setRow(r, line);
  }
}

void renderStatus() {
  char line[40], a[16], b[16];
  if (msgUntil && (int32_t)(millis() - msgUntil) < 0) {
    lcd::setRow(0, msg);
  } else {
    msgUntil = 0;
    if (runner::active())
      snprintf(line, sizeof line, "%-9s L%u c%u", stateText(), runner::line(), runner::cycle());
    else {
      char z[NUM_JOINTS + 1];
      for (uint8_t j = 0; j < NUM_JOINTS; j++) z[j] = motion::isZeroed(j) ? '1' : '0';
      z[NUM_JOINTS] = 0;
      snprintf(line, sizeof line, "%-9s N%s M%c", stateText(), z, motion::enabled() ? '1' : '0');
    }
    lcd::setRow(0, line);
  }
  for (uint8_t r = 0; r < 2; r++) {
    fmtFixed(a, 7, motion::positionDeg(r * 2), 1);
    fmtFixed(b, 7, motion::positionDeg(r * 2 + 1), 1);
    snprintf(line, sizeof line, "J%u%s  J%u%s", r * 2 + 1, a, r * 2 + 2, b);
    lcd::setRow(r + 1, line);
  }
  fmtFixed(a, 7, motion::positionDeg(4), 1);
  snprintf(line, sizeof line, "J5%s  G%3d%% %s", a, (int)(gripper::percent() + 0.5f),
           sdcard::available() ? "SD" : "");
  lcd::setRow(3, line);
}

void renderJog() {
  char line[40], v[16];
  snprintf(line, sizeof line, "Kustina J%u%s", jogJoint + 1, motion::isZeroed(jogJoint) ? "" : " (bez 0)");
  lcd::setRow(0, line);
  fmtFixed(v, 8, motion::positionDeg(jogJoint), 2);
  snprintf(line, sizeof line, "Poz:%s%c", v, lcd::DEG);
  lcd::setRow(1, line);
  fmtFixed(v, 0, kSteps[stepIdx], stepIdx == 0 ? 1 : 0);
  snprintf(line, sizeof line, "Solis %s%c Atr %u%%", v, lcd::DEG, speedPct);
  lcd::setRow(2, line);
  lcd::setRow(3, (msgUntil && (int32_t)(millis() - msgUntil) < 0) ? msg : "Griez=kust klik=OK");
}

void renderEdit() {
  char line[40];
  lcd::setRow(0, editWhat == Edit::Speed ? "Atrums" : "Satverejs");
  lcd::setRow(1, "");
  snprintf(line, sizeof line, "   %3u %%", editWhat == Edit::Speed ? speedPct : (unsigned)(gripper::percent() + 0.5f));
  lcd::setRow(2, line);
  lcd::setRow(3, "klik = gatavs");
}

void renderInfo() {
  lcd::setRow(0, FIRMWARE_NAME " " FIRMWARE_VERSION);
  lcd::setRow(1, BOARD_NAME);
  char line[40], v[16];
  fmtFixed(v, 0, motion::joint(0).stepsPerDeg, 1);
  snprintf(line, sizeof line, "%s solu/%c (J1)", v, lcd::DEG);
  lcd::setRow(2, line);
  lcd::setRow(3, "klik = atpakal");
}

void render() {
  switch (scr) {
    case Scr::Status: renderStatus(); break;
    case Scr::Jog: renderJog(); break;
    case Scr::Edit: renderEdit(); break;
    case Scr::Info: renderInfo(); break;
    default: renderList(); break;
  }
}

void go(Scr s, uint8_t selection = 0) {
  scr = s;
  sel = selection;
  top = 0;
  if (s == Scr::Menu) buildMenu();
}

// ------------------------------------------------------------------ darbības
void activate() {
  switch (scr) {
    case Scr::Status: go(Scr::Menu, 1); break;

    case Scr::Menu:
      switch (items[sel]) {
        case I_BACK: go(Scr::Status); break;
        case I_PAUSE:
          if (runner::state() == runner::State::Paused) runner::resume(); else runner::pause();
          go(Scr::Status);
          break;
        case I_STOP: runner::stop("ekrans"); motion::stop(); go(Scr::Status); break;
        case I_JOG: go(Scr::JointSel, 1); break;
        case I_STEP: stepIdx = (uint8_t)((stepIdx + 1) % (sizeof kSteps / sizeof kSteps[0])); break;
        case I_SPEED: editWhat = Edit::Speed; go(Scr::Edit); break;
        case I_GRIP: editWhat = Edit::Grip; go(Scr::Edit); break;
        case I_SD: go(Scr::Files, sdcard::available() && sdcard::count() ? 1 : 0); break;
        case I_HOME: check(motion::home(-1)); go(Scr::Status); break;
        case I_ZERO: askWhat = Ask::Zero; go(Scr::Confirm); break;
        case I_MOTORS:
          if (motion::enabled()) { askWhat = Ask::MotorsOff; go(Scr::Confirm); }
          else { check(motion::setEnabled(true)); go(Scr::Status); }
          break;
        case I_RESET: check(motion::reset()); go(Scr::Status); break;
        case I_INFO: go(Scr::Info); break;
      }
      break;

    case Scr::JointSel:
      if (sel == 0) go(Scr::Menu, 1);
      else { jogJoint = sel - 1; go(Scr::Jog); }
      break;

    case Scr::Jog: go(Scr::JointSel, jogJoint + 1); break;
    case Scr::Edit: go(Scr::Menu); break;
    case Scr::Info: go(Scr::Menu); break;

    case Scr::Files:
      if (sel == 0 || !sdcard::available()) { go(Scr::Menu); break; }
      if (sdcard::nameAt(sel - 1, fileSel)) go(Scr::RunOpts, 1);
      break;

    case Scr::RunOpts:
      if (sel == 0) { go(Scr::Files, 1); break; }
      {
        char err[12];
        if (!runner::start(fileSel, sel == 2, err, sizeof err)) {
          showMsg(!strcmp(err, "no_sd") ? "Nav SD kartes" : !strcmp(err, "busy") ? "Jau darbojas" : "Fails nav atverams");
          beep(120);
        }
        go(Scr::Status);
      }
      break;

    case Scr::Confirm:
      if (sel == 1) {
        if (askWhat == Ask::Zero) check(motion::zero(-1));
        else check(motion::setEnabled(false));
      }
      go(Scr::Status);
      break;
  }
}

void turn(int8_t d) {
  switch (scr) {
    case Scr::Status:
      break;
    case Scr::Jog: {
      const motion::Result r = motion::nudge(jogJoint, d * kSteps[stepIdx], speedPct);
      if (r == motion::Result::QueueFull) break;   // griež par ātru - vienkārši izlaižam
      check(r);
      break;
    }
    case Scr::Edit: {
      if (editWhat == Edit::Speed) {
        int v = speedPct + d * 5;
        speedPct = (uint8_t)(v < 5 ? 5 : (v > 100 ? 100 : v));
      } else {
        int v = (int)(gripper::percent() + 0.5f) + d * 5;
        gripper::setPercent((float)(v < 0 ? 0 : (v > 100 ? 100 : v)));
      }
      break;
    }
    case Scr::Info:
      break;
    default: {
      const int n = listCount();
      int s = (int)sel + d;
      sel = (uint8_t)(s < 0 ? 0 : (s >= n ? n - 1 : s));
      break;
    }
  }
}

void killPressed() {
  runner::stop("STOP poga");
  motion::estop();
  protocol::broadcast("[ESTOP]");
  beep(400);
  go(Scr::Status);
}

}  // namespace

// ------------------------------------------------------------------ publiskais
void setEnabled(bool on) {
  if (on == active) return;
  if (!on) {
    active = false;
    beeper.writeAtomic(false);
    return;
  }
  if (!pinsReady) {
    pinA.beginInput(BTN_EN1_PIN, true);
    pinB.beginInput(BTN_EN2_PIN, true);
    pinBtn.beginInput(BTN_ENC_PIN, true);
#ifdef KILL_PIN
    pinKill.beginInput(KILL_PIN, true);
#endif
    beeper.beginOutput(BEEPER_PIN, false);
    pinsReady = true;
  }
  encState = (uint8_t)((pinA.read() ? 2 : 0) | (pinB.read() ? 1 : 0));
  encRaw = 0;
  injDetents = 0;
  injClick = injKill = false;
  btnDown = !pinBtn.read();      // ja poga jau nospiesta - tas nav klikšķis
#ifdef KILL_PIN
  killDown = !pinKill.read();
#endif
  lcd::init();
  lcd::setRow(0, FIRMWARE_NAME " " FIRMWARE_VERSION);
  lcd::setRow(1, BOARD_NAME);
  scr = Scr::Status;
  lastRenderMs = lastInputMs = millis();
  showMsg("Ekrans ieslegts", 1500);
  active = true;                 // pēdējais: ISR sāk lasīt pogu tikai tagad
}

bool enabled() { return active; }
void encoderIsr() {
  if (!active) return;
  const uint8_t s = (uint8_t)((encState << 2) | (pinA.read() ? 2 : 0) | (pinB.read() ? 1 : 0));
  encRaw += kQuad[s & 15];
  encState = s & 3;
}

void inject(int8_t detents, bool click, bool kill) {
  injDetents = (int8_t)(injDetents + detents);
  injClick = injClick || click;
  injKill = injKill || kill;
}

void message(const char *m) {
  if (!active) return;
  // "[HOME] J1 ok" -> "HOME J1 ok"
  char t[lcd::COLS + 1];
  uint8_t o = 0;
  for (const char *p = m; *p && o < lcd::COLS; p++)
    if (*p != '[' && *p != ']') t[o++] = *p;
  t[o] = 0;
  showMsg(t);
  if (!strncmp(m, "[ESTOP]", 7) || strstr(m, "kluda") || !strncmp(m, "[ENDSTOP]", 9)) beep(300);
}

void poll() {
  if (!active) return;
  const uint32_t now = millis();

  // grozāmā poga
  int16_t raw;
  {
    CriticalSection cs;
    raw = encRaw;
  }
  int8_t det = (int8_t)(raw / ENCODER_PULSES);
  if (det) {
    CriticalSection cs;
    encRaw -= det * ENCODER_PULSES;
  }
#if ENCODER_REVERSE
  det = -det;
#endif
  det = (int8_t)(det + injDetents);
  injDetents = 0;

  // poga (zems līmenis = nospiesta), atlēcienu filtrs 30 ms
  bool click = injClick;
  injClick = false;
  const bool down = !pinBtn.read();
  if (down != btnDown && now - btnMs > 30) {
    btnDown = down;
    btnMs = now;
    if (down) click = true;
  }
  bool kill = injKill;
  injKill = false;
#ifdef KILL_PIN
  const bool kd = !pinKill.read();
  if (kd != killDown && now - killMs > 20) {
    killDown = kd;
    killMs = now;
    if (kd) kill = true;
  }
#endif

  bool changed = false;
  if (kill) { killPressed(); changed = true; }
  if (det) { turn(det); changed = true; }
  if (click) { beep(4); activate(); changed = true; }
  if (changed) lastInputMs = now;

  if (scr != Scr::Status && scr != Scr::Jog && now - lastInputMs > LCD_IDLE_TIMEOUT_MS) go(Scr::Status);
  if (beepUntil && (int32_t)(now - beepUntil) >= 0) { beeper.writeAtomic(false); beepUntil = 0; }

  if (changed || now - lastRenderMs >= 200) {
    lastRenderMs = now;
    render();
  }
  lcd::poll();
}

}  // namespace ui

#else
namespace ui {
void setEnabled(bool) {}
bool enabled() { return false; }
void poll() {}
void encoderIsr() {}
void message(const char *) {}
void inject(int8_t, bool, bool) {}
}
#endif
