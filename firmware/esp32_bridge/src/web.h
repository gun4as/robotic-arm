#pragma once
// HTTP (ports 80) servē web lapu, WebSocket (ports 81) - komandas un statuss.
#include <Arduino.h>

namespace web {
  void begin();
  void loop();
  void broadcast(const String &msg);
  void sendTo(uint8_t num, const String &msg);   // vienam klientam (atbilde uz tā komandu)
  bool wantRcRaw();       // kāda lapa skatās cilni "Pults" (sūtam #rcraw)
  String bridgeReply(const String &msg);   // tilta '#' komandas (arī PC programmai caur TCP)
}
