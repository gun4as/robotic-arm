// =============================================================================
//  Robot rokas pults: ESP32-S3 Futaba T8J korpusā
//   - stiki, potenciometrs, 12 trima pogas (BU4051 + 4 tiešās), nefiksējošā poga,
//     5 slēdži ar 2 pozīcijām un 2 ar 3 pozīcijām (caur CD74HC4067)
//   - sūta RcControlPacket caur ESP-NOW (50 Hz), saņem rokas pozīciju (ekrānam)
//   - krāsu ekrāns ST7789, Futabas LED (sarkana/zaļa/zila) un pīkstulis
//
//  Kalibrācija: turi BOOT 2 s.  1) stiki centrā -> BOOT
//                               2) visus stikus un potenciometru līdz galiem -> BOOT
//  Seriālais ports (115200) ik pēc 1 s drukā visu ievadu stāvokli (pārbaudei).
// =============================================================================
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPUpdateServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config.h"
#include "rc_protocol.h"
#include "screen.h"

namespace {

const int8_t kAnalogPins[RC_NUM_CHANNELS] = ANALOG_PINS;
const bool kInvert[RC_NUM_CHANNELS] = ANALOG_INVERT;
const bool kCentered[RC_NUM_CHANNELS] = ANALOG_CENTERED;
const uint8_t kMuxAddr[4] = MUX_ADDR_PINS;
const uint8_t kDirectBtn[] = DIRECT_BTN_PINS;
const uint8_t kBtnOrder[12] = BTN_ORDER;
const uint8_t kSwCh[] = SW_CHANNELS;
const bool kSwInvert[] = SW_INVERT;
constexpr uint8_t kNumSwitches = sizeof(kSwCh);
const uint8_t kSw3Ch[2] = SW3_CHANNELS;
const uint16_t kSw3Mv[2] = SW3_MID_MV;
const bool kSw3Invert[2] = SW3_INVERT;
const uint8_t kLedPins[3] = LED_PINS;

struct Cal { uint16_t min, mid, max; };
Cal cal[RC_NUM_CHANNELS];
float filt[RC_NUM_CHANNELS];

Preferences prefs;
uint16_t seq = 0;
uint32_t lastSendMs = 0;
bool armReady = false;   // ARM jābūt izslēgtam vismaz reizi pēc ieslēgšanas
enum class CalState : uint8_t { Off, Center, Range } calState = CalState::Off;
uint32_t calBtnSince = 0;
bool calBtnPrev = false;
bool calIgnoreRelease = false;   // atlaišana pēc 2 s turēšanas nav apstiprinājums

// ievadu stāvoklis
uint16_t buttons = 0, btnPrevRaw = 0;
uint16_t mux16 = 0, mux16PrevRaw = 0;   // HC4067 kanāli (bits = 1, ja kanāls savienots ar GND)
uint16_t mux16Mv[16];                   // HC4067 kanālu spriegumi (3 pozīciju slēdžiem)
uint8_t switches = 0, sw3 = 0;
int8_t lastPressed = -1;
uint32_t lastPressedMs = 0;

// atbilde no rokas
portMUX_TYPE telMux = portMUX_INITIALIZER_UNLOCKED;
RcTelemetryPacket tel = {};
volatile uint32_t lastTelemMs = 0;

// ------------------------------------------------------------ kalibrācija
void loadCal() {
  for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) cal[i] = {200, 2048, 3900};
  if (prefs.getBytesLength("cal") == sizeof(cal)) prefs.getBytes("cal", cal, sizeof(cal));
}

void saveCal() { prefs.putBytes("cal", cal, sizeof(cal)); }

uint16_t readRaw(uint8_t i) {
  if (kAnalogPins[i] < 0) return 2048;
  uint32_t s = 0;
  for (uint8_t k = 0; k < 4; k++) s += analogRead(kAnalogPins[i]);
  return (uint16_t)(s / 4);
}

int16_t mapChannel(uint8_t i, float raw) {
  if (kAnalogPins[i] < 0) return 0;
  const Cal &c = cal[i];
  float v;
  if (kCentered[i]) {
    if (raw >= c.mid) v = (raw - c.mid) / (float)max(1, c.max - c.mid);
    else v = (raw - c.mid) / (float)max(1, c.mid - c.min);
  } else {
    v = (raw - c.min) / (float)max(1, c.max - c.min) * 2.0f - 1.0f;
  }
  v = constrain(v, -1.0f, 1.0f);
  if (kInvert[i]) v = -v;
  return (int16_t)(v * 1000.0f);
}

void handleCalButton() {
  // BOOT poga (iekšā korpusā) vai nefiksējošā poga ar izslēgtu ARM (no ārpuses)
  const bool boot = digitalRead(CAL_BUTTON_PIN) == LOW;
  const bool moment = (buttons & (1u << RC_BTN_MOMENT)) && !(switches & (1u << SW_ARM_BIT));
  const bool pressed = boot || moment;
  const uint32_t now = millis();
  if (pressed && !calBtnPrev) calBtnSince = now;

  if (calState == CalState::Off) {
    if (pressed && now - calBtnSince > (boot ? 2000u : (uint32_t)CAL_MOMENT_MS)) {
      calState = CalState::Center;
      Serial.println("[cal] 1) atlaid stikus centrā un īsi nospied nefiksējošo pogu (vai BOOT)");
      calIgnoreRelease = true;
    }
  } else if (!pressed && calBtnPrev && calIgnoreRelease) {
    calIgnoreRelease = false;
  } else if (!pressed && calBtnPrev) {   // atlaišana = apstiprinājums
    if (calState == CalState::Center) {
      for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) {
        const uint16_t r = readRaw(i);
        cal[i] = {r, r, r};
      }
      calState = CalState::Range;
      Serial.println("[cal] 2) izkustini visus stikus un potenciometru līdz galiem, tad īsi nospied pogu");
    } else if (calState == CalState::Range) {
      for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++)
        if (!kCentered[i]) cal[i].mid = (cal[i].min + cal[i].max) / 2;
      saveCal();
      calState = CalState::Off;
      Serial.println("[cal] saglabāts");
    }
  }
  if (calState == CalState::Range) {
    for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) {
      const uint16_t r = readRaw(i);
      if (r < cal[i].min) cal[i].min = r;
      if (r > cal[i].max) cal[i].max = r;
    }
  }
  calBtnPrev = pressed;
}

// ------------------------------------------------------------ pogas un slēdži
// Abi multipleksori lieto kopīgas adreses: 16 adresēm nolasām HC4067 SIG, pirmajām 8 arī
// BU4051 COM. Uz BU4051 kanāliem ir kondensatori, tāpēc pēc adreses maiņas mazliet pagaidām.
void scanMux(uint16_t &bu4051, uint16_t &hc4067) {
  bu4051 = hc4067 = 0;
  for (uint8_t a = 0; a < 16; a++) {
    for (uint8_t k = 0; k < 4; k++) digitalWrite(kMuxAddr[k], (a >> k) & 1);
    delayMicroseconds(MUX_SETTLE_US);
    const uint16_t mv = (uint16_t)analogReadMilliVolts(MUX16_SIG_PIN);
    mux16Mv[a] = mv;
    if (mv < MUX16_LOW_MV) hc4067 |= (uint16_t)(1u << a);
    if (a < 8 && digitalRead(MUX8_COM_PIN) == LOW) bu4051 |= (uint16_t)(1u << a);
  }
}

// Stāvoklis mainās tikai, ja divi nolasījumi pēc kārtas sakrīt (pret drebēšanu)
uint16_t debounce(uint16_t stable, uint16_t raw, uint16_t &prevRaw) {
  const uint16_t agree = (uint16_t)~(raw ^ prevRaw);
  prevRaw = raw;
  return (uint16_t)((stable & ~agree) | (raw & agree));
}

void readInputs() {
  uint16_t m8, m16;
  scanMux(m8, m16);
  uint16_t wired = m8;   // vadu secībā: 0..7 = BU4051, 8..11 = tiešās
  for (uint8_t i = 0; i < sizeof(kDirectBtn); i++)
    if (digitalRead(kDirectBtn[i]) == LOW) wired |= (uint16_t)(1u << (8 + i));
  uint16_t b = 0;      // pogu numuri pēc novietojuma (BTN_ORDER)
  for (uint8_t i = 0; i < 12; i++)
    if (wired & (1u << i)) b |= (uint16_t)(1u << kBtnOrder[i]);
  if (((m16 >> MOMENT_CHANNEL) & 1) != MOMENT_NC) b |= (uint16_t)(1u << RC_BTN_MOMENT);

  const uint16_t newB = debounce(buttons, b, btnPrevRaw);
  const uint16_t pressedNow = newB & ~buttons;
  buttons = newB;
  for (uint8_t i = 0; i <= RC_BTN_MOMENT; i++)
    if (pressedNow & (1u << i)) {
      lastPressed = (int8_t)i;
      lastPressedMs = millis();
      Serial.printf("[poga] P%u nospiesta (BU4051 %02X, tiešās %X)\n", i, m8, (wired >> 8) & 0xF);
    }

  mux16 = debounce(mux16, m16, mux16PrevRaw);
  uint8_t s = 0;
  for (uint8_t i = 0; i < kNumSwitches; i++)
    if (((mux16 >> kSwCh[i]) & 1) != kSwInvert[i]) s |= (uint8_t)(1u << i);
  switches = s;
  // 3 pozīciju: ~0 V = 0, ~1 V = 2, ~3.3 V (vidus) = 1. Mainās tikai, ja 2 nolasījumi sakrīt.
  static uint8_t prevP[2] = {1, 1};
  uint8_t p[2];
  for (uint8_t i = 0; i < 2; i++) {
    const uint16_t mv = mux16Mv[kSw3Ch[i]];
    uint8_t now = mv < kSw3Mv[0] ? 0 : (mv < kSw3Mv[1] ? 2 : 1);
    if (kSw3Invert[i]) now = (uint8_t)(2 - now);
    p[i] = (now == prevP[i]) ? now : (uint8_t)((sw3 >> (2 * i)) & 3);
    prevP[i] = now;
  }
  sw3 = (uint8_t)(p[0] | (p[1] << 2));
}

// ------------------------------------------------------------ radio (ESP-NOW)
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t channel = 1;
uint8_t savedChannel = 0;
uint32_t channelSinceMs = 0;

void onTelemetry(const uint8_t *data, int len) {
  if (len != (int)sizeof(RcTelemetryPacket)) return;
  RcTelemetryPacket t;
  memcpy(&t, data, sizeof t);
  if (!rc_valid(t, RC_PKT_TELEMETRY, RC_NET_ID)) return;
  portENTER_CRITICAL(&telMux);
  tel = t;
  portEXIT_CRITICAL(&telMux);
  lastTelemMs = millis();
}
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t *, const uint8_t *d, int l) { onTelemetry(d, l); }
#else
void onRecv(const uint8_t *, const uint8_t *d, int l) { onTelemetry(d, l); }
#endif

void setChannel(uint8_t ch) {
  channel = ch;
  channelSinceMs = millis();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

bool linkOk() { return lastTelemMs && millis() - lastTelemMs < 1000; }

void radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) { Serial.println("[radio] ESP-NOW kļūda"); return; }
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, kBroadcast, 6);
  p.channel = 0;
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
  savedChannel = prefs.getUChar("chan", 6);
  setChannel(savedChannel);
  Serial.printf("[radio] ESP-NOW, kanāls %u\n", channel);
}

// Ja tilts neatbild, pārslēdzam kanālus 1..13 (tilts var būt mājas Wi-Fi kanālā).
void radioLoop() {
  const uint32_t now = millis();
  if (linkOk()) {
    if (savedChannel != channel) { savedChannel = channel; prefs.putUChar("chan", channel); }
    return;
  }
  const uint32_t lt = lastTelemMs;
  const uint32_t since = lt > channelSinceMs ? lt : channelSinceMs;
  if (now - since > ESPNOW_HOP_MS) setChannel(channel >= 13 ? 1 : channel + 1);
}

// ------------------------------------------------------------ LED un pīkstulis
constexpr uint8_t kLedCh0 = 0, kBuzzCh = 4;   // 7 = ekrāna gaisma
uint32_t beepUntil = 0;

void ledSet(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t v[3] = {r, g, b};
  for (uint8_t i = 0; i < 3; i++) {
    const uint32_t d = (uint32_t)v[i] * LED_BRIGHTNESS / 255;
    ledcWrite(kLedCh0 + i, LED_ACTIVE_LOW ? 255 - d : d);
  }
}

void beep(uint16_t ms, uint16_t hz = BUZZER_HZ) {
  ledcWriteTone(kBuzzCh, hz);
  beepUntil = millis() + ms;
}

void buzzerLoop() {
  if (beepUntil && (int32_t)(millis() - beepUntil) >= 0) {
    ledcWriteTone(kBuzzCh, 0);
    beepUntil = 0;
  }
}

void updateLed(bool armed, bool estop, uint8_t mode, uint8_t ctrlState) {
  const uint32_t t = millis();
  const bool blink = (t / 250) & 1;
  if (millis() - lastPressedMs < 60) { ledSet(255, 255, 255); return; }        // poga: balts uzplaiksnījums
  if (calState != CalState::Off) { ledSet(blink ? 255 : 0, 0, blink ? 0 : 255); return; }
  if (estop || ctrlState == 4) { ledSet(255, 0, 0); return; }                // avārija
  if (!linkOk()) { ledSet(blink ? 255 : 0, blink ? 160 : 0, 0); return; }   // dzeltena mirgo
  if (mode == 1) { ledSet(255, 0, 255); return; }                             // mācīšana: violeta
  if (armed) { ledSet(0, 0, 255); return; }                                   // vada: zila
  ledSet(0, 255, 0);                                                          // saite, ARM izslēgts
}

// ------------------------------------------------------------ atjaunināšana pa Wi-Fi
bool otaMode = false;
volatile int8_t otaPct = 0;
WebServer web(80);
HTTPUpdateServer httpUpdater;

char otaIp[20] = "";

void otaDraw() {
  ScreenData d = {};
  d.ota = otaPct;
  d.otaIp = otaIp[0] ? otaIp : nullptr;
  screen::draw(d);
}

// Sākumlapa: mājas Wi-Fi iestatīšana (ievada lietotājs no telefona) + saite uz /update
void otaPage() {
  String h = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
               "<title>Pults</title><body style='font-family:sans-serif;background:#15181d;color:#e6e9ee;"
               "max-width:420px;margin:20px auto;padding:0 16px'><h2>Robot rokas pults</h2>");
  h += otaIp[0] ? "<p>Mājas tīklā: <b>" + String(otaIp) + "</b></p>" : String(F("<p>Mājas tīkls: nav pieslēgts</p>"));
  h += F("<form method=post action=/wifi><p>Mājas Wi-Fi nosaukums<br><input name=s style='width:100%'></p>"
         "<p>Parole<br><input name=p type=password style='width:100%'></p>"
         "<p><button>Saglabāt un pieslēgties</button></p></form>"
         "<p><a style='color:#4c8dff' href=/update>Ielādēt programmu (firmware.bin)</a></p>");
  web.send(200, "text/html", h);
}

void otaWifiSave() {
  const String s = web.arg("s"), p = web.arg("p");
  if (s.length()) {
    prefs.putString("wssid", s);
    prefs.putString("wpass", p);
    WiFi.begin(s.c_str(), p.c_str());
  }
  web.send(200, "text/html", F("<meta charset=utf-8><body style='font-family:sans-serif'>Saglabāts. "
                                "Pults pieslēdzas - IP adrese parādīsies tās ekrānā. <a href=/>Atpakaļ</a>"));
}

void otaBegin() {
  otaMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(OTA_AP_SSID, OTA_AP_PASS);
  const String ssid = prefs.getString("wssid", "");
  if (ssid.length()) WiFi.begin(ssid.c_str(), prefs.getString("wpass", "").c_str());   // mājas tīkls
  httpUpdater.setup(&web);                    // http://<IP>/update
  web.on("/", otaPage);
  web.on("/wifi", HTTP_POST, otaWifiSave);
  web.begin();
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();                         // pio run -e s3_ota -t upload
  Update.onProgress([](size_t done, size_t total) {
    const int8_t p = total ? (int8_t)(done * 100 / total) : 0;
    if (p != otaPct) { otaPct = p < 1 ? 1 : p; otaDraw(); }
  });
  ledSet(255, 120, 0);
  beep(300, 2000);
  Serial.printf("[ota] Wi-Fi %s, http://%s/update\n", OTA_AP_SSID, WiFi.softAPIP().toString().c_str());
  otaDraw();
}

// stiku galējās vērtības starp diagnostikas rindām (lai īsas kustības netiktu palaistas garām)
int16_t extMin[5] = {1000, 1000, 1000, 1000, 1000}, extMax[5] = {-1000, -1000, -1000, -1000, -1000};

// Slēdži, kas nav sākuma pozīcijā (biti kā ScreenData::swWarn)
uint8_t switchWarn() {
  const uint8_t home3[2] = SW3_HOME;
  uint8_t w = switches & 0x1F;                     // 2 pozīciju: ieslēgts = nav sākumā
  if ((sw3 & 3) != home3[0]) w |= 0x20;
  if (((sw3 >> 2) & 3) != home3[1]) w |= 0x40;
  return w;
}

// Ieslēdzot: gaidām, kamēr visi slēdži ir sākuma pozīcijā (roku pults vēl nevada, radio nav ieslēgts)
void startupSwitchCheck() {
#if SW_STARTUP_CHECK
  uint32_t lastBeep = 0, lastDraw = 0;
  uint8_t prev = 0xFF;
  for (;;) {
    readInputs();
    const uint8_t w = switchWarn();
    if (!w) break;
    const uint32_t now = millis();
    if (w != prev || now - lastDraw > 500) {
      ScreenData d = {};
      d.ota = -1;
      d.swWarn = w;
      screen::draw(d);
      prev = w;
      lastDraw = now;
    }
    if (now - lastBeep > 1000) { beep(120, 1800); lastBeep = now; }
    ledSet(((now / 250) & 1) ? 255 : 0, 0, 0);
    buzzerLoop();
    delay(20);
  }
  if (prev != 0xFF) beep(60, 3200);   // viss kārtībā - turpinām
#endif
}

void printDiag(uint16_t battMv) {
  Serial.printf("[ext] %d %d %d %d %d %d %d %d %d %d\n", extMin[0], extMax[0], extMin[1], extMax[1], extMin[2],
                extMax[2], extMin[3], extMax[3], extMin[4], extMax[4]);
  for (uint8_t i = 0; i < 5; i++) { extMin[i] = 1000; extMax[i] = -1000; }
  Serial.printf("[in] stiki %5d %5d %5d %5d  pot %5d | pogas ", mapChannel(0, filt[0]), mapChannel(1, filt[1]),
                mapChannel(2, filt[2]), mapChannel(3, filt[3]), mapChannel(4, filt[4]));
  for (int8_t i = RC_BTN_MOMENT; i >= 0; i--) Serial.print((buttons >> i) & 1);
  Serial.print(" | HC4067 C15..C0 ");
  for (int8_t i = 15; i >= 0; i--) Serial.print((mux16 >> i) & 1);
  Serial.printf(" | SW %u%u%u%u%u | 3poz A=%u (%umV) B=%u (%umV) | bat %umV | saite %d\n", switches & 1, (switches >> 1) & 1,
                (switches >> 2) & 1, (switches >> 3) & 1, (switches >> 4) & 1, sw3 & 3, mux16Mv[kSw3Ch[0]], (sw3 >> 2) & 3, mux16Mv[kSw3Ch[1]], battMv,
                linkOk());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Ekrāns (SPI) vispirms: SPI kopnes inicializācija var pārņemt GPIO13 (MISO) u.c.,
  // tāpēc multipleksoru kājas iestatām pēc tās.
  screen::begin();
  pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);
  for (uint8_t i = 0; i < 4; i++) { pinMode(kMuxAddr[i], OUTPUT); digitalWrite(kMuxAddr[i], LOW); }
  pinMode(MUX8_COM_PIN, INPUT_PULLUP);
  pinMode(MUX16_SIG_PIN, INPUT);   // ārējais 10k pull-up, lasa ar ADC
  for (uint8_t i = 0; i < sizeof(kDirectBtn); i++) pinMode(kDirectBtn[i], INPUT_PULLUP);
  for (uint8_t i = 0; i < 3; i++) { ledcSetup(kLedCh0 + i, 1000, 8); ledcAttachPin(kLedPins[i], kLedCh0 + i); }
  ledSet(0, 0, 0);
  ledcSetup(kBuzzCh, BUZZER_HZ, 8);
  ledcAttachPin(BUZZER_PIN, kBuzzCh);
  ledcWriteTone(kBuzzCh, 0);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  prefs.begin("rcremote", false);
  loadCal();
  for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) filt[i] = readRaw(i);
  // Slēdžu stāvoklis jānolasa stabili PIRMS pirmā loop(): citādi (sākumā viss = 0)
  // ARM uz mirkli izskatītos izslēgts, un armReady atļautu vadību ar jau ieslēgtu ARM.
  for (uint8_t k = 0; k < 3; k++) { readInputs(); delay(5); }
  // Nefiksējošā poga turēta ieslēdzot -> atjaunināšanas režīms (roku nevada)
  if (buttons & (1u << RC_BTN_MOMENT)) { otaBegin(); return; }
  startupSwitchCheck();
  radioBegin();
  beep(80);
  Serial.printf("[pults] NET_ID=0x%04X, %u Hz\n", RC_NET_ID, ESPNOW_SEND_HZ);
}

void loop() {
  if (otaMode) {
    web.handleClient();
    ArduinoOTA.handle();
    buzzerLoop();
    // mājas tīkla IP uz ekrāna, kad pieslēdzas
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up != (otaIp[0] != 0)) {
      if (up) snprintf(otaIp, sizeof otaIp, "%s", WiFi.localIP().toString().c_str());
      else otaIp[0] = 0;
      if (up) Serial.printf("[ota] mājas tīklā: %s\n", otaIp);
      otaDraw();
    }
    return;
  }
  handleCalButton();
  radioLoop();
  buzzerLoop();
  readInputs();

  const bool swArm = switches & (1u << SW_ARM_BIT);
  const bool swEstop = switches & (1u << SW_ESTOP_BIT);
  if (!swArm) armReady = true;
  const bool armed = swArm && armReady && !swEstop && calState == CalState::Off;

  // Vienkāršs IIR filtrs pret ADC troksni
  for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) filt[i] += 0.35f * ((float)readRaw(i) - filt[i]);
  for (uint8_t i = 0; i < 5; i++) {
    const int16_t v = mapChannel(i, filt[i]);
    if (v < extMin[i]) extMin[i] = v;
    if (v > extMax[i]) extMax[i] = v;
  }

  // skaņas: saite, ARM, pogas
  static bool prevLink = false, prevArmed = false;
  static uint16_t prevButtons = 0;
  const bool link = linkOk();
  if (link && !prevLink) beep(60, 3200);
  if (!link && prevLink) beep(300, 1800);
  if (armed != prevArmed) beep(armed ? 120 : 60, armed ? 3200 : 2200);
  if (buttons & ~prevButtons) beep(12);
  prevLink = link;
  prevArmed = armed;
  prevButtons = buttons;

  // Baterija: vidējota (ADC troksnis ~±70 mV), bet lielu lēcienu pieņem uzreiz
  const float battRaw = analogReadMilliVolts(BATT_PIN) * BATT_DIVIDER * BATT_CAL;
  static float battF = 0;
  battF = (fabsf(battRaw - battF) > 1000.0f) ? battRaw : battF + 0.02f * (battRaw - battF);
  const uint16_t battMv = (uint16_t)battF;
  // Barošanas lēciens (baterija ieslēgta/izslēgta): ekrāna kontrolieris var pārstartēties un
  // palikt tumšs, tāpēc pēc brīža to inicializējam no jauna.
  static uint16_t prevBatt = 0;
  static uint32_t reinitAt = 0;
  if (prevBatt && abs((int)battMv - (int)prevBatt) > 1500) reinitAt = millis() + 300;
  prevBatt = battMv;
  if (reinitAt && (int32_t)(millis() - reinitAt) >= 0) {
    reinitAt = 0;
    screen::reinit();
    Serial.println("[ekrāns] barošanas lēciens - inicializēts no jauna");
  }
  updateLed(armed, swEstop, (sw3 >> 2) & 3, tel.ctrlState);

  const uint32_t now = millis();
  static uint32_t lastScreenMs = 0, lastDiagMs = 0;
  if (now - lastScreenMs >= 200) {
    lastScreenMs = now;
    ScreenData d = {};
    d.ota = -1;
    d.link = link;
    d.armed = armed;
    d.estop = swEstop;
    d.battMv = battMv;
    d.battLow = battMv > 1000 && battMv < BATT_LOW_MV;
#if USB_CHANNEL >= 0
    // kanāls NAV pievilkts uz GND = uz dalītāja ir 5 V no USB
    if (!(mux16 & (1u << USB_CHANNEL))) d.charge = battMv >= BATT_FULL_MV ? 2 : 1;
#endif
    d.channel = channel;
    d.speedPos = sw3 & 3;
    d.switches = switches;
    d.mode = (sw3 >> 2) & 3;
    d.calStep = calState == CalState::Center ? 1 : (calState == CalState::Range ? 2 : 0);
    d.pressed = (now - lastPressedMs < 1500) ? lastPressed : -1;
    portENTER_CRITICAL(&telMux);
    d.tel = tel;
    portEXIT_CRITICAL(&telMux);
    screen::draw(d);
  }
  if (now - lastDiagMs >= 1000) { lastDiagMs = now; printDiag(battMv); }

  if (now - lastSendMs < 1000 / ESPNOW_SEND_HZ) return;
  lastSendMs = now;

  RcControlPacket pkt = {};
  pkt.magic = RC_MAGIC;
  pkt.version = RC_PROTO_VERSION;
  pkt.type = RC_PKT_CONTROL;
  pkt.netId = RC_NET_ID;
  pkt.seq = seq++;
  for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) pkt.ch[i] = mapChannel(i, filt[i]);
  pkt.buttons = buttons;
  pkt.switches = switches;
  pkt.sw3 = sw3;
  pkt.flags = (armed ? RC_FLAG_ARMED : 0) | (swEstop ? RC_FLAG_ESTOP : 0) |
              (calState != CalState::Off ? RC_FLAG_CAL : 0);
  pkt.battMv = battMv;
  rc_seal(pkt);
  esp_now_send(kBroadcast, reinterpret_cast<const uint8_t *>(&pkt), sizeof pkt);
}
