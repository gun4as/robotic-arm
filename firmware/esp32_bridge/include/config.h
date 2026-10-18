#pragma once
// =============================================================================
//  Tilta konfigurācija (ESP32 / ESP32-S3 / Wemos D1 mini ESP8266)
// =============================================================================

#define BRIDGE_VERSION "0.1.0"

// --- Wi-Fi --------------------------------------------------------------------
// Tilts vienmēr veido savu piekļuves punktu (AP). Ja web lapā ievadīts mājas
// tīkls, tas papildus pieslēdzas arī tam (AP+STA).
#define WIFI_AP_SSID    "RobotRoka"
#define WIFI_AP_PASS    "robotroka"     // vismaz 8 simboli
#define WIFI_AP_CHANNEL 6               // ESP-NOW pults atradīs kanālu pati
#define MDNS_NAME       "robotroka"     // http://robotroka.local
// Mājas tīkls pēc noklusējuma (ja tukšs - iestata web lapā, cilnē "Wi-Fi / info").
// Web lapā saglabātais tīkls ir svarīgāks par šo.
#define WIFI_STA_SSID   ""
#define WIFI_STA_PASS   ""

// --- Savienojums ar kontrolieri (RAMPS / SKR) un plates pini ----------------------
// Kontroliera TX -> ESP RX  (RAMPS 5V: caur līmeņa pārveidotāju / dalītāju 1k+2k!)
#define CTRL_BAUD       115200
#define CTRL_STATUS_MS  100             // AUTO statusa periods

#if defined(ESP8266)
  // Wemos D1 mini. Aparatūras Serial tiek pārslēgts (swap) uz D7/D8, lai USB paliek brīvs
  // firmware ielādei. Atkļūdošanas izvade: Serial1 = D4 (tikai TX).
  #define CTRL_SERIAL     Serial
  #define CTRL_RX_PIN     13              // D7
  #define CTRL_TX_PIN     15              // D8
  #define DBG             Serial1
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 DevKitC (N16R8): GPIO 35-37 aizņem PSRAM, 19/20 - USB, 0/3/45/46 - palaišana
  #define CTRL_SERIAL     Serial2
  #define CTRL_RX_PIN     18
  #define CTRL_TX_PIN     17
  #define STATUS_RGB_PIN  48              // iebūvētā krāsainā LED (dažām platēm 38)
  #define LORA_PIN_SCK    12
  #define LORA_PIN_MISO   13
  #define LORA_PIN_MOSI   11
  #define LORA_PIN_SS     10
  #define LORA_PIN_RST    14
  #define LORA_PIN_DIO0   21
  #define DBG             Serial
#else
  // ESP32 DevKit (WROOM-32)
  #define CTRL_SERIAL     Serial2
  #define CTRL_RX_PIN     16
  #define CTRL_TX_PIN     17
  #define STATUS_LED_PIN  2
  #define LORA_PIN_SCK    18
  #define LORA_PIN_MISO   19
  #define LORA_PIN_MOSI   23
  #define LORA_PIN_SS     5
  #define LORA_PIN_RST    14
  #define LORA_PIN_DIO0   26
  #define DBG             Serial
#endif

// --- Pults (kopīgs abiem radio) -------------------------------------------------
#define RC_NET_ID       0x1234          // JĀSAKRĪT ar pulti
#define RC_JOG_HZ       25              // cik bieži sūtīt JOG kontrolierim
#define RC_DEADBAND     0.06f           // stika "mirušā zona" (0..1)
#define RC_EXPO         0.5f            // 0 = lineārs, 1 = ļoti mīksts centrā
#define RC_PRIORITY_MS  500             // pēc pēdējās RC kustības web vadība bloķēta

// Ātrums no 3 pozīciju slēdža A (pozīcija 0 / 1 = vidus / 2), % no locītavas max ātruma
#define RC_SPEED_LEVELS { 25, 50, 100 }

// Stiki -> locītavas: {kanāls, virziens}; -1 = nav. Kanāli: rc_protocol.h RC_CH_*
// BANK B ieslēdz slēdzis SW_BANK: tad kreisais stiks ↔ vada J5, nevis J4.
#define RC_MAP_BANK_A { {RC_CH_RX, 1}, {RC_CH_RY, 1}, {RC_CH_LY, 1}, {RC_CH_LX, 1}, {-1, 0} }
#define RC_MAP_BANK_B { {RC_CH_RX, 1}, {RC_CH_RY, 1}, {RC_CH_LY, 1}, {-1, 0}, {RC_CH_LX, 1} }
// 2 pozīciju slēdži (biti): 0 = ARM, 1 = AVĀRIJA (abi apstrādā pults), 2 = BANK, 3-4 brīvi
#define SW_BANK         2
#define RC_GRIPPER_CH   RC_CH_POT       // potenciometrs -> satvērējs (-1 lai atslēgtu)

// Trima pogas 0..11 -> {locītava 1..5 vai 6 = satvērējs, virziens}.
// Kura fiziskā poga ir kurš numurs, pults ekrāns parāda nospiežot ("P0".."P11").
// Noklusējums pēc pults trimu novietojuma: P0/P1 satvērējs, P2/P3 J3, P4/P5 J4, P6/P7 J5, P8/P9 J2, P10/P11 J1
#define RC_TRIM_MAP { {6, -1}, {6, 1}, {3, -1}, {3, 1}, {4, -1}, {4, 1},                       {5, -1}, {5, 1}, {2, -1}, {2, 1}, {1, -1}, {1, 1} }
#define RC_TRIM_STEP_DEG  0.5f          // viens spiediens = tik grādu
#define RC_TRIM_DELAY_MS  400           // turot pogu: pēc tik ilga laika...
#define RC_TRIM_REPEAT_MS 120           // ...atkārto ik pēc tik
#define RC_GRIP_STEP      5             // satvērēja pogas: % uz spiedienu
#define RC_HOME_HOLD_MS   2000          // nefiksējošā poga turēta tik ilgi -> HOME visām
// Nefiksējošā poga = Shift: turot + locītavas trims "+" = ZERO (nulle), "-" = HOME šai locītavai

// --- ESP-NOW ------------------------------------------------------------------
#define ESPNOW_TIMEOUT_MS 300

// --- LoRa (SX1276/SX1278) -----------------------------------------------------
#define LORA_FREQ       433775000L      // Hz (433.05-434.79 MHz ISM josla)
#define LORA_SF         7
#define LORA_BW         500E3
#define LORA_SYNC       0x5A
#define LORA_TIMEOUT_MS 600

#define NUM_JOINTS 5
