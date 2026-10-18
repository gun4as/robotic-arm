#include "tcp_server.h"
#include "config.h"
#include "controller_link.h"
#include "rc_link.h"
#include "web.h"
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif

namespace tcpsrv {
namespace {

constexpr uint8_t kMax = 2;
WiFiServer server(PORT);
WiFiClient cl[kMax];
String rx[kMax];

void handleLine(uint8_t i, String line) {
  line.trim();
  if (!line.length()) return;
  // Tilta komandas (#rcmap, #rcwatch ...) - atbild tilts pats, uz kontrolieri neiet
  if (line[0] == '#') {
    const String r = web::bridgeReply(line);
    if (r == "?") { sendTo(i, "#err unknown"); return; }
    int pos = 0;
    while (pos < (int)r.length()) {
      int nl = r.indexOf('\n', pos);
      if (nl < 0) nl = r.length();
      sendTo(i, r.substring(pos, nl));
      pos = nl + 1;
    }
    return;
  }
  // Kamēr pults aktīvi vada roku, PC jogs tiek ignorēts (kā web lapai)
  if (line.length() >= 3 && line.substring(0, 3).equalsIgnoreCase("JOG") && rc::hasPriority()) return;
  ctrl::send(line, ctrl::SRC_TCP + i);
}

}  // namespace

void begin() {
  server.begin();
  server.setNoDelay(true);
}

void loop() {
  if (server.hasClient()) {
#if defined(ESP8266)
    WiFiClient c = server.accept();
#else
    WiFiClient c = server.available();
#endif
    bool placed = false;
    for (uint8_t i = 0; i < kMax && !placed; i++) {
      if (!cl[i] || !cl[i].connected()) {
        cl[i] = c;
        cl[i].setNoDelay(true);
        rx[i] = "";
        placed = true;
        DBG.printf("[tcp] klients %u pieslēdzās\n", i);
      }
    }
    if (!placed) {
      c.println("err busy (jau 2 savienojumi)");
      c.stop();
    }
  }

  for (uint8_t i = 0; i < kMax; i++) {
    if (!cl[i] || !cl[i].connected()) continue;
    while (cl[i].available()) {
      const char c = (char)cl[i].read();
      if (c == '!' || c == '?') { ctrl::realtime(c); continue; }   // reāllaika simboli uzreiz
      if (c == '\n' || c == '\r') { handleLine(i, rx[i]); rx[i] = ""; continue; }
      if (rx[i].length() < 240) rx[i] += c;   // #rcmap_set rinda ~150 simbolu
    }
  }
}

void sendTo(uint8_t i, const String &line) {
  if (i < kMax && cl[i] && cl[i].connected()) {
    cl[i].print(line);
    cl[i].print('\n');
  }
}

void broadcast(const String &line) {
  for (uint8_t i = 0; i < kMax; i++) sendTo(i, line);
}

uint8_t clients() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMax; i++)
    if (cl[i] && cl[i].connected()) n++;
  return n;
}

}  // namespace tcpsrv
