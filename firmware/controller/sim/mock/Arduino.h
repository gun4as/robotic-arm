#pragma once
// Minimāls Arduino/LPC aizstājējs simulācijai uz datora.
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

extern uint64_t g_simUs;
inline uint32_t millis() { return (uint32_t)(g_simUs / 1000); }
inline uint32_t micros() { return (uint32_t)g_simUs; }
inline void delay(int) {}
inline void delayMicroseconds(unsigned) {}

struct LPC_GPIO_TypeDef {
  volatile uint32_t FIODIR, FIOPIN, FIOSET, FIOCLR;
};
inline uint32_t __get_PRIMASK() { return 0; }
inline void __disable_irq() {}
inline void __enable_irq() {}

#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define HEX 16
#define DEC 10

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t print(const char *s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v, int base = DEC) { return print((long)v, base); }
  size_t print(unsigned v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(uint8_t v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(long v, int base = DEC) {
    char b[24]; snprintf(b, sizeof b, base == HEX ? "%lX" : "%ld", v); return print(b);
  }
  size_t print(unsigned long v, int base = DEC) {
    char b[24]; snprintf(b, sizeof b, base == HEX ? "%lX" : "%lu", v); return print(b);
  }
  size_t print(double v, int digits = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", digits, v); return print(b); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + print("\n"); }
  template <typename T> size_t println(T v, int a) { size_t n = print(v, a); return n + print("\n"); }
  size_t println() { return print("\n"); }
};

class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() { return -1; }
  virtual void flush() {}
};

// Seriālais ports ar ievades rindu un izvades buferi testiem
class MockSerial : public Stream {
public:
  std::string in, out;
  void begin(unsigned long) {}
  size_t write(uint8_t c) override { out += (char)c; return 1; }
  int available() override { return (int)in.size(); }
  int read() override {
    if (in.empty()) return -1;
    int c = (uint8_t)in[0];
    in.erase(0, 1);
    return c;
  }
};
extern MockSerial Serial, Serial1, Serial2;
