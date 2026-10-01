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

### Prueba 2 — log 002 (2026-10-01): FUNCIONA
Arranca, menús y carreras jugables. `ActivateAccelerometer(1)` ya sale limpio. Sin crashes.
Commit inicial `885e2ba`. Reporte del usuario: **colores de los autos con el tono cambiado**.
- Análisis: la pintura de los autos (`bite::CShaderCarPaint::Begin`) = textura base (unidad 0)
  iluminada con material = color de pintura, + reflejo de entorno en la unidad 1 con
  `GL_ADD` (`SetTextureCombiner` modo 3), cuyas UV son **las normales** (3 componentes,
  `CVertexBuffer::ApplyComponent(1, 4)` → `glTexCoordPointer(3, ...)`) transformadas por
  una matriz de textura 3D (normal → espacio de cámara, ×0.5 + 0.5).
- vitaGL declaraba las UV del shader FFP como `float2` (`float4(tc, 0, 1)`): se perdía la z
  de la normal → el reflejo muestreaba la zona equivocada del mapa de entorno y teñía la
  carrocería. Las pistas no usan esa pasada, por eso solo fallaban los autos.
- Descartado con evidencia: texturas DXT/paletizadas (conversión correcta en vitaGL),
  `glMaterialxv`/`glLightxv` (correctos), colores de vértice (`GL_UNSIGNED_BYTE`, soportado),
  softfloat (el backend ES11 no usa floats en esa ruta).
- **Fix**: `vendor/vitaGL/source/shaders/ffp_v.h`: UV `float3` + `float4(tc, 1)` (unidades
  0 y 1). La caché de shaders en disco usa hash del fuente → se regenera sola.
- [x] Confirmado en consola (log 014): pintura y reflejos correctos.

### Prueba 3 — logs 007..014 (2026-10-01): asfalto negro y crash al salir — RESUELTOS
- **Asfalto/texturas en negro** (y autos teñidos por ello): algunas texturas DXT
  quedaban enteras en cero en la GPU aunque `glCompressedTexImage2D` recibía datos
  válidos (misma fuente: tex 63 en cero, tex 64 bien). vitaGL swizzlea el nivel 0
  con `sceGxmTransferCopy` asíncrono desde el pool temporal por frame; durante la
  carga de pista (muchas texturas sin frames) llegaban tarde o pisadas, y el
  `vgl_realloc` del nivel 1 podía mover el buffer con la copia en vuelo.
  **Fix** en `vendor/vitaGL/source/utils/gpu_utils.c`: `sceGxmTransferFinish()`
  antes de reasignar/liberar y swizzle de comprimidas en CPU
  (`VGL_ASYNC_COMPRESSED_UPLOAD 0`). Aplica a todas las texturas (pista y autos).
- **Crash/congelamiento al salir con CIRCLE** (dumps 0x0004fe239f, 0x0000852939):
  `OnDestroy` → `JNIManager::JniCloseAll()` hace `DeleteGlobalRef` de los objetos
  estáticos de `FuseOnInit` y de la JniTable; FalsoJNI les hace `free()` →
  `_free_r`. **Fix** en `source/main.c`: `DeleteGlobalRef` propio (ignora los
  placeholders; no-op durante `OnDestroy`), audio detenido antes de `OnDestroy`
  (`reimpl/audio.c`: espera con timeout de 1 s). `OnDestroy` tarda ~7 s (el motor
  libera recursos), luego sale limpio.
- Nota: el motor tiñe cada auto con el color de colisión del suelo bajo las ruedas
  (`CCarActor::Track` → `CCollision::Find`, datos crudos de la pista): en pistas de
  atardecer los autos se ven azulados; la ruta de cálculo es del propio motor (sin
  vitaGL ni loader de por medio). En consola (log 015) los autos salían azules o
  negros según el tramo, y el tinte no coincide con los colores de vértice de la
  misma pista (pista cálida R>B, tinte azul B≈2×R). **Opción `car_ground_tint`**
  (`source/patch.c`, parchea 3 `ldr` en `CCarActor::Render` 0x133168/88/a4):
  0 = sin tinte, 1 = original (por defecto), 2 = R/B invertidos.
- **Autos verdes/rojos/azules/negros** (log 016, con el tinte ya desactivado):
  bug de vitaGL en `ffp.c`: el color ambiente del material (glMaterial, sin
  color array) se leía del VBO que había dejado el `glColorPointer` de la pista
  → 4 floats basura; con luz ambiente 1.0 dominaba el color. **Fix**: los
  atributos de material constantes no se leen de un VBO viejo (3 sitios).
  - [x] Confirmado en consola: colores reales de los autos.

### v1.1.0 (2026-10-01): remapeo de controles
- Menú "PS Vita controls" con START + SELECT (`source/vita_menu.c`, dibujo en
  `source/overlay.c` con la fuente 8x8 del PSPSDK porque el motor no expone texto):
  acciones remapeables, dirección stick/acelerómetro, sensibilidad, invertir, tinte.
  Juego congelado mientras está abierto: no se corren frames del motor,
  `gettimeofday` descuenta el tiempo de pausa (`dynlib.c`) y el audio se silencia.
  Al cerrar se restaura todo el estado GL tocado (P3DStateMan cachea estados).
- `controls.txt` (formato de los ports Carnivores). START = BACK al soltar.

- [x] Arranque: llega a "OnCreate returned." y "Entering main loop." (log 001) (si no: dump +
      `psvita-toolkit analyze`). Poner `engine_log 1` para ver el log del motor.
- [x] Imagen: menú visible y orientación correcta (log 002) (si pantalla negra: revisar flags vitaGL).
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
