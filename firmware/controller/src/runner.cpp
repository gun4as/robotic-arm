#include "runner.h"
#include "motion.h"
#include "protocol.h"
#include "sdcard.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace runner {
namespace {

State st = State::Idle;
bool loopForever = false;
bool pauseRequested = false;
bool waitMotion = false;
uint32_t waitUntilMs = 0;
bool waiting = false;
uint16_t lineNo = 0, cycleNo = 0;
char fileName[13] = "";
char buf[96];
uint8_t esStopsSeen = 0;

void finish(const char *msg) {
  sdcard::close();
  st = State::Idle;
  waitMotion = waiting = false;
  if (msg) protocol::broadcast(msg);
}

// Rindas pirmais vārds lielajiem burtiem
bool headIs(const char *s, const char *word) {
  while (*word) {
    char c = *s++;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c != *word++) return false;
  }
  return *s == 0 || *s == ' ' || *s == '\t';
}

}  // namespace

bool start(const char *name, bool loop, char *err, uint8_t errSize) {
  if (st != State::Idle) { strncpy(err, "busy", errSize); return false; }
  if (!sdcard::available()) { strncpy(err, "no_sd", errSize); return false; }
  if (!sdcard::open(name)) { strncpy(err, "no_file", errSize); return false; }
  strncpy(fileName, name, sizeof fileName - 1);
  fileName[sizeof fileName - 1] = 0;
  loopForever = loop;
  pauseRequested = waitMotion = waiting = false;
  lineNo = 0;
  cycleNo = 1;
  esStopsSeen = motion::endstopStops();
  st = State::Running;
  char m[40];
  snprintf(m, sizeof m, "[RUN] sakts %s", fileName);
  protocol::broadcast(m);
  return true;
}

void stop(const char *reason) {
  if (st == State::Idle) return;
  char m[48];
  if (reason) snprintf(m, sizeof m, "[RUN] apturets (%s) L%u", reason, lineNo);
  finish(reason ? m : nullptr);
}

void pause() {
  if (st == State::Running) pauseRequested = true;
}

void resume() {
  if (st == State::Paused) { pauseRequested = false; st = State::Running; }
}

void poll() {
  if (st != State::Running) return;

  // gaidām, kamēr iepriekšējā kustība beidzas
  if (waitMotion) {
    const motion::State ms = motion::state();
    if (ms == motion::State::Alarm) { stop("Alarm"); return; }
    if (motion::endstopStops() != esStopsSeen) { stop("gala sledzis"); return; }
    if (ms != motion::State::Idle || motion::queued()) return;
    waitMotion = false;
  }
  if (waiting) {
    if ((int32_t)(millis() - waitUntilMs) < 0) return;
    waiting = false;
  }
  if (pauseRequested) { pauseRequested = false; st = State::Paused; return; }

  // dažas rindas vienā izsaukumā (komentāri, tukšas rindas)
  for (uint8_t guard = 0; guard < 8; guard++) {
    const int n = sdcard::readLine(buf, sizeof buf);
    if (n < 0) {
      if (loopForever) { sdcard::rewind(); lineNo = 0; cycleNo++; continue; }
      char m[40];
      snprintf(m, sizeof m, "[RUN] pabeigts %s", fileName);
      finish(m);
      return;
    }
    lineNo++;
    char *c = strchr(buf, ';');
    if (c) *c = 0;
    char *s = buf;
    while (*s == ' ' || *s == '\t') s++;
    for (char *e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
    if (!*s) continue;

    if (headIs(s, "WAIT")) {
      waitUntilMs = millis() + (uint32_t)atol(s + 4);
      waiting = true;
      return;
    }
    if (headIs(s, "RUN")) continue;   // programma nevar palaist programmu

    char err[16];
    if (!protocol::executeInternal(s, err, sizeof err)) {
      char m[48];
      snprintf(m, sizeof m, "[RUN] kluda L%u: %s", lineNo, err);
      finish(m);
      return;
    }
    waitMotion = headIs(s, "MOVE") || headIs(s, "MOVEJ") || headIs(s, "REL") || headIs(s, "HOME");
    return;
  }
}

State state() { return st; }
bool active() { return st != State::Idle; }
uint16_t line() { return lineNo; }
uint16_t cycle() { return cycleNo; }
const char *file() { return fileName; }

}  // namespace runner
