#include "cosmetics.h"

#include "game.h"

static CosmeticPattern pattern = COS_NONE;
static unsigned long patternAtMs = 0;  // when the pattern was set
static unsigned long lastStepMs = 0;   // when the LED pattern last advanced
static unsigned long lastLampMs = 0;   // when the lamp last updated
static unsigned long lastMarqueeMs = 0;
static unsigned long lastWeedMs = 0;
static unsigned long lastDuelMs = 0;
#if LCD_ANIMATION
static uint8_t marqueePos = 0;
static uint8_t weedPos = 0;
#endif
static uint8_t bangShift = 0;  // how far the display is currently pushed over
static uint8_t phase = 0;

// A false start strobes the lamp red over the top of whatever is running.
static unsigned long alarmUntilMs = 0;

// What the last winning draw was worth, for COS_RESULTS.
static uint8_t gradeBand = 2;

// Reaction times fall into six bands. The remark and the lamp colour both read
// from the same split, so a draw called "Lightning!" is always the same violet.
#define FLAVOUR_BANDS 6
#define FLAVOUR_PER_BAND 2

#if LCD_ANIMATION
// Defined further down, next to the rest of the screen animations.
static void tickMarquee();
static void tickTumbleweed();
static void tickDuel();
#endif

// The attract chase runs out along the blue LEDs and back along the red ones,
// so one lap is twice the number of stations.
#define ATTRACT_STEPS (PLAYER_COUNT * 2)

void cosmeticsSet(CosmeticPattern p) {
  pattern = p;
  patternAtMs = millis();
  lastStepMs = patternAtMs;
  // Back-dated so the lamp repaints on the very next pass. Left at patternAtMs
  // it would hold the previous pattern's colour for LAMP_STEP_MS, which reads
  // as a stutter on every state change.
  lastLampMs = patternAtMs - LAMP_STEP_MS;
  lastMarqueeMs = patternAtMs;
  lastWeedMs = patternAtMs;
  lastDuelMs = patternAtMs;
  bangShift = 0;
  phase = 0;

  // A pattern that blanks the screen must not be able to leave it blanked.
  lcdBlank(false);
}

void cosmeticsAlarm() { alarmUntilMs = millis() + ALARM_MS; }

void cosmeticsSetGrade(unsigned int ms) { gradeBand = reactionBand(ms); }

// True if `stepMs` has passed since `last`, and claims the step if so.
// Subtracting unsigned longs this way is what keeps it correct across the
// millis() rollover at 49.7 days.
static bool dueSince(unsigned long* last, unsigned int stepMs) {
  const unsigned long nowMs = millis();
  if ((nowMs - *last) < stepMs) {
    return false;
  }
  *last = nowMs;
  return true;
}

static bool due(unsigned int stepMs) { return dueSince(&lastStepMs, stepMs); }

// How long the current pattern has been running.
static unsigned long patternElapsed() { return millis() - patternAtMs; }

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

static uint8_t scale(uint8_t channel, uint8_t level) {
  return (uint8_t)(((uint16_t)channel * level) / 255);
}

static void setScaled(uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  setStatusRgb(scale(r, level), scale(g, level), scale(b, level));
}

// Linear blend from one colour to another, `mix` running 0 (all `a`) to 255.
static void setBlend(const uint8_t a[3], const uint8_t b[3], uint8_t mix) {
  uint8_t out[3];
  for (uint8_t i = 0; i < 3; i++) {
    out[i] = (uint8_t)(((uint16_t)a[i] * (255 - mix) + (uint16_t)b[i] * mix) / 255);
  }
  setStatusRgb(out[0], out[1], out[2]);
}

// A triangle wave over `periodMs`, squared so the ends ease rather than corner.
// Linear brightness ramps read as abrupt to the eye; this is the cheap fix.
static uint8_t breathLevel(unsigned long elapsedMs, unsigned int periodMs) {
  const uint16_t pos = (uint16_t)((elapsedMs % periodMs) * 255UL / periodMs);
  const uint8_t tri = pos < 128 ? (uint8_t)(pos * 2) : (uint8_t)((255 - pos) * 2);
  return (uint8_t)(((uint16_t)tri * tri) >> 8);
}

// The six grade colours, fastest first: violet, blue, green, amber, orange, red.
static const uint8_t GRADE_RGB[FLAVOUR_BANDS][3] PROGMEM = {
    {160,  60, 255},
    { 40, 120, 255},
    { 40, 220,  90},
    {240, 200,  40},
    {255, 110,  20},
    {220,  30,  20},
};

// One per game mode, so the box says which it is in from across the room.
static const uint8_t MODE_RGB[MODE_COUNT][3] PROGMEM = {
    { 40,  90, 255},  // Multiplayer — deep blue
    { 40, 220,  90},  // Easy         — green
    {255, 150,  20},  // Normal       — amber
    {230,  30,  25},  // Hard         — red
};

static void readRgb(const uint8_t table[][3], uint8_t row, uint8_t out[3]) {
  for (uint8_t i = 0; i < 3; i++) {
    out[i] = pgm_read_byte(&table[row][i]);
  }
}

void gradeRgb(uint8_t band, uint8_t* r, uint8_t* g, uint8_t* b) {
  if (band >= FLAVOUR_BANDS) {
    band = FLAVOUR_BANDS - 1;
  }
  uint8_t rgb[3];
  readRgb(GRADE_RGB, band, rgb);
  *r = rgb[0];
  *g = rgb[1];
  *b = rgb[2];
}

static void tickAttract() {
  // Hold the menu's static lights for a while first, so the box only starts
  // fidgeting once it has genuinely been left alone.
  if ((millis() - patternAtMs) < ATTRACT_IDLE_MS) {
    return;
  }

#if LCD_ANIMATION
  // The screen belongs to the attract show now: a banner up top and a
  // tumbleweed rolling along the bottom, both off lcdPatch so neither costs a
  // resync.
  tickMarquee();
  tickTumbleweed();
#endif

  if (!due(ATTRACT_STEP_MS)) {
    return;
  }

  const uint8_t step = phase % ATTRACT_STEPS;
  const bool outbound = step < PLAYER_COUNT;
  const uint8_t station =
      outbound ? step : (uint8_t)(ATTRACT_STEPS - 1 - step);

  setAllPlayerLeds(false, false);
  setPlayerLeds(station, outbound, !outbound);
  phase++;
}

static void tickReady() {
#if LCD_ANIMATION
  tickDuel();
#endif

  // One more blue LED per step, building up from the dark that entering Ready
  // left behind. Safe to tie to the clock because READY_MS is fixed and public.
  if (phase >= PLAYER_COUNT || !due(READY_STEP_MS)) {
    return;
  }
  // Skip anyone already out. killPlayer() lit their red LED and it has to stay
  // lit: this is decoration, and it does not get to overwrite the game telling
  // a player they have lost.
  if (players[phase].alive && playerInPlay(phase)) {
    setPlayerLeds(phase, true, false);
  }
  phase++;
}

static void tickSteady() {
  // A flat pulse. Not a countdown, not a ramp — see the header.
  if (!due(STEADY_PULSE_MS)) {
    return;
  }
  phase++;

  // Entering Steady inherits the four blue LEDs the Ready fill left on, so the
  // first step has to turn them off — otherwise the opening pulse is invisible
  // and the blink looks like it starts a beat late.
  const bool on = (phase & 1) == 0;
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (players[i].alive && playerInPlay(i)) {
      setPlayerLeds(i, on, false);
    }
  }
}

// The bang: what the screen does, one row per step.
//
// The first version blanked and kicked on the same step, which meant the recoil
// happened while the display was dark and nobody ever saw it. The kick now gets
// a step of its own, lit, before the blinking starts.
//
// {columns shifted, blanked}
static const uint8_t BANG_SEQ[][2] PROGMEM = {
    {BANG_RECOIL_COLS, 0},  // kicked sideways, lit — this is the recoil you see
    {0, 0},                 // and back
    {0, 1},                 // blink
    {0, 0},
    {0, 1},
    {0, 0},
};
#define BANG_SEQ_STEPS (sizeof(BANG_SEQ) / sizeof(BANG_SEQ[0]))

static void tickBang() {
#if !LCD_ANIMATION
  // Every screen effect here writes without a resync, so with animation off
  // the bang leaves the display alone entirely.
  return;
#else
  if (phase >= BANG_SEQ_STEPS || !due(BANG_BLINK_MS)) {
    return;
  }

  const uint8_t wantShift = pgm_read_byte(&BANG_SEQ[phase][0]);
  const bool wantBlank = pgm_read_byte(&BANG_SEQ[phase][1]) != 0;

  // Each nudge is one command byte, about 205 us. Even three of them is a
  // fraction of a millisecond, which is the whole reason this is allowed to
  // happen while a reaction is being measured.
  while (bangShift < wantShift) {
    lcdNudge(true);
    bangShift++;
  }
  while (bangShift > wantShift) {
    lcdNudge(false);
    bangShift--;
  }

  lcdBlank(wantBlank);
  phase++;
#endif
}

#if LCD_ANIMATION

// ---------------------------------------------------------------------------
// Attract: a marquee and a tumbleweed
// ---------------------------------------------------------------------------

// Windowed one character at a time across the top row. The star is a custom
// glyph, so this string only reads correctly with GLYPHS_WESTERN loaded.
static const char MARQUEE[] PROGMEM =
    "\x05 RSB5001 \x05 FASTEST DRAW IN THE WEST ";
#define MARQUEE_LEN (sizeof(MARQUEE) - 1)

static void tickMarquee() {
  if (!dueSince(&lastMarqueeMs, MARQUEE_STEP_MS)) {
    return;
  }

  char window[17];
  for (uint8_t i = 0; i < 16; i++) {
    window[i] = pgm_read_byte(&MARQUEE[(marqueePos + i) % MARQUEE_LEN]);
  }
  window[16] = '\0';
  lcdPatch(0, 0, window);

  marqueePos = (uint8_t)((marqueePos + 1) % MARQUEE_LEN);
}

static void tickTumbleweed() {
  if (!dueSince(&lastWeedMs, TUMBLEWEED_STEP_MS)) {
    return;
  }

  // Blank where it was, draw where it is now. Two characters of lcdPatch, so
  // the bottom row costs almost nothing to animate.
  char cell[2] = {' ', '\0'};
  lcdPatch(1, weedPos, cell);

  weedPos = (uint8_t)((weedPos + 1) % 16);
  cell[0] = GLYPH_TUMBLEWEED;
  lcdPatch(1, weedPos, cell);
}

// ---------------------------------------------------------------------------
// Ready: the duel
// ---------------------------------------------------------------------------

// Two figures walk in from the edges across the fixed READY_MS and stop a couple
// of cells short of each other. Safe to tie to the clock for the same reason the
// lamp's charge-up is: READY_MS is fixed and everybody knows it.
#define DUEL_START_GAP 15  // columns between them at the start
#define DUEL_END_GAP 5     // and at the end

static void tickDuel() {
  if (!dueSince(&lastDuelMs, DUEL_STEP_MS)) {
    return;
  }

  unsigned long t = patternElapsed();
  if (t > READY_MS) {
    t = READY_MS;
  }

  const uint8_t gap =
      (uint8_t)(DUEL_START_GAP -
                ((DUEL_START_GAP - DUEL_END_GAP) * t / READY_MS));
  const uint8_t left = (uint8_t)((15 - gap) / 2);
  const uint8_t right = (uint8_t)(left + gap);

  char row[17];
  for (uint8_t i = 0; i < 16; i++) {
    row[i] = ' ';
  }
  row[left] = GLYPH_PISTOL_R;
  row[right] = GLYPH_PISTOL_L;
  row[16] = '\0';
  lcdPatch(1, 0, row);
}

#endif  // LCD_ANIMATION

// ---------------------------------------------------------------------------
// The bar race
// ---------------------------------------------------------------------------
//
// The round, replayed. Runners set off together and travel at speeds inversely
// proportional to their reaction times, so the winner reaches the finish line
// exactly as the replay ends and everyone else is left short by the margin they
// actually lost by. You get to see a two-millisecond win.
//
// Lanes are however many the caller hands over: four in multiplayer, two in
// solo where the machine gets a lane of its own. Sixteen columns divided by the
// lane count, with the last column of each lane left as a gap.

#define RACE_MAX_LANES PLAYER_COUNT

static unsigned int raceMs[RACE_MAX_LANES];
static uint8_t raceLanes = 0;
static unsigned int raceFastestMs = 0;

void cosmeticsSetRace(const unsigned int* laneMs, uint8_t lanes) {
  if (lanes > RACE_MAX_LANES) {
    lanes = RACE_MAX_LANES;
  }
  raceLanes = lanes;
  raceFastestMs = 0;

  for (uint8_t i = 0; i < lanes; i++) {
    raceMs[i] = laneMs[i];
    if (raceMs[i] > 0 && (raceFastestMs == 0 || raceMs[i] < raceFastestMs)) {
      raceFastestMs = raceMs[i];
    }
  }
}

bool raceWorthShowing() {
#if !LCD_ANIMATION
  // The race is drawn with lcdPatch and custom glyphs, neither of which is
  // available with animation off. Saying no here also collapses the results
  // screen back to remark-then-placings, since the beats key off this.
  return false;
#else
  uint8_t runners = 0;
  for (uint8_t i = 0; i < raceLanes; i++) {
    if (raceMs[i] > 0) {
      runners++;
    }
  }
  return runners >= 1;
#endif
}

#if LCD_ANIMATION
static uint8_t raceCellsPerLane() {
  return raceLanes > 0 ? (uint8_t)(16 / raceLanes) : 4;
}
#endif

// One runner's bar, as `pixels` of travel rendered into its lane.
#if LCD_ANIMATION

static void raceRenderBar(uint8_t lane, uint8_t pixels) {
  const uint8_t cells = raceCellsPerLane();
  const uint8_t barCells = (uint8_t)(cells - 1);  // the last one is the gap
  char out[17];

  const uint8_t full = (uint8_t)(pixels / 5);
  const uint8_t part = (uint8_t)(pixels % 5);

  for (uint8_t c = 0; c < barCells; c++) {
    if (c < full) {
      out[c] = GLYPH_BAR(5);
    } else if (c == full && part > 0) {
      out[c] = GLYPH_BAR(part);
    } else {
      out[c] = ' ';
    }
  }
  out[barCells] = ' ';
  out[cells] = '\0';

  lcdPatch(1, (uint8_t)(lane * cells), out);
}

static void tickRace() {
  if (!due(RACE_STEP_MS) || raceFastestMs == 0) {
    return;
  }

  const uint8_t barPixels = (uint8_t)((raceCellsPerLane() - 1) * 5);

  unsigned long t = patternElapsed();
  if (t > RESULT_RACE_MS) {
    t = RESULT_RACE_MS;
  }

  for (uint8_t i = 0; i < raceLanes; i++) {
    if (raceMs[i] == 0) {
      continue;  // never drew, so there is nothing to move
    }
    // Distance = speed x time, with speed set by how far off the pace they
    // were. The fastest runner has a ratio of 1 and lands exactly on the line.
    unsigned long travel = (unsigned long)barPixels * t * raceFastestMs /
                           ((unsigned long)RESULT_RACE_MS * raceMs[i]);
    if (travel > barPixels) {
      travel = barPixels;
    }
    raceRenderBar(i, (uint8_t)travel);
  }
}

#endif  // LCD_ANIMATION

// ---------------------------------------------------------------------------
// Big digits
// ---------------------------------------------------------------------------
//
// Three columns by four half-rows per digit, drawn with the two half-height
// blocks in the bar set. Each LCD row carries two of the four grid rows, so a
// cell is blank, upper, lower or full depending on the pair above and below it.

static const uint8_t BIG_DIGITS[10][4] PROGMEM = {
    // one nibble-ish row per grid line, low three bits = the three columns
    {0b111, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b110, 0b111},  // 2
    {0b111, 0b011, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001},  // 4
    {0b111, 0b100, 0b011, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b111},  // 6
    {0b111, 0b001, 0b010, 0b010},  // 7
    {0b111, 0b101, 0b111, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001},  // 9
};

// The cell that shows grid rows `top` and `bottom` of one column.
#if LCD_ANIMATION
static char bigCell(bool top, bool bottom) {
  if (top && bottom) {
    return GLYPH_FULL;
  }
  if (top) {
    return GLYPH_HALF_UPPER;
  }
  if (bottom) {
    return GLYPH_HALF_LOWER;
  }
  return ' ';
}
#endif

// Draw `text` (digits only) in big numerals starting at `col`. Four columns per
// digit: three of glyph and one of air.
void lcdBigNumber(const char* text, uint8_t col) {
#if !LCD_ANIMATION
  (void)text;
  (void)col;
#else
  for (uint8_t d = 0; text[d] != '\0'; d++) {
    const char ch = text[d];
    if (ch < '0' || ch > '9') {
      continue;
    }
    const uint8_t digit = (uint8_t)(ch - '0');
    const uint8_t at = (uint8_t)(col + d * 4);
    if (at + 3 > 16) {
      break;
    }

    for (uint8_t row = 0; row < 2; row++) {
      const uint8_t topBits = pgm_read_byte(&BIG_DIGITS[digit][row * 2]);
      const uint8_t botBits = pgm_read_byte(&BIG_DIGITS[digit][row * 2 + 1]);

      char cells[4];
      for (uint8_t c = 0; c < 3; c++) {
        const uint8_t mask = (uint8_t)(1u << (2 - c));
        cells[c] = bigCell((topBits & mask) != 0, (botBits & mask) != 0);
      }
      cells[3] = '\0';
      lcdPatch(row, at, cells);
    }
  }
#endif
}

// ---------------------------------------------------------------------------
// The lamp
// ---------------------------------------------------------------------------

// A false start outranks every pattern: strobe hard red so the whole table sees
// who it was. Returns true if it has taken the lamp over.
static bool lampAlarm() {
  if ((long)(millis() - alarmUntilMs) >= 0) {
    return false;
  }
  const bool on = ((millis() / ALARM_STROBE_MS) & 1) == 0;
  setStatusRgb(on ? 255 : 40, 0, 0);
  return true;
}

static void lampAttract() {
  uint8_t rgb[3];

  if (patternElapsed() < ATTRACT_IDLE_MS) {
    // Breathing in the colour of the selected mode.
    readRgb(MODE_RGB, (uint8_t)mode(), rgb);
    const uint8_t level = breathLevel(patternElapsed(), BREATH_PERIOD_MS);
    setScaled(rgb[0], rgb[1], rgb[2], (uint8_t)(40 + (level * 215) / 255));
    return;
  }

  // Left alone long enough: firelight. Warm orange with the brightness stumbling
  // about, which is what makes it read as a flame rather than a fade.
  const uint8_t level = (uint8_t)random(150, 256);
  setScaled(255, 110, 20, level);
}

static void lampReady() {
  // Charging green to white across READY_MS. Fair game to ramp: READY_MS is
  // fixed and every player already knows how long Ready lasts.
  static const uint8_t from[3] = {40, 220, 90};
  static const uint8_t to[3] = {255, 255, 255};

  unsigned long t = patternElapsed();
  if (t > READY_MS) {
    t = READY_MS;
  }
  setBlend(from, to, (uint8_t)(t * 255UL / READY_MS));
}

static void lampSteady() {
  // A heartbeat at a flat HEARTBEAT_MS. Two thumps and a rest, the same every
  // cycle — it must never speed up, however tempting, because that would count
  // down to the bang.
  const unsigned long t = patternElapsed() % HEARTBEAT_MS;

  uint8_t level = 30;
  if (t < 110) {
    level = 255;
  } else if (t >= 200 && t < 290) {
    level = 190;
  }
  setScaled(255, 60, 30, level);
}

static void lampBang() {
  // White for an instant, then down to a hot red. Nothing here is timed against
  // anything the player needs to predict — the bang has already happened.
  static const uint8_t from[3] = {255, 255, 255};
  static const uint8_t to[3] = {255, 40, 10};

  unsigned long t = patternElapsed();
  if (t < BANG_FLASH_MS) {
    setStatusRgb(255, 255, 255);
    return;
  }
  t -= BANG_FLASH_MS;
  if (t > BANG_FADE_MS) {
    t = BANG_FADE_MS;
  }
  setBlend(from, to, (uint8_t)(t * 255UL / BANG_FADE_MS));
}

static void lampResults() {
  // Holding the colour that draw earned, with a slow swell so it does not look
  // like the box has frozen.
  uint8_t rgb[3];
  readRgb(GRADE_RGB, gradeBand, rgb);
  const uint8_t level = breathLevel(patternElapsed(), RESULT_SWELL_MS);
  setScaled(rgb[0], rgb[1], rgb[2], (uint8_t)(150 + (level * 105) / 255));
}

static void tickLamp() {
  if (!dueSince(&lastLampMs, LAMP_STEP_MS)) {
    return;
  }
  if (lampAlarm()) {
    return;
  }

  switch (pattern) {
    case COS_ATTRACT: lampAttract(); break;
    case COS_READY:   lampReady();   break;
    case COS_STEADY:  lampSteady();  break;
    case COS_BANG:    lampBang();    break;
    case COS_RESULTS:
    case COS_RACE:    lampResults(); break;
    case COS_NONE:
    default:                         break;
  }
}

void cosmeticsTick() {
  tickLamp();

  switch (pattern) {
    case COS_ATTRACT: tickAttract(); break;
    case COS_READY:   tickReady();   break;
    case COS_STEADY:  tickSteady();  break;
    case COS_BANG:    tickBang();    break;
#if LCD_ANIMATION
    case COS_RACE:    tickRace();    break;
#endif
    case COS_NONE:
    case COS_RESULTS:
    default:                         break;
  }
}

// ---------------------------------------------------------------------------
// Flavour text
// ---------------------------------------------------------------------------
//
// Kept in PROGMEM: twelve strings is a few hundred bytes, and there is no
// reason for any of it to sit in SRAM. None is longer than the 16 columns.

static const char fl0a[] PROGMEM = "Lightning!";
static const char fl0b[] PROGMEM = "Inhuman.";
static const char fl1a[] PROGMEM = "Sharp shooter";
static const char fl1b[] PROGMEM = "Quick draw!";
static const char fl2a[] PROGMEM = "Nice draw";
static const char fl2b[] PROGMEM = "Clean hit";
static const char fl3a[] PROGMEM = "Respectable";
static const char fl3b[] PROGMEM = "That'll do";
static const char fl4a[] PROGMEM = "Bit slow...";
static const char fl4b[] PROGMEM = "Heavy holster?";
static const char fl5a[] PROGMEM = "Were you asleep?";
static const char fl5b[] PROGMEM = "Next week, maybe";

static const char* const flavourLines[] PROGMEM = {
    fl0a, fl0b, fl1a, fl1b, fl2a, fl2b,
    fl3a, fl3b, fl4a, fl4b, fl5a, fl5b,
};

// Upper bound of each band but the last, which catches everything slower.
static const unsigned int flavourBandMs[] PROGMEM = {200, 280, 380, 500, 700};

uint8_t reactionBand(unsigned int ms) {
  for (uint8_t i = 0; i < (FLAVOUR_BANDS - 1); i++) {
    if (ms < pgm_read_word(&flavourBandMs[i])) {
      return i;
    }
  }
  return FLAVOUR_BANDS - 1;
}

void flavourFor(unsigned int ms, char* out, uint8_t len) {
  if (out == NULL || len == 0) {
    return;
  }

  const uint8_t band = reactionBand(ms);
  const uint8_t pick = (uint8_t)random(FLAVOUR_PER_BAND);
  const char* src = (const char*)pgm_read_ptr(
      &flavourLines[(band * FLAVOUR_PER_BAND) + pick]);

  strncpy_P(out, src, len - 1);
  out[len - 1] = '\0';
}
