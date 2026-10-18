#pragma once
// =============================================================================
//  Robot rokas kontroliera konfigurācija
//  Visas vērtības šeit ir rūpnīcas noklusējumi. Daļu (ātrumi, paātrinājumi,
//  limiti, strāva) var mainīt darbības laikā ar komandu CFG — ESP32 tilts tās
//  saglabā un pēc restarta atkal nosūta.
// =============================================================================

#define FIRMWARE_NAME    "RobotRoka"
#define FIRMWARE_VERSION "0.1.0"

#define NUM_JOINTS 5
#define JOINT_NAMES { "J1", "J2", "J3", "J4", "J5" }

// --- Mehānika ----------------------------------------------------------------
// 17HM5417 ir 0.9° motors -> 400 pilni soļi uz apgriezienu.
#define MOTOR_FULL_STEPS { 400, 400, 400, 400, 400 }

// Mikrosoļi. RAMPS: JĀSAKRĪT ar MS jumperiem zem draiveriem!
// SKR + TMC2209 UART: firmware pats tos ieraksta draiverī.
#if defined(BOARD_SKR14_TURBO)
  #define JOINT_MICROSTEPS { 16, 16, 16, 16, 16 }
#else
  // RAMPS: A4988 ar 3 jumperiem (MS1+MS2+MS3) = 1/16 -> 160 soļi/° (kā Pronterface M92 X160)
  #define JOINT_MICROSTEPS { 16, 16, 16, 16, 16 }
#endif

// Reduktora attiecība (cikloidālais 9:1)
#define JOINT_GEAR_RATIO { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f }

// Ja ass griežas pretējā virzienā, nomaini uz true
#define JOINT_INVERT_DIR { false, false, false, false, false }

// --- Kustības limiti (izejas vārpstas grādos) ---------------------------------
// Programmatūras limiti attiecībā pret nulles pozīciju (ZERO).
#define JOINT_MIN_DEG   { -170.0f,  -90.0f, -135.0f, -120.0f, -180.0f }
#define JOINT_MAX_DEG   {  170.0f,   90.0f,  135.0f,  120.0f,  180.0f }
#define JOINT_MAX_SPEED {   45.0f,   30.0f,   45.0f,   60.0f,   90.0f }  // °/s
#define JOINT_ACCEL     {   60.0f,   40.0f,   60.0f,  120.0f,  180.0f }  // °/s²

// --- Gala slēdži un HOME -----------------------------------------------------
// Pini: boards.h ENDSTOP_PINS. Slēdzis starp pinu un GND (iekšējais pull-up).
// Visu var mainīt ar CFG / PC programmu un saglabāt ar SAVE.
#define ENDSTOP_ENABLED  { false, false, false, false, false }
#define ENDSTOP_INVERT   { false, false, false, false, false }  // false = NO (nospiests -> GND), true = NC
#define HOME_DIR         { -1, -1, -1, -1, -1 }      // uz kuru pusi atrodas slēdzis
#define HOME_POS_DEG     { -172.0f, -92.0f, -137.0f, -122.0f, -182.0f }  // leņķis, kurā slēdzis nostrādā
#define HOME_SPEED       { 15.0f, 10.0f, 15.0f, 20.0f, 30.0f }  // °/s meklēšanai (lēnā fāze = 1/4)
#define HOME_BACKOFF_DEG { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f }       // atkāpšanās no slēdža
#define HOME_ORDER       { 3, 1, 2, 4, 5 }   // secība: mazākais numurs pirmais (J2 -> J3 -> J1 -> J4 -> J5)
// Pēc HOME roka brauc uz 0° (vidus stāvoklis) visām tikko atrastajām locītavām, ja 0 ir limitos.
#define HOME_THEN_ZERO   1
#define HOME_ZERO_SPEED  30.0f   // % no locītavas max ātruma
// Ja slēdzis nostrādā parastas kustības laikā virzienā uz to -> ESTOP.
#define ENDSTOP_STOP_ON_HIT 1

// --- Drošība -----------------------------------------------------------------
// Ja JOG komanda netiek atjaunota šajā laikā, ass bremzē līdz 0.
// (pults vai pārlūks pazaudēja savienojumu -> roka apstājas pati)
#define JOG_TIMEOUT_MS 300

// Kamēr nulle nav uzstādīta (ZERO), limiti nav zināmi: jogs strādā tikai
// ar samazinātu ātrumu un MOVE komandas ir aizliegtas.
#define UNZEROED_SPEED_PCT    30
#define REQUIRE_ZERO_FOR_MOVE 1

// Kustības komanda automātiski ieslēdz draiverus.
#define AUTO_ENABLE 1

// ESTOP: 0 = soļi apstājas uzreiz, motori tur pozīciju (roka nekrīt).
//        1 = arī atslēdz draiverus (roka var nokrist!).
#define ESTOP_DISABLES_MOTORS 0

// --- Satvērējs (servo) -------------------------------------------------------
#define GRIPPER_ENABLED   1
#define GRIPPER_OPEN_US   1000   // impulss pie 0%   (atvērts)
#define GRIPPER_CLOSED_US 2000   // impulss pie 100% (aizvērts)
#define GRIPPER_START_PCT 0

// --- Ekrāns: RepRapDiscount Smart Controller (20x4 LCD + grozāmā poga + SD) ----
// RAMPS: caur "Smart Adapter" (AUX-3/AUX-4). SKR: EXP1/EXP2 kabeļi.
#define LCD_ENABLED          1       // ekrāna atbalsts iekļauts firmware
// Vai ekrāns un SD karte ir PIESLĒGTI - iestata PC programmā / web lapā
// (CFG LCD 0|1, CFG SD 0|1, SAVE). Šeit tikai noklusējums, ja atmiņā nekā nav.
#if defined(BOARD_SKR14_TURBO)
  #define LCD_DEFAULT_ON     0       // SKR: bez ekrāna pogas pins "peld" -> ieslēgt tikai ar ekrānu
#else
  #define LCD_DEFAULT_ON     1       // RAMPS: pini ar pull-up, bez ekrāna nekas nenotiek
#endif
#define SD_DEFAULT_ON        1
#define LCD_IDLE_TIMEOUT_MS  30000   // izvēlne -> statusa ekrāns pēc neaktivitātes
#define ENCODER_PULSES       4       // impulsi uz vienu pogas "klikšķi"
#define ENCODER_REVERSE      0       // 1, ja griežot pa labi vērtība samazinās
// Programmas no SD kartes (fails *.TXT no PC programmas "Eksportēt tekstu").
// Pagaidām tikai Mega/RAMPS; SKR SD karte - vēlāk.
#define SD_ENABLED           1

// --- Seriālie porti ----------------------------------------------------------
#define USB_BAUD 115200
#define ESP_BAUD 115200
// ESP portā pieņem tikai rindas ar kontrolsummu "*HH" (tilts to pievieno pats).
// USB portā kontrolsumma nav obligāta, lai var rakstīt ar roku.
#define ESP_REQUIRE_CHECKSUM 1

// --- TMC2209 (tikai SKR V1.4 Turbo, UART režīms) -----------------------------
#define TMC_RUN_CURRENT_MA { 1000, 1000, 1000, 800, 800 }  // RMS mA
#define TMC_HOLD_PCT       50      // turēšanas strāva % no darba strāvas
#define TMC_STEALTHCHOP    0       // 0 = SpreadCycle (vairāk momenta), 1 = kluss
#define TMC_RSENSE         0.11f   // BTT TMC2209 V1.2
#define TMC_UART_BAUD      57600
