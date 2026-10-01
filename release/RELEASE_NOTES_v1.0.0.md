# Raging Thunder 2 Vita — v1.0.0

First public release: the full game (menus + races) playable on PS Vita.
Tested on real hardware (firmware with HENkaku/Enso, kubridge + libshacccg).

## What's included

- `ragingthunder2.vpk` — install with VitaShell (Title ID `PSVRT0002`).
- `LEEME.txt` — quick install guide in Spanish.

## Installation

1. Make sure `kubridge.skprx` is loaded (`*KERNEL` in taiHEN config) and
   `ur0:data/libshacccg.suprx` exists (ShaRKBR33D).
2. Install the VPK.
3. From your own `Raging Thunder 2 V1.0.16.apk` copy:
   - `lib/armeabi/librthunder2lite.so` → `ux0:data/ragingthunder2/main.so`
   - `assets/Data.vfs` → `ux0:data/ragingthunder2/assets/Data.vfs`
   - `assets/moregames/` → `ux0:data/ragingthunder2/assets/moregames/`
4. Launch the game. Saves, settings (`config.txt`) and logs are created
   automatically under `ux0:data/ragingthunder2/`.

> The APK and its `.so`/assets are **not** distributed here — use your own copy.

## Highlights

- Native loader for the Polarbit Fuse engine (GLES 1.1 via vitaGL, MSAA up to 4x).
- Controls: Cross/R accelerate, Square/L brake, Triangle nitro, Circle/Start back/pause,
  D-Pad menus, left stick steering (tilt emulation) or real motion sensor (`steering 1`),
  touch screen for menus/HUD.
- Engine audio mixer streamed through `SceAudioOut` (22050 Hz stereo).
- Fix included for the VBO name handling that crashed the menu background on first boot,
  and corrected car-paint environment reflections (delete old cached shaders if colors
  look wrong — they regenerate automatically).

## Known limitations

- News/multiplayer features have no connectivity (raw sockets, fail gracefully).
- Progress saving and IME name entry are implemented but lightly tested — please report.

## Reporting bugs

Attach `ux0:data/ragingthunder2/logs/ragingthunder2_NNN.log` (set `engine_log 1`
in `config.txt` for a verbose log) to your issue at
<https://github.com/MetalSyntax/Raging-Thunder-2-vita/issues>.
