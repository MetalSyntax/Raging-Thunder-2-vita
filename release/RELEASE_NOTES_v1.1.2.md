# Raging Thunder 2 Vita — v1.1.2

Bug-fix release: Time Attack no longer freezes the game.

## What's included

- `ragingthunder2.vpk` — install with VitaShell over any earlier version (Title ID
  `PSVRT0002`, saves and settings are kept).
- `LEEME.txt` — quick guide in Spanish.

## Fixed

- **The game froze after finishing a Time Attack race.** The game's network code
  expected its connections to never wait, but the port left them waiting forever.
  Time Attack results, ghosts and records now save and the summary screen shows up.
- **Saving the Time Attack ghost** is much faster (the port now batches the many
  tiny writes the game makes).
- **Tearing in the Start + Select controls menu** when moving through it.
- If the game ever hangs for 30 seconds, it now closes with an error report
  instead of leaving the console frozen.

## Known issues

- In-game text looks soft: the game draws its menus and HUD for a 480×320 screen
  and stretches them to the Vita's 960×544, the same as on Android phones.

## Reporting bugs

Attach `ux0:data/ragingthunder2/logs/ragingthunder2_NNN.log` (and any
`ux0:data/*.psp2dmp` crash dump) to your issue at
<https://github.com/MetalSyntax/Raging-Thunder-2-vita/issues>.
