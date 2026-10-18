// =============================================================================
//  Robot rokas tilts (ESP32 / ESP32-S3 / Wemos D1 mini)
//   - Wi-Fi AP "RobotRoka" (+ mājas tīkls, ja iestatīts), web lapa http://192.168.4.1
//   - WebSocket komandas -> kontrolieris (UART)
//   - Pults (ESP-NOW vai LoRa) -> JOG komandas
//   - USB seriālais (ne D1 mini): atkļūdošana; ierakstītās rindas aiziet uz kontrolieri
// =============================================================================
#include <Arduino.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
#else
  #include <WiFi.h>
  #include <ESPmDNS.h>
#endif
#include "config.h"
#include "settings.h"
#include "controller_link.h"
#include "rc_link.h"
#include "web.h"
#include "tcp_server.h"

static String usbLine;
static uint32_t lastRcStatusMs = 0;

// Rinda no kontroliera: "ok"/"err" tikai tam, kurš sūtīja komandu; pārējais visiem.
static void onControllerLine(const String &line, int16_t target) {
  if (target == ctrl::TO_ALL) {
    web::broadcast(line);
    tcpsrv::broadcast(line);
  } else if (target >= ctrl::SRC_TCP) {
    tcpsrv::sendTo((uint8_t)(target - ctrl::SRC_TCP), line);
  } else if (target >= ctrl::SRC_WEB) {
    web::sendTo((uint8_t)(target - ctrl::SRC_WEB), line);
  }
  if (!line.startsWith("<") && line != "ok") DBG.println(line);   // statusu (10x/s) nedrukājam
}

// Statusa LED: deg = kontrolieris atbild, mirgo = nav savienojuma
static void statusLed(bool on) {
#if defined(STATUS_RGB_PIN)
  static int8_t last = -1;
  if (last != (int8_t)on) {
    last = (int8_t)on;
    neopixelWrite(STATUS_RGB_PIN, 0, on ? 24 : 0, 0);   // zaļa
  }
#elif defined(STATUS_LED_PIN)
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#else
  (void)on;   // D1 mini: LED (D4) aizņem atkļūdošanas izvade
#endif
}

static void startWifi() {
  const String ssid = settings::wifiSsid();
  WiFi.persistent(false);
  WiFi.mode(ssid.length() ? WIFI_AP_STA : WIFI_AP);
  // Bez Wi-Fi miega režīma: citādi, esot pieslēgtam rūterim, pazūd ESP-NOW paketes no pults
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  WiFi.setSleep(false);
#endif
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);
  DBG.printf("[wifi] AP %s  IP %s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
  if (ssid.length()) {
    WiFi.begin(ssid.c_str(), settings::wifiPass().c_str());
    DBG.printf("[wifi] pieslēdzas tīklam %s ...\n", ssid.c_str());
  }
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

void setup() {
  DBG.begin(115200);
  delay(200);
  DBG.println("\n[bridge] RobotRoka tilts " BRIDGE_VERSION);
#if defined(STATUS_LED_PIN)
  pinMode(STATUS_LED_PIN, OUTPUT);
#endif

  settings::begin();
  startWifi();
  web::begin();
  tcpsrv::begin();
  ctrl::begin(onControllerLine);   // D1 mini: pēc šī Serial ir pārslēgts uz D7/D8
  rc::begin();
}

void loop() {
  web::loop();
  tcpsrv::loop();
  ctrl::loop();
  rc::loop();
#if defined(ESP8266)
  MDNS.update();
#endif

  const uint32_t now = millis();
  if (now - lastRcStatusMs >= 500) {
    lastRcStatusMs = now;
    web::broadcast(rc::statusLine());
  }
  // Cilne "Pults" atvērta: pults dati tiešraidē ~7x sekundē
  static uint32_t lastRawMs = 0;
  if (web::wantRcRaw() && now - lastRawMs >= 150) {
    lastRawMs = now;
    const String raw = rc::rawLine();
    web::broadcast(raw);
    tcpsrv::broadcast(raw);
  }

  // Kontrolieris klusē -> ik 3 s diagnostika web konsolē un atkļūdošanas izvadē
  static uint32_t lastDiagMs = 0;
  if (!ctrl::online() && now - lastDiagMs >= 3000) {
    lastDiagMs = now;
    const String d = ctrl::diagLine();
    web::broadcast(d);
    DBG.println(d);
  }

  static bool staReported = false;
  if (!staReported && WiFi.status() == WL_CONNECTED) {
    staReported = true;
    DBG.printf("[wifi] savienots, IP %s  (PC programmai: socket://%s:%u)\n", WiFi.localIP().toString().c_str(),
               WiFi.localIP().toString().c_str(), tcpsrv::PORT);
  }

#if !defined(ESP8266)
  // USB konsole -> kontrolieris
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      usbLine.trim();
      if (usbLine.length()) ctrl::send(usbLine, ctrl::SRC_BRIDGE);
      usbLine = "";
    } else if (usbLine.length() < 120) {
      usbLine += c;
    }
  }
#endif

  statusLed(ctrl::online() || ((now / 250) & 1));
}
