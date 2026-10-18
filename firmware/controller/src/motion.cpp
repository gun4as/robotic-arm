#include "motion.h"
#include "stepper.h"
#include "boards.h"
#include "endstops.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

namespace motion {
namespace {

struct MoveSeg {
  int32_t target[NUM_JOINTS];
  uint8_t useMask;
  float speed;        // 0..1
};

struct ActiveMove {
  int32_t start[NUM_JOINTS];
  int32_t target[NUM_JOINTS];
  float delta[NUM_JOINTS];
  uint8_t mask;
  float T, ta, D, t;
};

const uint16_t kFullSteps[NUM_JOINTS] = MOTOR_FULL_STEPS;
const uint16_t kMicro[NUM_JOINTS] = JOINT_MICROSTEPS;
const float kGear[NUM_JOINTS] = JOINT_GEAR_RATIO;
const bool kInvert[NUM_JOINTS] = JOINT_INVERT_DIR;
const float kMin[NUM_JOINTS] = JOINT_MIN_DEG;
const float kMax[NUM_JOINTS] = JOINT_MAX_DEG;
const float kSpeed[NUM_JOINTS] = JOINT_MAX_SPEED;
const float kAccel[NUM_JOINTS] = JOINT_ACCEL;
const bool kEsEnabled[NUM_JOINTS] = ENDSTOP_ENABLED;
const bool kEsInvert[NUM_JOINTS] = ENDSTOP_INVERT;
const int8_t kHomeDir[NUM_JOINTS] = HOME_DIR;
const float kHomePos[NUM_JOINTS] = HOME_POS_DEG;
const float kHomeSpeed[NUM_JOINTS] = HOME_SPEED;
const float kBackoff[NUM_JOINTS] = HOME_BACKOFF_DEG;
const uint8_t kHomeOrder[NUM_JOINTS] = HOME_ORDER;

constexpr uint32_t kTickUs = 1000000UL / CONTROL_HZ;
constexpr float kTickS = 1.0f / (float)CONTROL_HZ;

JointConfig cfg[NUM_JOINTS];
State st = State::Idle;
uint8_t zeroMask = 0;
uint8_t homedMask = 0;        // nulle no HOME: zināms, cik tālu ir slēdzis
bool homeRefChanged = false;
uint8_t esStops = 0;          // cik reizes gala slēdzis apturējis kustību (programmām)  // ZERO/SETPOS pārbīdīja 'leņķi pie slēdža' -> jāsaglabā
bool homeThenZero = HOME_THEN_ZERO;

float vel[NUM_JOINTS];        // pašreizējais ātrums, °/s
float jogTarget[NUM_JOINTS];  // vēlamais ātrums, °/s
uint32_t lastJogMs = 0;
uint32_t lastTickUs = 0;

MoveSeg queue[MOVE_QUEUE_SIZE];
uint8_t qHead = 0, qCount = 0;
int32_t lastTarget[NUM_JOINTS];
uint8_t queuedMask = 0;       // locītavas ar mērķi rindā / aktīvā kustībā
ActiveMove mv;

// HOME: locītavas pa vienai, katrai 4 fāzes
enum class HomePhase : uint8_t { Start, Seek, Backoff, Slow, Retract };
struct HomeRun {
  uint8_t seq[NUM_JOINTS];
  uint8_t n, idx;
  HomePhase phase;
  int32_t ref;
} hr;

// Gala slēdža sargs: kurā virzienā nedrīkst braukt, kamēr slēdzis nospiests
constexpr float kFarEndDeg = 10.0f;
struct Guard {
  int32_t lastPos;
  float freeDeg;     // nobraukts, kamēr slēdzis brīvs
  int8_t block;      // bloķētais virziens (+1/-1)
  bool hit;
} guard[NUM_JOINTS];

// Notikumu rinda protokolam
constexpr uint8_t kEvMax = 4, kEvLen = 48;
char evBuf[kEvMax][kEvLen];
uint8_t evHead = 0, evCount = 0;

void pushEvent(const char *msg) {
  if (evCount == kEvMax) { evHead = (uint8_t)((evHead + 1) % kEvMax); evCount--; }
  char *dst = evBuf[(evHead + evCount) % kEvMax];
  strncpy(dst, msg, kEvLen - 1);
  dst[kEvLen - 1] = 0;
  evCount++;
}

inline uint8_t jbit(uint8_t j) { return (uint8_t)(1u << j); }
void applyLimits() {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    if (zeroMask & jbit(j))
      stepper::setLimits(j, lroundf(cfg[j].minDeg * cfg[j].stepsPerDeg),
                            lroundf(cfg[j].maxDeg * cfg[j].stepsPerDeg));
    else
      stepper::clearLimits(j);
  }
}

void clearQueue() {
  qHead = 0;
  qCount = 0;
  queuedMask = 0;
}

void haltAll() {
  stepper::stopAll();
  for (uint8_t j = 0; j < NUM_JOINTS; j++) { vel[j] = 0; jogTarget[j] = 0; }
}

void autoEnable() {
#if AUTO_ENABLE
  if (!stepper::enabled()) stepper::enable(true);
#endif
}

// ---------------------------------------------------------------- JOG režīms
void tickJog(float dt) {
  if ((uint32_t)(millis() - lastJogMs) > JOG_TIMEOUT_MS)
    for (uint8_t j = 0; j < NUM_JOINTS; j++) jogTarget[j] = 0;

  bool moving = false;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const JointConfig &c = cfg[j];
    float vt = jogTarget[j];
    const float a = c.accel;
    if (guard[j].hit && vt * (float)guard[j].block > 0.0f) { vt = 0.0f; vel[j] = 0.0f; }

    if (zeroMask & jbit(j)) {
      // Lielākais ātrums, no kura vēl paspējam nobremzēt līdz limitam.
      const float p = positionDeg(j);
      const float up = c.maxDeg - p, dn = p - c.minDeg;
      const float vUp = up > 0 ? sqrtf(2.0f * a * up) : 0.0f;
      const float vDn = dn > 0 ? sqrtf(2.0f * a * dn) : 0.0f;
      if (vt > vUp) vt = vUp;
      if (vt < -vDn) vt = -vDn;
    }

    const float dv = a * dt;
    float v = vel[j];
    if (v < vt) { v += dv; if (v > vt) v = vt; }
    else if (v > vt) { v -= dv; if (v < vt) v = vt; }
    vel[j] = v;
    stepper::setRate(j, v * c.stepsPerDeg);
    if (v != 0.0f || jogTarget[j] != 0.0f) moving = true;
  }
  if (!moving) st = State::Idle;
}

// --------------------------------------------------------------- MOVE režīms
float profile(float t) {
  if (t <= 0) return 0;
  if (t >= mv.T) return 1;
  if (t < mv.ta) return 0.5f * t * t / (mv.ta * mv.D);
  if (t <= mv.T - mv.ta) return (t - 0.5f * mv.ta) / mv.D;
  const float r = mv.T - t;
  return 1.0f - 0.5f * r * r / (mv.ta * mv.D);
}

void finishMove() {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) { stepper::setRate(j, 0); vel[j] = 0; }
  applyLimits();
  if (qCount == 0) queuedMask = 0;
  st = State::Idle;
}

// Sinhronizēts trapecveida profils: kopīgs laiks T un paātrinājuma laiks ta,
// lai neviena locītava nepārsniedz savu ātrumu un paātrinājumu.
//   D = T - ta.  Ierobežojumi: D >= d/v  un  D*ta >= d/a  (katrai locītavai)
bool beginMove(const MoveSeg &s) {
  float Dv = 0, K = 0;
  mv.mask = 0;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const int32_t p = stepper::position(j);
    mv.start[j] = p;
    mv.target[j] = (s.useMask & jbit(j)) ? s.target[j] : p;
    mv.delta[j] = (float)(mv.target[j] - p);
    const float ad = fabsf(mv.delta[j]);
    if (ad < 0.5f) continue;
    mv.mask |= jbit(j);
    float vmax = cfg[j].maxSpeed * cfg[j].stepsPerDeg * s.speed;
    const float rmax = stepper::maxRate() * 0.95f;
    if (vmax > rmax) vmax = rmax;
    const float amax = cfg[j].accel * cfg[j].stepsPerDeg;
    if (ad / vmax > Dv) Dv = ad / vmax;
    if (ad / amax > K) K = ad / amax;
  }
  if (!mv.mask) return false;

  const float sk = sqrtf(K);
  if (sk >= Dv) { mv.D = sk; mv.ta = sk; }        // trīsstūra profils
  else          { mv.D = Dv; mv.ta = K / Dv; }    // trapece
  mv.T = mv.D + mv.ta;
  mv.t = 0;

  // ISR robežas: neviena locītava nevar aiziet tālāk par savu mērķi.
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const int32_t a = mv.start[j], b = mv.target[j];
    stepper::setLimits(j, a < b ? a : b, a < b ? b : a);
  }
  autoEnable();
  st = State::Move;
  return true;
}

void tickMove(float dt) {
  mv.t += dt;
  const float s = profile(mv.t + kTickS);   // kur jābūt nākamā takts beigās
  bool arrived = true;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    if (!(mv.mask & jbit(j))) continue;
    const int32_t p = stepper::position(j);
    if (p != mv.target[j]) arrived = false;
    const float want = (float)mv.start[j] + mv.delta[j] * s;
    float rate = (want - (float)p) / kTickS;
    if (mv.delta[j] > 0 ? rate < 0 : rate > 0) rate = 0;   // nekad atpakaļ
    stepper::setRate(j, rate);
    vel[j] = rate / cfg[j].stepsPerDeg;
  }
  if ((mv.t >= mv.T && arrived) || mv.t > mv.T + 1.0f) finishMove();
}

void startNextMove() {
  while (qCount) {
    MoveSeg s = queue[qHead];
    qHead = (uint8_t)((qHead + 1) % MOVE_QUEUE_SIZE);
    qCount--;
    if (beginMove(s)) return;
  }
  queuedMask = 0;
}

// --------------------------------------------------------------- HOME režīms
void homeFail(uint8_t j, const char *why) {
  haltAll();
  applyLimits();
  st = State::Alarm;
  char m[kEvLen];
  snprintf(m, sizeof m, "[HOME] kluda J%u: %s", j + 1, why);
  pushEvent(m);
}

// Pēc HOME: atrastās locītavas brauc uz 0° (vidus stāvoklis), ja 0 ir limitos.
void homeGoZero() {
  float deg[NUM_JOINTS] = {0};
  bool use[NUM_JOINTS] = {false};
  bool any = false;
  for (uint8_t i = 0; i < hr.n; i++) {
    const uint8_t j = hr.seq[i];
    if (cfg[j].minDeg <= 0.0f && cfg[j].maxDeg >= 0.0f) use[j] = any = true;
  }
  if (any && queueMove(deg, use, HOME_ZERO_SPEED) == Result::Ok) pushEvent("[HOME] brauc uz 0");
}

void tickHome() {
  const uint8_t j = hr.seq[hr.idx];
  const JointConfig &c = cfg[j];
  const float spd = c.stepsPerDeg;
  const float dir = c.homeDir < 0 ? -1.0f : 1.0f;
  const bool hit = endstops::triggered(j);
  const int32_t p = stepper::position(j);
  const float moved = fabsf((float)(p - hr.ref)) / spd;   // grādi kopš fāzes sākuma

  switch (hr.phase) {
    case HomePhase::Start:
      // uz slēdža otrā galā (J1 ~360°): atkāpjoties brauktu vēl tālāk -> lai nobrauc nost
      if (hit && guard[j].block != c.homeDir) { homeFail(j, "uz sledza otra gala"); return; }
      stepper::clearLimits(j);
      hr.ref = p;
      hr.phase = hit ? HomePhase::Backoff : HomePhase::Seek;   // jau uz slēdža -> vispirms nost
      break;

    case HomePhase::Seek:   // ātri uz slēdža pusi
      if (hit) { stepper::setRate(j, 0); hr.ref = p; hr.phase = HomePhase::Backoff; break; }
      if (moved > c.maxDeg - c.minDeg + 20.0f) { homeFail(j, "sledzis nav atrasts"); return; }
      stepper::setRate(j, dir * c.homeSpeed * spd);
      break;

    case HomePhase::Backoff:   // nost no slēdža
      if (!hit && moved >= c.backoff) { stepper::setRate(j, 0); hr.ref = p; hr.phase = HomePhase::Slow; break; }
      if (moved > c.backoff * 3.0f + 5.0f) { homeFail(j, "sledzis neatlaizas"); return; }
      stepper::setRate(j, -dir * c.homeSpeed * spd);
      break;

    case HomePhase::Slow:   // lēnām atpakaļ precīzam punktam
      if (hit) {
        stepper::setRate(j, 0);
        stepper::setPosition(j, lroundf(c.homePos * spd));
        zeroMask |= jbit(j);
        homedMask |= jbit(j);
        hr.ref = stepper::position(j);
        hr.phase = HomePhase::Retract;
        break;
      }
      if (moved > c.backoff * 2.0f + 5.0f) { homeFail(j, "lenaja faze nav atrasts"); return; }
      stepper::setRate(j, dir * c.homeSpeed * 0.25f * spd);
      break;

    case HomePhase::Retract:   // atstājam slēdzi brīvu
      if (moved >= c.backoff) {
        stepper::setRate(j, 0);
        char m[kEvLen];
        snprintf(m, sizeof m, "[HOME] J%u ok", j + 1);
        pushEvent(m);
        if (++hr.idx >= hr.n) {
          applyLimits();
          st = State::Idle;
          pushEvent("[HOME] pabeigts");
          if (homeThenZero) homeGoZero();
          return;
        }
        hr.phase = HomePhase::Start;
        break;
      }
      stepper::setRate(j, -dir * c.homeSpeed * spd);
      break;
  }
}

// Slēdzis nostrādāja parastas kustības laikā, braucot uz to -> avārija.
// Viens slēdzis var nostrādāt abos kustības galos (piem. J1 griežas gandrīz 360°):
// ja slēdzim uzbrauc no tālienes, braucot PROM no HOME puses, tas ir otrais gals,
// un tad bloķēts ir pretējais virziens. Mazs attālums = atlēciens, pametot slēdzi.
void endstopGuard() {
  const bool active = st == State::Jog || st == State::Move;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    const int32_t p = stepper::position(j);
    const int32_t dp = p - guard[j].lastPos;
    guard[j].lastPos = p;
    if (!endstops::triggered(j)) {
      if (guard[j].freeDeg < 1e6f) guard[j].freeDeg += fabsf((float)dp) / cfg[j].stepsPerDeg;
      guard[j].hit = false;
      continue;
    }
    const int8_t hd = cfg[j].homeDir;
    if (!guard[j].hit) {   // tikko uzbrauca slēdzim
      const float dir = dp != 0 ? (float)dp : vel[j];
      const bool farEnd = guard[j].freeDeg > kFarEndDeg;
      if (farEnd && dir * hd < 0.0f) guard[j].block = (int8_t)-hd;
      else if (farEnd || dir * hd > 0.0f) guard[j].block = hd;
      guard[j].hit = true;
      guard[j].freeDeg = 0;
    }
#if ENDSTOP_STOP_ON_HIT
    if (active && vel[j] * (float)guard[j].block > 0.0f) {
      // Apstājamies pie slēdža bez avārijas: šī locītava uzreiz, pārējās bremzē normāli.
      // Prom no slēdža drīkst braukt uzreiz (bez RESET).
      if (st == State::Move) stop();
      jogTarget[j] = 0.0f;
      vel[j] = 0.0f;
      stepper::setRate(j, 0.0f);
      esStops++;
      char m[kEvLen];
      snprintf(m, sizeof m, "[ENDSTOP] J%u apstajas pie sledza", j + 1);
      pushEvent(m);
    }
#else
    (void)active;
#endif
  }
}

}  // namespace

// =============================================================================
void loadDefaults() {
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    JointConfig &c = cfg[j];
    const float deg = c.stepsPerDeg > 0 ? positionDeg(j) : 0.0f;   // FACTORY: leņķis paliek
    c.motorSteps = kFullSteps[j];
    c.microsteps = kMicro[j];
    c.gear = kGear[j];
    c.stepsPerDeg = (float)c.motorSteps * (float)c.microsteps * c.gear / 360.0f;
    stepper::setPosition(j, lroundf(deg * c.stepsPerDeg));
    c.minDeg = kMin[j];
    c.maxDeg = kMax[j];
    c.maxSpeed = kSpeed[j];
    c.accel = kAccel[j];
    c.invert = kInvert[j];
    c.esEnabled = kEsEnabled[j];
    c.esInvert = kEsInvert[j];
    c.homeDir = kHomeDir[j] < 0 ? -1 : 1;
    c.homePos = kHomePos[j];
    c.homeSpeed = kHomeSpeed[j];
    c.backoff = kBackoff[j];
    c.homeOrder = kHomeOrder[j];
  }
}

void init() {
  loadDefaults();
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    stepper::setInvert(j, cfg[j].invert);
    vel[j] = 0;
    jogTarget[j] = 0;
    lastTarget[j] = 0;
    guard[j] = {stepper::position(j), 1e6f, cfg[j].homeDir, false};
  }
  applyLimits();
  lastTickUs = micros();
}
void update() {
  const uint32_t now = micros();
  const uint32_t el = now - lastTickUs;
  if (el < kTickUs) return;
  lastTickUs = now;
  float dt = (float)el * 1e-6f;
  if (dt > 0.05f) dt = 0.05f;

  endstops::update();
  endstopGuard();

  switch (st) {
    case State::Jog:  tickJog(dt); break;
    case State::Move: tickMove(dt); break;
    case State::Home: tickHome(); break;
    case State::Idle: if (qCount) startNextMove(); break;
    case State::Alarm: break;
  }
}

Result jog(const float pct[NUM_JOINTS]) {
  bool any = false;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) if (pct[j] != 0) any = true;
  // "Visi nulle" jogs (pogas atlaišana) nekad nav kļūda.
  if (!any && st != State::Jog) return Result::Ok;
  if (st == State::Alarm) return Result::Alarm;
  if (st == State::Move || st == State::Home) return Result::Busy;

  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    float p = pct[j];
    if (p > 100) p = 100;
    if (p < -100) p = -100;
    float scale = (zeroMask & jbit(j)) ? 1.0f : (UNZEROED_SPEED_PCT / 100.0f);
    jogTarget[j] = p / 100.0f * cfg[j].maxSpeed * scale;
  }
  lastJogMs = millis();
  if (any) {
    autoEnable();
    if (st == State::Idle) applyLimits();
    st = State::Jog;
  }
  return Result::Ok;
}

static int32_t baseTarget(uint8_t j) {
  return (queuedMask & jbit(j)) ? lastTarget[j] : stepper::position(j);
}

Result queueMove(const float deg[NUM_JOINTS], const bool use[NUM_JOINTS], float speedPct) {
  if (st == State::Alarm) return Result::Alarm;
  if (st == State::Home) return Result::Busy;
  if (qCount >= MOVE_QUEUE_SIZE) return Result::QueueFull;
  if (speedPct > 100) speedPct = 100;
  if (speedPct < 1) speedPct = 1;

  MoveSeg s;
  s.useMask = 0;
  s.speed = speedPct / 100.0f;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) {
    s.target[j] = 0;
    if (!use[j]) continue;
    if (isnan(deg[j])) return Result::BadArg;
#if REQUIRE_ZERO_FOR_MOVE
    if (!(zeroMask & jbit(j))) return Result::NotZeroed;
#endif
    if ((zeroMask & jbit(j)) && (deg[j] < cfg[j].minDeg - 0.001f || deg[j] > cfg[j].maxDeg + 0.001f))
      return Result::Limit;
    s.target[j] = lroundf(deg[j] * cfg[j].stepsPerDeg);
    s.useMask |= jbit(j);
  }
  if (!s.useMask) return Result::BadArg;

  queue[(qHead + qCount) % MOVE_QUEUE_SIZE] = s;
  qCount++;
  for (uint8_t j = 0; j < NUM_JOINTS; j++)
    if (s.useMask & jbit(j)) lastTarget[j] = s.target[j];
  queuedMask |= s.useMask;
  return Result::Ok;
}

Result queueRelative(const float delta[NUM_JOINTS], const bool use[NUM_JOINTS], float speedPct) {
  float deg[NUM_JOINTS];
  for (uint8_t j = 0; j < NUM_JOINTS; j++)
    deg[j] = use[j] ? (float)baseTarget(j) / cfg[j].stepsPerDeg + delta[j] : 0;
  return queueMove(deg, use, speedPct);
}

Result nudge(uint8_t j, float deltaDeg, float speedPct) {
  if (j >= NUM_JOINTS) return Result::BadArg;
  if (st == State::Alarm) return Result::Alarm;
  if (st == State::Home || st == State::Jog) return Result::Busy;
  if (qCount >= MOVE_QUEUE_SIZE) return Result::QueueFull;
  const JointConfig &c = cfg[j];
  const bool zeroed = zeroMask & jbit(j);
  const int32_t target = baseTarget(j) + lroundf(deltaDeg * c.stepsPerDeg);
  if (zeroed) {
    const float deg = (float)target / c.stepsPerDeg;
    if (deg < c.minDeg - 0.001f || deg > c.maxDeg + 0.001f) return Result::Limit;
  } else if (speedPct > UNZEROED_SPEED_PCT) {
    speedPct = UNZEROED_SPEED_PCT;
  }
  MoveSeg s;
  s.useMask = jbit(j);
  s.speed = (speedPct < 1 ? 1 : (speedPct > 100 ? 100 : speedPct)) / 100.0f;
  for (uint8_t i = 0; i < NUM_JOINTS; i++) s.target[i] = 0;
  s.target[j] = target;
  queue[(qHead + qCount) % MOVE_QUEUE_SIZE] = s;
  qCount++;
  lastTarget[j] = target;
  queuedMask |= s.useMask;
  return Result::Ok;
}

void stop() {
  clearQueue();
  if (st == State::Move) {
    // Pārejam uz JOG ar mērķi 0 -> bremzē ar normālu paātrinājumu.
    applyLimits();
    for (uint8_t j = 0; j < NUM_JOINTS; j++) jogTarget[j] = 0;
    st = State::Jog;
  } else if (st == State::Jog) {
    for (uint8_t j = 0; j < NUM_JOINTS; j++) jogTarget[j] = 0;
  } else if (st == State::Home) {
    haltAll();
    applyLimits();
    st = State::Idle;
    pushEvent("[HOME] partraukts");
  }
}

void estop() {
  haltAll();
  clearQueue();
  applyLimits();
  st = State::Alarm;
#if ESTOP_DISABLES_MOTORS
  stepper::enable(false);
#endif
}

Result reset() {
  if (st == State::Alarm) st = State::Idle;
  return Result::Ok;
}

Result setEnabled(bool on) {
  if (!on) {
    haltAll();
    clearQueue();
    applyLimits();
    if (st != State::Alarm) st = State::Idle;
  }
  stepper::enable(on);
  return Result::Ok;
}

bool enabled() { return stepper::enabled(); }

void setHomeThenZero(bool on) { homeThenZero = on; }

Result home(int8_t j) {
  if (st == State::Alarm) return Result::Alarm;
  if (st != State::Idle || qCount) return Result::Busy;
  hr.n = 0;
  if (j >= 0) {
    if (j >= NUM_JOINTS) return Result::BadArg;
    if (!cfg[j].esEnabled) return Result::NoEndstop;
    hr.seq[hr.n++] = (uint8_t)j;
  } else {
    // visas ar slēdzi, kārtotas pēc homeOrder (vienādiem - pēc numura)
    for (uint16_t o = 0; o <= 255 && hr.n < NUM_JOINTS; o++)
      for (uint8_t i = 0; i < NUM_JOINTS; i++)
        if (cfg[i].esEnabled && cfg[i].homeOrder == o) hr.seq[hr.n++] = i;
    if (!hr.n) return Result::NoEndstop;
  }
  hr.idx = 0;
  hr.phase = HomePhase::Start;
  for (uint8_t i = 0; i < hr.n; i++) {
    zeroMask &= (uint8_t)~jbit(hr.seq[i]);
    homedMask &= (uint8_t)~jbit(hr.seq[i]);
  }
  autoEnable();
  st = State::Home;
  return Result::Ok;
}

bool popEvent(char *out, uint8_t size) {
  if (!evCount) return false;
  strncpy(out, evBuf[evHead], size - 1);
  out[size - 1] = 0;
  evHead = (uint8_t)((evHead + 1) % kEvMax);
  evCount--;
  return true;
}

// Locītavai ar gala slēdzi pēc HOME: ja pašreizējo vietu pārsauc par 'deg' grādiem, slēdža leņķis
// pārbīdās par tikpat -> nākamais HOME roku noliks tieši šeit (piem. ZERO vidū = vidus ir 0).
void shiftHomeRef(uint8_t j, float deg) {
  if (!cfg[j].esEnabled || !(homedMask & jbit(j))) return;
  const float d = deg - positionDeg(j);
  if (fabsf(d) < 0.005f) return;
  cfg[j].homePos += d;
  homeRefChanged = true;
  // bez %f (AVR printf to neprot): simtdaļas kā veseli skaitļi
  const long c = lroundf(cfg[j].homePos * 100.0f);
  const unsigned long a = (unsigned long)(c < 0 ? -c : c);
  char m[kEvLen];
  snprintf(m, sizeof m, "[ZERO] J%u sledzis tagad %s%lu.%02lu", j + 1, c < 0 ? "-" : "", a / 100, a % 100);
  pushEvent(m);
}

uint8_t endstopStops() { return esStops; }

bool takeHomeRefChanged() {
  const bool c = homeRefChanged;
  homeRefChanged = false;
  return c;
}

Result zero(int8_t j) {
  if (st != State::Idle && st != State::Alarm) return Result::Busy;
  if (j >= NUM_JOINTS) return Result::BadArg;
  for (uint8_t i = 0; i < NUM_JOINTS; i++) {
    if (j >= 0 && i != (uint8_t)j) continue;
    shiftHomeRef(i, 0.0f);
    stepper::setPosition(i, 0);
    zeroMask |= jbit(i);
  }
  applyLimits();
  return Result::Ok;
}

Result setPositionDeg(uint8_t j, float deg) {
  if (st != State::Idle && st != State::Alarm) return Result::Busy;
  if (j >= NUM_JOINTS || isnan(deg)) return Result::BadArg;
  shiftHomeRef(j, deg);
  stepper::setPosition(j, lroundf(deg * cfg[j].stepsPerDeg));
  zeroMask |= jbit(j);
  applyLimits();
  return Result::Ok;
}

State state() { return st; }

const char *stateName() {
  switch (st) {
    case State::Idle: return "Idle";
    case State::Jog: return "Jog";
    case State::Move: return "Move";
    case State::Home: return "Home";
    case State::Alarm: return "Alarm";
  }
  return "?";
}

bool isZeroed(uint8_t j) { return zeroMask & jbit(j); }
uint8_t queued() { return qCount; }

float positionDeg(uint8_t j) { return (float)stepper::position(j) / cfg[j].stepsPerDeg; }

JointConfig &joint(uint8_t j) { return cfg[j]; }

Result setDrive(uint8_t j, uint16_t motorSteps, uint16_t microsteps, float gear) {
  if (j >= NUM_JOINTS) return Result::BadArg;
  if (st != State::Idle && st != State::Alarm) return Result::Busy;
  if (motorSteps < 1 || motorSteps > 2000) return Result::BadArg;
  if (microsteps < 1 || microsteps > 256 || (microsteps & (microsteps - 1))) return Result::BadArg;   // 2^n
  if (!(gear > 0.001f && gear <= 1000.0f)) return Result::BadArg;
  JointConfig &c = cfg[j];
  const float deg = positionDeg(j);        // roka fiziski nekustas -> leņķis paliek
  c.motorSteps = motorSteps;
  c.microsteps = microsteps;
  c.gear = gear;
  c.stepsPerDeg = (float)motorSteps * (float)microsteps * gear / 360.0f;
  stepper::setPosition(j, lroundf(deg * c.stepsPerDeg));
  clearQueue();
  applyLimits();
  return Result::Ok;
}

Result configChanged() {
  if (st != State::Idle && st != State::Alarm) return Result::Busy;
  for (uint8_t j = 0; j < NUM_JOINTS; j++) stepper::setInvert(j, cfg[j].invert);
  applyLimits();
  return Result::Ok;
}

const char *resultName(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::NotZeroed: return "not_zeroed";
    case Result::Limit: return "limit";
    case Result::QueueFull: return "queue_full";
    case Result::Alarm: return "alarm";
    case Result::Busy: return "busy";
    case Result::BadArg: return "bad_arg";
    case Result::NoEndstop: return "no_endstop";
  }
  return "?";
}

}  // namespace motion
