# Reference material

Historical versions kept for reference. **None of these are built.** They use the
`.ino.txt` extension deliberately — the Arduino IDE concatenates every `.ino` in a
sketch folder into one translation unit, so a second `.ino` here would break the build
with duplicate definitions of `setup()`, `loop()` and ~30 globals.

| File | Date | What it is |
|---|---|---|
| `RSB500-2021-06-25-0944.ino.txt` | 25 Jun 2021 | First draft. Has a copy-paste bug where the player-4 branch tests `p2ButtonPressed`/`p2alive`. |
| `RSB5000-2021-06-25-1738.ino.txt` | 25 Jun 2021 | Second draft, later the same day. Adds score tallying. |
| `RSB5001-2023-dfplayer.ino.txt` | 21 Oct 2023 | The DFPlayer Mini + menu version. **This is the baseline the current sketch was rewritten from** — it matches the wiring in the physical box. |
| `Arduino-Wiring-Fritzing-Connections-with-16x2-Character-LCD.png` | 25 Jun 2021 | Stock LastMinuteEngineers illustration of an **Uno** with a 16x2 LCD. No LEDs, buttons or buzzer, and not the right board — it documents the LCD hookup and nothing else. |

The middle generation — the June 2021 buzzer version with no MP3 module — is not
duplicated here; it is the tracked `rsb5001.ino` as of commit `40d8f17`:

```bash
git show 40d8f17:rsb5001.ino
```

See `../CODE-REVIEW.md` for the review of that version.
