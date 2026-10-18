#pragma once
// =============================================================================
//  Kustību vadība (locītavu telpā).
//   * JOG  - ātruma režīms pultij/web pogām. Paātrinājuma rampas, bremzēšana
//            pirms programmatūras limitiem, sargsuns (JOG_TIMEOUT_MS).
//   * MOVE - punkts-punkts kustības rindā. Visas locītavas sāk un beidz
//            vienlaicīgi (sinhronizēts trapecveida profils).
// =============================================================================
#include <stdint.h>
#include "config.h"

namespace motion {

enum class State : uint8_t { Idle, Jog, Move, Home, Alarm };

enum class Result : uint8_t { Ok, NotZeroed, Limit, QueueFull, Alarm, Busy, BadArg, NoEndstop };

struct JointConfig {
  // Piedziņa (maināma ar CFG MOTOR_STEPS / MICROSTEPS / GEAR)
  uint16_t motorSteps; // pilni soļi apgriezienā: 200 = 1.8° motors, 400 = 0.9° motors
  uint16_t microsteps; // 1, 2, 4 ... 256 (RAMPS: jāsakrīt ar jumperiem; SKR: ieraksta TMC)
  float gear;          // reduktora attiecība (piem. 9 = 9:1)
  float stepsPerDeg;   // aprēķināts: motorSteps * microsteps * gear / 360
  float minDeg, maxDeg;
  float maxSpeed;      // °/s
  float accel;         // °/s²
  bool invert;
  // gala slēdzis / HOME
  bool esEnabled;
  bool esInvert;       // true = NC slēdzis
  int8_t homeDir;      // -1 / +1
  float homePos;       // leņķis, kurā slēdzis nostrādā
  float homeSpeed;     // °/s
  float backoff;       // °
  uint8_t homeOrder;   // mazākais pirmais
};

void init();
void update();         // izsaukt pēc iespējas biežāk no loop()

// Ātrums % no maxSpeed katrai locītavai (-100..100). Masīvs NUM_JOINTS garumā.
Result jog(const float pct[NUM_JOINTS]);
// Absolūta kustība grādos. use[j]=false -> locītava paliek, kur ir.
Result queueMove(const float deg[NUM_JOINTS], const bool use[NUM_JOINTS], float speedPct);
// Relatīva kustība no pēdējā mērķa (vai pašreizējās pozīcijas).
Result queueRelative(const float delta[NUM_JOINTS], const bool use[NUM_JOINTS], float speedPct);

// Viens solis vienai locītavai (ekrāna grozāmā poga). Strādā arī bez nulles
// (tad bez limitiem, ātrums ierobežots kā jogam bez nulles).
Result nudge(uint8_t joint, float deltaDeg, float speedPct);

void stop();           // kontrolēta bremzēšana, rinda tiek iztīrīta
void estop();          // tūlītēja apstāšanās -> Alarm
Result reset();        // Alarm -> Idle
Result setEnabled(bool on);
bool enabled();

Result home(int8_t joint);                 // -1 = visas ar slēdzi (pēc homeOrder)
void setHomeThenZero(bool on);             // pēc HOME braukt uz 0° (noklusējums HOME_THEN_ZERO)
Result zero(int8_t joint);                 // -1 = visas
Result setPositionDeg(uint8_t joint, float deg);
// true, ja ZERO/SETPOS pēc HOME mainīja 'leņķi pie slēdža' (protokols tad saglabā konfigurāciju)
bool takeHomeRefChanged();
// Skaitītājs: palielinās katru reizi, kad gala slēdzis aptur kustību (SD programma tad apstājas)
uint8_t endstopStops();

State state();
const char *stateName();
bool isZeroed(uint8_t joint);
uint8_t queued();
float positionDeg(uint8_t joint);

JointConfig &joint(uint8_t j);
void loadDefaults();                       // rūpnīcas konfigurācija no config.h
// Maina piedziņu. Pozīcija grādos paliek tā pati (pārrēķina soļos). Tikai stāvot.
Result setDrive(uint8_t joint, uint16_t motorSteps, uint16_t microsteps, float gear);
Result configChanged();                    // pēc CFG izmaiņām (tikai stāvot)
const char *resultName(Result r);

// Notikumu ziņas protokolam ("[HOME] ...", "[ENDSTOP] ..."). false = nav ziņu.
bool popEvent(char *out, uint8_t size);

}  // namespace motion
