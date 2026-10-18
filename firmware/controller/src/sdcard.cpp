#include "sdcard.h"
#include "config.h"
#include "boards.h"

#if defined(SIM_BUILD)
// datora būvē - sim/mock/mock_sd.h

#elif defined(__AVR__) && SD_ENABLED && LCD_ENABLED
#include <SD.h>
#include <SPI.h>
#include <string.h>

namespace sdcard {
namespace {
bool on = false;
bool pinsReady = false;
bool mounted = false;
bool inserted = false;
uint32_t lastCheckMs = 0;
File file;

bool isTxt(const char *n) {
  const char *dot = strrchr(n, '.');
  return dot && (!strcmp(dot, ".TXT") || !strcmp(dot, ".txt"));
}

void unmount() {
  if (file) file.close();
  if (mounted) SD.end();
  mounted = false;
}
}  // namespace

void setEnabled(bool en) {
  if (en && !pinsReady) {
    pinMode(SD_DETECT_PIN, INPUT_PULLUP);
    pinMode(SD_SS_PIN, OUTPUT);
    digitalWrite(SD_SS_PIN, HIGH);
    pinsReady = true;
  }
  if (!en) { unmount(); inserted = false; }
  on = en;
}

bool enabled() { return on; }

void poll() {
  if (!on || millis() - lastCheckMs < 300) return;
  lastCheckMs = millis();
  const bool now = digitalRead(SD_DETECT_PIN) == LOW;   // slēdzis uz GND, kad karte ielikta
  if (now && !inserted) mounted = SD.begin(SD_SS_PIN);
  if (!now && inserted) unmount();
  inserted = now;
}

bool available() { return mounted; }

uint8_t count() {
  if (!mounted) return 0;
  uint8_t n = 0;
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory() && isTxt(f.name()) && n < 255) n++;
    f.close();
  }
  root.close();
  return n;
}

bool nameAt(uint8_t index, char *out) {
  if (!mounted) return false;
  uint8_t n = 0;
  bool found = false;
  File root = SD.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory() && isTxt(f.name())) {
      if (n++ == index) {
        strncpy(out, f.name(), 12);
        out[12] = 0;
        found = true;
      }
    }
    f.close();
    if (found) break;
  }
  root.close();
  return found;
}

bool open(const char *name) {
  if (!mounted) return false;
  if (file) file.close();
  file = SD.open(name, FILE_READ);
  return (bool)file;
}

int readLine(char *buf, uint8_t size) {
  if (!file) return -1;
  uint8_t n = 0;
  int c = -1;
  while ((c = file.read()) >= 0) {
    if (c == '\n') break;
    if (c == '\r') continue;
    if (n < size - 1) buf[n++] = (char)c;
  }
  buf[n] = 0;
  if (c < 0 && n == 0) return -1;
  return n;
}

void rewind() {
  if (file) file.seek(0);
}

void close() {
  if (file) file.close();
}

}  // namespace sdcard

#else
// SD nav pieejama šai platei / konfigurācijai
namespace sdcard {
static bool on = false;
void setEnabled(bool en) { on = en; }
bool enabled() { return on; }
void poll() {}
bool available() { return false; }
uint8_t count() { return 0; }
bool nameAt(uint8_t, char *) { return false; }
bool open(const char *) { return false; }
int readLine(char *, uint8_t) { return -1; }
void rewind() {}
void close() {}
}
#endif
