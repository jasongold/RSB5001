#include "game.h"

Player players[PLAYER_COUNT];

static GameMode currentMode = MODE_MULTI;
static uint8_t currentSoloPlayer = 0;

bool isSolo(GameMode m) { return m != MODE_MULTI; }

const char* modeName(GameMode m) {
  switch (m) {
    case MODE_SOLO_EASY:   return "Solo - Easy";
    case MODE_SOLO_NORMAL: return "Solo - Normal";
    case MODE_SOLO_HARD:   return "Solo - Hard";
    case MODE_MULTI:
    default:               return "Multiplayer";
  }
}

void setMode(GameMode m) { currentMode = m; }
GameMode mode() { return currentMode; }

void setSoloPlayer(uint8_t index) { currentSoloPlayer = index; }
uint8_t soloPlayer() { return currentSoloPlayer; }

bool playerInPlay(uint8_t index) {
  if (!isSolo(currentMode)) {
    return true;
  }
  return index == currentSoloPlayer;
}

void roundReset() {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    players[i].alive = playerInPlay(i);
    players[i].fired = false;
    players[i].firedAtUs = 0;
    players[i].place = 0;
  }
}

void killPlayer(uint8_t index) {
  if (index >= PLAYER_COUNT || !players[index].alive) {
    return;
  }
  players[index].alive = false;
  setPlayerLeds(index, false, true);
}

bool recordShot(uint8_t index, unsigned long atUs) {
  if (index >= PLAYER_COUNT || players[index].fired) {
    return false;
  }

  players[index].fired = true;
  players[index].firedAtUs = atUs;

  return winner() == (int8_t)index;
}

// True if timestamp `a` came before `b`. Casting the unsigned difference to
// signed is what keeps this correct when micros() rolls over mid-round, which it
// does every ~71 minutes; a plain `a < b` would rank the wrong player first.
static inline bool isBefore(unsigned long a, unsigned long b) {
  return (long)(a - b) < 0;
}

int8_t winner() {
  int8_t best = -1;

  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (!players[i].fired || !players[i].alive) {
      continue;
    }
    if (best < 0 || isBefore(players[i].firedAtUs, players[best].firedAtUs)) {
      best = (int8_t)i;
    }
  }
  return best;
}

unsigned int winnerReactionMs(unsigned long bangAtUs) {
  const int8_t w = winner();
  if (w < 0) {
    return 0;
  }
  return (unsigned int)((players[w].firedAtUs - bangAtUs) / 1000UL);
}

void assignPlaces() {
  // Selection sort over at most four entries: repeatedly take the earliest
  // timestamp not yet placed.
  uint8_t nextPlace = 1;

  for (uint8_t pass = 0; pass < PLAYER_COUNT; pass++) {
    int8_t best = -1;

    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
      if (!players[i].fired || players[i].place != 0) {
        continue;
      }
      if (best < 0 || isBefore(players[i].firedAtUs, players[best].firedAtUs)) {
        best = (int8_t)i;
      }
    }

    if (best < 0) {
      break;  // nobody left who fired
    }
    players[best].place = nextPlace++;
  }
}

void awardScores() {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    if (players[i].alive && playerInPlay(i)) {
      players[i].score++;
    }
  }
}

void killEveryone() {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    players[i].alive = false;
  }
}

void showAliveLeds() {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    setPlayerLeds(i, players[i].alive, !players[i].alive);
  }
}

// ---------------------------------------------------------------------------
// Single player
// ---------------------------------------------------------------------------

int botDrawReactionMs(GameMode m) {
  int lo, hi;

  switch (m) {
    case MODE_SOLO_EASY:
      lo = BOT_EASY_LO_MS;
      hi = BOT_EASY_HI_MS;
      // On Easy the machine sometimes draws before the bang and loses outright,
      // so holding your nerve is rewarded.
      if (BOT_EASY_FALSE_START_PCT > 0 &&
          random(100) < BOT_EASY_FALSE_START_PCT) {
        return -1;
      }
      break;
    case MODE_SOLO_HARD:
      lo = BOT_HARD_LO_MS;
      hi = BOT_HARD_HI_MS;
      break;
    case MODE_SOLO_NORMAL:
    default:
      lo = BOT_NORMAL_LO_MS;
      hi = BOT_NORMAL_HI_MS;
      break;
  }

  return (int)random(lo, hi);
}
