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
- After the round the screen shows the winner and their reaction time, with a remark on how
  quick it was; after a moment the remark gives way to the order everyone drew in
  (`P3>P1>P4>P2`), and then the running scores. Any button goes back to the menu.

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
| Status RGB | R 40, G 38, B 36 (or 44/45/46 for hardware PWM — see below) |
| DFPlayer Mini | SoftwareSerial RX 54, TX 55 |
| Buzzer (unused) | 7 |

Buttons are wired to Vcc with external pull-down resistors — HIGH means pressed.

All of this lives in [`config.h`](config.h), which is the only file containing a pin number.

There is no wiring diagram for the box. The PNG in [`reference/`](reference/) is a stock
illustration of an Arduino Uno with a 16x2 LCD and a contrast pot — useful for the LCD
hookup, but it has no LEDs, no buttons and no buzzer on it, and it is not even the right
board. `config.h` is the only record of how the box is actually wired.

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
| `cosmetics.h` / `.cpp` | The light show: LED patterns, screen blinks, flavour text |
| `reference/` | Earlier versions, and an LCD hookup illustration |

The round is a non-blocking state machine — `loop()` polls the buttons, runs one pass of the
current state, and returns. See [`CODE-REVIEW.md`](CODE-REVIEW.md) for the review of the
earlier version that prompted the rewrite.

## The light show

All of it lives in [`cosmetics.h`](cosmetics.h) / [`.cpp`](cosmetics.cpp), driven by a
`cosmeticsTick()` that runs once per `loop()` pass and steps a pattern only when one is due.

| When | LEDs | Centre lamp | Screen |
|---|---|---|---|
| Menu | all on | breathing, in the mode's colour | mode and prompt |
| Menu, 10 s idle | chase out in blue, back in red | firelight flicker | marquee up top, tumbleweed rolling below |
| Ready | blue LEDs light in turn | charges green to white | two gunslingers walking in |
| Steady | pulse together, flat 220 ms | heartbeat, flat 60 bpm | "Steady" |
| Bang | every live station on | white flash, fades to hot red | kicks 3 columns sideways, then blinks twice |
| False start | the offender goes red | red strobe over the top | a skull and who did it |
| Results | winner blue, losers red | the colour that draw earned | remark, race, the time in big numerals, placings |

The centre lamp shows a **colour grade** for the winning time — violet under 200 ms, through
blue, green, amber and orange, to a deep red past 700 ms. Those are the same six bands the
remarks use, so a draw called "Lightning!" is always the same violet.

### The bar race

The results screen replays the round. Runners set off together and travel at speeds inversely
proportional to their reaction times, so the winner reaches the finish line exactly as the
replay ends and everyone else falls short by the margin they actually lost by. You get to
watch a two-millisecond win.

Multiplayer gives each station a lane; solo gives you one and the machine the other, so a
single player still gets a race. Then the winning time fills the screen in numerals two rows
tall, built from half-height blocks.

The whole show runs to about five seconds, so **any button skips it** and goes straight to
the scores.

### The scores screen

Two pages, flipping every 2.5 seconds, because the screen was already waiting for a button
and both halves are worth seeing:

```
 P1  P2 ★P3  P4        Recent winners
  1   0   3   1        3 1 3 4 3 3 2 1
```

The totals use all sixteen columns — four columns of four, with the spare column ahead of
each label holding a star for whoever is leading. Ties star everyone level at the top, and a
table of all zeroes stars nobody. Scores are right-aligned under the station number, so the
grid survives getting into three figures.

The history says who won each of the last eight rounds, oldest on the left, which is usually
the more interesting of the two — it shows streaks, where the totals only show the standings.
A dash means no station won that round: nobody drew in time, or in solo the machine did.

Any button still returns to the menu from either page.

### Custom glyphs, and the eight-slot problem

`createChar()` gives eight CGRAM slots and the bar race alone wants five, so glyphs come in
two sets, swapped on state entry:

| Set | Slots | Worn by |
|---|---|---|
| Western | two pistols, skull, tumbleweed, star | menu, Ready, Steady, scores |
| Bars | five bar widths, plus two half blocks for the big numerals | results |

Loading a set is 64 writes, about 13 ms — fine when a screen changes, never during a round.
The consequence worth knowing: **no western glyph may still be on screen when the results
load the bars.** Character codes stay where they are while CGRAM changes underneath them, so
a skull left up would silently turn into a bar. That is why the false-start skull lives only
on the Ready and Steady screens.

### Two rules this code has to follow

**Nothing shown during Steady may depend on `steadyDurationMs`.** A lamp that warmed toward
the bang, or a pulse that quickened into it, would hand the players the one thing the whole
game depends on them not knowing. The Steady heartbeat and pulse run at fixed rates and loop.

Ready is the opposite: `READY_MS` is fixed and every player knows how long it lasts, so the
green-to-white charge and the duel walking in give nothing away.

**The screen is expensive and the LEDs are not.** Every `lcdShow()` resyncs the controller and
repaints all 32 cells — roughly 19 ms, blocking, with no `buttonsPoll()` running — while a
`digitalWrite` to an LED costs about 4 µs. So animation uses `lcdPatch()`, which skips the
resync at ~205 µs a character, and the only screen effects allowed between the bang and the
first press are `lcdBlank()` and `lcdNudge()` — single command bytes, no repaint.

That budget is also why the bang effects were wrong at first. The first version blinked three
times at 55 ms and kicked a single column, all while the display was dark: technically cheap,
correct, and completely invisible. It now kicks three columns *lit*, then blinks twice at
120 ms, for a few hundred microseconds more.

### Colour on the centre lamp

The lamp is a common-cathode RGB LED and will make any colour. Whether the Mega can ask it to
depends on which pins it is on, and `STATUS_LED_HARDWARE_PWM` in [`config.h`](config.h) picks
how:

- **`0` (default, as built)** — lamp on 40/38/36. Those are not PWM pins, so the colour is
  generated in firmware by bit-angle modulation from a Timer5 interrupt: bit-plane *p* is held
  for 2^*p* ticks, so eight interrupts paint a full 8-bit frame instead of the 256 a naive
  software PWM would need. About 960 interrupts a second.
- **`1`** — move three wires to 44/45/46 and the colour comes from `analogWrite()` instead.
  No interrupt, no CPU cost.

Worth knowing: 44/45/46 is where the 2021 builds had it, and they blended properly. The 2023
rebuild moved the wires to 40/38/36 but kept the `analogWrite()` calls, and on non-PWM pins
every value ≥ 128 just latches HIGH — so the amber that
[`reference/RSB5001-2023-dfplayer.ino.txt:382`](reference/) asks for has been coming out
white-ish ever since. Either setting above fixes that; only `0` needs no screwdriver.

The **station LEDs cannot do this** — they are discrete blue and red parts on non-PWM pins, so
they are on, off, or both, whatever the lamp is doing.

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
