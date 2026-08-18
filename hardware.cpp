#include "hardware.h"

#include <DFRobotDFPlayerMini.h>
#include <LiquidCrystal.h>

#if !AUDIO_USE_HARDWARE_SERIAL
#include <SoftwareSerial.h>
#endif

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

static LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

#if AUDIO_USE_HARDWARE_SERIAL
#define AUDIO_PORT Serial1
#else
static SoftwareSerial audioSerial(AUDIO_SW_RX, AUDIO_SW_TX);
#define AUDIO_PORT audioSerial
#endif

static DFRobotDFPlayerMini dfPlayer;
static bool audioReady = false;

static const uint8_t playerBlue[PLAYER_COUNT] = {P1_BLUE, P2_BLUE, P3_BLUE, P4_BLUE};
static const uint8_t playerRed[PLAYER_COUNT] = {P1_RED, P2_RED, P3_RED, P4_RED};

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

struct ButtonState {
  uint8_t pin;
  bool lastRaw;        // input level on the previous poll
  bool pending;        // a press is waiting to be consumed
  unsigned long downAtUs;
  unsigned long lastEdgeMs;  // start of the current lockout
};

static ButtonState buttons[BUTTON_COUNT] = {
    {P1_BUTTON, false, false, 0, 0},
    {P2_BUTTON, false, false, 0, 0},
    {P3_BUTTON, false, false, 0, 0},
    {P4_BUTTON, false, false, 0, 0},
    {START_BUTTON, false, false, 0, 0},
};

static inline bool isPressed(int reading) {
#if BUTTONS_ACTIVE_LOW
  return reading == LOW;
#else
  return reading == HIGH;
#endif
}

void buttonsPoll() {
  const unsigned long nowMs = millis();

  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    ButtonState& b = buttons[i];
    const bool raw = isPressed(digitalRead(b.pin));

    // Leading edge, and not still inside the lockout from the last one.
    // Subtracting unsigned longs this way is what makes the comparison survive
    // the millis() rollover at 49.7 days; `nowMs < deadline` would not.
    if (raw && !b.lastRaw && (nowMs - b.lastEdgeMs) >= DEBOUNCE_LOCKOUT_MS) {
      b.pending = true;
      b.downAtUs = micros();
      b.lastEdgeMs = nowMs;
    }

    b.lastRaw = raw;
  }
}

bool buttonWentDown(ButtonId id) {
  if (!buttons[id].pending) {
    return false;
  }
  buttons[id].pending = false;
  return true;
}

unsigned long buttonDownAt(ButtonId id) { return buttons[id].downAtUs; }

void buttonsClearAll() {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    buttons[i].pending = false;
  }
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

void setStatusColor(StatusColor color) {
  bool r = false, g = false, b = false;

  switch (color) {
    case COLOR_RED:     r = true;                     break;
    case COLOR_GREEN:            g = true;            break;
    case COLOR_BLUE:                      b = true;   break;
    case COLOR_YELLOW:  r = true; g = true;           break;
    case COLOR_MAGENTA: r = true;         b = true;   break;
    case COLOR_CYAN:             g = true; b = true;  break;
    case COLOR_WHITE:   r = true; g = true; b = true; break;
    case COLOR_OFF:
    default:                                          break;
  }

  digitalWrite(STATUS_R, r ? HIGH : LOW);
  digitalWrite(STATUS_G, g ? HIGH : LOW);
  digitalWrite(STATUS_B, b ? HIGH : LOW);
}

void setPlayerLeds(uint8_t index, bool blue, bool red) {
  if (index >= PLAYER_COUNT) {
    return;
  }
  digitalWrite(playerBlue[index], blue ? HIGH : LOW);
  digitalWrite(playerRed[index], red ? HIGH : LOW);
}

void setAllPlayerLeds(bool blue, bool red) {
  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    setPlayerLeds(i, blue, red);
  }
}

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------

void lcdShow(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void lcdLine(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t written = 0;
  while (text[written] != '\0' && written < 16) {
    lcd.write(text[written]);
    written++;
  }
  while (written < 16) {
    lcd.write(' ');
    written++;
  }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void audioPlay(uint8_t track) {
  if (!audioReady) {
    return;
  }
  dfPlayer.playMp3Folder(track);
}

// ---------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------

void hardwareBegin() {
  Serial.begin(115200);

  // Buttons. The original looped `pinMode(i, INPUT)` over the loop counter
  // rather than the pin array, so it configured pins 0-3 — the hardware serial
  // pair and two of the LCD data lines — and left the fifth button out entirely.
  // The real button pins happened to be configured separately further down,
  // which is the only reason the game ran at all.
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
#if BUTTONS_ACTIVE_LOW
    pinMode(buttons[i].pin, INPUT_PULLUP);
#else
    pinMode(buttons[i].pin, INPUT);
#endif
    buttons[i].lastRaw = isPressed(digitalRead(buttons[i].pin));
    buttons[i].pending = false;
    // Back-date the lockout so a button pressed at power-on is not swallowed.
    buttons[i].lastEdgeMs = millis() - DEBOUNCE_LOCKOUT_MS;
  }

  for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
    pinMode(playerBlue[i], OUTPUT);
    pinMode(playerRed[i], OUTPUT);
  }
  pinMode(STATUS_R, OUTPUT);
  pinMode(STATUS_G, OUTPUT);
  pinMode(STATUS_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // LCD contrast in place of a trim pot. See the Timer2 warning in config.h
  // before putting anything back on tone().
  pinMode(LCD_CONTRAST_PIN, OUTPUT);
  analogWrite(LCD_CONTRAST_PIN, LCD_CONTRAST);

  lcd.begin(16, 2);
  lcd.clear();

  AUDIO_PORT.begin(9600);
  audioReady = dfPlayer.begin(AUDIO_PORT);
  if (audioReady) {
    dfPlayer.volume(AUDIO_VOLUME);
    DBGLN(F("DFPlayer Mini online."));
  } else {
    // With AUDIO_USE_HARDWARE_SERIAL 0 this is expected: the SoftwareSerial RX
    // pin cannot receive on a Mega, so the module's ACK never arrives even
    // though playback works. See config.h.
    DBGLN(F("DFPlayer did not answer - check the SD card and wiring."));
#if !AUDIO_USE_HARDWARE_SERIAL
    // Send anyway; transmit is unaffected by the RX pin problem.
    audioReady = true;
    dfPlayer.volume(AUDIO_VOLUME);
#endif
  }
}
