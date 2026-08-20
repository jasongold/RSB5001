/*
  cosmetics.h — the light show.

  Everything in here is decoration. It reads game state and drives the LEDs, the
  centre lamp and the screen; it never changes a rule, and no reaction time
  depends on it.

  Three constraints shape the whole file:

    - Nothing shown during Steady may correlate with steadyDurationMs. A lamp
      that warms toward the bang, or a pulse that quickens into it, would hand
      the players the one thing the game depends on them not knowing. Steady
      effects run at a fixed rate and loop forever.

      Ready is the opposite: READY_MS is fixed and public, so ramping across it
      gives nothing away and the charge-up does exactly that.

    - Every step is non-blocking, and nothing between the bang and the first
      press touches the screen except lcdBlank() and the hardware scroll, both
      single command bytes. buttonsPoll() has to keep running.

    - The lamp updates at LAMP_STEP_MS rather than every pass. Fades want to be
      smooth, not free-running.
*/

#ifndef RSB5001_COSMETICS_H
#define RSB5001_COSMETICS_H

#include <Arduino.h>

#include "config.h"
#include "hardware.h"

enum CosmeticPattern : uint8_t {
  COS_NONE,     // hold still; whatever the state entry left behind stays
  COS_ATTRACT,  // menu: the mode colour breathing, then a chase and firelight
  COS_READY,    // blue LEDs fill in turn; the lamp charges green to white
  COS_STEADY,   // a flat pulse and a steady heartbeat, both meaning nothing
  COS_BANG,     // screen blink and recoil, lamp white then hot
  COS_RESULTS,  // the lamp holds the colour that draw earned
  COS_RACE      // the round replayed as four bars racing to a finish line
};

// Start a pattern. Resets its phase and its clock, so calling this again is how
// the attract chase is sent back to the beginning after a button press.
void cosmeticsSet(CosmeticPattern pattern);

// Advance whatever is running, if a step is due. Call once per loop() pass.
void cosmeticsTick();

// Somebody drew early. Strobes the lamp red for a moment, over the top of
// whatever pattern is running, then hands it back.
void cosmeticsAlarm();

// Remember what the winning time was worth, for COS_RESULTS to show.
void cosmeticsSetGrade(unsigned int ms);

// Hand over the round to be replayed: one entry per lane, in milliseconds from
// the bang, 0 meaning that lane never drew. Four lanes in multiplayer, two in
// solo where the machine gets a lane of its own. Call before starting COS_RACE.
void cosmeticsSetRace(const unsigned int* laneMs, uint8_t lanes);

// True if there is anything worth racing — at least two stations drew.
bool raceWorthShowing();

// Which of the six reaction bands a time falls in, 0 (fastest) to 5.
// The remark and the colour both read from this, so a draw called "Lightning!"
// is always the same violet.
uint8_t reactionBand(unsigned int ms);

// The colour a band earns.
void gradeRgb(uint8_t band, uint8_t* r, uint8_t* g, uint8_t* b);

// A remark for a reaction time, written into `out`. Two lines per band, picked
// at random, so the same draw does not always get the same words back.
void flavourFor(unsigned int ms, char* out, uint8_t len);

// Draw a run of digits two rows tall, starting at `col`. Needs GLYPHS_BARS
// loaded — the numerals are built from the two half blocks in that set.
void lcdBigNumber(const char* text, uint8_t col);

#endif  // RSB5001_COSMETICS_H
