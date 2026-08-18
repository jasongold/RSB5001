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

// Clear the display and write both lines. Pass "" for a blank line.
void lcdShow(const char* line1, const char* line2);

// Rewrite one row in place, padded to 16 characters so leftovers cannot show
// through. Row 0 is the top.
void lcdLine(uint8_t row, const char* text);

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void audioPlay(uint8_t track);

// ---------------------------------------------------------------------------

// Configure every pin and bring up the LCD and audio module.
void hardwareBegin();

#endif  // RSB5001_HARDWARE_H
