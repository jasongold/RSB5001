// Minimal stub of the Arduino API, just enough to compile and exercise
// rsb5001.ino on a normal computer.
#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

typedef uint8_t byte;

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
#define A0 54

// ---- fake hardware state -------------------------------------------------

extern unsigned long g_now;
extern int g_pinState[80];
extern int g_pinLevel[80];          // what digitalRead returns
extern std::vector<std::string> g_lcdLines;
extern std::string g_lcdRow[2];
extern int g_lcdCursorRow;
extern int g_lcdCursorCol;

inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int v) { g_pinState[pin] = v; }
inline int  digitalRead(int pin) { return g_pinLevel[pin]; }
inline void analogWrite(int pin, int v) { g_pinState[pin] = v; }
inline int  analogRead(int) { return 511; }
// Every read of the clock advances time by 1ms and gives the test a chance to
// drive the fake buttons, so the sketch's blocking while-loops terminate.
extern void (*g_onTick)();
inline unsigned long millis() { g_now++; if (g_onTick) g_onTick(); return g_now; }
inline void delay(unsigned long ms) { g_now += ms; }
inline void tone(int, int, int) {}

// deterministic stand-in for the AVR PRNG
extern unsigned long g_seed;
inline void randomSeed(unsigned long s) { g_seed = s ? s : 1; }
inline long random(long lo, long hi) {
  g_seed = g_seed * 1103515245UL + 12345UL;
  return lo + (long)((g_seed >> 16) % (unsigned long)(hi - lo));
}

struct SerialStub {
  void begin(long) {}
  void print(const char*) {}
  void println(const char*) {}
};
extern SerialStub Serial;
