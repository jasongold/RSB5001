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

// The centre RGB LED, as eight named combinations. Full colour is available
// through setStatusRgb() below; this stays because most of the game only ever
// wants "green" or "red" and saying so reads better than three numbers.
void setStatusColor(StatusColor color);

// Any colour, 0-255 per channel. setStatusColor() above is a convenience
// wrapper over this with an eight-entry palette.
//
// How the intermediate values are produced depends on STATUS_LED_HARDWARE_PWM:
// either analogWrite() on Timer5 pins, or software PWM from a Timer5 interrupt.
// Callers do not need to care which.
void setStatusRgb(uint8_t r, uint8_t g, uint8_t b);

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

// Blank the display without disturbing what is written on it, and bring it back.
//
// This is the HD44780 display on/off command: a single byte, about 205 us. Every
// other call in this section costs a resync and a full repaint — roughly 19 ms,
// during which buttonsPoll() does not run. That gap is why this is the only
// screen effect cheap enough to use between the bang and the first press.
//
// lcdResync() writes 0x0C, so any resync leaves the display back on.
void lcdBlank(bool blank);

// Shift the whole display one column and put it back. Like lcdBlank() this is a
// single HD44780 command — about 205 us, no repaint — so the screen can be made
// to kick at the bang without costing anyone their reaction time.
void lcdNudge(bool right);

// Overwrite a few cells in place, leaving the rest of the screen alone.
//
// No resync and no full repaint: about 205 us per character, against the ~19 ms
// lcdShow() costs. This is the write an animation frame uses; lcdShow() is what
// a state entry uses. A garbled display still heals on the next screen change,
// and animation pays nothing for that.
void lcdPatch(uint8_t row, uint8_t col, const char* text);

// The eight CGRAM slots, loaded a set at a time.
//
// Eight is not many and the bar race alone wants five, so glyphs come in sets
// swapped on state entry. Loading one is 64 writes, about 13 ms — fine when a
// screen changes, never during a round. Asking for the set already loaded costs
// nothing.
enum GlyphSet : uint8_t {
  GLYPHS_NONE,
  GLYPHS_WESTERN,  // two pistols, skull, tumbleweed, star, bullet
  GLYPHS_BARS      // five bar widths, plus the two half blocks for big digits
};

void lcdLoadGlyphs(GlyphSet set);

// Slot numbers, written into strings as characters. Slot 0 is left unused: a
// NUL byte ends a C string, so the sets start at 1.
#define GLYPH_PISTOL_R 1
#define GLYPH_PISTOL_L 2
#define GLYPH_SKULL 3
#define GLYPH_TUMBLEWEED 4
#define GLYPH_STAR 5

// GLYPHS_BARS puts the two half-height blocks the big digits are built from
// above the five bar widths, so the results screen never swaps sets mid-screen.
#define GLYPH_HALF_UPPER 6
#define GLYPH_HALF_LOWER 7
#define GLYPH_FULL 5

// GLYPHS_BARS puts widths 1..5 in slots 1..5, so the character for an n-pixel
// stub is just n.
#define GLYPH_BAR(width) ((char)(width))

// The leader mark on the scores screen and the mark on a false start.
//
// Custom glyphs when animation is on; plain ASCII when it is off, because with
// LCD_ANIMATION 0 no glyph set is ever loaded and a glyph code would render as
// whatever CGRAM happens to be holding.
#if LCD_ANIMATION
#define MARK_STAR ((char)GLYPH_STAR)
#define MARK_SKULL ((char)GLYPH_SKULL)
#else
#define MARK_STAR '*'
#define MARK_SKULL '!'
#endif

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void audioPlay(uint8_t track);

// ---------------------------------------------------------------------------

// Configure every pin and bring up the LCD and audio module.
void hardwareBegin();

#endif  // RSB5001_HARDWARE_H
