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

#if STATUS_LED_HARDWARE_PWM

void setStatusRgb(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(STATUS_R, r);
  analogWrite(STATUS_G, g);
  analogWrite(STATUS_B, b);
}

static void statusPwmBegin() {}  // analogWrite() needs no setting up

#else

// Software PWM by bit-angle modulation. See the note in config.h for why this
// exists and why it is eight interrupts a frame rather than 256.
//
// `statusLevel` is written by the main loop and read by the ISR. Each element is
// a single byte, and AVR byte stores are atomic, so a torn read is impossible
// and no cli/sei guard is needed — the worst case is one frame showing an old
// channel value alongside a new one, which is invisible at 100 Hz.
static volatile uint8_t statusLevel[3] = {0, 0, 0};
static const uint8_t statusPin[3] = {STATUS_R, STATUS_G, STATUS_B};
static volatile uint8_t bamPlane = 0;

void setStatusRgb(uint8_t r, uint8_t g, uint8_t b) {
  statusLevel[0] = r;
  statusLevel[1] = g;
  statusLevel[2] = b;
}

ISR(TIMER5_COMPA_vect) {
  // Schedule the next plane before touching anything else.
  //
  // CTC resets TCNT5 to zero at the compare match, so everything done in here
  // runs while the counter is already climbing towards the next one. Writing
  // OCR5A last meant racing three digitalWrite() calls — around 30 counts —
  // against the shortest plane's 78, and if TCNT5 ever got past the new value
  // the compare would be missed and the timer would run the whole way round to
  // 0xFFFF: a 32 ms stall, visible as the lamp hitching. Setting it first costs
  // nothing and removes the race rather than just making it unlikely.
  OCR5A = (uint16_t)STATUS_BAM_TICK_COUNTS << bamPlane;

  // Light every channel whose value has this bit set, and hold for 2^plane
  // ticks. Over a whole frame each channel is lit for exactly its own value out
  // of 255.
  const uint8_t mask = (uint8_t)(1u << bamPlane);
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(statusPin[i], (statusLevel[i] & mask) ? HIGH : LOW);
  }

  bamPlane = (uint8_t)((bamPlane + 1) % STATUS_BAM_PLANES);
}

static void statusPwmBegin() {
  // Timer5, CTC on OCR5A, /8 prescale. No output-compare pins are enabled, so
  // this drives nothing but the interrupt — pins 44-46 stay ordinary I/O.
  noInterrupts();
  TCCR5A = 0;
  TCCR5B = (1 << WGM52) | (1 << CS51);  // CTC, clk/8
  TCNT5 = 0;
  OCR5A = STATUS_BAM_TICK_COUNTS;
  TIMSK5 = (1 << OCIE5A);
  interrupts();
}

#endif  // STATUS_LED_HARDWARE_PWM

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

  setStatusRgb(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0);
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

// A shadow copy of what should be on screen. The resync clears the display, so
// a single-row update has to repaint the row it is not changing.
static char lcdBuf[2][17] = {{0}, {0}};

static void pulseEnable() {
  digitalWrite(LCD_EN, LOW);
  delayMicroseconds(1);
  digitalWrite(LCD_EN, HIGH);
  delayMicroseconds(1);  // the enable pulse must be held at least 450 ns
  digitalWrite(LCD_EN, LOW);
  delayMicroseconds(100);  // most commands settle in 37 us
}

static void writeNibble(uint8_t value) {
  digitalWrite(LCD_D4, (value >> 0) & 0x01);
  digitalWrite(LCD_D5, (value >> 1) & 0x01);
  digitalWrite(LCD_D6, (value >> 2) & 0x01);
  digitalWrite(LCD_D7, (value >> 3) & 0x01);
  pulseEnable();
}

static void writeCommand(uint8_t value) {
  writeNibble(value >> 4);
  writeNibble(value & 0x0F);
}

void lcdResync() {
  digitalWrite(LCD_RS, LOW);
  digitalWrite(LCD_EN, LOW);

  // Three 8-bit function sets. Whatever half-byte the controller was waiting
  // for, these leave it in 8-bit mode; the 0x02 then selects 4-bit.
  writeNibble(0x03);
  delayMicroseconds(4500);
  writeNibble(0x03);
  delayMicroseconds(4500);
  writeNibble(0x03);
  delayMicroseconds(150);
  writeNibble(0x02);

  writeCommand(0x28);  // 4-bit, 2 lines, 5x8 font   (LiquidCrystal 0x28)
  writeCommand(0x0C);  // display on, cursor off, no blink        (0x0C)
  writeCommand(0x01);  // clear
  delayMicroseconds(2000);
  writeCommand(0x06);  // entry mode: advance right, no shift     (0x06)
}

// Resync, then repaint both rows from the shadow copy.
static void lcdRepaint() {
  lcdResync();
  for (uint8_t row = 0; row < 2; row++) {
    lcd.setCursor(0, row);
    for (uint8_t col = 0; col < 16; col++) {
      const char c = lcdBuf[row][col];
      lcd.write(c ? c : ' ');
    }
  }
}

static void setBufRow(uint8_t row, const char* text) {
  uint8_t i = 0;
  while (i < 16 && text[i] != '\0') {
    lcdBuf[row][i] = text[i];
    i++;
  }
  while (i < 16) {
    lcdBuf[row][i++] = ' ';
  }
  lcdBuf[row][16] = '\0';
}

void lcdShow(const char* line1, const char* line2) {
  setBufRow(0, line1);
  setBufRow(1, line2);
  lcdRepaint();
}

void lcdBlank(bool blank) {
  if (blank) {
    lcd.noDisplay();
  } else {
    lcd.display();
  }
}

void lcdNudge(bool right) {
  if (right) {
    lcd.scrollDisplayRight();
  } else {
    lcd.scrollDisplayLeft();
  }
}

void lcdPatch(uint8_t row, uint8_t col, const char* text) {
  if (row > 1 || col >= 16) {
    return;
  }

  // Keep the shadow buffer truthful, so the next full repaint still agrees with
  // what is actually on the glass.
  uint8_t i = 0;
  while (text[i] != '\0' && (col + i) < 16) {
    lcdBuf[row][col + i] = text[i];
    i++;
  }
  if (i == 0) {
    return;
  }

  lcd.setCursor(col, row);
  for (uint8_t n = 0; n < i; n++) {
    lcd.write(lcdBuf[row][col + n]);
  }
}

// 5x8 bitmaps, one byte per row, low five bits used.
//
// The western set is what the menu and the round wear; the bar set is what the
// results screen wears. Only one can be loaded at a time, which is why no
// western glyph may ever be left on screen when the results load the bars — the
// character codes would stay put while CGRAM changed underneath them, and a
// skull would silently turn into a bar.
static const uint8_t GLYPHS_WESTERN_DATA[7][8] PROGMEM = {
    {0x00, 0x00, 0x1C, 0x1F, 0x04, 0x04, 0x00, 0x00},  // pistol, facing right
    {0x00, 0x00, 0x07, 0x1F, 0x04, 0x04, 0x00, 0x00},  // pistol, facing left
    {0x0E, 0x1F, 0x15, 0x1F, 0x0E, 0x0E, 0x00, 0x00},  // skull
    {0x00, 0x0A, 0x15, 0x0E, 0x15, 0x0A, 0x00, 0x00},  // tumbleweed
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00},  // star
    {0x00, 0x00, 0x0E, 0x1F, 0x1F, 0x0E, 0x00, 0x00},  // bullet
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // unused
};

// The bar set carries the race's five widths in slots 1-5, and then the two
// half-height blocks the big digits are built from in 6 and 7. Sharing one set
// means the results screen never has to swap glyphs part-way through.
static const uint8_t GLYPHS_HALF_UPPER[8] PROGMEM = {
    0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00};
static const uint8_t GLYPHS_HALF_LOWER[8] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F};

static GlyphSet loadedGlyphs = GLYPHS_NONE;

void lcdLoadGlyphs(GlyphSet set) {
  if (set == loadedGlyphs || set == GLYPHS_NONE) {
    return;
  }

  uint8_t rows[8];

  for (uint8_t slot = 1; slot <= 7; slot++) {
    if (set == GLYPHS_WESTERN) {
      for (uint8_t r = 0; r < 8; r++) {
        rows[r] = pgm_read_byte(&GLYPHS_WESTERN_DATA[slot - 1][r]);
      }
    } else if (slot <= 5) {
      // A bar of `slot` pixels: the same solid stub on every row.
      const uint8_t bits = (uint8_t)((0x1F << (5 - slot)) & 0x1F);
      for (uint8_t r = 0; r < 8; r++) {
        rows[r] = bits;
      }
    } else {
      const uint8_t* src =
          (slot == 6) ? GLYPHS_HALF_UPPER : GLYPHS_HALF_LOWER;
      for (uint8_t r = 0; r < 8; r++) {
        rows[r] = pgm_read_byte(&src[r]);
      }
    }
    lcd.createChar(slot, rows);
  }

  // createChar leaves the controller addressing CGRAM; anything written after
  // it would land in the glyph table rather than on the screen. Put the cursor
  // back into display memory before returning.
  lcd.setCursor(0, 0);
  loadedGlyphs = set;
}

void lcdLine(uint8_t row, const char* text) {
  if (row > 1) {
    return;
  }
  setBufRow(row, text);
  lcdRepaint();
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
  statusPwmBegin();
  pinMode(BUZZER, OUTPUT);

  // LCD contrast in place of a trim pot. See the Timer2 warning in config.h
  // before putting anything back on tone().
  pinMode(LCD_CONTRAST_PIN, OUTPUT);
  analogWrite(LCD_CONTRAST_PIN, LCD_CONTRAST);

  lcd.begin(16, 2);
  lcd.clear();

  AUDIO_PORT.begin(9600);

  // isACK = false is load-bearing, not a tidy-up.
  //
  // With ACK enabled — the library's default — every command goes through
  //     while (_isSending) { waitAvailable(); }
  // and waitAvailable() only gives up after a 500 ms timeout. On the current
  // wiring the module's replies can never arrive (the SoftwareSerial RX pin
  // cannot receive on a Mega; see config.h), so that timeout is paid in full on
  // every single sound. Nothing else runs during it: buttonsPoll() is not
  // called, so a player who presses and releases inside those 500 ms has their
  // press discarded entirely.
  //
  // With ACK off the library sends and waits 10 ms, and begin() reports success
  // on the strength of `!isACK` rather than a reply it will never hear.
  audioReady = dfPlayer.begin(AUDIO_PORT, /*isACK=*/false, /*doReset=*/true);

  if (audioReady) {
    dfPlayer.volume(AUDIO_VOLUME);
    DBGLN(F("DFPlayer Mini online (fire-and-forget, no ACK)."));
  } else {
    DBGLN(F("DFPlayer did not start - check the SD card and wiring."));
  }
}
