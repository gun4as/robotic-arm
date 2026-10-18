#pragma once
// Savienojums ar kontrolieri (RAMPS / SKR) caur UART.
//
// Tiltam var būt vairāki "klienti" (web lapas, PC programma caur TCP, pults).
// Kontrolieris uz katru komandu (izņemot JOG) atbild ar tieši vienu "ok"/"err",
// atbildes nāk tādā pašā secībā kā komandas. Tāpēc tilts atceras, kurš ko sūtīja,
// un "ok"/"err" nosūta tikai sūtītājam. Pārējās rindas (statuss, [..] notikumi) - visiem.
#include <Arduino.h>

namespace ctrl {

enum class State : uint8_t { Unknown = 0, Idle, Jog, Move, Alarm, Home };

// Avota / adresāta numuri
constexpr int16_t TO_ALL = -1;       // visiem klientiem
constexpr int16_t SRC_BRIDGE = -2;   // tilta paša komandas (pults, AUTO) - atbildi izmet
constexpr int16_t SRC_WEB = 0;       // 0..99   web klients (WebSocket numurs)
constexpr int16_t SRC_TCP = 100;     // 100..199 TCP klients (PC programma)

typedef void (*LineHandler)(const String &line, int16_t target);

void begin(LineHandler onLine);
void loop();
void send(const String &line, int16_t source = SRC_BRIDGE);
void realtime(char c);          // '!' = ESTOP, '?' = statuss
State state();
bool online();                  // statuss saņemts pēdējā sekundē
const String &lastStatus();
String diagLine();               // "#diag tx=.. rx=.. ..." savienojuma pārbaudei

}  // namespace ctrl
