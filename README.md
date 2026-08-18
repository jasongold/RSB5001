# RSB5001

My son and I created a physical version of Ready, Steady, Bang using an Arduino.

Four players, each with a button and a pair of LEDs. The screen says **Ready**, then
**Steady**, then — after a delay you cannot predict — **Bang!** First to hit their button
wins the round. Draw before the bang and you are out.

There is also a single-player mode where you race the machine at one of three difficulties.

## Playing

- The **centre button** cycles the mode: Multiplayer → Solo Easy → Solo Normal → Solo Hard.
- **Any player button** starts the round. In a solo game, whichever station you start from
  becomes yours.
- After the round the screen shows the winner and their reaction time, then the order
  everyone drew in (`P3>P1>P4>P2`), then the running scores. Any button goes back to the menu.

Scores are kept until the board is powered off.

## Hardware

Arduino Mega 2560, chosen for the pin count — every button gets its own pin rather than
sharing one analog input, so presses can be timed independently.

| | Pins |
|---|---|
| LCD (16x2, 4-bit) | RS 12, EN 13, D4–D7 5/4/3/2 |
| LCD contrast | PWM on 10 |
| Player 1 | button 25, blue 23, red 22 |
| Player 2 | button 27, blue 24, red 26 |
| Player 3 | button 29, blue 28, red 30 |
| Player 4 | button 31, blue 32, red 34 |
| Start / select | 52 |
| Status RGB | R 40, G 38, B 36 |
| DFPlayer Mini | SoftwareSerial RX 54, TX 55 |
| Buzzer (unused) | 7 |

Buttons are wired to Vcc with external pull-down resistors — HIGH means pressed.

All of this lives in [`config.h`](config.h), which is the only file containing a pin number.
The wiring diagram is in [`reference/`](reference/).

### Sound

A DFPlayer Mini plays from the SD card's `MP3` folder:

| Track | Sound |
|---|---|
| 2 | gunshot, on a successful draw |
| 3 | on drawing too early |
| 4 | "Ready" |
| 5 | "Bang!" |
| 6 | "Steady" |
| 7 | revolver spin, on the menu |

### Two wiring changes worth making

Both are optional, and the code runs on the current wiring without them. Each is behind a
flag in `config.h`.

**`AUDIO_USE_HARDWARE_SERIAL`** — move the DFPlayer from SoftwareSerial to `Serial1`
(DFPlayer RX to pin 18, TX to pin 19). SoftwareSerial needs a pin-change-interrupt-capable
RX pin and on the Mega only 10–15, 50–53 and A8–A15 qualify, so pin 54 (A0) can never
receive; the module's replies are lost, which is why the sketch reports it did not answer.
It also disables interrupts for about 10 ms while sending each command — including the
command sent just before the bang — which stops `millis()` counting and skews the timing.

**`BUTTONS_ACTIVE_LOW`** — rewire the five buttons to ground and use the AVR's internal
pull-ups instead. Removes five resistors, and removes the failure mode where a popped wire
leaves an input floating and firing at random.

## Building

Open the folder in the Arduino IDE and select **Arduino Mega or Mega 2560**. Needs the
`LiquidCrystal` and `DFRobotDFPlayerMini` libraries.

From the command line:

```bash
arduino-cli compile --fqbn arduino:avr:mega .
```

## Layout

| File | |
|---|---|
| `rsb5001.ino` | The game state machine |
| `config.h` | Pins, timings, feature flags |
| `hardware.h` / `.cpp` | Buttons, LEDs, LCD, audio |
| `game.h` / `.cpp` | Players, scoring, placings, the solo opponent |
| `reference/` | Earlier versions and the wiring diagram |

The round is a non-blocking state machine — `loop()` polls the buttons, runs one pass of the
current state, and returns. See [`CODE-REVIEW.md`](CODE-REVIEW.md) for the review of the
earlier version that prompted the rewrite.

## Ideas not yet built

- Remember the fastest time ever, in EEPROM so it survives a power cycle
- A way to reset the scores without pulling the plug
- Choose how many players are in, so absent stations are not marked dead every round

## Bench simulator

[`simulator/index.html`](simulator/index.html) is the sketch's state machine ported to the
browser, so the game can be played and the logic checked without flashing the board. Same
constants, same screens, same rules. Open the file directly — no build step, no server.

Press **1–4** to draw and **space** for the centre button. It shows the current state, the
steady delay it rolled, and a serial-monitor pane mirroring the debug output.

It is a port, not an emulator: timestamps come from `performance.now()` and the poll runs once
per animation frame, so reaction times are quantised to about 16 ms where the board resolves
far finer. There is no DFPlayer, so the countdown is silent.
