/*
  RSB5001 — Ready Steady Bang

  My son and I built a physical version of Ready Steady Bang on an Arduino Mega.
  Four stations, each with a button and a blue/red LED pair; a centre
  select button; a 16x2 LCD; an RGB status light; and a DFPlayer Mini that
  speaks the countdown.

  The screen says Ready, then Steady, then — after a delay you cannot predict —
  Bang! First to hit their button wins. Draw before the bang and you are out.

  Wiring, timings and feature flags all live in config.h; nothing else in the
  sketch contains a pin number.

  Structure:
    config.h      pin map, timings, feature flags
    hardware.*    buttons, LEDs, LCD, audio
    game.*        players, scoring, placings, the single-player opponent
    this file     the state machine

  The round is a non-blocking state machine: loop() polls the buttons, runs one
  pass of the current state, and returns. Nothing blocks, nothing spins. The
  earlier version ran a whole round top-to-bottom inside the start handler with
  blocking wait loops, which is why a press meant for one screen could still be
  sitting in a flag two screens later.

  See reference/ for the earlier versions and CODE-REVIEW.md for the review that
  prompted this rewrite.
*/

#include "config.h"
#include "cosmetics.h"
#include "game.h"
#include "hardware.h"

enum GameState : uint8_t {
  ST_MENU,     // choosing a mode; waiting for a station to start the round
  ST_READY,    // "Ready" is up
  ST_STEADY,   // "Steady" is up; the bang is coming at an unknown moment
  ST_BANG,     // draw!
  ST_RESULTS,  // who won, and how fast
  ST_SCORES    // running totals
};

static GameState state = ST_MENU;
static unsigned long stateEnteredMs = 0;

static unsigned long bangAtUs = 0;      // micros() when "Bang!" went up
static unsigned long steadyDurationMs = 0;
static unsigned long firstShotMs = 0;   // millis() of the first valid draw
static bool haveFirstShot = false;

static int botReactionMs = 0;           // solo only; negative = machine misfired
static bool botResolved = false;
static bool botWon = false;

static bool seeded = false;

// The results screen runs in beats; see runResults().
enum ResultPhase : uint8_t {
  RESULT_PHASE_FLAVOUR,
  RESULT_PHASE_RACE,
  RESULT_PHASE_BIGTIME,
  RESULT_PHASE_PLACINGS
};
static ResultPhase resultPhase = RESULT_PHASE_PLACINGS;

// The winning reaction time, kept as text for the big numerals.
static char bigTime[6] = "";
static const char* bigTimeText() { return bigTime; }

// Lane labels for the race header: four stations, or you against the machine.
static const char* raceHeader() {
  // Two lanes are eight columns each, four lanes are four.
  return isSolo(mode()) ? "You     Bot" : "P1  P2  P3  P4";
}

// When each beat gives way to the next. A round with nothing to replay skips
// straight from the remark to the placings and keeps the timing it always had.
static unsigned long raceEndsMs() {
  return RESULT_FLAVOUR_MS + RESULT_RACE_MS;
}
static unsigned long bigTimeEndsMs() {
  return raceEndsMs() + RESULT_BIGTIME_MS;
}
static unsigned long resultDwellMs() {
  if (!raceWorthShowing()) {
    return RESULT_DWELL_MS;
  }
  return bigTimeEndsMs() + RESULT_PLACINGS_MS;
}

// ---------------------------------------------------------------------------

static void enter(GameState next);

static unsigned long elapsed() { return millis() - stateEnteredMs; }

// Seed the PRNG from a floating analog pin mixed with the time of the player's
// first press. Without this the AVR's generator starts from the same state on
// every power-up, so the Steady-to-Bang delays came out in an identical sequence
// every session — learnable within a few games, which defeats the whole point.
static void seedRandomOnce() {
  if (seeded) {
    return;
  }
  randomSeed(analogRead(SEED_PIN) ^ micros());
  seeded = true;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

static void showMenu() {
  lcdShow(modeName(mode()), "Any button=go");
  setAllPlayerLeds(true, true);
  // Also restarts the idle countdown, so any trip back through the menu puts
  // the attract chase back to the beginning rather than partway through a sweep.
  cosmeticsSet(COS_ATTRACT);
}

static void runMenu() {
  // The centre button cycles the mode. It is only read here — during a round
  // this state is not running, so a press mid-round can no longer bump the mode
  // out of range and leave the box unable to start a game.
  if (buttonWentDown(BTN_START)) {
    setMode((GameMode)((mode() + 1) % MODE_COUNT));
    audioPlay(TRACK_SPIN);
    DBG(F("mode: "));
    DBGLN(modeName(mode()));
    showMenu();
  }

  // Any station button starts a round. In solo, that station becomes the human.
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (buttonWentDown((ButtonId)i)) {
      seedRandomOnce();
      if (isSolo(mode())) {
        setSoloPlayer(i);
      }
      enter(ST_READY);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Ready / Steady — drawing now is a false start
// ---------------------------------------------------------------------------

static void watchForFalseStart() {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (!buttonWentDown((ButtonId)i)) {
      continue;
    }
    if (!players[i].alive || !playerInPlay(i)) {
      continue;
    }

    players[i].falseStart = true;
    killPlayer(i);
    audioPlay(TRACK_MISS);
    cosmeticsAlarm();

    // The skull is a western glyph, which is loaded through Ready and Steady.
    // It must never survive into the results screen: that loads the bar set,
    // and the character code would stay put while CGRAM changed underneath it.
    char line[17];
    snprintf(line, sizeof(line), "%c P%u fired early", MARK_SKULL,
             (unsigned)(i + 1));
    lcdLine(1, line);

    DBG(F("false start: P"));
    DBGLN(i + 1);
  }
}

// ---------------------------------------------------------------------------
// Bang
// ---------------------------------------------------------------------------

static void announceWinner(uint8_t index) {
  char line[17];
  snprintf(line, sizeof(line), "P%u wins! %ums", (unsigned)(index + 1),
           winnerReactionMs(bangAtUs));
  lcdLine(0, line);

  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    setPlayerLeds(i, i == index, i != index && playerInPlay(i));
  }
}

static void runBang() {
  // Collect draws. The winner is shown the moment they hit, but the state stays
  // open a little longer so 2nd/3rd/4th can be ranked — see PLACING_WINDOW_MS.
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (!buttonWentDown((ButtonId)i)) {
      continue;
    }
    if (!players[i].alive || !playerInPlay(i)) {
      continue;
    }

    const bool isWinningShot = recordShot(i, buttonDownAt((ButtonId)i));

    if (!haveFirstShot) {
      haveFirstShot = true;
      firstShotMs = millis();
    }
    if (isWinningShot) {
      audioPlay(TRACK_HIT);
      announceWinner(i);
    }
  }

  // Solo: the machine draws at its own reaction time whether or not the human
  // has. A negative draw means it misfired before the bang and has already lost.
  if (isSolo(mode()) && !botResolved && botReactionMs >= 0 &&
      elapsed() >= (unsigned long)botReactionMs) {
    botResolved = true;
    const int8_t human = winner();
    if (human < 0) {
      botWon = true;
      if (!haveFirstShot) {
        haveFirstShot = true;
        firstShotMs = millis();
      }
    }
  }

  // Round over?
  if (haveFirstShot && (millis() - firstShotMs) >= PLACING_WINDOW_MS) {
    enter(ST_RESULTS);
    return;
  }
  if (elapsed() >= BANG_TIMEOUT_MS) {
    enter(ST_RESULTS);
  }
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

// The result screen carries two lines' worth of information in one row: a
// remark on the winning time first, then the detail it replaces at
// RESULT_FLAVOUR_MS. Building the two rows separately is what lets the bottom
// one be rewritten part-way through the dwell.

static void buildResultTop(char* out, uint8_t len) {
  if (isSolo(mode())) {
    const uint8_t me = soloPlayer();

    // Order matters: drawing early is checked before anything else, because the
    // loser of an honest round is also marked dead by the time we get here.
    if (players[me].falseStart) {
      snprintf(out, len, "You fired early!");
    } else if (botReactionMs < 0) {
      snprintf(out, len, "Bot fired early!");
    } else if (botWon || !players[me].fired) {
      snprintf(out, len, "Bot wins  %dms", botReactionMs);
    } else {
      snprintf(out, len, "You win!  %ums", winnerReactionMs(bangAtUs));
    }
    return;
  }

  const int8_t w = winner();
  if (w < 0) {
    snprintf(out, len, "Everyone dies!");
    return;
  }
  snprintf(out, len, "P%u wins! %ums", (unsigned)(w + 1),
           winnerReactionMs(bangAtUs));
}

static void buildResultDetail(char* out, uint8_t len) {
  if (isSolo(mode())) {
    const uint8_t me = soloPlayer();

    if (players[me].falseStart) {
      snprintf(out, len, "Bot wins");
    } else if (botReactionMs < 0) {
      snprintf(out, len, "You win");
    } else if (botWon || !players[me].fired) {
      snprintf(out, len, "Too slow!");
    } else {
      snprintf(out, len, "Bot was %dms", botReactionMs);
    }
    return;
  }

  if (winner() < 0) {
    snprintf(out, len, "Nobody drew");
    return;
  }

  // "P3>P1>P4>P2" — everyone who drew, fastest first. Four entries is 11
  // characters, so this always fits the 16-column display.
  uint8_t used = 0;
  out[0] = '\0';
  for (uint8_t place = 1; place <= PLAYER_COUNT; place++) {
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
      if (players[i].place != place || used + 3 >= len) {
        continue;
      }
      if (used > 0) {
        out[used++] = '>';
      }
      out[used++] = 'P';
      out[used++] = (char)('1' + i);
      out[used] = '\0';
    }
  }
}

static void showResultDetail() {
  char detail[17];
  buildResultDetail(detail, sizeof(detail));
  lcdLine(1, detail);
}

// The results screen, beat by beat:
//
//   remark  ->  the race  ->  the winning time in big numerals  ->  placings
//
// The race and the big time are skipped when there is nothing to show, and any
// button skips the lot. None of it is timed against a reaction — the round's
// last press is long past — so the ~19 ms a full write costs is free here.
static void runResults() {
  const unsigned long t = elapsed();

  if (resultPhase == RESULT_PHASE_FLAVOUR && t >= RESULT_FLAVOUR_MS) {
    if (raceWorthShowing()) {
      resultPhase = RESULT_PHASE_RACE;
      lcdLoadGlyphs(GLYPHS_BARS);
      lcdShow(raceHeader(), "");
      cosmeticsSet(COS_RACE);
      return;
    }
    resultPhase = RESULT_PHASE_PLACINGS;
    showResultDetail();
    return;
  }

  if (resultPhase == RESULT_PHASE_RACE && t >= raceEndsMs()) {
    resultPhase = RESULT_PHASE_BIGTIME;
    cosmeticsSet(COS_RESULTS);
    // GLYPHS_BARS is still loaded and the numerals are built from it, so this
    // needs no glyph swap.
    lcdShow("", "");
    lcdBigNumber(bigTimeText(), 1);
    return;
  }

  if (resultPhase == RESULT_PHASE_BIGTIME && t >= bigTimeEndsMs()) {
    resultPhase = RESULT_PHASE_PLACINGS;
    char top[17];
    buildResultTop(top, sizeof(top));
    lcdShow(top, "");
    showResultDetail();
  }
}

static void enterResults() {
  const int8_t w = winner();

  if (isSolo(mode())) {
    if (botWon || (botReactionMs >= 0 && w < 0)) {
      killPlayer(soloPlayer());
    }
  } else if (w >= 0) {
    // Everyone who did not draw first loses.
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
      if (i != (uint8_t)w) {
        killPlayer(i);
      }
    }
  } else {
    killEveryone();  // the bang timeout expired with nobody drawing
  }

  assignPlaces();
  awardScores();
  recordWinner(winner());
  showAliveLeds();

  // The lamp holds the colour that draw earned. With nobody to grade, it just
  // sits red.
  if (w >= 0) {
    cosmeticsSetGrade(winnerReactionMs(bangAtUs));
    cosmeticsSet(COS_RESULTS);
  } else {
    cosmeticsSet(COS_NONE);
    setStatusColor(COLOR_RED);
  }

  char top[17], bottom[17];
  buildResultTop(top, sizeof(top));

  // Hand the round's timings to the replay. In solo the machine gets a lane of
  // its own, so there is always something to race against; in multiplayer each
  // station gets one and anybody who did not draw sits at 0.
  unsigned int lanes[PLAYER_COUNT];
  if (isSolo(mode())) {
    const uint8_t me = soloPlayer();
    lanes[0] = (players[me].fired && !players[me].falseStart)
                   ? (unsigned int)((players[me].firedAtUs - bangAtUs) / 1000UL)
                   : 0;
    lanes[1] = botReactionMs > 0 ? (unsigned int)botReactionMs : 0;
    cosmeticsSetRace(lanes, 2);
  } else {
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
      lanes[i] = (players[i].fired && !players[i].falseStart)
                     ? (unsigned int)((players[i].firedAtUs - bangAtUs) / 1000UL)
                     : 0;
    }
    cosmeticsSetRace(lanes, PLAYER_COUNT);
  }

  // The winning draw earns a remark now and its time in big numerals a couple
  // of beats later. Everything else — a false start, nobody drawing at all —
  // goes straight to the detail, which in those rounds is the interesting half.
  //
  // winner() is read after the kills above, so a solo player who lost is already
  // excluded here and cannot be handed a remark for a time they did not win with.
  bigTime[0] = '\0';
  if (winner() >= 0) {
    const unsigned int ms = winnerReactionMs(bangAtUs);
    snprintf(bigTime, sizeof(bigTime), "%u", ms);
    resultPhase = RESULT_PHASE_FLAVOUR;
    flavourFor(ms, bottom, sizeof(bottom));
  } else {
    resultPhase = RESULT_PHASE_PLACINGS;
    buildResultDetail(bottom, sizeof(bottom));
  }

  lcdShow(top, bottom);
}

// The scores screen carries two pages and flips between them every
// SCORES_PAGE_MS. The old single page used eleven of the sixteen columns and
// never said who was ahead; this says both without needing another button.
static uint8_t scoresPage = 0;

// Four columns of four. The spare column ahead of each label holds a star for
// whoever is leading — the one thing the old layout never told you. Ties star
// everyone level at the top, and a table of all zeroes stars nobody.
static void showScoreTotals() {
  uint8_t best = 0;
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (players[i].score > best) {
      best = players[i].score;
    }
  }

  char line0[17], line1[17];
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    const uint8_t at = (uint8_t)(i * 4);
    const bool leads = (best > 0 && players[i].score == best);

    line0[at] = leads ? MARK_STAR : ' ';
    line0[at + 1] = 'P';
    line0[at + 2] = (char)('1' + i);
    line0[at + 3] = ' ';

    // Right-aligned in the three columns under the label, so the units digit
    // always sits under the station number however big the score gets.
    snprintf(&line1[at], 5, "%3u ", (unsigned)players[i].score);
  }
  line0[16] = '\0';
  line1[16] = '\0';

  lcdShow(line0, line1);
}

// Who won the last few rounds, oldest on the left. A round no station won shows
// a dash — nobody drew in time, or in solo the machine got there first.
static void showScoreHistory() {
  char line[17];
  uint8_t used = 0;

  const uint8_t count = winnerHistoryCount();
  for (uint8_t i = 0; i < count && used + 2 <= 16; i++) {
    const int8_t w = winnerHistoryAt(i);
    line[used++] = (w < 0) ? '-' : (char)('1' + w);
    line[used++] = ' ';
  }
  line[used] = '\0';

  lcdShow("Recent winners", line);
}

static void showScores() {
  if (scoresPage == 0) {
    showScoreTotals();
  } else {
    showScoreHistory();
  }
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

static void enter(GameState next) {
  state = next;
  stateEnteredMs = millis();

  switch (next) {
    case ST_MENU:
#if LCD_ANIMATION
      // The attract marquee and tumbleweed need these; so does the duel that
      // starts a moment later in Ready.
      lcdLoadGlyphs(GLYPHS_WESTERN);
#endif
      showMenu();
      break;

    case ST_READY:
      // Drop anything registered before now. Without this, a player idly tapping
      // their button while the menu was up had that press still latched when the
      // round began — and died during "Ready" without touching anything.
      buttonsClearAll();
      roundReset();

      haveFirstShot = false;
      botResolved = false;
      botWon = false;
      botReactionMs = 0;

      setAllPlayerLeds(false, false);
      // The blue LEDs light in turn across the fixed READY_MS, so the box is
      // doing something through the wait rather than sitting dark.
      cosmeticsSet(COS_READY);
      lcdShow("Ready", "");
      audioPlay(TRACK_READY);
      break;

    case ST_STEADY:
      steadyDurationMs = random(STEADY_MIN_MS, STEADY_MAX_MS);
      // A flat pulse, at a rate that has nothing to do with steadyDurationMs.
      cosmeticsSet(COS_STEADY);
      lcdShow("Steady", "");
      audioPlay(TRACK_STEADY);
      DBG(F("steady for "));
      DBGLN(steadyDurationMs);
      break;

    case ST_BANG:
      // Every station still in the round lights up at the moment of truth: the
      // Steady pulse left them in whatever half of its cycle it happened to be
      // in, and this is the unambiguous "go". Anyone already out keeps their red
      // LED, which is why this is not a blanket setAllPlayerLeds().
      showAliveLeds();
      lcdShow("Bang!", "");
      audioPlay(TRACK_BANG);
      // Blinks the screen with the display on/off command — a few hundred
      // microseconds each, spread across BANG_BLINK_MS steps, and the only
      // screen work allowed while a reaction is being measured.
      cosmeticsSet(COS_BANG);
      if (isSolo(mode())) {
        botReactionMs = botDrawReactionMs(mode());
      }
      // Taken last so that the LCD write and the audio command — the audio
      // especially, if it is still on SoftwareSerial — are not counted as part
      // of anyone's reaction time.
      bangAtUs = micros();
      stateEnteredMs = millis();
      break;

    case ST_RESULTS:
      enterResults();
      break;

    case ST_SCORES:
      // Drop presses left over from the round so the scores stay up long enough
      // to read, rather than being skipped by the shot that ended it.
      cosmeticsSet(COS_NONE);
      setStatusColor(COLOR_BLUE);
#if LCD_ANIMATION
      // The leader star is a western glyph, and the results screen leaves the
      // bar set loaded behind it.
      lcdLoadGlyphs(GLYPHS_WESTERN);
#endif
      scoresPage = 0;
      buttonsClearAll();
      showScores();
      break;
  }
}

// ---------------------------------------------------------------------------

void setup() {
  hardwareBegin();
  setMode(MODE_MULTI);
  roundReset();
  enter(ST_MENU);
  DBGLN(F("RSB5001 ready"));
}

void loop() {
  buttonsPoll();

  // Decoration only, and non-blocking: it steps a pattern if one is due and
  // returns immediately otherwise. Sits ahead of the switch so it still runs on
  // the passes where a state handler returns early.
  cosmeticsTick();

  switch (state) {
    case ST_MENU:
      runMenu();
      break;

    case ST_READY:
      watchForFalseStart();
      if (elapsed() >= READY_MS) {
        enter(ST_STEADY);
      }
      break;

    case ST_STEADY:
      watchForFalseStart();
      if (elapsed() >= steadyDurationMs) {
        enter(ST_BANG);
      }
      break;

    case ST_BANG:
      runBang();
      break;

    case ST_RESULTS:
      // Any button cuts the celebration short. The show runs to nearly five
      // seconds with everything in it, which is a long time to stand still if
      // you already know who won — so patient people get the replay and
      // impatient people get on with the next round.
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if (buttonWentDown((ButtonId)i)) {
          enter(ST_SCORES);
          return;
        }
      }
      runResults();
      if (elapsed() >= resultDwellMs()) {
        enter(ST_SCORES);
      }
      break;

    case ST_SCORES:
      // Flip between the totals and the recent history. The screen was already
      // waiting for a button, so both fit without adding one.
      {
        const uint8_t page = (uint8_t)((elapsed() / SCORES_PAGE_MS) % 2);
        if (page != scoresPage) {
          scoresPage = page;
          showScores();
        }
      }
      // Any button returns to the menu, so whoever is nearest can move things
      // along.
      for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if (buttonWentDown((ButtonId)i)) {
          enter(ST_MENU);
          return;
        }
      }
      break;
  }
}
