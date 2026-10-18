// =============================================================================
//  Kustību loģikas simulācija uz datora (bez dzelžiem).
//  Kompilē īsto firmware (stepper, motion, protocol, endstops, storage ...)
//  ar mock HAL un pārbauda uzvedību.
//    cd firmware/controller/sim && pio run -e sim && .pio/build/sim/program.exe
// =============================================================================
#include "Arduino.h"
#include "../../src/stepper.cpp"
#include "../../src/motion.cpp"
#include "../../src/endstops.cpp"
#include "../../src/gripper.cpp"
#include "../../src/tmc2209.cpp"
#include "../../src/storage.cpp"
#include "../../src/protocol.cpp"
#include "../../src/lcd.cpp"
#include "../../src/runner.cpp"
#include "../../src/ui.cpp"
#include "mock_hal.h"
#include "mock_sd.h"

MockSerial Serial, Serial1, Serial2;

// ---- simulācijas dzinējs -----------------------------------------------------
static double g_t = 0;                       // µs
static const double kIsrUs = 1e6 / STEP_ISR_HZ;
static int32_t prevPos[NUM_JOINTS];
static double lastChangeUs[NUM_JOINTS];
static int consecutiveViolations = 0;
static int32_t stepsLastTick[NUM_JOINTS];

// "Fiziskā" locītavas pozīcija soļos: mainās tikai no ISR soļiem, nevis no
// ZERO/SETPOS/HOME pozīcijas pārrakstīšanas.
static int64_t phys[NUM_JOINTS];
// Virtuālie gala slēdži (NO uz GND): nospiests, kad phys <= physSwitch
static bool switchPresent[NUM_JOINTS];
static int64_t physSwitch[NUM_JOINTS];
// Tas pats slēdzis otrā galā (locītava griežas ~360°): nospiests arī, kad phys >= physSwitchHi
static bool switchHiPresent[NUM_JOINTS];
static int64_t physSwitchHi[NUM_JOINTS];
static const pin_id_t kSimEsPins[NUM_JOINTS] = ENDSTOP_PINS;

// Ātruma/paātrinājuma uzraudzība ar 50 ms logu (pirmo logu pēc reset izlaižam)
static double winStartUs = 0;
static int32_t winPos[NUM_JOINTS];
static double winVel[NUM_JOINTS];
static double maxVel[NUM_JOINTS], maxAcc[NUM_JOINTS];
static bool winPrimed = false;

static void resetStats() {
  for (int j = 0; j < NUM_JOINTS; j++) {
    maxVel[j] = 0; maxAcc[j] = 0; winVel[j] = 0;
    winPos[j] = stepper::position(j);
    prevPos[j] = winPos[j];
  }
  winStartUs = g_t;
  winPrimed = false;
}

static void step(double us) {
  const double end = g_t + us;
  while (g_t < end) {
    g_t += kIsrUs;
    g_simUs = (uint64_t)g_t;
    stepper_isr();
    for (int j = 0; j < NUM_JOINTS; j++) {
      const int32_t p = stepper::position(j);
      const int32_t d = p - prevPos[j];
      if (d != 0) {
        lastChangeUs[j] = g_t;
        if (d > 1 || d < -1) consecutiveViolations++;
        if (stepsLastTick[j] != 0) consecutiveViolations++;   // soļi divos taktos pēc kārtas
      }
      stepsLastTick[j] = d;
      phys[j] += d;
      setPinLevel(kSimEsPins[j], !((switchPresent[j] && phys[j] <= physSwitch[j]) ||
                                    (switchHiPresent[j] && phys[j] >= physSwitchHi[j])));
    }    motion::update();
    static int pollDiv = 0;
    if (++pollDiv >= 16) {
      pollDiv = 0;
#ifndef SIM_NO_UI
      runner::poll();
      ui::poll();
#endif
    }
    for (int j = 0; j < NUM_JOINTS; j++) prevPos[j] = stepper::position(j);   // ignorē pozīcijas pārrakstīšanu
    if (g_t - winStartUs >= 50000) {
      const double dt = (g_t - winStartUs) * 1e-6;
      for (int j = 0; j < NUM_JOINTS; j++) {
        const double v = (stepper::position(j) - winPos[j]) / dt / motion::joint(j).stepsPerDeg;
        const double a = fabs(v - winVel[j]) / dt;
        if (fabs(v) > maxVel[j]) maxVel[j] = fabs(v);
        if (winPrimed && a > maxAcc[j]) maxAcc[j] = a;
        winVel[j] = v;
        winPos[j] = stepper::position(j);
      }
      winStartUs = g_t;
      winPrimed = true;
    }
  }
}

static bool runUntilIdle(double maxSec) {
  const double end = g_t + maxSec * 1e6;
  while (g_t < end) {
    step(1000);
    if (motion::state() == motion::State::Idle && motion::queued() == 0) return true;
  }
  return false;
}

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { \
  if (cond) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static int32_t stepsOf(int j, float deg) { return lroundf(deg * motion::joint(j).stepsPerDeg); }

// ---- testi ------------------------------------------------------------------
static void testUnzeroed() {
  printf("[test] MOVE bez ZERO -> not_zeroed\n");
  float d[NUM_JOINTS] = {10, 0, 0, 0, 0};
  bool u[NUM_JOINTS] = {true, false, false, false, false};
  CHECK(motion::queueMove(d, u, 50) == motion::Result::NotZeroed, "gaidīju NotZeroed");
}

static void testSyncMove() {
  printf("[test] sinhronizēts MOVE\n");
  CHECK(motion::zero(-1) == motion::Result::Ok, "zero");
  float d[NUM_JOINTS] = {90, 45, -30, 10, 0};
  bool u[NUM_JOINTS] = {true, true, true, true, true};
  resetStats();
  const double t0 = g_t;
  CHECK(motion::queueMove(d, u, 100) == motion::Result::Ok, "queueMove");
  CHECK(runUntilIdle(30), "nebeidzās");
  double tmin = 1e18, tmax = 0;
  for (int j = 0; j < NUM_JOINTS; j++) {
    CHECK(stepper::position(j) == stepsOf(j, d[j]), "J%d pozīcija %ld != %ld", j + 1,
          (long)stepper::position(j), (long)stepsOf(j, d[j]));
    const auto &c = motion::joint(j);
    CHECK(maxVel[j] <= c.maxSpeed * 1.08, "J%d ātrums %.1f > %.1f", j + 1, maxVel[j], c.maxSpeed);
    CHECK(maxAcc[j] <= c.accel * 1.3 + 10, "J%d paātrinājums %.1f > %.1f", j + 1, maxAcc[j], c.accel);
    if (d[j] != 0) {
      tmin = fmin(tmin, lastChangeUs[j]);
      tmax = fmax(tmax, lastChangeUs[j]);
    }
  }
  printf("  ilgums %.2f s, locītavu beigu izkliede %.1f ms\n", (tmax - t0) * 1e-6, (tmax - tmin) * 1e-3);
  CHECK(tmax - tmin < 60000, "locītavas nebeidz vienlaicīgi (%.1f ms)", (tmax - tmin) * 1e-3);
  // J1: 90° ar 45°/s, 60°/s² -> T = 90/45 + 45/60 = 2.75 s
  CHECK(fabs((tmax - t0) * 1e-6 - 2.75) < 0.1, "J1 ilgums %.2f, gaidīju ~2.75", (tmax - t0) * 1e-6);
}

static void testQueue() {
  printf("[test] rinda ar 3 kustībām\n");
  bool u[NUM_JOINTS] = {true, false, false, false, false};
  float a[NUM_JOINTS] = {0}, b[NUM_JOINTS] = {0}, c[NUM_JOINTS] = {0};
  a[0] = 10; b[0] = -20; c[0] = 5;
  CHECK(motion::queueMove(a, u, 100) == motion::Result::Ok, "q1");
  CHECK(motion::queueMove(b, u, 100) == motion::Result::Ok, "q2");
  CHECK(motion::queueMove(c, u, 100) == motion::Result::Ok, "q3");
  int32_t minSeen = INT32_MAX;
  const double end = g_t + 20e6;
  while (g_t < end && !(motion::state() == motion::State::Idle && motion::queued() == 0)) {
    step(1000);
    if (stepper::position(0) < minSeen) minSeen = stepper::position(0);
  }
  CHECK(minSeen == stepsOf(0, -20), "neaizgāja līdz -20 (min %ld)", (long)minSeen);
  CHECK(stepper::position(0) == stepsOf(0, 5), "beigās nav 5°");
  // pārējās locītavas nekustējās
  CHECK(stepper::position(1) == stepsOf(1, 45), "J2 pakustējās");
}

static void testRelative() {
  printf("[test] REL\n");
  float d[NUM_JOINTS] = {0, -5, 0, 0, 0};
  bool u[NUM_JOINTS] = {false, true, false, false, false};
  CHECK(motion::queueRelative(d, u, 100) == motion::Result::Ok, "rel");
  CHECK(motion::queueRelative(d, u, 100) == motion::Result::Ok, "rel2");
  runUntilIdle(10);
  CHECK(stepper::position(1) == stepsOf(1, 35), "J2 %.2f != 35", motion::positionDeg(1));
}

static void testLimitReject() {
  printf("[test] MOVE ārpus limita\n");
  float d[NUM_JOINTS] = {0, 100, 0, 0, 0};
  bool u[NUM_JOINTS] = {false, true, false, false, false};
  CHECK(motion::queueMove(d, u, 50) == motion::Result::Limit, "gaidīju Limit");
}

static void jogFor(const float pct[NUM_JOINTS], double sec) {
  const double end = g_t + sec * 1e6;
  while (g_t < end) {
    motion::jog(pct);
    step(100000);   // 10 Hz kā web UI
  }
}

static void testJogWatchdog() {
  printf("[test] JOG sargsuns\n");
  float p[NUM_JOINTS] = {50, 0, 0, 0, 0};
  resetStats();
  jogFor(p, 1.0);
  CHECK(motion::state() == motion::State::Jog, "nav Jog");
  const int32_t atStop = stepper::position(0);
  // vairs nesūtām JOG -> jāapstājas pēc JOG_TIMEOUT_MS + bremzēšana
  step((JOG_TIMEOUT_MS + 1000) * 1000.0);
  CHECK(motion::state() == motion::State::Idle, "neapstājās");
  const double travelled = (stepper::position(0) - atStop) / motion::joint(0).stepsPerDeg;
  printf("  pēc pēdējās komandas nobrauca %.2f°\n", travelled);
  CHECK(travelled < 22.5 * (JOG_TIMEOUT_MS / 1000.0) + 22.5 * 22.5 / (2 * 60) + 1, "par tālu");
  CHECK(maxAcc[0] <= 60 * 1.3 + 10, "paātrinājums %.1f", maxAcc[0]);
}

static void testJogSoftLimit() {
  printf("[test] JOG pret limitu\n");
  float p[NUM_JOINTS] = {0, 100, 0, 0, 0};
  jogFor(p, 8.0);
  const float pos = motion::positionDeg(1);
  printf("  J2 apstājās pie %.3f° (limits 90)\n", pos);
  CHECK(pos <= 90.0f + 0.01f && pos > 89.0f, "J2 %.3f", pos);
  float z[NUM_JOINTS] = {0};
  motion::jog(z);
  runUntilIdle(3);
  // atpakaļ uz leju no limita jāvar
  float n[NUM_JOINTS] = {0, -50, 0, 0, 0};
  jogFor(n, 0.5);
  motion::jog(z);
  runUntilIdle(3);
  CHECK(motion::positionDeg(1) < 89.0f, "nevar aizbraukt no limita");
}

static void testReverse() {
  printf("[test] JOG virziena maiņa\n");
  float a[NUM_JOINTS] = {0, 0, 100, 0, 0}, b[NUM_JOINTS] = {0, 0, -100, 0, 0};
  resetStats();
  jogFor(a, 1.0);
  jogFor(b, 2.0);
  float z[NUM_JOINTS] = {0};
  motion::jog(z);
  runUntilIdle(5);
  CHECK(maxAcc[2] <= 60 * 1.3 + 10, "J3 paātrinājums %.1f", maxAcc[2]);
  CHECK(consecutiveViolations == 0, "soļu laika pārkāpumi: %d", consecutiveViolations);
}

static void testStopDuringMove() {
  printf("[test] STOP kustības laikā\n");
  float d[NUM_JOINTS] = {-150, 0, 0, 0, 0};
  bool u[NUM_JOINTS] = {true, false, false, false, false};
  const float startDeg = motion::positionDeg(0);
  CHECK(motion::queueMove(d, u, 100) == motion::Result::Ok, "q");
  step(1.5e6);
  const float atStop = motion::positionDeg(0);
  resetStats();
  motion::stop();
  CHECK(runUntilIdle(5), "neapstājās");
  const float endDeg = motion::positionDeg(0);
  printf("  sākums %.1f, STOP pie %.1f, apstājās %.1f\n", startDeg, atStop, endDeg);
  CHECK(endDeg > -150 && endDeg < atStop, "nepareiza bremzēšana");
  CHECK(maxAcc[0] <= 60 * 1.3 + 10, "bremzēšana %.1f", maxAcc[0]);
}

static void testEstop() {
  printf("[test] ESTOP\n");
  float d[NUM_JOINTS] = {100, 0, 0, 0, 0};
  bool u[NUM_JOINTS] = {true, false, false, false, false};
  motion::queueMove(d, u, 100);
  step(1.0e6);
  motion::estop();
  const int32_t p = stepper::position(0);
  step(0.5e6);
  CHECK(stepper::position(0) == p, "kustas pēc ESTOP");
  CHECK(motion::state() == motion::State::Alarm, "nav Alarm");
  float j[NUM_JOINTS] = {10, 0, 0, 0, 0};
  CHECK(motion::jog(j) == motion::Result::Alarm, "jog Alarm laikā");
  CHECK(motion::queueMove(d, u, 50) == motion::Result::Alarm, "move Alarm laikā");
  motion::reset();
  CHECK(motion::state() == motion::State::Idle, "reset");
  CHECK(motion::queueMove(d, u, 100) == motion::Result::Ok, "move pēc reset");
  CHECK(runUntilIdle(10), "nebeidzās");
  CHECK(stepper::position(0) == stepsOf(0, 100), "pēc reset nav 100°");
}

static void testSlowMove() {
  printf("[test] lēns MOVE (S5)\n");
  float d[NUM_JOINTS] = {0, 0, 0, 0, 30};
  bool u[NUM_JOINTS] = {false, false, false, false, true};
  resetStats();
  motion::queueMove(d, u, 5);
  CHECK(runUntilIdle(30), "nebeidzās");
  CHECK(stepper::position(4) == stepsOf(4, 30), "J5");
  CHECK(maxVel[4] <= 90 * 0.05 * 1.1, "ātrums %.2f", maxVel[4]);
}

// ---- protokols ----------------------------------------------------------------
static std::string sendLine(MockSerial &s, const std::string &line) {
  Serial.out.clear();
  Serial1.out.clear();
  s.in += line + "\n";
  protocol::poll();
  for (int j = 0; j < NUM_JOINTS; j++) prevPos[j] = stepper::position(j);   // ZERO/SETPOS nav soļi
  return s.out;
}
static std::string withSum(const std::string &l) {
  uint8_t x = 0;
  for (char c : l) x ^= (uint8_t)c;
  char b[4];
  snprintf(b, sizeof b, "*%02X", x);
  return l + b;
}
static bool has(const std::string &out, const char *what) { return out.find(what) != std::string::npos; }

static void testProtocol() {
  printf("[test] protokols un kontrolsumma\n");
  protocol::init();
  std::string o;
  o = sendLine(Serial, "INFO");
  CHECK(has(o, "[INFO] RobotRoka"), "INFO: %s", o.c_str());
  o = sendLine(Serial1, "STATUS");
  CHECK(has(o, "err checksum"), "ESP bez kontrolsummas: %s", o.c_str());
  o = sendLine(Serial1, withSum("STATUS"));
  CHECK(o.rfind("<Idle|P:", 0) == 0, "ESP ar kontrolsummu: %s", o.c_str());
  o = sendLine(Serial1, "STATUS*00");
  CHECK(has(o, "err checksum"), "nepareiza summa: %s", o.c_str());
  std::string good = withSum("MOVE 45 -20 30 10 90 S30");
  std::string bad = good.substr(0, 12) + good.substr(18);   // pazaudēti baiti vidū
  o = sendLine(Serial1, bad);
  CHECK(has(o, "err checksum") && motion::queued() == 0, "bojāta rinda pieņemta: %s -> %s", bad.c_str(), o.c_str());

  o = sendLine(Serial, "MOVE 10 _ _ _ _ S80");
  CHECK(o == "ok\n" && motion::queued() == 1, "MOVE ar _: %s", o.c_str());
  runUntilIdle(10);
  CHECK(fabs(motion::positionDeg(0) - 10) < 0.02, "J1 %.2f", motion::positionDeg(0));
  o = sendLine(Serial1, withSum("MOVEJ 2 15 S40"));
  CHECK(o == "ok\n", "MOVEJ: %s", o.c_str());
  runUntilIdle(10);
  CHECK(fabs(motion::positionDeg(1) - 15) < 0.02, "J2 %.2f", motion::positionDeg(1));

  o = sendLine(Serial, "JOG 0");
  CHECK(o.empty(), "JOG 0 atbild: %s", o.c_str());
  o = sendLine(Serial, "JOG abc");
  CHECK(has(o, "[JOG] bad_arg") && !has(o, "err"), "JOG abc: %s", o.c_str());
  o = sendLine(Serial, "CFG SPEED 2 25");
  CHECK(o == "ok\n" && motion::joint(1).maxSpeed == 25.0f, "CFG SPEED: %s", o.c_str());
  o = sendLine(Serial, "CFG MIN 2 500");
  CHECK(has(o, "err bad_arg") && motion::joint(1).minDeg == -90.0f, "MIN >= MAX jānoraida un jāatstāj vecā vērtība (%.1f)", motion::joint(1).minDeg);
  o = sendLine(Serial, "CFG");
  CHECK(has(o, "[CFG] J2") && has(o, "speed=25.00") && has(o, "[CFG] end"), "CFG: %s", o.c_str());
  o = sendLine(Serial, "GRIP 75");
  CHECK(o == "ok\n" && gripper::percent() == 75.0f, "GRIP");
  o = sendLine(Serial, "?");
  CHECK(has(o, "|G:75>"), "statuss: %s", o.c_str());
  o = sendLine(Serial, "FOO 1");
  CHECK(has(o, "err unknown_cmd"), "FOO: %s", o.c_str());
  o = sendLine(Serial, std::string(120, 'A'));
  CHECK(has(o, "err line_too_long"), "gara rinda: %s", o.c_str());

  // '!' rindas vidū -> ESTOP uzreiz
  o = sendLine(Serial, "MOVE 10 2!0");
  CHECK(has(o, "[ESTOP]") && motion::state() == motion::State::Alarm, "ESTOP rindas vidū: %s", o.c_str());
  CHECK(has(o, "err alarm"), "MOVE pēc ESTOP: %s", o.c_str());
  o = sendLine(Serial, "RESET");
  CHECK(o == "ok\n" && motion::state() == motion::State::Idle, "RESET: %s", o.c_str());
  o = sendLine(Serial, "MOVE 500 S50");
  CHECK(has(o, "err limit"), "limits: %s", o.c_str());
}

// ---- gala slēdži, HOME, SAVE ------------------------------------------------------
static std::string allOut() { std::string o = Serial.out; Serial.out.clear(); return o; }

static void testHoming() {
  printf("[test] HOME ar gala slēdzi\n");
  motion::setHomeThenZero(false);   // vecie testi pārbauda stāvokli pie slēdža
  sendLine(Serial, "RESET");
  // J1: slēdzis 60° negatīvajā virzienā no pašreizējās fiziskās pozīcijas
  const float spd = motion::joint(0).stepsPerDeg;
  switchPresent[0] = true;
  physSwitch[0] = phys[0] - (int64_t)(60 * spd);
  std::string o = sendLine(Serial, "HOME 1");
  CHECK(has(o, "err no_endstop"), "HOME bez ES: %s", o.c_str());
  CHECK(sendLine(Serial, "CFG ES 1 1") == "ok\n", "CFG ES");
  CHECK(sendLine(Serial, "CFG HOME_POS 1 -172") == "ok\n", "CFG HOME_POS");
  o = sendLine(Serial, "?");
  CHECK(has(o, "|S:0----|"), "S lauks: %s", o.c_str());
  o = sendLine(Serial, "HOME");
  CHECK(o == "ok\n" && motion::state() == motion::State::Home, "HOME start: %s", o.c_str());
  std::string log;
  const double end = g_t + 30e6;
  while (g_t < end && motion::state() == motion::State::Home) { step(10000); protocol::poll(); log += allOut(); }
  CHECK(motion::state() == motion::State::Idle, "HOME nebeidzās (%s): %s", motion::stateName(), log.c_str());
  CHECK(has(log, "[HOME] J1 ok") && has(log, "[HOME] pabeigts"), "ziņas: %s", log.c_str());
  CHECK(motion::isZeroed(0), "J1 nav nulles");
  // fiziski: slēdzis pie -172°, pēc atkāpšanās 5° -> -167°
  const double posDeg = motion::positionDeg(0);
  const double physFromSwitch = (double)(phys[0] - physSwitch[0]) / spd;
  printf("  pēc HOME J1 = %.3f°, fiziski %.3f° no slēdža\n", posDeg, physFromSwitch);
  CHECK(fabs(posDeg - (-167.0)) < 0.1, "J1 %.3f", posDeg);
  CHECK(fabs((posDeg + 172.0) - physFromSwitch) < 0.1, "slēdža punkts neprecīzs: %.3f vs %.3f", posDeg + 172.0, physFromSwitch);

  // atkārtots HOME dod to pašu rezultātu (atkārtojamība)
  sendLine(Serial, "HOME 1");
  while (motion::state() == motion::State::Home) step(10000);
  CHECK(fabs(motion::positionDeg(0) - posDeg) < 0.05, "atkārtojamība %.3f vs %.3f", motion::positionDeg(0), posDeg);
}

static void testHomeFail() {
  printf("[test] HOME bez slēdža -> Alarm\n");
  sendLine(Serial, "CFG ES 2 1");
  switchPresent[1] = false;
  sendLine(Serial, "HOME 2");
  std::string log;
  const double end = g_t + 60e6;
  while (g_t < end && motion::state() == motion::State::Home) { step(20000); protocol::poll(); log += allOut(); }
  CHECK(motion::state() == motion::State::Alarm, "nav Alarm: %s", motion::stateName());
  CHECK(has(log, "[HOME] kluda J2"), "ziņa: %s", log.c_str());
  sendLine(Serial, "RESET");
  sendLine(Serial, "CFG ES 2 0");
}

static void testEndstopGuard() {
  printf("[test] slēdzis kustības laikā -> apstājas bez avārijas\n");
  // J1 ir -167°, slēdzis -172°. Braucam uz leju (limitu atslēdzam, lai tiek līdz slēdzim).
  sendLine(Serial, "CFG MIN 1 -179");
  float p[NUM_JOINTS] = {-30, 0, 0, 0, 0};
  std::string log;
  for (int i = 0; i < 40 && !has(log, "[ENDSTOP] J1"); i++) {
    motion::jog(p); step(100000); protocol::poll(); log += allOut();
  }
  CHECK(has(log, "[ENDSTOP] J1 apstajas"), "ziņa: %s", log.c_str());
  CHECK(motion::state() != motion::State::Alarm, "nedrīkst būt avārija: %s", motion::stateName());
  // turpinot spiest uz slēdža pusi - nekustas un jaunas ziņas nav
  const double at = motion::positionDeg(0);
  log.clear();
  for (int i = 0; i < 10; i++) { motion::jog(p); step(100000); protocol::poll(); log += allOut(); }
  CHECK(fabs(motion::positionDeg(0) - at) < 0.05, "brauc slēdzī: %.3f -> %.3f", at, motion::positionDeg(0));
  CHECK(!has(log, "[ENDSTOP]"), "atkārtotas ziņas: %s", log.c_str());
  // prom no slēdža - uzreiz, bez RESET
  float up[NUM_JOINTS] = {30, 0, 0, 0, 0};
  for (int i = 0; i < 5; i++) { motion::jog(up); step(100000); }
  CHECK(motion::state() == motion::State::Jog && motion::positionDeg(0) > at + 1.0,
        "nevar braukt prom no slēdža: %s %.2f", motion::stateName(), motion::positionDeg(0));
  float z[NUM_JOINTS] = {0};
  motion::jog(z);
  runUntilIdle(3);
  // MOVE uz slēdža pusi: apstājas pie slēdža, Idle, nākamais MOVE prom strādā
  std::string o = sendLine(Serial, "MOVE -178 _ _ _ _ S50");
  CHECK(o == "ok\n", "MOVE: %s", o.c_str());
  runUntilIdle(20);
  protocol::poll();
  o = allOut();
  CHECK(has(o, "[ENDSTOP] J1"), "MOVE slēdzī - nav ziņas: %s", o.c_str());
  CHECK(motion::state() == motion::State::Idle && motion::positionDeg(0) > -173.5 && motion::positionDeg(0) < -171.0,
        "MOVE slēdzī: %s %.2f", motion::stateName(), motion::positionDeg(0));
  o = sendLine(Serial, "MOVE -167 _ _ _ _ S50");
  CHECK(o == "ok\n", "MOVE prom: %s", o.c_str());
  runUntilIdle(20);
  CHECK(fabs(motion::positionDeg(0) + 167.0) < 0.05, "J1 %.2f", motion::positionDeg(0));
  sendLine(Serial, "CFG MIN 1 -170");
  allOut();
}

static void testEndstopFarEnd() {
  printf("[test] viens slēdzis abos galos (J1 ~360°)\n");
  const float spd = motion::joint(0).stepsPerDeg;
  // slēdzis pie -172°, otrā galā tas pats slēdzis pie +170° (342° tālāk)
  switchHiPresent[0] = true;
  physSwitchHi[0] = physSwitch[0] + (int64_t)(342 * spd);
  sendLine(Serial, "CFG MAX 1 179");
  float up[NUM_JOINTS] = {40, 0, 0, 0, 0};
  float down[NUM_JOINTS] = {-30, 0, 0, 0, 0};
  float z[NUM_JOINTS] = {0};
  std::string log;
  for (int i = 0; i < 200 && !has(log, "[ENDSTOP] J1"); i++) {
    motion::jog(up); step(100000); protocol::poll(); log += allOut();
  }
  CHECK(has(log, "[ENDSTOP] J1") && motion::state() != motion::State::Alarm,
        "otrā galā neapstājās: %s %.2f", motion::stateName(), motion::positionDeg(0));
  CHECK(motion::positionDeg(0) > 165 && motion::positionDeg(0) < 175, "apstājās %.2f", motion::positionDeg(0));
  const double at = motion::positionDeg(0);
  for (int i = 0; i < 10; i++) { motion::jog(up); step(100000); }
  CHECK(fabs(motion::positionDeg(0) - at) < 0.05, "otrā galā brauc tālāk: %.2f", motion::positionDeg(0));
  motion::jog(z);
  runUntilIdle(3);
  // HOME no otrā gala atteic (atkāpjoties brauktu vēl tālāk)
  sendLine(Serial, "HOME 1");
  for (int i = 0; i < 20 && motion::state() == motion::State::Home; i++) { step(10000); protocol::poll(); log += allOut(); }
  CHECK(motion::state() == motion::State::Alarm && has(log, "otra gala"), "HOME otrā galā: %s", motion::stateName());
  sendLine(Serial, "RESET");
  // atpakaļ (uz HOME pusi) drīkst
  for (int i = 0; i < 10; i++) { motion::jog(down); step(100000); }
  CHECK(motion::state() == motion::State::Jog && motion::positionDeg(0) < at - 1.0, "nevar braukt prom no otrā gala");
  // un vēlreiz uz otro galu -> atkal apstājas
  log.clear();
  for (int i = 0; i < 40 && !has(log, "[ENDSTOP] J1"); i++) { motion::jog(up); step(100000); protocol::poll(); log += allOut(); }
  CHECK(has(log, "[ENDSTOP] J1"), "atkārtoti otrā galā neapstājās");
  for (int i = 0; i < 10; i++) { motion::jog(down); step(100000); }
  motion::jog(z);
  runUntilIdle(3);
  // HOME no vidus joprojām strādā
  sendLine(Serial, "HOME 1");
  const double end = g_t + 30e6;
  while (g_t < end && motion::state() == motion::State::Home) step(10000);
  CHECK(motion::state() == motion::State::Idle && fabs(motion::positionDeg(0) + 167.0) < 0.1,
        "HOME pēc otrā gala: %s %.2f", motion::stateName(), motion::positionDeg(0));
  switchHiPresent[0] = false;
  sendLine(Serial, "CFG MAX 1 170");
  allOut();
}

static void testHomeThenZero() {
  printf("[test] pēc HOME roka brauc uz 0°\n");
  motion::setHomeThenZero(true);
  const float spd = motion::joint(0).stepsPerDeg;
  std::string log = sendLine(Serial, "HOME 1");
  CHECK(log == "ok\n", "HOME: %s", log.c_str());
  const double end = g_t + 60e6;
  while (g_t < end && (motion::state() != motion::State::Idle || motion::queued())) {
    step(10000); protocol::poll(); log += allOut();
  }
  CHECK(has(log, "[HOME] pabeigts") && has(log, "[HOME] brauc uz 0"), "ziņas: %s", log.c_str());
  CHECK(fabs(motion::positionDeg(0)) < 0.05, "J1 pēc HOME %.3f (jābūt 0)", motion::positionDeg(0));
  const double physFromSwitch = (double)(phys[0] - physSwitch[0]) / spd;
  CHECK(fabs(physFromSwitch - 172.0) < 0.1, "fiziski %.3f° no slēdža (jābūt 172)", physFromSwitch);
}

static void testZeroMovesHomeRef() {
  printf("[test] ZERO pēc HOME pārbīda slēdža leņķi (vidus = 0)\n");
  // J1 ir 0 pēc HOME (slēdzis -172). Aizbraucam uz +10 un saucam to par 0.
  const float spd = motion::joint(0).stepsPerDeg;
  std::string o = sendLine(Serial, "MOVE 10 _ _ _ _ S50");
  runUntilIdle(10);
  const double physMid = (double)(phys[0] - physSwitch[0]) / spd;   // 182° no slēdža
  o = sendLine(Serial, "ZERO 1");
  CHECK(has(o, "ok") && has(o, "[ZERO] J1 sledzis tagad -182.00"), "ZERO: %s", o.c_str());
  CHECK(fabs(motion::joint(0).homePos + 182.0f) < 0.01f, "homePos %.2f", motion::joint(0).homePos);
  // saglabājās: LOAD atgriež to pašu
  CHECK(sendLine(Serial, "LOAD") == "ok\n" && fabs(motion::joint(0).homePos + 182.0f) < 0.01f, "nav saglabāts");
  // nākamais HOME noliek roku tajā pašā fiziskajā vietā ar 0°
  sendLine(Serial, "MOVE -50 _ _ _ _ S50");
  runUntilIdle(10);
  sendLine(Serial, "HOME 1");
  const double end = g_t + 60e6;
  while (g_t < end && (motion::state() != motion::State::Idle || motion::queued())) step(10000);
  const double phys1 = (double)(phys[0] - physSwitch[0]) / spd;
  CHECK(fabs(motion::positionDeg(0)) < 0.05 && fabs(phys1 - physMid) < 0.1,
        "pēc HOME %.3f°, fiziski %.3f (jābūt %.3f)", motion::positionDeg(0), phys1, physMid);
  // locītava bez HOME: ZERO slēdža leņķi nemaina
  const float hp2 = motion::joint(1).homePos;
  sendLine(Serial, "ZERO 2");
  CHECK(motion::joint(1).homePos == hp2, "J2 homePos mainījās");
  sendLine(Serial, "CFG HOME_POS 1 -172");
  allOut();
}

static void testNudge() {
  printf("[test] NUDGE (pults trima pogas)\n");
  const double before = motion::positionDeg(1);
  std::string o = sendLine(Serial, "NUDGE 2 1.5 S50");
  CHECK(o == "ok\n", "NUDGE: %s", o.c_str());
  runUntilIdle(5);
  CHECK(fabs(motion::positionDeg(1) - before - 1.5) < 0.02, "J2 %.3f -> %.3f", before, motion::positionDeg(1));
  CHECK(has(sendLine(Serial, "NUDGE 9 1"), "err bad_arg"), "NUDGE nepareiza locītava");
  CHECK(has(sendLine(Serial, "NUDGE 2"), "err bad_arg"), "NUDGE bez leņķa");
  sendLine(Serial, "NUDGE 2 -1.5 S50");
  runUntilIdle(5);
  allOut();
}

static void testSaveLoad() {
  printf("[test] SAVE / FACTORY / LOAD\n");
  CHECK(sendLine(Serial, "CFG SPEED 3 33") == "ok\n", "CFG");
  CHECK(sendLine(Serial, "CFG GRIP_OPEN 1100") == "ok\n", "GRIP_OPEN");
  CHECK(sendLine(Serial, "SAVE") == "ok\n", "SAVE");
  CHECK(sendLine(Serial, "FACTORY") == "ok\n", "FACTORY");
  CHECK(motion::joint(2).maxSpeed == 45.0f && !motion::joint(0).esEnabled && gripper::openUs() == 1000, "FACTORY nestrādā");
  CHECK(sendLine(Serial, "LOAD") == "ok\n", "LOAD");
  CHECK(motion::joint(2).maxSpeed == 33.0f && motion::joint(0).esEnabled && gripper::openUs() == 1100, "LOAD nestrādā");
  std::string o = sendLine(Serial, "CFG");
  CHECK(has(o, "home_pos=-172.00") && has(o, "grip_open=1100") && o.size() > 3 && o.substr(o.size() - 3) == "ok\n",
        "CFG izdruka: %s", o.c_str());
  // bojāti dati -> LOAD atsakās
  g_storage[10] ^= 0xFF;
  CHECK(has(sendLine(Serial, "LOAD"), "err storage"), "bojāti dati pieņemti");
  g_storage[10] ^= 0xFF;
}

static void testRepliesEndWithOk() {
  printf("[test] katra komanda beidzas ar ok/err\n");
  const char *cmds[] = {"INFO", "STATUS", "HELP", "CFG", "ES", "TMC", "; komentārs", "ZERO 9", "FOO"};
  for (const char *c : cmds) {
    std::string o = sendLine(Serial, c);
    const size_t a = o.rfind("ok\n"), b = o.rfind("err ");
    const bool endsOk = o.size() >= 3 && a == o.size() - 3;
    const bool endsErr = b != std::string::npos && o.find('\n', b) == o.size() - 1;
    CHECK(endsOk || endsErr, "'%s' -> %s", c, o.c_str());
    int n = 0;
    for (size_t i = 0; (i = o.find("ok\n", i)) != std::string::npos; i++) if (i == 0 || o[i - 1] == '\n') n++;
    CHECK(n + (endsErr ? 1 : 0) == 1, "'%s' atbildes: %d", c, n);
  }
}

// ---- ekrāns un programmas no SD ----------------------------------------------------
static std::string lcdText() { return sendLine(Serial, "LCD"); }
static void key(const char *k) { sendLine(Serial, std::string("KEY ") + k); step(20000); }

static void testLcd() {
  printf("[test] ekrāns: statuss, izvēlne, kustināšana ar pogu\n");
  sendLine(Serial, "RESET");
  sendLine(Serial, "ZERO");                       // iepriekšējie testi atstāja J2 bez nulles
  step(3500000);                                    // iepriekšējā ziņa (3 s) nodziest
  std::string o = lcdText();
  CHECK(has(o, "[LCD] |GATAVS") && has(o, "J1") && has(o, "J5"), "statusa ekrāns:\n%s", o.c_str());
  key("C");                                         // -> izvēlne, kursors uz "Kustinat locitavu"
  o = lcdText();
  CHECK(has(o, ">Kustinat locitavu"), "izvēlne:\n%s", o.c_str());
  key("C");                                         // -> locītavu saraksts, J1
  o = lcdText();
  CHECK(has(o, ">J1"), "locītavu saraksts:\n%s", o.c_str());
  key("+1");                                        // -> J2
  key("C");                                         // -> kustināt J2
  o = lcdText();
  CHECK(has(o, "Kustina J2"), "kustināšanas ekrāns:\n%s", o.c_str());
  const float before = motion::positionDeg(1);
  key("+3");                                        // 3 klikšķi x 1° (noklusējuma solis)
  runUntilIdle(10);
  step(300000);
  CHECK(fabs(motion::positionDeg(1) - (before + 3)) < 0.02, "J2 %.2f -> %.2f", before, motion::positionDeg(1));
  key("-1");
  runUntilIdle(10);
  CHECK(fabs(motion::positionDeg(1) - (before + 2)) < 0.02, "J2 atpakaļ %.2f", motion::positionDeg(1));
  key("C");                                         // atpakaļ uz sarakstu
  key("-5");                                        // uz "< Atpakal"
  key("C");                                         // -> izvēlne
  key("+2");                                        // "Atrums"
  o = lcdText();
  CHECK(has(o, ">Atrums: 30%"), "ātruma rinda:\n%s", o.c_str());
  key("C");
  key("+4");                                        // +20%
  key("C");
  o = lcdText();
  CHECK(has(o, "Atrums: 50%"), "ātrums mainīts:\n%s", o.c_str());
  step((LCD_IDLE_TIMEOUT_MS + 500) * 1000.0);       // neaktivitāte -> statuss
  CHECK(has(lcdText(), "|GATAVS"), "atgriezās statusā");
}

static void testSdRun() {
  printf("[test] programma no SD kartes\n");
  g_sdFiles.clear();
  g_sdFiles["PROG1.TXT"] = "; RobotRoka programma\r\nMOVE 10.00 _ _ _ _ S100  ; sākums\r\nGRIP 50\r\nWAIT 200\r\n"
                           "\r\nMOVEJ 2 -10 S100\r\nMOVE 0 0 _ _ _ S100\r\n";
  g_sdFiles["SLIKTA.TXT"] = "MOVE 10 S100\nMOVE 999 S100\n";
  std::string o = sendLine(Serial, "SD");
  CHECK(has(o, "[SD] PROG1.TXT") && has(o, "[SD] SLIKTA.TXT"), "SD saraksts: %s", o.c_str());
  CHECK(has(sendLine(Serial, "RUN NAV.TXT"), "err no_file"), "nav faila");

  std::string log;
  o = sendLine(Serial, "RUN prog1.txt");
  CHECK(o.find("ok") != std::string::npos, "RUN: %s", o.c_str());
  CHECK(has(sendLine(Serial, "?"), "|R:"), "statusā R lauks");
  bool sawGrip = false;
  const double end = g_t + 30e6;
  while (g_t < end && runner::active()) {
    step(10000);
    protocol::poll();
    log += allOut();
    if (gripper::percent() == 50 && motion::positionDeg(1) > -9) sawGrip = true;
  }
  CHECK(!runner::active() && has(log, "[RUN] pabeigts PROG1.TXT"), "pabeigts: %s", log.c_str());
  CHECK(sawGrip, "GRIP izpildīts pirms MOVEJ");
  CHECK(fabs(motion::positionDeg(0)) < 0.02 && fabs(motion::positionDeg(1)) < 0.02, "beigu pozīcija %.2f %.2f",
        motion::positionDeg(0), motion::positionDeg(1));

  log.clear();
  sendLine(Serial, "RUN SLIKTA.TXT");
  while (runner::active()) { step(10000); protocol::poll(); log += allOut(); }
  CHECK(has(log, "[RUN] kluda L2: limit"), "kļūda programmā: %s", log.c_str());

  printf("[test] programma atkārtoti + STOP + ekrāna STOP poga\n");
  sendLine(Serial, "RUN PROG1.TXT L");
  for (int i = 0; i < 2000 && runner::cycle() < 2; i++) step(10000);
  CHECK(runner::cycle() >= 2, "otrais cikls (%u)", runner::cycle());
  sendLine(Serial, "STOP");
  CHECK(!runner::active(), "STOP aptur programmu");
  runUntilIdle(5);
  sendLine(Serial, "RUN PROG1.TXT L");
  step(500000);
  key("K");                                         // ekrāna STOP poga
  CHECK(motion::state() == motion::State::Alarm && !runner::active(), "STOP poga -> Alarm");
  CHECK(has(lcdText(), "ESTOP"), "ekrānā ziņa");
  sendLine(Serial, "RESET");
  g_sdPresent = false;
  CHECK(has(sendLine(Serial, "RUN PROG1.TXT"), "err no_sd"), "bez kartes");
  g_sdPresent = true;
}

static void testHwSettings() {
  printf("[test] ekrāns/SD ieslēgšana no PC, SAVE/LOAD, vecais atmiņas formāts\n");
  std::string o = sendLine(Serial, "CFG");
  CHECK(has(o, " lcd=1 sd=1"), "CFG rāda lcd/sd: %s", o.c_str());
  CHECK(sendLine(Serial, "CFG LCD 0") == "ok\n" && !ui::enabled(), "CFG LCD 0");
  const float before = motion::positionDeg(0);
  key("C"); key("C"); key("C"); key("+5");          // pogas bez ekrāna neko nedara
  runUntilIdle(3);
  CHECK(fabs(motion::positionDeg(0) - before) < 1e-4, "bez ekrāna poga nekustina (%.3f -> %.3f, %s)", before, motion::positionDeg(0), motion::stateName());
  CHECK(sendLine(Serial, "CFG SD 0") == "ok\n", "CFG SD 0");
  CHECK(has(sendLine(Serial, "SD"), "err no_sd") && has(sendLine(Serial, "RUN PROG1.TXT"), "err no_sd"), "SD atslēgta");
  CHECK(sendLine(Serial, "SAVE") == "ok\n", "SAVE");
  CHECK(sendLine(Serial, "FACTORY") == "ok\n" && ui::enabled() && sdcard::enabled(), "FACTORY -> noklusējums (ieslēgts)");
  CHECK(sendLine(Serial, "LOAD") == "ok\n" && !ui::enabled() && !sdcard::enabled(), "LOAD -> atkal atslēgts");
  o = sendLine(Serial, "CFG");
  CHECK(has(o, " lcd=0 sd=0"), "CFG pēc LOAD: %s", o.c_str());
  CHECK(sendLine(Serial, "CFG LCD 1") == "ok\n" && sendLine(Serial, "CFG SD 1") == "ok\n", "ieslēgt atpakaļ");
  step(500000);
  CHECK(has(lcdText(), "Ekrans ieslegts"), "ekrāns atkal strādā");

  // Vecā firmware ieraksts (1. versija) - iestatījumi jāielasa, ekrāns paliek kā ir
  storage::RecordV1 v1;
  memset(&v1, 0, sizeof v1);
  v1.h.magic = storage::kMagic;
  v1.h.version = 1;
  v1.h.size = sizeof v1;
  for (int j = 0; j < NUM_JOINTS; j++) {
    v1.b.j[j].minDeg = -100; v1.b.j[j].maxDeg = 100; v1.b.j[j].maxSpeed = 20; v1.b.j[j].accel = 50;
    v1.b.j[j].homeSpeed = 10; v1.b.j[j].homeDir = -1;
  }
  v1.b.gripOpenUs = 1200; v1.b.gripClosedUs = 1800;
  v1.crc = storage::crc16(reinterpret_cast<const uint8_t *>(&v1), sizeof v1 - 2);
  memcpy(g_storage.data(), &v1, sizeof v1);
  CHECK(sendLine(Serial, "LOAD") == "ok\n", "LOAD v1");
  CHECK(motion::joint(3).maxDeg == 100 && motion::joint(3).maxSpeed == 20 && gripper::openUs() == 1200 && ui::enabled(),
        "v1 ielasīts (max %.0f, speed %.0f)", motion::joint(3).maxDeg, motion::joint(3).maxSpeed);
  sendLine(Serial, "FACTORY");
  sendLine(Serial, "SAVE");
}

static void testDrive() {
  printf("[test] motora soļi / mikrosoļi / reduktors no PC\n");
  sendLine(Serial, "RESET");
  sendLine(Serial, "ZERO");
  CHECK(sendLine(Serial, "MOVEJ 4 30 S100") == "ok\n", "MOVEJ 30");
  runUntilIdle(10);
  CHECK(fabs(motion::positionDeg(3) - 30) < 0.02, "J4 %.3f", motion::positionDeg(3));
  CHECK(motion::joint(3).stepsPerDeg == 160.0f, "sākumā 160 soļi/° (%.3f)", motion::joint(3).stepsPerDeg);

  // 1.8° motors (200 soļi) -> 80 soļi/°, leņķis paliek 30°
  CHECK(sendLine(Serial, "CFG MOTOR_STEPS 4 200") == "ok\n", "CFG MOTOR_STEPS");
  CHECK(motion::joint(3).stepsPerDeg == 80.0f, "80 soļi/° (%.3f)", motion::joint(3).stepsPerDeg);
  CHECK(fabs(motion::positionDeg(3) - 30) < 0.02, "leņķis saglabājās: %.3f", motion::positionDeg(3));
  const int64_t physBefore = phys[3];
  sendLine(Serial, "MOVEJ 4 60 S100");
  runUntilIdle(10);
  CHECK(fabs(motion::positionDeg(3) - 60) < 0.02, "J4 60 (%.3f)", motion::positionDeg(3));
  CHECK(phys[3] - physBefore == 30 * 80, "fiziski 2400 soļi (%lld)", (long long)(phys[3] - physBefore));

  // cits reduktors
  CHECK(sendLine(Serial, "CFG GEAR 4 5.18") == "ok\n", "CFG GEAR");
  CHECK(fabs(motion::joint(3).stepsPerDeg - 200 * 16 * 5.18f / 360) < 0.01, "soļi/° ar 5.18");
  CHECK(fabs(motion::positionDeg(3) - 60) < 0.02, "leņķis pēc GEAR %.3f", motion::positionDeg(3));
  CHECK(has(sendLine(Serial, "CFG MICROSTEPS 4 12"), "err bad_arg"), "12 nav 2^n");
  CHECK(has(sendLine(Serial, "CFG GEAR 4 0"), "err bad_arg"), "GEAR 0");
  CHECK(has(sendLine(Serial, "CFG MOTOR_STEPS 4 0"), "err bad_arg"), "MOTOR_STEPS 0");
  CHECK(sendLine(Serial, "CFG MICROSTEPS 4 8") == "ok\n", "MICROSTEPS 8");
  std::string o = sendLine(Serial, "CFG");
  CHECK(has(o, "J4 name=J4 motor_steps=200 microsteps=8 gear=5.180"), "CFG izdruka: %s", o.c_str());

  // SAVE -> FACTORY (atpakaļ uz config.h, leņķis paliek) -> LOAD
  CHECK(sendLine(Serial, "SAVE") == "ok\n", "SAVE");
  CHECK(sendLine(Serial, "FACTORY") == "ok\n" && motion::joint(3).stepsPerDeg == 160.0f, "FACTORY");
  CHECK(fabs(motion::positionDeg(3) - 60) < 0.05, "leņķis pēc FACTORY %.3f", motion::positionDeg(3));
  CHECK(sendLine(Serial, "LOAD") == "ok\n", "LOAD");
  CHECK(motion::joint(3).motorSteps == 200 && motion::joint(3).microsteps == 8 &&
        fabs(motion::joint(3).gear - 5.18f) < 1e-4, "LOAD piedziņa");
  CHECK(fabs(motion::positionDeg(3) - 60) < 0.05, "leņķis pēc LOAD %.3f", motion::positionDeg(3));

  // 2. versijas ieraksts (bez piedziņas) -> piedziņa paliek no config.h
  sendLine(Serial, "FACTORY");
  storage::RecordV2 v2;
  memset(&v2, 0, sizeof v2);
  v2.h.magic = storage::kMagic;
  v2.h.version = 2;
  v2.h.size = sizeof v2;
  for (int j = 0; j < NUM_JOINTS; j++) {
    v2.b.j[j].minDeg = -150; v2.b.j[j].maxDeg = 150; v2.b.j[j].maxSpeed = 25; v2.b.j[j].accel = 70;
    v2.b.j[j].homeSpeed = 10; v2.b.j[j].homeDir = -1;
  }
  v2.b.gripOpenUs = 1000; v2.b.gripClosedUs = 2000;
  v2.lcdOn = 1; v2.sdOn = 1;
  v2.crc = storage::crc16(reinterpret_cast<const uint8_t *>(&v2), sizeof v2 - 2);
  memcpy(g_storage.data(), &v2, sizeof v2);
  CHECK(sendLine(Serial, "LOAD") == "ok\n" && motion::joint(1).maxSpeed == 25 && motion::joint(1).stepsPerDeg == 160.0f,
        "v2 ielasīts, piedziņa no config.h");
  sendLine(Serial, "FACTORY");
  sendLine(Serial, "SAVE");
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("Simulācija: STEP_ISR_HZ=%lu CONTROL_HZ=%lu\n",
         (unsigned long)STEP_ISR_HZ, (unsigned long)CONTROL_HZ);
  stepper::init();
  motion::init();
  endstops::init();
  ui::setEnabled(true);
  sdcard::setEnabled(true);
  printf("  J1 soļi/° = %.3f\n", motion::joint(0).stepsPerDeg);

  testUnzeroed();
  testSyncMove();
  testQueue();
  testRelative();
  testLimitReject();
  testJogWatchdog();
  testJogSoftLimit();
  testReverse();
  testStopDuringMove();
  testEstop();
  testSlowMove();
  testProtocol();
  testHoming();
  testHomeFail();
  testEndstopGuard();
  testEndstopFarEnd();
  testHomeThenZero();
  testZeroMovesHomeRef();
  testNudge();
  testSaveLoad();
  testRepliesEndWithOk();
  testLcd();
  testSdRun();
  testHwSettings();
  testDrive();
  CHECK(consecutiveViolations == 0, "soļu laika pārkāpumi: %d", consecutiveViolations);

  printf("\n%d pārbaudes izdevās, %d neizdevās\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
