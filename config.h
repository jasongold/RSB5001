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

// LCD animation: the marquee, the tumbleweed, the duel, the bar race, the big
// numerals, the recoil and the bang blink.
//
// Set this to 0 if the display garbles. Every write then goes back through
// lcdResync(), exactly as it did before any of this existed, so a dropped nibble
// heals on the very next screen change.
//
// Why it matters: animation deliberately skips that resync — 205 us a character
// against 19 ms — which is affordable but removes the safety net. On a panel
// that drops nibbles there is then nothing to recover it, and the attract screen
// makes it permanent because it writes continuously and never calls lcdShow().
//
// The real cure is moving LCD_EN off pin 13; see the note beside it above. With
// that wire moved this can go back to 1.
//
// None of this touches the LEDs or the centre lamp. They keep their full show
// either way.
#define LCD_ANIMATION 0

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
// The part itself is a 4-pin common-cathode RGB LED and will make any colour.
// Whether the Mega can ask it to depends entirely on which pins it is on.
//
// 0 = lamp on 40/38/36, the wiring as built in 2023. Those are plain digital
//     pins — the Mega's PWM pins are 2-13 and 44-46 only — so colour is
//     generated in software from a Timer5 interrupt. See STATUS_BAM_* below.
// 1 = lamp moved to 44/45/46 and colour comes from analogWrite(). Three jumper
//     wires, no interrupt, no CPU cost.
//
// Worth knowing: 44/45/46 is where the 2021 builds had it, and they blended
// properly — reference/RSB500-2021-06-25-0944.ino.txt:299 asks for
// analogWrite(statusRGB_R, 120) to mix a real yellow. The 2023 rebuild moved the
// wires to 40/38/36 but kept those analogWrite() calls, and on non-PWM pins
// every value >= 128 just latches HIGH, so that yellow has been coming out
// white-ish ever since. Either setting below fixes that; only this one costs
// nothing to run.
#define STATUS_LED_HARDWARE_PWM 0

#if STATUS_LED_HARDWARE_PWM
#define STATUS_R 44
#define STATUS_G 45
#define STATUS_B 46
#else
#define STATUS_R 40
#define STATUS_G 38
#define STATUS_B 36

// Software PWM, by bit-angle modulation rather than a counter: bit-plane p is
// held for 2^p ticks, so eight interrupts paint a full 8-bit frame instead of
// the 256 a naive software PWM would need.
//
// Timer5 at /8 prescale counts every 0.5 us, so 78 counts is a ~39 us tick. A
// whole frame is 255 ticks, near enough 10 ms, giving about 100 Hz refresh for
// roughly 960 interrupts per second.
//
// Timer5 is free. Note it is NOT Timer2, which drives analogWrite() on the LCD
// contrast pin and is the conflict the tone() warning above is about.
#define STATUS_BAM_TICK_COUNTS 78
#define STATUS_BAM_PLANES 8
#endif

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
// Cosmetics — the light show
// ---------------------------------------------------------------------------
//
// None of these may be derived from steadyDurationMs. A pulse that quickens as
// the bang approaches, or a bar that empties into it, hands the players the one
// thing the game depends on them not knowing.

// How long the menu sits still before the attract chase starts up.
#define ATTRACT_IDLE_MS 10000

// One step of the attract chase.
#define ATTRACT_STEP_MS 110

// One step of the Ready fill: the four blue LEDs light in turn across READY_MS.
#define READY_STEP_MS (READY_MS / PLAYER_COUNT)

// The Steady pulse. Deliberately a fixed rate — see the note above.
#define STEADY_PULSE_MS 220

// The bang, on screen. Two blinks and a sideways kick, each step this long.
//
// The first attempt used three 55 ms blinks and a one-column kick, which was
// correct, cheap, and effectively invisible - about three video frames. Longer
// steps and a wider kick cost a few hundred microseconds more in total and are
// the difference between an effect and a rumour.
#define BANG_BLINK_MS 120
#define BANG_RECOIL_COLS 3

// The attract screen.
#define MARQUEE_STEP_MS 220
#define TUMBLEWEED_STEP_MS 190

// The duel walking in across Ready.
#define DUEL_STEP_MS 60

// How long the winning time holds in big numerals, after the race.
#define RESULT_BIGTIME_MS 1100

// How long the results screen shows its remark before the detail line replaces
// it. Well past the last button press of the round.
#define RESULT_FLAVOUR_MS 1200

// How often the lamp recomputes its colour. Fades want to be smooth, not
// free-running: at 20 ms this is 50 Hz, well past what the eye resolves, and it
// keeps the work off the vast majority of loop passes.
#define LAMP_STEP_MS 20

// The menu breath, one full in-and-out.
#define BREATH_PERIOD_MS 3600

// The Steady heartbeat: two thumps and a rest, this long end to end. FLAT by
// construction — if this ever varied with steadyDurationMs it would count the
// players down to the bang.
#define HEARTBEAT_MS 1000

// The bang: white for this long, then this long fading to a hot red.
#define BANG_FLASH_MS 90
#define BANG_FADE_MS 320

// The slow swell on the results screen, so a held colour still looks alive.
#define RESULT_SWELL_MS 2200

// A false start strobes the lamp red for this long, at this rate.
#define ALARM_MS 900
#define ALARM_STROBE_MS 70

// The bar race: how long the replay runs, and how often a bar redraws. Each
// redraw is a handful of lcdPatch() characters, so 60 ms is both cheap and
// smooth enough to read as motion.
#define RESULT_RACE_MS 1700
#define RACE_STEP_MS 60

// How long the placings hold after the race, before the scores screen.
#define RESULT_PLACINGS_MS 900

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

// How many past rounds the scores screen remembers the winner of.
#define WINNER_HISTORY 8

// How long each page of the scores screen holds before flipping to the other.
#define SCORES_PAGE_MS 2500

#endif  // RSB5001_CONFIG_H
