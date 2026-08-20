/*
  game.h — the rules: who is alive, who fired when, who won, and the scores.

  Everything here is an array over PLAYER_COUNT. In the original, each of these
  operations was written out four times with the digit changed, which is how the
  first draft ended up testing p2's flags in p4's branch.
*/

#ifndef RSB5001_GAME_H
#define RSB5001_GAME_H

#include <Arduino.h>

#include "config.h"
#include "hardware.h"

enum GameMode : uint8_t {
  MODE_MULTI,
  MODE_SOLO_EASY,
  MODE_SOLO_NORMAL,
  MODE_SOLO_HARD,
  MODE_COUNT
};

struct Player {
  bool alive;
  bool fired;
  // Drew before the bang. Distinct from !alive, which also covers losing
  // honestly: at the end of a round everyone but the winner is marked dead, so
  // !alive on its own cannot tell a false start from simply being too slow.
  bool falseStart;
  unsigned long firedAtUs;  // micros() at the leading edge of the press
  uint8_t score;
  uint8_t place;  // 1 = first to draw; 0 = never drew
};

extern Player players[PLAYER_COUNT];

// True in any of the solo modes.
bool isSolo(GameMode mode);

// Human-readable menu label, e.g. "Solo - Normal".
const char* modeName(GameMode mode);

// Which stations take part: all four in multiplayer, only soloPlayer in solo.
bool playerInPlay(uint8_t index);

// Clear alive/fired/place for a new round. Scores survive; they are only reset
// by a power cycle.
void roundReset();

// The station whose button started the current solo game.
void setSoloPlayer(uint8_t index);
uint8_t soloPlayer();
void setMode(GameMode mode);
GameMode mode();

// Mark a player as having drawn too early. Idempotent.
void killPlayer(uint8_t index);

// Record a valid draw at `atUs`. Returns true if this was the winning shot.
bool recordShot(uint8_t index, unsigned long atUs);

// Whoever drew first, or -1 if nobody did.
int8_t winner();

// Reaction time of the winning draw, measured from `bangAtUs`.
unsigned int winnerReactionMs(unsigned long bangAtUs);

// Rank everyone who fired by firedAtUs and fill in Player::place.
void assignPlaces();

// Everyone still alive scores a point.
void awardScores();

// Who won the last few rounds, oldest first. The scores screen shows this
// alongside the totals: the totals say who is ahead, the history says who has
// been winning lately, which is usually the more interesting of the two.
//
// -1 means no station won that round — nobody drew, or in solo the machine did.
void recordWinner(int8_t index);
uint8_t winnerHistoryCount();
int8_t winnerHistoryAt(uint8_t i);

// Kill everyone. Used when the bang timeout expires with no valid draw.
void killEveryone();

// Drive each station's LED pair from its alive flag.
void showAliveLeds();

// --- single player -------------------------------------------------------

// Draw the machine's reaction time for this round, in ms from the bang. A
// negative result means the machine jumped the gun and loses outright.
int botDrawReactionMs(GameMode mode);

#endif  // RSB5001_GAME_H
