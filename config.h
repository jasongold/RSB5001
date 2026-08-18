/*
  config.h — every pin number, timing and feature flag for RSB5001.

  This is the only file to edit when the wiring changes. Nothing else in the
  sketch contains a bare pin number.

  Board: Arduino Mega 2560.
*/

#ifndef RSB5001_CONFIG_H
#define RSB5001_CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Feature flags — flip these to match the box
// ---------------------------------------------------------------------------

// Print state transitions and button events to the USB serial monitor.
#define DEBUG 1

// 0 = DFPlayer on SoftwareSerial(54, 55), i.e. the wiring as built in 2023.
// 1 = DFPlayer on Serial1 (Mega pin 18 = TX1, pin 19 = RX1).
//
// Setting this to 1 requires moving two wires:
//     DFPlayer RX : Mega pin 55 (A1)  ->  Mega pin 18 (TX1)
//     DFPlayer TX : Mega pin 54 (A0)  ->  Mega pin 19 (RX1)
//
// Worth doing. SoftwareSerial needs a pin-change-interrupt-capable RX pin, and
// on the ATmega2560 only 10-15, 50-53 and A8-A15 (62-69) qualify — A0 is not
// one of them, so myDFPlayer.begin() never sees the module's ACK and returns
// false on every boot. Transmit still works, which is why playback seems fine.
//
// It also costs accuracy: SoftwareSerial disables interrupts while it bit-bangs
// each 10-byte command frame (~10.4 ms at 9600 baud), during which millis()
// stops counting — and a command is sent immediately before the "Bang!" timer
// starts. Serial1 is a hardware UART and does none of that.
#define AUDIO_USE_HARDWARE_SERIAL 0

// 0 = buttons wired to Vcc with external pull-down resistors, HIGH = pressed
//     (as built).
// 1 = buttons wired to ground using the AVR's internal pull-ups, LOW = pressed.
//
// Setting this to 1 requires rewiring all five buttons to GND and removing the
// five external pull-down resistors. It removes the floating-input failure mode
// where a popped wire makes a station fire at random.
#define BUTTONS_ACTIVE_LOW 0

// ---------------------------------------------------------------------------
// Pin map — as built, Oct 2023
// ---------------------------------------------------------------------------

// LCD in 4-bit mode: LiquidCrystal(rs, enable, d4, d5, d6, d7)
#define LCD_RS 12
#define LCD_EN 13  // see note below
#define LCD_D4 5
#define LCD_D5 4
#define LCD_D6 3
#define LCD_D7 2

// NOTE ON LCD_EN: pin 13 also drives the Mega's on-board LED through a series
// resistor, which loads the enable line. If the display garbles, move this wire
// to pin 11 and change LCD_EN to 11 — that is what the 2021 build used, and it
// is the most likely explanation for why the 2023 sketch called lcd.begin()
// (the full ~50 ms controller re-init) eight times a round in place of a
// 2 ms lcd.clear().

// LCD contrast, driven by PWM instead of a trim pot.
#define LCD_CONTRAST_PIN 10
#define LCD_CONTRAST 75

// WARNING: on the Mega, tone() claims Timer2, and Timer2 is what drives
// analogWrite() on pins 9 and 10 — including LCD_CONTRAST_PIN above. Every beep
// would glitch the contrast voltage. Harmless today only because all audio goes
// through the DFPlayer and there are no tone() calls left. If you ever bring the
// piezo buzzer back, move the contrast to a Timer1 pin (11 or 12) first.

// Player stations: button, blue LED (alive), red LED (dead).
#define P1_BUTTON 25
#define P1_BLUE 23
#define P1_RED 22

#define P2_BUTTON 27
#define P2_BLUE 24
#define P2_RED 26

#define P3_BUTTON 29
#define P3_BLUE 28
#define P3_RED 30

#define P4_BUTTON 31
#define P4_BLUE 32
#define P4_RED 34

// Centre start/select button.
#define START_BUTTON 52

// Centre RGB status LED.
//
// These are plain digital pins: the Mega's PWM pins are 2-13 and 44-46 only, so
// analogWrite() here just writes HIGH for values >= 128 and LOW below. The
// sketch uses digitalWrite() and a fixed palette to be honest about that. To get
// real colour blending, move these three wires to 44/45/46 (Timer5) — the pins
// the 2021 build used.
#define STATUS_R 40
#define STATUS_G 38
#define STATUS_B 36

// Piezo buzzer. Unused while the DFPlayer is fitted; see the Timer2 warning.
#define BUZZER 7

// DFPlayer Mini, when AUDIO_USE_HARDWARE_SERIAL is 0.
#define AUDIO_SW_RX 54  // A0 — cannot receive; see the flag comment above
#define AUDIO_SW_TX 55  // A1

// Unconnected analog pin, read for entropy to seed random().
#define SEED_PIN A15

// ---------------------------------------------------------------------------
// Timings, in milliseconds
// ---------------------------------------------------------------------------

// How long "Ready" holds before "Steady".
#define READY_MS 1000

// The Steady -> Bang gap is drawn uniformly from [STEADY_MIN_MS, STEADY_MAX_MS).
// The floor was 100 ms as built, which is below human reaction time and made
// those rounds a coin toss — anyone who had already started moving won.
#define STEADY_MIN_MS 800
#define STEADY_MAX_MS 4000

// How long after "Bang!" to keep waiting before declaring that nobody drew.
#define BANG_TIMEOUT_MS 3000

// After the first valid shot, keep listening this long so 2nd/3rd/4th place can
// be ranked. The winner is shown immediately, so the round still feels instant.
// Set to 0 to end the round on the first shot, as the original did.
#define PLACING_WINDOW_MS 600

// How long the results screen holds before flipping to the scores screen.
#define RESULT_DWELL_MS 2500

// Ignore further edges on a button for this long after it fires. This is a
// lockout applied *after* the press is registered, not a settling time required
// *before* it — the press itself is taken on the leading edge, so it costs the
// measurement nothing.
#define DEBOUNCE_LOCKOUT_MS 50

// ---------------------------------------------------------------------------
// Single player: the machine's reaction time, drawn from [lo, hi) per round
// ---------------------------------------------------------------------------

#define BOT_EASY_LO_MS 380
#define BOT_EASY_HI_MS 560
#define BOT_NORMAL_LO_MS 260
#define BOT_NORMAL_HI_MS 380
#define BOT_HARD_LO_MS 180
#define BOT_HARD_HI_MS 260

// Percent chance the machine jumps the gun on Easy, so beginners sometimes win
// by holding their nerve. Set to 0 to disable.
#define BOT_EASY_FALSE_START_PCT 12

// ---------------------------------------------------------------------------
// DFPlayer track numbers — files in the SD card's MP3 folder
// ---------------------------------------------------------------------------
//
// Taken from the calls in the 2023 sketch, which disagreed with that sketch's
// own header comment. The code is the source of truth here.

#define TRACK_HIT 2     // gunshot, on a successful draw
#define TRACK_MISS 3    // on drawing too early
#define TRACK_READY 4   // "Ready"
#define TRACK_BANG 5    // "Bang!"
#define TRACK_STEADY 6  // "Steady"
#define TRACK_SPIN 7    // revolver spin, on the menu

#define AUDIO_VOLUME 30  // 0-30

// ---------------------------------------------------------------------------

#define PLAYER_COUNT 4

#endif  // RSB5001_CONFIG_H
