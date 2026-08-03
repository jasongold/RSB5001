/*

Me and my Son's version of Ready Steady Bang!
We call it RSB5001 x

more documentation coming soon......

Ideas for future additions:
- Might be cool to see 2nd/3rd/4th place
- Single player
- Keep track of fastest time


 */

// include the library code:
#include <LiquidCrystal.h>

// Creates an LCD object. Parameters: (rs, enable, d4, d5, d6, d7)
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);


// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

// Everything is kept in arrays now instead of p1/p2/p3/p4 copies of each
// variable. Player 1 is index 0, player 2 is index 1, and so on. Adding a
// fifth player is now just a matter of adding their pins to these lists.
const int NUM_PLAYERS = 4;

const int blueLed[NUM_PLAYERS] = {23, 26, 29, 32};
const int redLed[NUM_PLAYERS]  = {24, 27, 30, 33};

// The buttons: the players first, then the start button on the end.
const byte buttons[] = {22, 25, 28, 31, 34};
const int NUMBUTTONS = sizeof(buttons) / sizeof(buttons[0]);
const int START_BUTTON = NUM_PLAYERS;  // the last entry in buttons[]

const int statusRGB_R = 44;
const int statusRGB_G = 45;
const int statusRGB_B = 46;

const int buzzer = 7;             // buzzer to arduino pin 7

const int lcdContrastPin = 10;    // instead of a potentiometer
const int lcdContrast = 75;

const int randomSeedPin = A0;     // MUST be left unconnected - see setup()

// How the buttons are wired.
//   0 = buttons go to +5V and the board has pull-down resistors (how it is now)
//   1 = buttons go to GND and we use the Arduino's own pull-up resistors
// Only change this if you rewire the box, otherwise every button will read
// as permanently pressed.
#define USE_INTERNAL_PULLUPS 0

#if USE_INTERNAL_PULLUPS
  const int BUTTON_MODE     = INPUT_PULLUP;
  const int BUTTON_PRESSED  = LOW;
  const int BUTTON_RELEASED = HIGH;
#else
  const int BUTTON_MODE     = INPUT;
  const int BUTTON_PRESSED  = HIGH;
  const int BUTTON_RELEASED = LOW;
#endif


// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

const unsigned long timeMin = 100;         // minimum time on "BANG"
const unsigned long timeMax = 4000;        // maximum time on "BANG"
const unsigned long readyTime = 1000;      // how long "Ready" stays up
const unsigned long bangAllDead = 3000;    // everyone died...nobody shot. this is the time after bang to wait for button presses
const unsigned long drawWindow = 2;        // shots this close together (ms) count as a draw

// Buzzer notes
const int toneReadySteady = 3520;
const int toneBang = 4699;
const int toneDead = 1760;
const int toneLength = 50;

// Game modes
const int MODE_READY = 0;
const int MODE_STEADY = 1;
const int MODE_BANG = 2;


// ---------------------------------------------------------------------------
// Variables that change while we play
// ---------------------------------------------------------------------------

int gameMode = MODE_READY;
bool gameStarted = false;

bool alive[NUM_PLAYERS];
int score[NUM_PLAYERS];          // survives between games, only reset on power up

int winner;                      // -1 means nobody has shot yet
int drawPartner;                 // the other player when it is too close to call
bool isDraw;

unsigned long ranDelay;          // random delay for the time for BANG
unsigned long bangStart;         // when did it say bang?
unsigned long bangEnd;           // exactly when was the winning button pushed?
unsigned long realTime;          // time it took to push button after bang


// Buttons waiting to be dealt with
bool playerShot[NUM_PLAYERS];
unsigned long playerShotTime[NUM_PLAYERS];
bool startPressed = false;


// ---------------------------------------------------------------------------
// Debounce code to stop bad reads of the buttons
// adapted from https://forum.arduino.cc/t/debouncing-multiple-buttons-with-arrays-sample-for-review/499457
//
// Each button now gets its OWN debounce timer. When they all shared one, any
// button twitching would restart the wait for every other button too - so two
// players shooting at nearly the same moment interfered with each other, and
// drumming on a button could stop anything registering at all.
// ---------------------------------------------------------------------------

int buttonState[NUMBUTTONS];
int lastButtonState[NUMBUTTONS];
bool buttonIsPressed[NUMBUTTONS];
unsigned long buttonPressTime[NUMBUTTONS];   // when it actually went down
unsigned long lastDebounceTime[NUMBUTTONS];  // one per button
const unsigned long debounceDelay = 10;      // the debounce time; increase if the output flickers


void setup() {
  Serial.begin(9600);

  // Set up the buttons. This used to say pinMode(i, ...) which configured
  // pins 0-3 (the serial pins and two of the LCD pins) instead of the
  // actual buttons.
  for (int i = 0; i < NUMBUTTONS; i++) {
    pinMode(buttons[i], BUTTON_MODE);
    buttonState[i] = BUTTON_RELEASED;
    lastButtonState[i] = BUTTON_RELEASED;
    buttonIsPressed[i] = false;
    lastDebounceTime[i] = 0;
  }

  // Without this the "random" delay before BANG is the exact same sequence
  // every single time the box is switched on, so you can learn the timings.
  // analogRead on an unconnected pin picks up electrical noise to seed it.
  randomSeed(analogRead(randomSeedPin));

  // For LCD for contrast (instead of pent)
  pinMode(lcdContrastPin, OUTPUT);
  analogWrite(lcdContrastPin, lcdContrast);

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.clear();

  // initialize LEDs as outputs
  for (int i = 0; i < NUM_PLAYERS; i++) {
    pinMode(blueLed[i], OUTPUT);
    pinMode(redLed[i], OUTPUT);
  }
  pinMode(statusRGB_R, OUTPUT);
  pinMode(statusRGB_G, OUTPUT);
  pinMode(statusRGB_B, OUTPUT);

  // buzzer
  pinMode(buzzer, OUTPUT); // Set buzzer as an output

  resetGame();
  lampTest();     // flash every LED once so you can see they all still work
  updateLeds();
  setStatusColour(0, 0, 255);

  // initial start screen
  lcd.setCursor(0, 0);
  lcd.print("Press the blue");
  lcd.setCursor(0, 1);
  lcd.print("button to start");
}


void loop() {
  check_buttons();
  action();

  if (gameStarted) {
    playRound();
  } else if (startPressed) {
    startPressed = false;
    gameStarted = true;
  }

  // Note: the LEDs are deliberately NOT written here any more. This used to
  // force all eight of them on every time round the loop, which wiped out the
  // who-won-and-who-died display the moment a game finished.
}


// ---------------------------------------------------------------------------
// Reading the buttons
// ---------------------------------------------------------------------------

void check_buttons() {
  unsigned long now = millis();

  for (int i = 0; i < NUMBUTTONS; i++) {
    // read the state of the switch into a local variable:
    int reading = digitalRead(buttons[i]);

    // If the switch changed, due to noise or pressing then reset this
    // button's debouncing timer
    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = now;
    }

    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    if ((now - lastDebounceTime[i]) > debounceDelay) {
      if (reading != buttonState[i]) {
        buttonState[i] = reading;
        if (buttonState[i] == BUTTON_PRESSED) {
          buttonIsPressed[i] = true;   // set your flag for the adjustment function
          // lastDebounceTime is when the button physically went down, which is
          // debounceDelay ago. Using it instead of now means the reaction time
          // we report doesn't include the time spent debouncing, and ties are
          // settled on when people actually pressed.
          buttonPressTime[i] = lastDebounceTime[i];
        }
      }
    }

    // save the reading. Next time through the loop, it'll be the lastButtonState:
    lastButtonState[i] = reading;
  }
}


void action() {
  for (int i = 0; i < NUMBUTTONS; i++) {
    if (!buttonIsPressed[i]) {
      continue;
    }
    buttonIsPressed[i] = false;    // reset the button

    if (i == START_BUTTON) {
      startPressed = true;
    } else {
      playerShot[i] = true;
      playerShotTime[i] = buttonPressTime[i];
    }
  }
}


// ---------------------------------------------------------------------------
// Playing a round
// ---------------------------------------------------------------------------

void playRound() {
  startPressed = false;
  clearShots();

  // now starting READY
  gameMode = MODE_READY;
  tone(buzzer, toneReadySteady, toneLength); // ready & steady
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready");
  setStatusColour(0, 255, 0);
  allLedsOff();

  // wait for a second between ready and steady - but let people kill themselves
  waitForShot(readyTime);

  // Now starting STEADY
  if (!everyoneDead()) {
    gameMode = MODE_STEADY;
    tone(buzzer, toneReadySteady, toneLength); // ready & steady sound
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Steady");
    // Yellow (turn red and green on):
    setStatusColour(255, 160, 0);

    // random delay from steady to bang
    // if someone shoots before delay is up, they die!
    ranDelay = random(timeMin, timeMax);
    waitForShot(ranDelay);
  }

  // Now starting BANG
  if (!everyoneDead()) {
    gameMode = MODE_BANG;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Bang!");
    setStatusColour(255, 0, 0);
    tone(buzzer, toneBang, toneLength); // bang

    // let's see who shoots first!!
    bangStart = millis();
    waitForShot(bangAllDead);
  }

  showResult();

  // keep showing results and wait for start button again
  waitOnStartButton();
  resetGame();
  // gameStarted stays true, so the next round begins straight away instead of
  // making you press the start button a second time.
}


// this waits for someone to either shoot who is alive and runs till time delay ends
// it handles all three 'wait' periods (ready/steady/bang)
void waitForShot(unsigned long duration) {
  unsigned long start = millis();

  // Subtracting like this instead of comparing against millis() + duration
  // means it still behaves when millis() wraps around after ~49 days.
  while ((millis() - start) < duration) {
    check_buttons();
    action();
    handleShots();

    if (gameMode == MODE_BANG) {
      if (winner >= 0) {
        return;                 // we have a winner, no need to wait it out
      }
    } else if (everyoneDead()) {
      return;                   // nobody left to shoot, get on with it
    }
  }
}


// Work out what the button presses we have collected actually mean.
void handleShots() {
  int firstPlayer = -1;
  unsigned long firstTime = 0;
  int secondPlayer = -1;
  unsigned long secondTime = 0;

  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (!playerShot[i]) {
      continue;
    }
    playerShot[i] = false;

    // note - you only can shoot if you are alive
    if (!alive[i]) {
      continue;
    }

    if (gameMode < MODE_BANG) {
      // jumped the gun, you're dead
      alive[i] = false;
      tone(buzzer, toneDead, toneLength);
      updateLeds();
      continue;
    }

    // After BANG, sort out who was genuinely first using the times we recorded
    // when the buttons went down. Checking p1 then p2 then p3 then p4 the way
    // it used to meant player 1 won every close finish.
    unsigned long t = playerShotTime[i];
    if (firstPlayer < 0 || t < firstTime) {
      secondPlayer = firstPlayer;
      secondTime = firstTime;
      firstPlayer = i;
      firstTime = t;
    } else if (secondPlayer < 0 || t < secondTime) {
      secondPlayer = i;
      secondTime = t;
    }
  }

  if (firstPlayer >= 0) {
    winner = firstPlayer;
    bangEnd = firstTime;
    if (secondPlayer >= 0 && (secondTime - firstTime) <= drawWindow) {
      isDraw = true;
      drawPartner = secondPlayer;
    }
  }
}


void showResult() {
  // Work out who is left standing
  if (winner < 0) {
    // everyone died - either they all jumped the gun or nobody shot in time
    for (int i = 0; i < NUM_PLAYERS; i++) {
      alive[i] = false;
    }
  } else {
    for (int i = 0; i < NUM_PLAYERS; i++) {
      alive[i] = (i == winner) || (isDraw && i == drawPartner);
    }
  }

  // update LEDs to reflect who is alive/dead
  updateLeds();

  lcd.clear();
  lcd.setCursor(0, 0);
  if (winner < 0) {
    lcd.print("Everyone dies!");
  } else if (isDraw) {
    lcd.print("Draw! P");
    lcd.print(winner + 1);
    lcd.print(" & P");
    lcd.print(drawPartner + 1);
  } else {
    // how long did it take? bangEnd is stamped the moment the button went
    // down, so this no longer includes the time spent updating the LEDs.
    realTime = bangEnd - bangStart;
    lcd.print("P");
    lcd.print(winner + 1);
    lcd.print(" wins! ");
    lcd.print(realTime);
  }
  tone(buzzer, toneDead, toneLength); // dead

  // awards point to whoever is still standing
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (alive[i]) {
      score[i]++;
    }
  }

  lcd.setCursor(0, 1);
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (i > 0) {
      lcd.print(" ");
    }
    lcd.print(score[i]);
  }
}


// wait for start button to be pressed
void waitOnStartButton() {
  startPressed = false;
  while (!startPressed) {
    check_buttons();
    action();
  }
  startPressed = false;  // unpress it again for the future
  clearShots();          // ignore any button mashing during the results screen
}


void resetGame() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    alive[i] = true;     // player is alive
  }
  clearShots();
  winner = -1;           // nobody has shot
  drawPartner = -1;
  isDraw = false;
  realTime = 0;
  startPressed = false;
}


void clearShots() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    playerShot[i] = false;
    playerShotTime[i] = 0;
  }
}


bool everyoneDead() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    if (alive[i]) {
      return false;
    }
  }
  return true;
}


// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

// show who is alive (blue) and who is dead (red)
void updateLeds() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    digitalWrite(blueLed[i], alive[i] ? HIGH : LOW);
    digitalWrite(redLed[i], alive[i] ? LOW : HIGH);
  }
}


void allLedsOff() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    digitalWrite(blueLed[i], LOW);
    digitalWrite(redLed[i], LOW);
  }
}


// nice to see they all work
void lampTest() {
  for (int i = 0; i < NUM_PLAYERS; i++) {
    digitalWrite(blueLed[i], HIGH);
    digitalWrite(redLed[i], HIGH);
  }
  delay(400);
  allLedsOff();
}


void setStatusColour(int r, int g, int b) {
  analogWrite(statusRGB_R, r);
  analogWrite(statusRGB_G, g);
  analogWrite(statusRGB_B, b);
}
