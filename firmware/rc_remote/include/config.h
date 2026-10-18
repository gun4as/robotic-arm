#pragma once
// =============================================================================
//  Pults konfigurācija: ESP32-S3 (N16R8) Futaba T8J korpusā, ESP-NOW
//  Futabas pogu plate (BU4051 + 12 pogas + potenciometrs + LED + pīkstulis) caur
//  tās savienotājiem H1/H2, slēdži caur CD74HC4067, krāsu ekrāns ST7789 240x280.
//  Shēma un tabulas: docs/elektronika.md "Pults".
// =============================================================================

#define RC_NET_ID 0x1234        // JĀSAKRĪT ar esp32_bridge/include/config.h

// NEDRĪKST lietot: 0 (BOOT/kalibrācija), 35-37 (PSRAM), 43/44 (USB-UART), 46, 48 (RGB LED)

// --- Analogās ieejas (tikai ADC1 = GPIO 1-10; ADC2 nestrādā kopā ar ESP-NOW) -----
// Kārtība = kanāli RC_CH_*: labais ↔, labais ↕, kreisais ↕, kreisais ↔, potenciometrs, brīvs
#define ANALOG_PINS     { 1, 2, 5, 4, 6, -1 }   // kreisā stika asis pielodētas otrādi: ↕ = GPIO5, ↔ = GPIO4
#define ANALOG_INVERT   { false, false, false, false, false, false }
// Abi stiki centrējas; potenciometrs - nē
#define ANALOG_CENTERED { true, true, true, true, false, false }
#define BATT_PIN        7
#define BATT_DIVIDER    ((100.0f + 33.0f) / 33.0f)   // 100k / 33k
#define BATT_CAL        1.0328f        // korekcija: multimetra rādījums / ekrāna rādījums
#define BATT_LOW_MV     6600           // 2x 18650: zem šī - brīdinājums

// --- Multipleksori: kopīgas adreses ----------------------------------------------
// S0/S1/S2 = BU4051 A/B/C (H2.9/H2.8/H2.7) un HC4067 S0/S1/S2; S3 tikai HC4067
#define MUX_ADDR_PINS   { 13, 14, 47, 21 }
#define MUX_SETTLE_US   60             // gaidīšana pēc adreses maiņas (kondensatori uz BU4051 kanāliem)

// BU4051 (Futabas pogu plate): 8 trima pogas -> pogas 0..7
#define MUX8_COM_PIN    10             // H2.6 + 4.7k uz 3V3
// Tiešās trima pogas H1.3..H1.6 -> pogas 8..11
#define DIRECT_BTN_PINS { 38, 39, 40, 41 }
// Pārnumurēšana pēc novietojuma: [vadu numurs] = pogas numurs (P0/P1 = kreisā kolonna augšā,
// P2/P3 = kreisā kolonna apakšā, P4/P5 = zem kreisā stika, P6/P7 = labā kolonna augšā,
// P8/P9 = labā kolonna apakšā, P10/P11 = zem labā stika; pārī pirmā = "-", otrā = "+")
#define BTN_ORDER       { 6, 1, 0, 7, 2, 5, 3, 4, 8, 9, 11, 10 }

// CD74HC4067: slēdži (kanāls -> GND), SIG (ADC) + 10k uz 3V3, EN -> GND.
// Visus kanālus lasa kā spriegumu: < MUX16_LOW_MV = savienots ar GND.
#define MUX16_SIG_PIN   9
#define MUX16_LOW_MV    400
#define SW_CHANNELS     { 0, 5, 4, 3, 2 }   // 2 pozīciju slēdži SW1..SW5 (SW4 = shēmas SW5, SW5 = shēmas SW4)
// true = slēdzis apgriezts ("ieslēgts", kad kanāls NAV savienots ar GND)
#define SW_INVERT       { true, true, true, true, true }   // Futabas slēdži: "izslēgts" = pie GND
// Burti kā uz Futabas pults: SW1..SW5 = G, F, A, B, D
#define SW_LABELS       { "G", "F", "A", "B", "D" }
#define SW_ARM_BIT      0              // SW1 = ARM
// Ieslēdzot pults gaida, kamēr visi slēdži ir sākuma pozīcijā (pīkst un rāda, kurš jāpārslēdz):
// 2 pozīciju slēdži - izslēgti; 3 pozīciju E (ātrums) un C (režīms) - šajā pozīcijā (0/1/2)
#define SW_STARTUP_CHECK 1
#define SW3_HOME        { 0, 0 }       // E = lēni, C = vadība
#define SW3_INVERT      { true, false } // E, C: apgriezt galus (0 <-> 2), vidus paliek vidus
#define SW_ESTOP_BIT    1              // SW2 = AVĀRIJAS STOP
// 3 pozīciju slēdži uz viena kanāla: COM -> kanāls, ON1 -> GND (uz plates), ON2 -> 4.7k -> GND
//   ~0 V = pozīcija 0,  ~1.05 V = pozīcija 2,  ~3.3 V (vidus) = pozīcija 1
#define SW3_CHANNELS    { 7, 6 }       // E = ātrums (C7), C = režīms (C6)
#define SW3_MID_MV      { 400, 2200 }  // robežas: zem 1. = poz. 0, starp = poz. 2, virs 2. = poz. 1
#define MOMENT_CHANNEL  1              // nefiksējošā poga (KEY1) -> poga 12
#define MOMENT_NC       1              // 1 = normāli slēgta (miera stāvoklī pie GND, nospiežot atvienojas)
// USB uzlāde (CN3302 IN+ -> 10k -> C10 -> 15k -> GND): kanāls augstā līmenī = USB pievienots
#define USB_CHANNEL     -1             // -1 = nav pieslēgts (ieslēgšanai: 10)
#define BATT_FULL_MV    8300           // ar USB un virs šī = pilna

// --- Gaisma un skaņa --------------------------------------------------------------
// Futabas LED: kopējais plus (H1.1) -> 3V3, mīnusi -> ESP: deg, kad kāja = 0
#define LED_PINS        { 3, 8, 42 }   // sarkanā (H1.8), zaļā (H1.9), zilā (H1.7)
#define LED_ACTIVE_LOW  1
#define LED_BRIGHTNESS  255            // 0..255
#define BUZZER_PIN      45             // -> 1 kΩ -> H2.4 (tranzistora bāze)
#define BUZZER_HZ       2700

// --- Ekrāns ST7789 1.69" 240x280 (SPI) --------------------------------------------
#define TFT_SCLK        12             // uz ekrāna "SCL"
#define TFT_MOSI        11             // uz ekrāna "SDA"
#define TFT_CS          15
#define TFT_DC          16
#define TFT_RST         17
#define TFT_BL          18             // gaismas spilgtums (PWM)
#define TFT_BRIGHTNESS  200            // 0..255
#define TFT_OFFSET_Y    20             // 280 rindu panelis 320 rindu atmiņā
// 0 = stāvus (kontakti augšā), 1 / 3 = guļus (kontakti pa kreisi / pa labi), 2 = otrādi
#define TFT_ROTATION    1

#define CAL_BUTTON_PIN  0              // BOOT poga: turi 2 s -> kalibrācija
#define CAL_MOMENT_MS   5000           // vai nefiksējošā poga tik ilgi (ar izslēgtu ARM)

// --- Atjaunināšana pa Wi-Fi (korpuss aizskrūvēts): ieslēdz pulti, turot nefiksējošo pogu ---
#define OTA_AP_SSID     "RobotRoka-Pults"
#define OTA_AP_PASS     "robotroka123"   // vismaz 8 simboli
#define OTA_HOSTNAME    "robotroka-pults"

// --- Radio --------------------------------------------------------------------
#define ESPNOW_SEND_HZ  50
#define ESPNOW_HOP_MS   600            // bez atbildes tik ilgi -> nākamais Wi-Fi kanāls
