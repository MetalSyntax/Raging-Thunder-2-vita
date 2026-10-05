# Raging Thunder 2 Vita — v1.1.1

Bug-fix release: saves load again and the Start + Select controls menu shows up.

## What's included

- `ragingthunder2.vpk` — install with VitaShell over v1.1.0 or v1.0.0 (Title ID
  `PSVRT0002`, saves and settings are kept).
- `LEEME.txt` — quick guide in Spanish.

## Fixed

- **"Failed to load profile, file is either corrupt or wrong version"** after
  finishing a cup or playing an Instant Race and restarting the game. The save
  file was fine but the port reported its size wrongly, so the game could never
  read it back. Career progress is now kept between sessions.
- **Start + Select did not open the PS Vita controls menu** (the game paused or
  kept going, but no menu appeared). The menu now opens over the paused game in
  menus and in races, so controls can be remapped without editing `controls.txt`.

## Notes

- Progress saved with v1.1.0 or v1.0.0 could not be loaded, and the game started a
  fresh profile when that happened, so earlier Career progress may already be lost.
  Everything saved from v1.1.1 on is kept.
- Installation from scratch is the same as v1.0.0 (see `README.md` / `LEEME.txt`).

## Reporting bugs

Attach `ux0:data/ragingthunder2/logs/ragingthunder2_NNN.log` to your issue at
<https://github.com/MetalSyntax/Raging-Thunder-2-vita/issues>.
