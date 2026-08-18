/*
  hardware.h — everything that touches a pin: buttons, LEDs, LCD, audio.

  The game logic talks to the box only through this interface, so nothing above
  this layer needs to know a pin number or which audio module is fitted.
*/

#ifndef RSB5001_HARDWARE_H
#define RSB5001_HARDWARE_H

#include <Arduino.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Debug output
// ---------------------------------------------------------------------------
//
// Replaces the `#define Serial if(DEBUG)Serial` trick the 2023 sketch used. That
// one expanded every call site into a bare `if`, so the first time anyone wrote
//     if (cond) Serial.println("a"); else b();
// the `else` would have bound to the macro's hidden `if` instead. These wrap in
// do/while(0), which cannot do that.

#if DEBUG
#define DBG(x) \
  do { Serial.print(x); } while (0)
#define DBGLN(x) \
  do { Serial.println(x); } while (0)
#else
#define DBG(x) \
  do {         \
  } while (0)
#define DBGLN(x) \
  do {           \
  } while (0)
#endif

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

enum ButtonId : uint8_t {
  BTN_P1,
  BTN_P2,
  BTN_P3,
  BTN_P4,
  BTN_START,
  BUTTON_COUNT
};

// Sample every button. Call once per loop pass and nowhere else.
//
// A press is registered on the leading edge — the instant the input goes active
// — and the button is then ignored for DEBOUNCE_LOCKOUT_MS to swallow contact
// bounce. The original did the opposite: it required the input to hold steady
// for 50 ms *before* accepting the press, which added that 50 ms to every
// reaction time it reported. It also shared one timer across all five buttons,
// so any button's bounce pushed out the deadline for the other four.
void buttonsPoll();

// True once per press. Consumes the edge, so a second call returns false.
bool buttonWentDown(ButtonId id);

// micros() at the leading edge of that button's most recent press. Not consumed
// by buttonWentDown(), so it stays readable for as long as you need it.
unsigned long buttonDownAt(ButtonId id);

// Drop any presses registered but not yet consumed. Called when entering a
// round so that idle taps made while the menu was up cannot kill anyone.
void buttonsClearAll();

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

enum StatusColor : uint8_t {
  COLOR_OFF,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_BLUE,
  COLOR_YELLOW,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE
};

// The centre RGB LED. Only these eight combinations are available: the pins it
// is wired to have no PWM (see config.h), so there is no brightness control and
// no blending between them.
void setStatusColor(StatusColor color);

void setPlayerLeds(uint8_t index, bool blue, bool red);
void setAllPlayerLeds(bool blue, bool red);

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------

// Write both lines. Pass "" for a blank line.
//
// Every write resyncs the controller first — see lcdResync() below — so a
// garbled display fixes itself on the very next screen change.
void lcdShow(const char* line1, const char* line2);

// Rewrite one row, padded to 16 characters. Row 0 is the top. The other row is
// preserved from a shadow copy, since the resync clears the display.
void lcdLine(uint8_t row, const char* text);

// Put the HD44780 back into a known state without the power-on settling delay.
//
// The display intermittently loses 4-bit nibble sync: a glitch on the enable
// line clocks in half a byte, and every character after it is misread. The
// suspected source is that the enable line sits on pin 13, which also drives
// the board's LED through its resistor — moving it to pin 11 would be the
// actual cure. Until then this recovers from it.
//
// lcd.clear() cannot: the clear command travels over the same desynced link.
// Only the init sequence resyncs the controller, because the three leading
// 0x03 nibbles are understood whatever half-byte the controller was waiting on.
//
// lcd.begin() would also work, but it opens with a 50 ms wait for the supply to
// rise after power-on, which is irrelevant to a resync. Skipping it takes this
// from about 65 ms to about 12 ms — short enough to run before every write
// without the blank screen becoming a visible cue that "Bang!" is coming.
//
// The state left behind matches LiquidCrystal's own defaults exactly (0x28 /
// 0x0C / 0x06), so the library's cached settings stay truthful afterwards.
void lcdResync();

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void audioPlay(uint8_t track);

// ---------------------------------------------------------------------------

// Configure every pin and bring up the LCD and audio module.
void hardwareBegin();

#endif  // RSB5001_HARDWARE_H
