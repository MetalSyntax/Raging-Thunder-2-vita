# Plan de Port — Raging Thunder 2 (PS Vita)

> Generado por psvita-port-toolkit el 2026-09-30. Punto de partida con lo detectado automáticamente --
confirmar todo con objdump/Ghidra/jadx a mano antes de asumirlo como cierto.

## 0. Contexto

- **Juego:** Raging Thunder 2
- **Paquete Java:** com.polarbit.rthunder2lite (motor Polarbit Fuse: com.polarbit.fuse.*)
- **APK original:** `Raging Thunder 2 V1.0.16.apk`
- **TITLEID asignado:** `PSVRT0002`

**¿Motor conocido?** Revisar si algún port hermano (bajo la misma BASE_DIR) comparte motor antes de
reusar su código -- confirmar con símbolos JNI reales, no por analogía superficial.

## 1. Detección automática

- **ABI(s):** armeabi, x86
- **ABI elegida:** armeabi
- **Nota de arquitectura:** armeabi (ARMv5TE/ARMv6, soft-float, sin NEON) — corre en el Cortex-A9 de la Vita; ojo con la ABI de float (softfp) al llamar a funciones del .so. x86 descartado.
- **Versión de GLES:** GLES 1.1 CONFIRMADO (CApplication::Init, PDisplayProperties3D = 0xb). GLES 1.x (fixed-function) vía wrapper `GLES::`/`fuseGL::` con punteros de función resueltos en runtime (el .so no linkea libGLESv1_CM/libEGL; NEEDED = liblog, libdl, libc, libm). Hay también símbolos GLES2 (`glCreateShader`/`glShaderSource`) — confirmar cuál ruta usa en Fase 3.

## 2. .so encontrados (ABI armeabi)

- `ragingthunder2_extract/lib/armeabi/librthunder2lite.so` (2771 KB)
- `ragingthunder2_extract/lib/x86/librthunder2lite.so` (3397 KB)


## 3. Exports JNI (convención `Java_*`)

- `Java_com_polarbit_fuse_Jni_AudioMix`
- `Java_com_polarbit_fuse_Jni_FuseDecrypt`
- `Java_com_polarbit_fuse_Jni_FuseEncrypt`
- `Java_com_polarbit_fuse_Jni_Log`
- `Java_com_polarbit_fuse_Jni_OnCreate`
- `Java_com_polarbit_fuse_Jni_OnDestroy`
- `Java_com_polarbit_fuse_Jni_OnEvent`
- `Java_com_polarbit_fuse_Jni_OnEventMessage`
- `Java_com_polarbit_fuse_Jni_OnEventMessage2`
- `Java_com_polarbit_fuse_Jni_OnEventMessage3`
- `Java_com_polarbit_fuse_MainTask_FuseOnInit`
- `Java_com_polarbit_fuse_MainTask_processTouchpadAsPointer`


## 4. Checklist

- [x] Repo creado desde soloader-boilerplate, git init, .gitignore anti-DMCA.
- [x] APK decompilado (jadx) y .so decompilado(s) (Ghidra) -- ver sección 2/3.
- [x] Análisis del motor real (ciclo de vida nativo, reuso de otro port o boilerplate genérico).
- [x] Bootstrap del loader: so_file_load/so_relocate/so_resolve, primer build.
- [x] Tabla JNI (FalsoJNI): registrar exports + callbacks hacia "Java".
- [ ] Primer arranque en consola real.
- [x] Gráficos (wrappers GL según versión detectada).
- [x] Input, Audio, Assets, LiveArea/VPK.
- [ ] Pruebas en hardware real.

## 5. Herramientas

Este port se gestiona con **psvita-port-toolkit** (standalone, fuera de este repo). Desde el
toolkit: `Continuar con un port existente` → elegí esta carpeta (ya tiene `.psvita-toolkit.json`).

## 6. Estándar de logs en consola

El log del juego vive en `<DATA_PATH>logs/<slug>_NNN.log` (`<slug>` = ragingthunder2), incremental de
001 a 999 con `next.idx` (nunca `log_<timestamp>.txt`, nunca base 000, nunca ruta hardcodeada:
usar `DATA_PATH`). La subida FTP crea `logs/` y `saves/` en la consola aunque estén vacíos.
Comandos del toolkit (sin IA, deterministas): `psvita-toolkit log-standard --fix-dirs` (audita
y crea los dirs), `psvita-toolkit log-trace --ensure-flag` (inyecta trazas `[TRACE] >> f()` por
función bajo `#ifdef PORT_TRACE`; compilar con `-DPORT_TRACE=ON` para debug, `OFF` para
producción, o `psvita-toolkit log-trace --remove` para quitarlas del árbol).
