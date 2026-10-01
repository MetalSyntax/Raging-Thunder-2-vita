# Raging Thunder 2 Vita — v1.1.0

Remappable controls and the graphics/exit fixes found on real hardware.

## What's included

- `ragingthunder2.vpk` — install with VitaShell over v1.0.0 (Title ID `PSVRT0002`,
  saves and settings are kept).
- `LEEME.txt` — quick guide in Spanish.

## New

- **PS Vita controls menu — Start + Select** (menus or race; the game pauses):
  - ✕ replace an action's button, ■ add one, ▲ clear it.
  - Steering: left stick or the Vita's motion sensor, sensitivity, invert tilt.
  - Car ground tint: off / original / R-B swapped.
  - ● or Start saves and closes.
- **`ux0:data/ragingthunder2/controls.txt`**: the same bindings as text
  (`ACCELERATE = CROSS`, `BRAKE = SQUARE`, ...). Created on first boot.

## Fixed

- Cars showed the wrong colors (green / red / blue / black) — vitaGL was reading the
  material color from a stale vertex buffer.
- Black asphalt and other missing textures at the start of a track (DXT upload race
  in vitaGL).
- Quitting with Circle from the main menu crashed and could freeze the console.
  It now exits cleanly (it takes a few seconds while the engine frees its data).

## Notes

- Cars get a tint from the ground under them, as in the original game (darker in
  tunnels, cooler on dusk tracks). Set *Car ground tint* to *Off* (or
  `car_ground_tint 0` in `config.txt`) for the plain paint colors.
- Installation from scratch is the same as v1.0.0 (see `README.md` / `LEEME.txt`).

## Reporting bugs

Attach `ux0:data/ragingthunder2/logs/ragingthunder2_NNN.log` to your issue at
<https://github.com/MetalSyntax/Raging-Thunder-2-vita/issues>.
