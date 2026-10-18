#pragma once
// Servo aizstājējs simulācijai
class Servo {
public:
  int us = 0;
  int attach(int) { return 0; }
  void writeMicroseconds(int v) { us = v; }
};
