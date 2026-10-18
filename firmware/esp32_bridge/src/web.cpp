#include "web.h"
#include "config.h"
#include "controller_link.h"
#include "rc_link.h"
#include "rc_protocol.h"
#include "settings.h"
#include "web_index.h"
#include <WebSocketsServer.h>
#if defined(ESP8266)
  #include <ESP8266WebServer.h>
  #include <ESP8266WiFi.h>
  typedef ESP8266WebServer HttpServer;
#else
  #include <WebServer.h>
  #include <WiFi.h>
  typedef WebServer HttpServer;
#endif

namespace web {
namespace {

HttpServer http(80);
WebSocketsServer ws(81);

uint32_t rcWatchUntil = 0;

// Pults funkciju piešķiršana teksta rindā (web lapai) un atpakaļ
// "#rcmap t=J:D,...x12 a=C:D,...x5 b=C:D,...x5 s=25,50,100 st=5 g=5"
String rcMapLine() {
  const settings::RcMap &m = settings::rcMap();
  String s = "#rcmap t=";
  for (uint8_t i = 0; i < 12; i++) s += String(m.trimJoint[i]) + ":" + String(m.trimDir[i]) + (i < 11 ? "," : "");
  for (uint8_t k = 0; k < 2; k++) {
    s += k ? " b=" : " a=";
    for (uint8_t j = 0; j < 5; j++) s += String(m.bank[k][j][0]) + ":" + String(m.bank[k][j][1]) + (j < 4 ? "," : "");
  }
  s += " s=" + String(m.speed[0]) + "," + String(m.speed[1]) + "," + String(m.speed[2]);
  s += " st=" + String(m.trimStep10) + " g=" + String(m.gripStep);
  return s;
}

// Nolasa "A:B,A:B,..." pārus; atgriež cik pāru izdevās
uint8_t parsePairs(const String &v, int8_t *a, int8_t *b, uint8_t n) {
  uint8_t got = 0;
  int pos = 0;
  while (got < n && pos < (int)v.length()) {
    int comma = v.indexOf(',', pos);
    if (comma < 0) comma = v.length();
    const String item = v.substring(pos, comma);
    const int colon = item.indexOf(':');
    if (colon < 0) break;
    a[got] = (int8_t)item.substring(0, colon).toInt();
    b[got] = (int8_t)item.substring(colon + 1).toInt();
    got++;
    pos = comma + 1;
  }
  return got;
}

bool parseRcMap(const String &line, settings::RcMap &m) {
  m = settings::rcMap();
  int pos = line.indexOf(' ');
  while (pos >= 0 && pos < (int)line.length()) {
    int next = line.indexOf(' ', pos + 1);
    if (next < 0) next = line.length();
    const String tok = line.substring(pos + 1, next);
    const int eq = tok.indexOf('=');
    if (eq > 0) {
      const String k = tok.substring(0, eq), v = tok.substring(eq + 1);
      if (k == "t") {
        int8_t a[12], b[12];
        if (parsePairs(v, a, b, 12) != 12) return false;
        for (uint8_t i = 0; i < 12; i++) {
          if (a[i] < 0 || a[i] > 6) return false;
          m.trimJoint[i] = a[i];
          m.trimDir[i] = b[i] < 0 ? -1 : 1;
        }
      } else if (k == "a" || k == "b") {
        int8_t a[5], b[5];
        if (parsePairs(v, a, b, 5) != 5) return false;
        for (uint8_t j = 0; j < 5; j++) {
          if (a[j] < -1 || a[j] >= RC_NUM_CHANNELS) return false;
          m.bank[k == "b"][j][0] = a[j];
          m.bank[k == "b"][j][1] = b[j] < 0 ? -1 : 1;
        }
      } else if (k == "s") {
        int c1 = v.indexOf(','), c2 = v.indexOf(',', c1 + 1);
        if (c1 < 0 || c2 < 0) return false;
        const long s0 = v.substring(0, c1).toInt(), s1 = v.substring(c1 + 1, c2).toInt(), s2 = v.substring(c2 + 1).toInt();
        if (s0 < 1 || s0 > 100 || s1 < 1 || s1 > 100 || s2 < 1 || s2 > 100) return false;
        m.speed[0] = s0; m.speed[1] = s1; m.speed[2] = s2;
      } else if (k == "st") {
        const long v10 = v.toInt();
        if (v10 < 1 || v10 > 200) return false;
        m.trimStep10 = (uint8_t)v10;
      } else if (k == "g") {
        const long g = v.toInt();
        if (g < 1 || g > 50) return false;
        m.gripStep = (uint8_t)g;
      }
    }
    pos = next < (int)line.length() ? next : -1;
  }
  return true;
}

// sendTXT prasa nekonstantu String atsauci
void reply(uint8_t num, String msg) { ws.sendTXT(num, msg); }

void handleIndex() {
  http.sendHeader("Content-Encoding", "gzip");
  http.sendHeader("Cache-Control", "no-cache");
  http.send_P(200, "text/html", reinterpret_cast<const char *>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
}

String infoLine() {
  String s = "#info bridge=" BRIDGE_VERSION " rc=";
  s += rc::linkName();
  s += " net=0x" + String(RC_NET_ID, HEX);
  s += " ap=" + WiFi.softAPIP().toString();
  s += " sta=" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-"));
  s += " ssid=" + settings::wifiSsid();
  s += " ctrl=" + String(ctrl::online() ? 1 : 0);
  s += " pc=socket://" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + ":23";
  return s;
}

}  // namespace

// Tilta "#" komandas (kopīgas web lapai un PC programmai caur TCP). Atbildes rindas atdalītas ar '\n';
// tukša virkne = komanda izpildīta bez atbildes; nullptr-ekvivalents "?" = nezināma komanda.
String bridgeReply(const String &msg) {
  if (msg == "#info") return infoLine();
  if (msg == "#rc") return rc::statusLine();
  if (msg == "#rcwatch") { rcWatchUntil = millis() + 3000; return ""; }   // cilne "Pults" atkārto ik ~2 s
  if (msg == "#rcmap") return rcMapLine();
  if (msg.startsWith("#rcmap_set ")) {
    settings::RcMap m;
    if (!parseRcMap(msg, m)) return "#err rcmap";
    settings::setRcMap(m);
    return "#ok rcmap\n" + rcMapLine();
  }
  if (msg == "#rcmap_def") {
    settings::RcMap m;
    settings::rcMapDefaults(m);
    settings::setRcMap(m);
    return "#ok rcmap\n" + rcMapLine();
  }
  return "?";
}

namespace {

// Komandas tiltam pašam sākas ar '#'. Viss pārējais iet uz kontrolieri.
void bridgeCommand(uint8_t num, const String &msg) {
  if (msg.startsWith("#wifi\t")) {
    // #wifi<TAB>ssid<TAB>parole   (tukšs ssid = tikai AP režīms)
    int a = msg.indexOf('\t', 6);
    String ssid = a < 0 ? msg.substring(6) : msg.substring(6, a);
    String pass = a < 0 ? "" : msg.substring(a + 1);
    settings::setWifi(ssid, pass);
    reply(num, "#ok wifi - restartējas");
    delay(500);
    ESP.restart();
  } else {
    const String r = bridgeReply(msg);
    if (r == "?") { reply(num, "#err unknown"); return; }
    int pos = 0;
    while (pos < (int)r.length()) {
      int nl = r.indexOf('\n', pos);
      if (nl < 0) nl = r.length();
      reply(num, r.substring(pos, nl));
      pos = nl + 1;
    }
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      reply(num, infoLine());
      break;
    case WStype_DISCONNECTED:
      // Klients pazuda jogojot -> apturam (kontroliera sargsuns ir rezerve).
      if (ctrl::state() == ctrl::State::Jog && !rc::hasPriority()) ctrl::send("JOG 0 0 0 0 0");
      break;
    case WStype_TEXT: {
      String msg;
      msg.reserve(len);
      for (size_t i = 0; i < len; i++) msg += (char)payload[i];
      msg.trim();
      if (!msg.length()) break;
      if (msg == "!") { ctrl::realtime('!'); break; }
      if (msg[0] == '#') { bridgeCommand(num, msg); break; }
      if (msg.length() >= 3 && msg.substring(0, 3).equalsIgnoreCase("JOG") && rc::hasPriority()) {
        reply(num, "#rc_priority");
        break;
      }
      ctrl::send(msg, ctrl::SRC_WEB + num);
      break;
    }
    default:
      break;
  }
}

}  // namespace

void begin() {
  http.on("/", handleIndex);
  http.onNotFound([]() { http.send(404, "text/plain", "404"); });
  http.begin();
  ws.begin();
  ws.onEvent(onWsEvent);
}

void loop() {
  http.handleClient();
  ws.loop();
}

void sendTo(uint8_t num, const String &msg) {
  String m = msg;
  ws.sendTXT(num, m);
}

bool wantRcRaw() { return (int32_t)(rcWatchUntil - millis()) > 0; }

void broadcast(const String &msg) {
  String m = msg;   // broadcastTXT prasa nekonstantu atsauci
  ws.broadcastTXT(m);
}

}  // namespace web
