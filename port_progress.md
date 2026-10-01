# Registro de Progreso — Raging Thunder 2 (PS Vita)

## Fase 1: Configuración y Preparación (Completada — 2026-09-30)
- Repo creado desde soloader-boilerplate, `.gitignore` anti-DMCA.
- APK `Raging Thunder 2 V1.0.16.apk` copiado y extraído.
- ABI detectada: armeabi, x86 (elegida: armeabi).
- GLES detectado: GLES 1.x vía wrapper `GLES::`/`fuseGL::` con punteros resueltos en runtime
  (`dlopen`/`dlsym`); NEEDED = liblog, libdl, libc, libm.

## Fase 2: Decompilación (Completada — 2026-09-30)
- jadx: `decompiled/apk_jadx/`. Ghidra: `decompiled/librthunder2lite_armeabi/ghidra/`.

## Fase 3: Análisis del Motor Real (Completada — 2026-10-01)
Motor **Polarbit Fuse** (`com.polarbit.fuse.*`). Ningún port hermano bajo `PSVITA Develop/`
usa este motor (grep de "polarbit" sin resultados) → loader propio; solo se reutiliza
infraestructura genérica de los ports Carnivores (vitaGL vendorizada, softfloat, patrón de audio).

Confirmado en el .so real (objdump/nm + Ghidra) y el Java (jadx):
- **Sin RegisterNatives**: los 12 `Java_*` exportados + `JNI_OnLoad` (solo guarda el JavaVM).
- **Toda llamada nativo→Java** sale de una sola tabla `JniTable` (.data 0x2a1ea4, 54 entradas
  `{class, name, sig, jclass, jmethodID, isStatic}`), resuelta en `FuseOnInit` →
  `JNIManager::InitJni`. Volcada con Python desde el ELF: coincide 1:1 con `source/java.c`.
- **GLES 1.1 confirmado**: `CApplication::Init` pone `local_62 = 0xb` en `PDisplayProperties3D`
  → `PAndroidDisplay::Init` usa la rama GLES1 (`>= 0xc` sería GLES2) → `dlopen("libGLESv1_CM.so")`.
- **Ciclo de vida** (MainTask.java/RenderThread): `FuseOnInit(activity, mainTask, sensor,
  utils, audio, egl)` → `OnEvent(3,0,w,h)` (único evento que se guarda antes de OnCreate) →
  `OnCreate(apkPath, filesDir+"/")` → loop `OnEvent(0,1,0,0,0)` (= 1 frame completo:
  FlushEvents + Run + `FuseEgl.EglUpdateDisplay` = swap) hasta que devuelve 0.
- **Archivos** (`PFile::Open`): `FUSEAPP_SAVEPATH+name` → `/sdcard+...` → APK zip
  (`PZipVFS`) → `Data.vfs` (PVFS). El zip vacío (APK inexistente) es seguro: `OpenZip` falla
  limpio y `PZipVFS::Open` con 0 entradas devuelve "no encontrado".
- **Input**: `PEventQueue::OnEvent` (0x2894xx). Teclas = keycodes Android → tablas
  `m_keycodes`/`m_keymasks` de `PAndroidSystemManager::Init` + overrides Xperia Play de
  `CApplication::Init` (DPAD_CENTER 0x400, BUTTON_X 0x200, L1/R1 0x200/0x400, BACK 0x40...).
  Táctil `OnEvent(1,1,x,y,action|(id+1)<<16)`. Acelerómetro `OnEvent(4,0,x,y,z)` m/s²×6553.
- **Audio**: el motor mezcla (`PAudioPlayer`), Java solo bombea: `AudioCreate(rate,ch,bits,buf)`
  + `Jni.AudioMix(ByteBuffer, frames)`; `frames × settings+0xc` (= bytes/frame, verificado en
  `PAudioDeviceWaveOut::Open`).
- Sin DRM real (`Drm::ValidateRights` vacío). Sin `pthread_create` (motor de un hilo).
- Imports: los 61 resueltos por `default_dynlib` (verificado con `comm` contra `nm -D`);
  GL: 281 nombres pedidos, todos cubiertos (vitaGL o stub logueado).

## Fase 4: Bootstrap del loader (Completada — 2026-10-01, compila; sin probar en consola)
- `source/main.c`: reproduce MainTask/RenderThread en un hilo (stack 4 MB), cola de callbacks
  Java→nativo diferidos (`fuse_queue_*`, entregados entre frames), salida limpia
  (`OnEvent(0,0)` + `OnDestroy`). APK falso `/data/app/...apk`, filesDir `DATA_PATH saves/`.
- `GetDirectBufferAddress` sobreescrito en runtime (sin tocar el submódulo FalsoJNI): el
  "ByteBuffer" de AudioMix es memoria cruda.
- **Bug corregido**: `getenv`/`setenv` del boilerplate eran stubs → todas las rutas del motor
  quedaban `"(null)Data.vfs"`. Ahora usan el entorno real de newlib (`reimpl/sys.c`).
- `gai_strerror` no existe en newlib → stub propio (`dynlib.c`).
- `glLightf`/`glLightx` (antes `ret0`) → envueltas sobre `glLightfv` de vitaGL (atenuaciones).
- EGL: se usa `vendor/vitaGL/source/egl.c` (cubre los 19 `egl*` pedidos);
  `source/reimpl/egl.c` queda fuera del build (choque de símbolos). `VENDORED.md` corregido.
- `converter.c` de FalsoJNI agregado al build (faltaba `utf16_to_utf8`).

## Fase 5: JNI / Gráficos / Input / Audio / Assets / LiveArea (Completada — 2026-10-01)
- `source/java.c`: las 54 entradas de `JniTable` (EGL→vitaGL, FuseSystem, FuseAudio, sensor,
  InputDialog→IME de Vita, billing/ads/facebook/webview/media → no-op o "fallido").
- Gráficos: vitaGL vendorizada con `SOFTFP_ABI=1 NO_SPLASHSCREEN=1 NO_DEBUG=1
  HAVE_SHADER_CACHE=1` (la prebuilt del SDK sin softfp = pantalla negra, lección Carnivores).
  MSAA 4x por defecto (`msaa` en config.txt).
- `source/input.c`: botones → keycodes Xperia Play (Cruz/R = acelerar, Cuadrado/L = frenar,
  Triángulo, Círculo/Start = BACK), táctil con slots estables, stick izq. → d-pad (menús y
  modo "botones") o → acelerómetro emulado cuando el juego lo activa (modo inclinación);
  `steering 1` usa el acelerómetro real de la Vita.
- `source/reimpl/audio.c`: hilo SceAudioOut (granule 512) que llama `Jni.AudioMix`.
- `source/reimpl/softfloat.c`: helpers soft-float de libgcc → VFP (22 hooks).
- LiveArea: icon0/bg0/pic0/startup generados desde `res/drawable/icon.png` del APK
  (8-bit indexado, sin `._*`). `psvita-toolkit log-standard --fix-dirs` OK, `clean-junk` OK.
- Build: `~/rt2-src` (symlink sin espacios) → `~/rt2-build`; artefactos copiados a `build/`
  (`eboot.bin`, `ragingthunder2.vpk`, ELF `ragingthunder2` para símbolos de dumps).

## Instalación en la consola
1. Instalar `build/ragingthunder2.vpk` (VitaShell). Requiere `kubridge.skprx` y
   `ur0:data/libshacccg.suprx`.
2. Copiar a `ux0:data/ragingthunder2/` (staging listo en `ux0_data/ragingthunder2/`):
   - `main.so` (= `lib/armeabi/librthunder2lite.so` del APK)
   - `assets/Data.vfs` (y `assets/moregames/`) del APK
   - `logs/`, `saves/` (se crean solos). `res/` NO hace falta.
3. Ajustes en `ux0:data/ragingthunder2/config.txt` (se crea en el primer arranque):
   `language`, `steering` (0 stick / 1 motion), `steer_sensitivity`, `invert_steering`,
   `show_fps`, `msaa`, `engine_log`, `vfp_float`, `xperia_pad`.

## Fase 6: Pruebas en hardware real (EN CURSO)

### Prueba 1 — log 001 + `ragingthunder2-psp2core-1790833057-...psp2dmp` (2026-10-01)
Arranque completo hasta "Entering main loop." + audio 22050 Hz estéreo OK; crash casi en el menú.
- **Crash**: data abort en vitaGL `glNamedBufferData` (buffers.c:397) ← `P3DBackendES11::glBufferData`
  ← `bite::CVertexBuffer::BindStatic()` ← `CPolyMesh::Read` (cargando el fondo del menú).
  (Ojo: `psvita-toolkit analyze` autodetectó mal la base del .so, 0x9811e000; la real es
  0x98000000 → LR = .so+0x2114bc.)
- **Causa**: el motor no usa `glGenBuffers` para VBO/IBO: `bite::GenBufferID()` es un contador
  (1, 2, 3...) — legal en GLES1 — y vitaGL trata el nombre como puntero `vbo*` → lee la dir. 1.
  Además `glIsBuffer` era `ret0`, y `BindStatic` borra el VBO si da 0.
- **Fix**: `source/reimpl/gl_buffers.c`: tabla nombre-del-juego → nombre-vitaGL (se crea el
  buffer real en el primer `glBindBuffer`), `glGenBuffers`/`glDeleteBuffers`/`glIsBuffer`
  sobre la misma tabla. Texturas no afectadas (sí usan `glGenTextures`).
- Visto en el log: `FuseSensor.ActivateAccelerometer(-1742070108)`. Bug del propio motor:
  `JniSensorActivate` (0x204528) nunca pasa el bool (deja basura de r3). La JVM solo toma el
  byte bajo → siempre activado; java.c ahora hace `(v & 0xff) != 0`. Consecuencia: el
  acelerómetro está siempre "activo", así que el stick izquierdo siempre maneja la dirección
  por inclinación; en menús usar la cruceta.
- Warnings `ioctl(SIOCGIFCONF)`/`fcntl(F_GETFL/F_SETFL)` justo antes del crash: socket de
  News/red, no relacionados. Vigilar en la próxima prueba.

Probar un fallo a la vez; log en `ux0:data/ragingthunder2/logs/ragingthunder2_NNN.log`.
- [x] Arranque: llega a "OnCreate returned." y "Entering main loop." (log 001) (si no: dump +
      `psvita-toolkit analyze`). Poner `engine_log 1` para ver el log del motor.
- [ ] Imagen: menú visible y orientación correcta (si pantalla negra: revisar flags vitaGL).
- [ ] Texturas: formato elegido por el motor (PVRTC/ETC1/S3TC anunciados por vitaGL).
- [ ] Audio: log "audio: SceAudioOut port ... up"; sonido sin cortes ni ruido.
- [ ] Botones en menús (D-pad/Cruz/Círculo) y en carrera (acelerar/frenar/nitro).
- [ ] Táctil en menús.
- [ ] Dirección: modo inclinación con stick (signo → `invert_steering`), y `steering 1`
      con el acelerómetro real (convención de signos de SceMotion sin verificar).
- [ ] `xperia_pad 1` (por defecto): confirmar que el HUD no exige táctil; si molesta, probar 0.
- [ ] Guardado: partida/progreso persiste en `saves/` tras reiniciar.
- [ ] Teclado (InputDialog → IME) al ingresar nombre, si el juego lo pide.
- [ ] Red: News/PMultiplayer intentan sockets sin `sceNetInit`; confirmar que no cuelga.
- [ ] Salir desde el menú del juego cierra la app limpio.
- [ ] Rendimiento (`show_fps 1`) y MSAA 4x vs 2x.
