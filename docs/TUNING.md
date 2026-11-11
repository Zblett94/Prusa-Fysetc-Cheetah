# Tuning the input shaper and the pressure advance from the LCD

Both STM32 builds (SKR Mini E3 V3, Cheetah V3) have **Settings > Input shaper, PA**:

| Item | Meaning |
|---|---|
| X shaper / Y shaper | Type: ZV, ZVD, MZV (default), EI, EI2, EI3 (2 and 3 hump EI) or Off. A click steps to the next one |
| X freq. / Y freq. | Shaper frequency, 10 to 100 Hz in 0.1 Hz steps (turn fast for 1 Hz steps) |
| Press.adv | Pressure advance in seconds, 0 = off, in 0.001 steps (turn fast for 0.01) |
| Shaper defaults | Back to the MK3.5 values: MZV, X 50.7 Hz, Y 40.6 Hz |

Every change is stored at once and used from the next move on. The menu is not available
during a print. The defaults are those of the Original Prusa MK3.5, which has the same
mechanics: tune only if you see ringing (ripples after corners) or changed the printer
(other extruder, bed, belts).

The values come from two test prints. The slicer makes them: a line of G-code changes the
setting with the height, and you read the best height off the print. The lines below are
for PrusaSlicer (*Printer Settings > Custom G-code > Before layer change G-code*); remove
them again after the test.

## Input shaper frequency

1. Model: a square tube, 60 x 60 mm, 60 mm high, one or two perimeters, no infill, no
   top layers (or a "ringing tower" model). Sharp corners matter, the rest does not.
2. Print fast, or there is nothing to see: outer perimeter 100 mm/s or more, and the
   highest acceleration you use.
3. Before layer change G-code:

       M593 F{20 + layer_z}

   The frequency of both axes rises with the height: 20 Hz at the bottom, 80 Hz at 60 mm.
4. Look at the ripples after the corners in grazing light. A wall shows the vibration of
   the axis that moves *across* it:
   - the **left and right walls** (running front to back) show the **X axis**,
   - the **front and back walls** (running left to right) show the **Y axis**.
5. On each pair of walls find the height where the ripples are weakest, measure it from
   the bed, and enter **20 + height in mm** as X freq. and Y freq.

**Switch the printer off and on after the test print, before you enter the values.** The
test leaves both axes at its last frequency (not stored); the menu stores the settings of
both axes together and would keep that frequency for the axis you did not change. If two heights look equally good, take the lower frequency. If
ripples remain everywhere, try the type EI at the best frequency (smoother, slightly
rounder corners).

## Pressure advance

1. Model: the same square tube, 50 mm high, printed at your normal speeds.
2. Before layer change G-code:

       M572 S{layer_z * 0.002}

   The pressure advance rises with the height: 0 at the bottom, 0.1 at 50 mm.
3. Look at the corners. Too little: bulging corners. Too much: rounded corners with gaps
   before them. Find the height where the corners are sharp and the walls even.
4. Enter **height in mm x 0.002** as Press.adv (a geared BMG extruder is typically
   between 0.02 and 0.06 with PLA).

The value of the LCD is the default after power-on. A slicer profile can still set its
own value for a print: the MK3S filament profiles of PrusaSlicer contain a line
`M900 K...` in the filament start G-code, which then replaces the LCD value until the
next start. Delete that line (or put your value there) to print with the tuned one.
Different materials want different values (PETG and flexibles more than PLA): per
material values belong in the filament start G-code as `M572 S<value>`.

## The same from G-code

`M593 X F51.5`, `M593 Y T3 F38` (type 3 = EI), `M593 W` (store), `M593` (report);
`M572 S0.045` (pressure advance), `M572` (report). See the comments of `gcode_M593()` and
`gcode_M572()` in `Firmware/Marlin_main.cpp`.
