/*

Me and my Son's version of Ready Steady Bang!
We call it RSB5001

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

// Setup variables for the buttons and LEDs
// using const because they will never change
const int p1Button = 22;
const int p1BlueLed = 23;
const int p1RedLed = 24;

const int p2Button = 25;
const int p2BlueLed = 26;
const int p2RedLed = 27;

const int p3Button = 28;
const int p3BlueLed = 29;
const int p3RedLed = 30;

const int p4Button = 31;
const int p4BlueLed = 32;
const int p4RedLed = 33;

const int startButton = 34;

const int statusRGB_B = 46;
const int statusRGB_G = 45;
const int statusRGB_R = 44;


const int buzzer = 7; //buzzer to arduino pin 7

// settings
const int timeMin = 100;  // minimum time on "BANG"
const int timeMax = 4000;  // maximum time on "BANG"
const unsigned long bangAllDead = 3000;  // everyone died...nobody shot. this is the time after bang to wait for button presses


// variables that will change
int long ranDelay; // random delay for the time for BANG
int realTime;  //time it took to push button after bang
unsigned long steadyStart; // when did it say steady?
unsigned long bangStart;  // when did it say bang?
unsigned long bangEnd;  // when was button pushed to shoot?
unsigned long startWait; // start of wait time
unsigned long shootDelayWait = 0; // how long to wait before end of ready and steady - set each time
int gameMode = 0; // 0 is ready & 1 is steady, and 2 is bang
int p1Score = 0;
int p2Score = 0;
int p3Score = 0;
int p4Score = 0;

// variables that need to be reset for each game (same ones should be below as well)
int gameStarted = 0;  // is there a game happening?
int p1ButtonPressed = 0; // button not pressed
int p2ButtonPressed = 0; // button not pressed
int p3ButtonPressed = 0; // button not pressed
int p4ButtonPressed = 0; // button not pressed
int startButtonPressed = 0; // button not pressed
int deadPlayer = 0; // did someone die by shooting too soon?
int alivePlayer = 0;  // who lived??
int gameEnded = 0; // the game is not over
int p1alive = 1;  // player is alive
int p2alive = 1;  // player is alive
int p3alive = 1;  // player is alive
int p4alive = 1;  // player is alive


// debounce code to stop bad reads of the buttons
// adapted from https://forum.arduino.cc/t/debouncing-multiple-buttons-with-arrays-sample-for-review/499457
byte buttons[] = {p1Button, p2Button, p3Button, p4Button, startButton}; // pin numbers of the buttons that we'll use
#define NUMBUTTONS sizeof(buttons)
int buttonState[NUMBUTTONS];
int lastButtonState[NUMBUTTONS];
boolean buttonIsPressed[NUMBUTTONS];
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
long lastDebounceTime = 0; // the last time the output pin was toggled
long debounceDelay = 50; // the debounce time; increase if the output flickers
// end debounce code



void setup() {
  // debounce code
  Serial.begin(9600);
  // define pins:
  for (int i=0; i<(NUMBUTTONS-1); i++) {
    pinMode(i, INPUT);
    lastButtonState[i]=LOW;
    buttonIsPressed[i]=false;
  }

  pinMode(10, OUTPUT);
  int Contrast=75;
  analogWrite(10, Contrast);
  // For LCD for contrast (instead of pent)


  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Clears the LCD screen
  lcd.clear();

  // initialize LEDs as outputs
  pinMode(p1BlueLed, OUTPUT);
  pinMode(p1RedLed, OUTPUT);
  pinMode(p2BlueLed, OUTPUT);
  pinMode(p2RedLed, OUTPUT);
  pinMode(p3BlueLed, OUTPUT);
  pinMode(p3RedLed, OUTPUT);
  pinMode(p4BlueLed, OUTPUT);
  pinMode(p4RedLed, OUTPUT);
  pinMode(statusRGB_R, OUTPUT);
  pinMode(statusRGB_G, OUTPUT);
  pinMode(statusRGB_B, OUTPUT);

  // buzzer
  pinMode(buzzer, OUTPUT); // Set buzzer as an output

  // initalize buttons as inputs
  pinMode(p1Button, INPUT);
  pinMode(p2Button, INPUT);
  pinMode(p3Button, INPUT);
  pinMode(p4Button, INPUT);
  pinMode(startButton, INPUT);

  // initial start screen
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Press the blue");
  lcd.setCursor(0, 1);
  lcd.print("button to start");
}


void loop() {
  analogWrite(statusRGB_R, 0);
  analogWrite(statusRGB_G, 0);
  analogWrite(statusRGB_B, 255);

  check_buttons();
  action();
  if (gameStarted == 1){
    startGame();
  }

  //turn LEDs on (nice to see they all work)
  digitalWrite (p1BlueLed, HIGH);
  digitalWrite (p1RedLed, HIGH);
  digitalWrite (p2BlueLed, HIGH);
  digitalWrite (p2RedLed, HIGH);
  digitalWrite (p3BlueLed, HIGH);
  digitalWrite (p3RedLed, HIGH);
  digitalWrite (p4BlueLed, HIGH);
  digitalWrite (p4RedLed, HIGH);
}


//debounce code
void check_buttons() {
  for (int currentButton=0; currentButton<NUMBUTTONS; currentButton++) {
    // read the state of the switch into a local variable:
    int reading = digitalRead(buttons[currentButton]); // check to see if you just pressed the button and you've waited long enough since the last press to ignore any noise:
    // If the switch changed, due to noise or pressing then reset the debouncing timer
    if (reading != lastButtonState[currentButton]) { lastDebounceTime = millis(); }
    // whatever the reading is at, it's been there for longer than the debounce delay, so take it as the actual current state:
    if ((millis() - lastDebounceTime) > debounceDelay) {
      // if the button state has changed:
      if (reading != buttonState[currentButton]) {
        buttonState[currentButton] = reading;
        if (buttonState[currentButton]==HIGH) { //pushing down on the button
          buttonIsPressed[currentButton]=true; // set your flag for the adjustment function
        }
      }
    }
    // save the reading.  Next time through the loop, it'll be the lastButtonState:
    lastButtonState[currentButton] = reading;
  }
}

//debounce code
void action() {
  //lcd.setCursor(0, 1);
  for (int currentButton=0; currentButton<NUMBUTTONS; currentButton++) {
    if (buttonIsPressed[currentButton]) {
      //Serial.print("button "); Serial.println(buttons[currentButton]);
      if (buttons[currentButton]==startButton) { // -------- Start button
          if (gameStarted == 0){
            gameStarted = 1;
          }
          //lcd.print("Pressed start");
          startButtonPressed = 1;
      }
      if ((buttons[currentButton]==p1Button) || (buttons[currentButton]==p2Button) || (buttons[currentButton]==p3Button) || (buttons[currentButton]==p4Button)){
        if (buttons[currentButton]==p1Button) { // -------- Player 1 button
            p1ButtonPressed = 1;
            //lcd.print("Pressed Player 1");
        } else if (buttons[currentButton]==p2Button) { // -------- Player 2 button
            p2ButtonPressed = 1;
            //lcd.print("Pressed Player 2");
        } else if (buttons[currentButton]==p3Button) { // -------- Player 3 button
            p3ButtonPressed = 1;
            //lcd.print("Pressed Player 3");
        } else if (buttons[currentButton]==p4Button) { // -------- Player 4 button
            p4ButtonPressed = 1;
            //lcd.print("Pressed Player 4");
        }
      }
      buttonIsPressed[currentButton]=false; //reset the button
    }
  }
}


void startGame(){
  gameStarted = 1;
  // reset the start button to 'unpressed' so we can check for it again later
  startButtonPressed = 0;

  // now starting READY
  gameMode = 0; // 0 is ready & 1 is steady, and 2 is bang

  tone(buzzer, 3520, 50); // ready & steady

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready");

  analogWrite(statusRGB_R, 0);
  analogWrite(statusRGB_G, 255);
  analogWrite(statusRGB_B, 0);

  // turn off player lights
  digitalWrite (p1BlueLed, LOW);
  digitalWrite (p1RedLed, LOW);
  digitalWrite (p2BlueLed, LOW);
  digitalWrite (p2RedLed, LOW);
  digitalWrite (p3BlueLed, LOW);
  digitalWrite (p3RedLed, LOW);
  digitalWrite (p4BlueLed, LOW);
  digitalWrite (p4RedLed, LOW);

  // wait for a second between ready and steady - but let people kill themselves
  shootDelayWait = millis() + 1000;
  waitForShot();

  // Now starting STEADY
  gameMode = 1; // 0 is ready & 1 is steady, and 2 is bang
  tone(buzzer, 3520, 50); // ready & steady sound
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Steady");
  // Yellow (turn red and green on):
  analogWrite(statusRGB_R, 255 );
  analogWrite(statusRGB_G, 160);
  analogWrite(statusRGB_B, 0);

  // random delay from steady to bang
  // set steady start to right now
  // wait for time to expire but also check for buttons being pushed in case someone jumps the gun
  // if someone shoots before delay is up, they die!
  ranDelay = random(timeMin, timeMax);
  shootDelayWait = millis() + ranDelay;
  waitForShot();

  // Now starting BANG
  gameMode = 2; // 0 is ready & 1 is steady, and 2 is bang
  // waiting on first player to shoot after bang!
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bang!");
  analogWrite(statusRGB_R, 255);
  analogWrite(statusRGB_G, 0);
  analogWrite(statusRGB_B, 0);
  tone(buzzer, 4699, 50); // bang
  lcd.setCursor(0, 1);

  // let's see who shoots first!!
  bangStart = millis();  // also used below for calculating reflex time
  shootDelayWait = bangStart + bangAllDead;
  waitForShot();

  // check if everyone died to mark them as dead
  if (alivePlayer == 0){
    p1alive = 0;  // player is dead
    p2alive = 0;  // player is dead
    p3alive = 0;  // player is dead
    p4alive = 0;  // player is dead
  }
  showAlive();  // either from all dead or from someone shooting correctly

  // how long did it take?
  realTime = millis() - bangStart;
  lcd.clear();
  lcd.setCursor(0, 0);
  if (alivePlayer == 0){
    lcd.print(String("Everyone dies!"));
  } else {
    lcd.print(String("P") + String(alivePlayer) + String(" wins! ") +String(realTime));
  }
  tone(buzzer, 1760, 50); // dead

  // update LEDs to reflect who is alive/dead
  showAlive();

  // awards point to winner
  if(p1alive == 1){
    p1Score = p1Score +1;
  }
  if(p2alive == 1){
    p2Score = p2Score +1;
  }

  if(p3alive == 1){
    p3Score = p3Score +1;
  }

  if(p4alive == 1){
    p4Score = p4Score +1;
  }

  //Serial.print(p1Score); Serial.print(" "); Serial.print(p2Score); Serial.print(" "); Serial.print(p3Score); Serial.print(" "); Serial.print(p4Score); Serial.print(" ");
  lcd.setCursor(0, 1);
  lcd.print(String(p1Score) + String(" ") + String(p2Score) + String(" ") + String(p3Score) + String(" ") + String(p4Score));

  // keep showing results and wait for start button again
  waitOnStartButton();
  resetGame();
}



// this waits for someone to either shoot who is alive and runs till time delay ends
// it handles all three 'wait' periods (ready/steady/bang)
void waitForShot(){
  while ((millis() < shootDelayWait) && (alivePlayer == 0)){
    //Serial.print("waitForShot:  "); Serial.print("game mode: "); Serial.print(gameMode); Serial.print(" start: "); Serial.print(millis()); Serial.print(" delaywait: "); Serial.print(shootDelayWait); Serial.print("\n");
    check_buttons();
    action();
    if ( (p1ButtonPressed == 1) || (p2ButtonPressed == 1) || (p3ButtonPressed == 1) || (p4ButtonPressed == 1)){
      // someone pushed a button...let's see who (we track it this way cause it checks everyone at the exact same time)
      // note below - you only can push a button if you are alive
      if ((p1ButtonPressed == 1) && (p1alive == 1)){
        if (gameMode < 2) {  // 2 is bang
          deadPlayer = 1;
          p1alive = 0;
          killPlayer();
        } else {
          alivePlayer = 1;
          p2alive = 0;  // player is dead
          p3alive = 0;  // player is dead
          p4alive = 0;  // player is dead
        }
      }
      else if ((p2ButtonPressed == 1) && (p2alive == 1)){
        if (gameMode < 2) {  // 2 is bang
          deadPlayer = 2;
          p2alive = 0;
          killPlayer();
        } else {
          alivePlayer = 2;
          p1alive = 0;  // player is dead
          p3alive = 0;  // player is dead
          p4alive = 0;  // player is dead
        }
      }
      else if ((p3ButtonPressed == 1) && (p3alive == 1)){
        if (gameMode < 2) {  // 2 is bang
          deadPlayer = 3;
          p3alive = 0;
          killPlayer();
        } else {
          alivePlayer = 3;
          p1alive = 0;  // player is dead
          p2alive = 0;  // player is dead
          p4alive = 0;  // player is dead
        }
      }
      else if ((p4ButtonPressed == 1) && (p4alive == 1)){
        if (gameMode < 2) {  // 2 is bang
          deadPlayer = 4;
          p4alive = 0;
          killPlayer();
        } else {
          alivePlayer = 4;
          p1alive = 0;  // player is dead
          p2alive = 0;  // player is dead
          p3alive = 0;  // player is dead
        }
      }
    }
  }
}



// update LEDs to dead
void killPlayer(){
  if (p1alive == 0){
    digitalWrite (p1BlueLed, LOW);
    digitalWrite (p1RedLed, HIGH);
  }
  if (p2alive == 0){
    digitalWrite (p2BlueLed, LOW);
    digitalWrite (p2RedLed, HIGH);
  }
  if (p3alive == 0){
    digitalWrite (p3BlueLed, LOW);
    digitalWrite (p3RedLed, HIGH);
  }
  if (p4alive == 0){
    digitalWrite (p4BlueLed, LOW);
    digitalWrite (p4RedLed, HIGH);
  }
}


// update LEDs to show who is alive/dead at end of game
void showAlive(){
  if (p1alive == 1){
    digitalWrite (p1BlueLed, HIGH);
    digitalWrite (p1RedLed, LOW);
  } else {
    digitalWrite (p1BlueLed, LOW);
    digitalWrite (p1RedLed, HIGH);
  }
  if (p2alive == 1){
    digitalWrite (p2BlueLed, HIGH);
    digitalWrite (p2RedLed, LOW);
  } else {
    digitalWrite (p2BlueLed, LOW);
    digitalWrite (p2RedLed, HIGH);
  }
  if (p3alive == 1){
    digitalWrite (p3BlueLed, HIGH);
    digitalWrite (p3RedLed, LOW);
  } else {
    digitalWrite (p3BlueLed, LOW);
    digitalWrite (p3RedLed, HIGH);
  }
  if (p4alive == 1){
    digitalWrite (p4BlueLed, HIGH);
    digitalWrite (p4RedLed, LOW);
  } else {
    digitalWrite (p4BlueLed, LOW);
    digitalWrite (p4RedLed, HIGH);
  }
}

void resetGame(){
  gameStarted = 0;  // is there a game happening?
  p1ButtonPressed = 0; // button not pressed
  p2ButtonPressed = 0; // button not pressed
  p3ButtonPressed = 0; // button not pressed
  p4ButtonPressed = 0; // button not pressed
  startButtonPressed = 0; // button not pressed
  deadPlayer = 0; // did someone die by shooting too soon?
  alivePlayer = 0;  // who lived??
  p1alive = 1;  // player is alive
  p2alive = 1;  // player is alive
  p3alive = 1;  // player is alive
  p4alive = 1;  // player is alive
  gameEnded = 0; // the game is not over
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("New Game! Press");
  lcd.setCursor(0, 1);
  lcd.print("blue to start");

}

// wait for start button to be pressed
void waitOnStartButton(){
  while(1){
    check_buttons();
    action();
    if (startButtonPressed == 1) {
      startButtonPressed = 0;  // unpress it again for the future
      return;
    }
  }
}
