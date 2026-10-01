# Changelog

All notable changes to this port are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.1.0] — 2026-10-01

### Added

- **PS Vita controls menu** (Start + Select, in menus or in a race): remap every
  action, steering mode (left stick / tilt), steering sensitivity, invert tilt and
  car ground tint. The game is paused while it is open (frozen clock, muted audio).
- **`controls.txt`** (`ux0:data/ragingthunder2/`): button bindings, editable by hand
  (`ACTION = BUTTON, BUTTON`), created with the defaults on first boot.
  Start stays back/pause (sent on release so the Start + Select combo doesn't pause).
- `car_ground_tint` setting (0 off / 1 original / 2 R-B swapped).

### Fixed

- **Car colors** (cars green, red, blue or black instead of their paint): vitaGL read
  the constant material ambient (`glMaterial`, no color array) from the VBO left on
  that slot by the track's `glColorPointer` — garbage floats dominating the lit color.
- **Black road / textures**: some DXT textures stayed all zero on the GPU. The
  asynchronous `sceGxmTransferCopy` swizzle (from the per-frame temp pool) raced the
  level loading and the next mip level's `realloc`; compressed uploads are now
  swizzled on the CPU, with `sceGxmTransferFinish()` before reallocating.
- **Crash / console freeze when quitting** with Circle in the main menu: `OnDestroy`
  → `JNIManager::JniCloseAll()` freed static JNI placeholders and JniTable refs
  through FalsoJNI's `DeleteGlobalRef`. The audio pump is now stopped before
  `OnDestroy` (with a 1 s timeout). Quitting takes a few seconds but exits cleanly.

## [1.0.0] — 2026-10-01

First public release. Menus and races playable on real hardware.

### Added

- Native PS Vita loader for `librthunder2lite.so` (armeabi, Polarbit Fuse engine)
  via so-loader + FalsoJNI.
- Full JNI table (54 entries): lifecycle, EGL, system, audio, sensor, IME input
  dialog; stubs/no-ops for billing, ads, social and media.
- vitaGL rendering (vendored, `SOFTFP_ABI=1`), MSAA up to 4x at 960×544.
- Input: Xperia Play button mapping, stable multi-touch slots, left-stick tilt
  emulation and optional real accelerometer steering.
- Audio: engine mixer (`Jni.AudioMix`) streamed through `SceAudioOut`.
- Assets: APK ZIP + `Data.vfs` (PVFS) file access; saves/logs/config under
  `ux0:data/ragingthunder2/`.
- LiveArea (icon0/bg0/pic0/startup) and VPK packaging (`PSVRT0002`).

### Fixed

- VBO handling: the engine invents buffer names without `glGenBuffers`
  (legal in GLES 1.1); names are now remapped to real vitaGL buffers —
  fixes the boot crash in the menu background loader.
- Car-paint environment reflections: FFP shader UVs widened to `float3` so the
  normal-based reflection samples the right env-map region. Cached shaders
  regenerate automatically (hash-based cache).
- Real `getenv`/`setenv` (engine paths were resolving to `"(null)Data.vfs"`).
- `glLightf`/`glLightx` wrapped over `glLightfv`; `gai_strerror` stub for newlib;
  `ActivateAccelerometer` masked to the low byte (engine bug passed garbage).

### Known limitations

- News/multiplayer: no connectivity (raw sockets without `sceNetInit`).
- Save persistence, IME name entry and motion-sign conventions lightly tested.
