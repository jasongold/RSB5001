// Host-side test harness for rsb5001.ino.
// Compiles the sketch against stub Arduino/LiquidCrystal headers so the game
// logic can be exercised without the hardware.

#include "Arduino.h"

unsigned long g_now = 0;
int g_pinState[80] = {0};
int g_pinLevel[80] = {0};
std::string g_lcdRow[2];
int g_lcdCursorRow = 0;
int g_lcdCursorCol = 0;
unsigned long g_seed = 1;
SerialStub Serial;
void (*g_onTick)() = nullptr;

// The Arduino build auto-generates these; plain g++ needs them up front.
void check_buttons();
void action();
void playRound();
void waitForShot(unsigned long duration);
void handleShots();
void showResult();
void waitOnStartButton();
void resetGame();
void clearShots();
bool everyoneDead();
void updateLeds();
void allLedsOff();
void lampTest();
void setStatusColour(int r, int g, int b);

#include "rsb5001.ino"

// ---------------------------------------------------------------------------

static int failures = 0;

static void check(bool ok, const char* what) {
  printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static unsigned long releaseAt[80] = {0};

static void pressPin(int pin, unsigned long hold) {
  g_pinLevel[pin] = BUTTON_PRESSED;
  releaseAt[pin] = g_now + hold;
}

static void releaseDue() {
  for (int p = 0; p < 80; p++) {
    if (releaseAt[p] && g_now >= releaseAt[p]) {
      g_pinLevel[p] = BUTTON_RELEASED;
      releaseAt[p] = 0;
    }
  }
}

static bool resultOnScreen() {
  const std::string& r = g_lcdRow[0];
  return r.find("wins!") != std::string::npos ||
         r.find("dies!") != std::string::npos ||
         r.find("Draw!") != std::string::npos;
}

// scenario config
static int  sc_shootAfterBang[NUM_PLAYERS];  // ms after bang, -1 = never
static int  sc_earlyShooter;                 // player index, -1 = none
static bool sc_fired[NUM_PLAYERS];
static bool sc_earlyFired;
static bool sc_startFired;

// playRound() finishes with resetGame(), so grab the outcome the moment the
// result appears on screen.
static int snapWinner;
static bool snapAlive[NUM_PLAYERS];
static unsigned long snapRealTime;
static bool snapped;

static void scenarioTick() {
  releaseDue();

  if (resultOnScreen() && !snapped) {
    snapped = true;
    snapWinner = winner;
    snapRealTime = realTime;
    for (int i = 0; i < NUM_PLAYERS; i++) snapAlive[i] = alive[i];
  }

  if (sc_earlyShooter >= 0 && !sc_earlyFired && gameMode == MODE_READY && g_now > 40) {
    pressPin(buttons[sc_earlyShooter], 40);
    sc_earlyFired = true;
  }

  if (gameMode == MODE_BANG && bangStart > 0) {
    unsigned long since = g_now - bangStart;
    for (int i = 0; i < NUM_PLAYERS; i++) {
      if (sc_shootAfterBang[i] >= 0 && !sc_fired[i] && since >= (unsigned long)sc_shootAfterBang[i]) {
        pressPin(buttons[i], 40);
        sc_fired[i] = true;
      }
    }
  }

  if (resultOnScreen() && !sc_startFired) {
    pressPin(buttons[START_BUTTON], 40);
    sc_startFired = true;
  }
}

static void newScenario(int early, int p0, int p1, int p2, int p3) {
  sc_shootAfterBang[0] = p0;
  sc_shootAfterBang[1] = p1;
  sc_shootAfterBang[2] = p2;
  sc_shootAfterBang[3] = p3;
  sc_earlyShooter = early;
  for (int i = 0; i < NUM_PLAYERS; i++) sc_fired[i] = false;
  sc_earlyFired = false;
  sc_startFired = false;
  snapped = false;
  snapWinner = -1;
  snapRealTime = 0;
  bangStart = 0;
  for (int p = 0; p < 80; p++) { g_pinLevel[p] = BUTTON_RELEASED; releaseAt[p] = 0; }
  resetGame();
}

int main() {
  g_onTick = scenarioTick;

  printf("\nsetup()\n");
  setup();
  check(g_lcdRow[0] == "Press the blue", "boot screen line 1");
  check(g_lcdRow[1] == "button to start", "boot screen line 2");

  // ---- a normal round: player 3 shoots first after Bang -------------------
  printf("\nround: P3 shoots 120ms after Bang\n");
  newScenario(-1, -1, -1, 120, -1);
  playRound();
  check(snapWinner == 2, "player 3 is the winner");
  check(snapAlive[2] && !snapAlive[0] && !snapAlive[1] && !snapAlive[3], "only player 3 survives");
  check(score[2] == 1, "player 3 scored");
  check(snapRealTime >= 100 && snapRealTime < 400, "reported time is plausible");

  // ---- nobody shoots ------------------------------------------------------
  printf("\nround: nobody shoots\n");
  newScenario(-1, -1, -1, -1, -1);
  playRound();
  check(snapWinner == -1, "no winner");
  check(!snapAlive[0] && !snapAlive[1] && !snapAlive[2] && !snapAlive[3], "everyone dies");
  check(g_lcdRow[0] == "Everyone dies!", "everyone-dies message");

  // ---- jumping the gun ----------------------------------------------------
  printf("\nround: P2 shoots during Ready, P1 wins after Bang\n");
  newScenario(1, 150, -1, -1, -1);
  playRound();
  check(snapWinner == 0, "player 1 wins");
  check(!snapAlive[1], "player 2 died early");

  // ---- a dead player cannot win ------------------------------------------
  printf("\nround: P4 jumps the gun then mashes after Bang\n");
  newScenario(3, -1, -1, -1, 100);
  playRound();
  check(snapWinner != 3, "player 4 cannot win after dying early");

  // ---- tie-break by timestamp, not by seat number -------------------------
  // Both presses land in the same pass. The old code always gave it to the
  // lowest-numbered player; this must give it to whoever was actually first.
  printf("\nhandleShots: P4 pressed 6ms before P1, same pass\n");
  resetGame();
  gameMode = MODE_BANG;
  bangStart = 1000;
  g_now = 1100;
  playerShot[0] = true; playerShotTime[0] = 1056;
  playerShot[3] = true; playerShotTime[3] = 1050;
  handleShots();
  check(winner == 3, "earliest press wins regardless of player number");
  check(!isDraw, "6ms apart is not a draw");

  printf("\nhandleShots: P1 and P3 within 1ms, same pass\n");
  resetGame();
  gameMode = MODE_BANG;
  bangStart = 1000;
  g_now = 1100;
  playerShot[0] = true; playerShotTime[0] = 1050;
  playerShot[2] = true; playerShotTime[2] = 1051;
  handleShots();
  check(isDraw, "1ms apart is called a draw");
  check((winner == 0 && drawPartner == 2), "draw is between P1 and P3");

  // ---- the random delay is actually seeded --------------------------------
  printf("\nrandom delays vary\n");
  randomSeed(analogRead(A0));
  long a = random(timeMin, timeMax);
  long b = random(timeMin, timeMax);
  long c = random(timeMin, timeMax);
  check(!(a == b && b == c), "successive delays differ");
  check(a >= (long)timeMin && a < (long)timeMax, "delay is in range");

  printf("\n%s (%d failure%s)\n\n",
         failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
