# RSB5001 — Code Review

**Reviewed:** `rsb5001.ino` (500 lines) at commit `8fc06a2`, the version tracked in `jasongold/RSB5001`.
**Date:** 2026-08-17
**Hardware:** Arduino Mega. LCD `LiquidCrystal lcd(12, 11, 5, 4, 3, 2)`; player buttons on 22 / 25 / 28 / 31; blue+red LEDs per player on 23–33; start button 34; status RGB on 44–46; buzzer on 7; LCD contrast driven from pin 10.

This document is self-contained — it can be picked up without the conversation that produced it.

---

## Repo state (read this first)

Three facts that affect any work on this project:

1. **The sketch folder will not compile as checked out.** `RSB5001/` contains two `.ino` files — `rsb5001.ino` (tracked) and `RSB5001 1.txt.ino` (untracked). The Arduino IDE concatenates *every* `.ino` in a sketch folder into a single translation unit, so opening this sketch yields duplicate definitions of `setup()`, `loop()`, and ~30 globals. Resolve before building.

2. **The newest code is not in git.** `RSB5001 1.txt.ino` (652 lines, Oct 2023) adds a DFPlayer Mini MP3 module and a single/multiplayer menu. It has never been committed and exists only on this disk. The repo's tracked `rsb5001.ino` is the *middle* generation (Jun 2021). Its filename — spaces plus a double extension — is also not a valid Arduino main sketch name.

3. **Two earlier drafts live outside the repo**, unversioned and un-backed-up, in `../RSB5000/`: `RSB500/RSB500.ino` (Jun 25 2021 09:44) and `RSB5000/RSB5000.ino` (Jun 25 2021 17:38), plus the only copy of the Fritzing wiring diagram (`RSB5000/RSB5001/Arduino-Wiring-Fritzing-Connections-with-16x2-Character-LCD.png`). `RSB5000/RSB5001/RSB5001.ino` is a byte-identical duplicate of the 2023 file and is the only genuinely disposable item there; its nested `.git` has zero commits and no remote.

**Open decision:** whether to delete `../RSB5000/`. Deleting the whole tree destroys the two 2021 drafts and the wiring diagram permanently. Options discussed: (A) delete only `RSB5000/RSB5001/` and move the PNG into this repo; (B) commit the drafts + PNG + 2023 sketch here first, then delete the tree; (C) delete it all. Not yet decided.

---

## CRITICAL

### 1. Button presses made before the round starts kill players instantly

`rsb5001.ino:214`, `:233`, `:468`

`action()` sets `p1ButtonPressed = 1` on any player press, in any state. Those flags are cleared **only** in `resetGame()`, which runs at the *end* of a game. `startGame()` clears `startButtonPressed` (`:236`) but never the player flags.

Repro: power on → a player idly taps their button during setup → `p1ButtonPressed` stays `1` → someone presses start → the first `waitForShot()` sees `p1ButtonPressed == 1 && p1alive == 1` and kills P1 during "Ready", before they touched anything. Also triggers on any press during the "New Game! Press blue to start" screen.

Fix: clear all four player flags at the top of `startGame()`. The 2023 sketch already solves this with an `unpressButtons()` helper — backport it.

### 2. `random()` is never seeded

`rsb5001.ino:280`

`ranDelay = random(timeMin, timeMax)` with no `randomSeed()` anywhere in the file. The AVR PRNG starts from the same state on every reset, so the Steady→Bang delay sequence is byte-identical every power-up. Players who play a few sessions can learn the rhythm, which defeats the entire premise of a reaction game.

Fix: `randomSeed(analogRead(A0))` in `setup()` using a genuinely floating analog pin, or seed from `micros()` at the first start-button press (better entropy).

---

## HIGH

### 3. One shared debounce timer across all five buttons

`rsb5001.ino:94`, `:183-185`

```c
if (reading != lastButtonState[currentButton]) { lastDebounceTime = millis(); }
if ((millis() - lastDebounceTime) > debounceDelay) { ... }
```

`lastDebounceTime` is a single global, so *any* button's edge resets the debounce window for *every* button. Two players pressing ~10 ms apart push each other's deadline out; bounce on one button delays all five. The comment at `:357` claims the design "checks everyone at the exact same time," but this makes near-simultaneous presses the least reliable case — which is the case the game exists to measure.

Fix: `unsigned long lastDebounceTime[NUMBUTTONS]`, indexed per button.

### 4. Ties always go to the lowest-numbered player

`rsb5001.ino:356-395`

`waitForShot()` resolves the winner with an `if / else if / else if / else if` chain from P1 to P4. When two players' flags are set on the same pass — exactly what the shared 50 ms debounce window produces — P1 wins unconditionally. No per-press timestamps exist, so a real tie is decided by player number rather than speed.

Fix: record `pressedAt[i]` in `check_buttons()` and select the minimum.

### 5. `setup()` configures the wrong pins and skips the start button

`rsb5001.ino:104-108`

```c
for (int i=0; i<(NUMBUTTONS-1); i++) {
    pinMode(i, INPUT);          // <-- pin i, not buttons[i]
    lastButtonState[i]=LOW;
    buttonIsPressed[i]=false;
}
```

`pinMode(i, INPUT)` configures pins 0, 1, 2, 3 — pins 0/1 are the hardware serial RX/TX opened at `:102`, and pins 2/3 are the LCD's `d7`/`d6`. Both are masked by luck (the USART overrides port direction; `lcd.begin()` at `:117` re-initializes the LCD pins), and the real button pins are configured separately at `:138-142`, which is why the game runs at all.

Second bug: `i < NUMBUTTONS-1` covers 4 of 5 entries, leaving the start button's array slots uninitialized — harmless only because globals are zero-initialized.

Fix: `pinMode(buttons[i], INPUT)` over the full range.

### 6. Pressing start mid-round skips the results screen

`rsb5001.ino:210`, `:495`

`action()` runs inside `waitForShot()`, so a start press during Ready/Steady/Bang sets `startButtonPressed = 1`. The flag is still set when the round ends, so `waitOnStartButton()` returns immediately and `resetGame()` wipes the display — winner and reaction time flash past in one loop iteration.

Fix: clear `startButtonPressed` when entering the results wait, or ignore the start button while `gameStarted == 1`.

---

## MEDIUM

| # | Issue | Where |
|---|-------|-------|
| 7 | `#define NUMBUTTONS sizeof(buttons)` is correct only because `buttons[]` is `byte`. Changing the type to `int` silently doubles every loop bound and reads garbage. Use `sizeof(buttons)/sizeof(buttons[0])`. | `:88` |
| 8 | Reaction time is measured at loop exit, not at the press, so it includes up to 50 ms of debounce plus a loop iteration — systematically inflated. `bangEnd` was declared for this purpose and never used. | `:311`, `:60` |
| 9 | `String` concatenation on an 8 KB AVR builds several heap temporaries per call; over a long session this fragments the heap and can hang the board mid-game. Use sequential `lcd.print()` calls or `snprintf` into a `char buf[17]`. | `:317`, `:342` |
| 10 | `while (millis() < shootDelayWait)` is the rollover-unsafe comparison. Correct idiom: `while (millis() - start < duration)`. Bites once per 49.7 days of uptime. | `:354` |
| 11 | Buttons are `INPUT` with HIGH = pressed, so all five need external pull-down resistors. A missing resistor or popped wire leaves the input floating and firing randomly. `INPUT_PULLUP` with inverted logic removes both the parts and the failure mode. | `:138-142` |
| 12 | Scores accumulate with no reset path short of a power cycle — `resetGame()` deliberately doesn't touch `p1Score`–`p4Score`. Possibly intentional; there is no way to zero them. | `:64-67`, `:468` |
| 13 | Dead state: `steadyStart`, `bangEnd`, `startWait`, `gameEnded` are declared and never read. `deadPlayer` is assigned on every early shot but never displayed — a player who jumps the gun gets a red LED and no explanation. | `:58-61`, `:78` |

---

## LOW

- `timeMin = 100` (`:50`) — a 100 ms Steady→Bang gap is below human reaction time, making those rounds a coin flip. 800–1000 ms is a more playable floor.
- `int long ranDelay` (`:56`) — legal but backwards; `long` reads better.
- Magic numbers: tone frequencies `3520` / `4699` / `1760`, contrast pin `10` and value `75`, the `1000` ms ready delay.
- `lcd.begin()` called twice in `setup()` (`:117`, `:145`), each followed by `clear()`.
- Comment typo at `:113`: "instead of pent" → "pot" (potentiometer).

---

## Refactoring options, highest value first

**A. Collapse the four players into arrays.** The single biggest win: roughly 200 of 500 lines are the same block copy-pasted four times with the digit changed — `waitForShot()` (`:353-414`), `killPlayer()`, `showAlive()`, the scoring block, and the LED-off wall in `startGame()`.

```c
struct Player { byte button, blueLed, redLed; bool alive; byte score; bool pressed; };
Player players[4] = { {22,23,24}, {25,26,27}, {28,29,30}, {31,32,33} };
```

`showAlive()` becomes a four-line loop. Note the historical precedent: the pre-repo `RSB5000.ino` had a copy-paste bug where the player-4 branch tested `p2ButtonPressed`/`p2alive` — fixed in `rsb5001.ino`, and structurally impossible once the players are an array.

**B. Convert to a non-blocking state machine.** `startGame()` runs a whole round top-to-bottom with blocking `waitForShot()` calls, which is why `loop()` is dead during a round and why start-button handling leaks across states (#6). An `enum GameState { IDLE, READY, STEADY, BANG, RESULTS }` with a `switch` in `loop()` and one timestamp per transition fixes #1 and #6 and makes the single-player mode sketched in the 2023 file straightforward.

**C. Per-button debounce with press timestamps.** Fixes #3, #4, and #8 together — the change that actually makes the game fair.

**D. Extract two helpers:** `lcdShow(const char* l1, const char* l2)` and `setAllPlayerLeds(bool on)`. Removes the repeated clear/setCursor/print triplets and the eight-line `digitalWrite` walls.

**E. Name the audio constants.** Relevant when merging the 2023 work — `myDFPlayer.playMp3Folder(5)` should read `playMp3Folder(TRACK_BANG)`.

---

## Suggested order of work

1. Resolve the two-`.ino` conflict so the sketch compiles (see Repo state #1).
2. Fix #1 and #2 — a handful of lines each, and the two that most change how the game plays.
3. Fix #5 and #6 — small, self-contained.
4. Refactor A, then C — C is much easier once players are an array.
5. Refactor B if single-player mode is still wanted.

---

## Notes on the untracked 2023 sketch

Not reviewed in depth, but two things spotted while comparing versions:

- **Single-player mode is advertised but not implemented.** The LCD offers "Press clicker for singleplayer", and `loop()` has `if (gameMode == 1) { //startSingleGame(); }` — the call is commented out and `startSingleGame()` was never written. Selecting it hangs at the wait loop.
- **Reaction timing is measured against the wrong event.** `bangStart = millis()` is recorded right after the `playMp3Folder(5)` command is queued over SoftwareSerial, not when players actually hear "Bang!" — the DFPlayer's own latency is typically 100–500 ms. The LCD updates before the sound plays, so visual players get an advantage. On a Mega, using a spare hardware UART (`Serial1`) instead of SoftwareSerial would also stop the interrupt-disabled TX bursts from perturbing `millis()`.
