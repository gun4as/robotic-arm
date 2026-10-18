#pragma once
// =============================================================================
//  TCP ports (23) PC programmai caur tīklu: tas pats teksta protokols kā USB.
//  PC programmā: "Tilts tīklā (robotroka.local)" vai  socket://<IP>:23
// =============================================================================
#include <Arduino.h>

namespace tcpsrv {
  constexpr uint16_t PORT = 23;
  void begin();
  void loop();
  void sendTo(uint8_t client, const String &line);
  void broadcast(const String &line);
  uint8_t clients();
}
