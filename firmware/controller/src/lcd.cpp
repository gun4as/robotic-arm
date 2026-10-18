#include "lcd.h"
#include "config.h"
#include "hal.h"
#include <string.h>

#if LCD_ENABLED

namespace lcd {
namespace {

FastPin rs, en, d4, d5, d6, d7;
char fb[ROWS][COLS + 1];       // ko gribam redzēt
char shown[ROWS][COLS];        // kas ir uz ekrāna
uint8_t curRow = 0xFF, curCol = 0xFF;
uint8_t scan = 0;              // kur turpināt meklēt atšķirības
uint32_t lastUs = 0;
const uint8_t kRowAddr[ROWS] = {0x00, 0x40, 0x14, 0x54};

// AVR: LCD pini koplieto portus ar soļu pinem -> atomāra rakstīšana
void nibble(uint8_t v) {
  d4.writeAtomic(v & 1);
  d5.writeAtomic(v & 2);
  d6.writeAtomic(v & 4);
  d7.writeAtomic(v & 8);
  en.writeAtomic(true);
  delayMicroseconds(1);
  en.writeAtomic(false);
  delayMicroseconds(1);
}

void send(uint8_t v, bool data) {
  rs.writeAtomic(data);
  nibble(v >> 4);
  nibble(v & 0x0F);
  lastUs = micros();
}

}  // namespace

void init() {
  rs.beginOutput(LCD_RS_PIN, false);
  en.beginOutput(LCD_EN_PIN, false);
  d4.beginOutput(LCD_D4_PIN, false);
  d5.beginOutput(LCD_D5_PIN, false);
  d6.beginOutput(LCD_D6_PIN, false);
  d7.beginOutput(LCD_D7_PIN, false);
  delay(50);
  // HD44780 inicializācija 4 bitu režīmā (datu lapas secība)
  nibble(0x3); delay(5);
  nibble(0x3); delayMicroseconds(150);
  nibble(0x3); delayMicroseconds(150);
  nibble(0x2); delayMicroseconds(150);
  send(0x28, false); delayMicroseconds(60);   // 4 biti, 2 "līnijas", 5x8
  send(0x0C, false); delayMicroseconds(60);   // ekrāns ieslēgts, bez kursora
  send(0x01, false); delay(3);                // notīrīt
  send(0x06, false); delayMicroseconds(60);   // kursors uz priekšu
  memset(shown, ' ', sizeof shown);
  clear();
}

void clear() {
  for (uint8_t r = 0; r < ROWS; r++) {
    memset(fb[r], ' ', COLS);
    fb[r][COLS] = 0;
  }
}

void put(uint8_t r, uint8_t col, const char *text) {
  if (r >= ROWS) return;
  for (uint8_t c = col; c < COLS && *text; c++) fb[r][c] = *text++;
}

void setRow(uint8_t r, const char *text) {
  if (r >= ROWS) return;
  uint8_t c = 0;
  for (; c < COLS && text[c]; c++) fb[r][c] = text[c];
  for (; c < COLS; c++) fb[r][c] = ' ';
}

const char *row(uint8_t r) { return fb[r < ROWS ? r : 0]; }

void poll() {
  if ((uint32_t)(micros() - lastUs) < 60) return;   // HD44780 vajag ~40 µs pēc katra baita
  for (uint8_t n = 0; n < ROWS * COLS; n++) {
    const uint8_t i = (uint8_t)((scan + n) % (ROWS * COLS));
    const uint8_t r = i / COLS, c = i % COLS;
    if (fb[r][c] == shown[r][c]) continue;
    scan = i;
    if (r != curRow || c != curCol) {
      send((uint8_t)(0x80 | (kRowAddr[r] + c)), false);   // kursora adrese
      curRow = r;
      curCol = c;
      return;
    }
    send((uint8_t)fb[r][c], true);
    shown[r][c] = fb[r][c];
    curCol++;
    if (curCol >= COLS) curRow = 0xFF;   // HD44780 rindas nav secīgas atmiņā
    return;
  }
}

}  // namespace lcd

#else
namespace lcd {
void init() {}
void clear() {}
void setRow(uint8_t, const char *) {}
void put(uint8_t, uint8_t, const char *) {}
const char *row(uint8_t) { return ""; }
void poll() {}
}
#endif
