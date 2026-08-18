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
  lcdShowFresh(modeName(mode()), "Any button=go");
  setStatusColor(COLOR_BLUE);
  setAllPlayerLeds(true, true);
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

    char line[17];
    snprintf(line, sizeof(line), "P%u fired early!", (unsigned)(i + 1));
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

static void showSoloResult() {
  char top[17], bottom[17];
  const uint8_t me = soloPlayer();

  // Order matters: drawing early is checked before anything else, because the
  // loser of an honest round is also marked dead by the time we get here.
  if (players[me].falseStart) {
    snprintf(top, sizeof(top), "You fired early!");
    snprintf(bottom, sizeof(bottom), "Bot wins");
  } else if (botReactionMs < 0) {
    // The machine drew before the bang.
    snprintf(top, sizeof(top), "Bot fired early!");
    snprintf(bottom, sizeof(bottom), "You win");
  } else if (botWon || !players[me].fired) {
    snprintf(top, sizeof(top), "Bot wins  %dms", botReactionMs);
    snprintf(bottom, sizeof(bottom), "Too slow!");
  } else {
    snprintf(top, sizeof(top), "You win!  %ums", winnerReactionMs(bangAtUs));
    snprintf(bottom, sizeof(bottom), "Bot was %dms", botReactionMs);
  }

  lcdShowFresh(top, bottom);
}

static void showMultiResult() {
  const int8_t w = winner();

  if (w < 0) {
    lcdShowFresh("Everyone dies!", "Nobody drew");
    return;
  }

  char top[17];
  snprintf(top, sizeof(top), "P%u wins! %ums", (unsigned)(w + 1),
           winnerReactionMs(bangAtUs));

  // "P3>P1>P4>P2" — everyone who drew, fastest first. Four entries is 11
  // characters, so this always fits the 16-column display.
  char bottom[17] = "";
  uint8_t used = 0;
  for (uint8_t place = 1; place <= PLAYER_COUNT; place++) {
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
      if (players[i].place != place || used + 3 >= (uint8_t)sizeof(bottom)) {
        continue;
      }
      if (used > 0) {
        bottom[used++] = '>';
      }
      bottom[used++] = 'P';
      bottom[used++] = (char)('1' + i);
      bottom[used] = '\0';
    }
  }

  lcdShowFresh(top, bottom);
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
  showAliveLeds();
  setStatusColor(w >= 0 ? COLOR_GREEN : COLOR_RED);

  if (isSolo(mode())) {
    showSoloResult();
  } else {
    showMultiResult();
  }
}

static void showScores() {
  char bottom[17];
  snprintf(bottom, sizeof(bottom), "%2u %2u %2u %2u", (unsigned)players[0].score,
           (unsigned)players[1].score, (unsigned)players[2].score,
           (unsigned)players[3].score);
  lcdShowFresh("P1 P2 P3 P4", bottom);
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

static void enter(GameState next) {
  state = next;
  stateEnteredMs = millis();

  switch (next) {
    case ST_MENU:
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
      setStatusColor(COLOR_GREEN);
      lcdShowFresh("Ready", "");
      audioPlay(TRACK_READY);
      break;

    case ST_STEADY:
      steadyDurationMs = random(STEADY_MIN_MS, STEADY_MAX_MS);
      setStatusColor(COLOR_YELLOW);
      lcdShow("Steady", "");
      audioPlay(TRACK_STEADY);
      DBG(F("steady for "));
      DBGLN(steadyDurationMs);
      break;

    case ST_BANG:
      setStatusColor(COLOR_RED);
      lcdShow("Bang!", "");
      audioPlay(TRACK_BANG);
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
      if (elapsed() >= RESULT_DWELL_MS) {
        enter(ST_SCORES);
      }
      break;

    case ST_SCORES:
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
