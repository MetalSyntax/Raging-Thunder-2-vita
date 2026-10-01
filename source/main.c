/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  main.c
 * @brief Drives librthunder2lite.so the way the Polarbit Fuse Java layer does.
 *
 * Android order (decompiled/apk_jadx/sources/com/polarbit/fuse/Fuse.java,
 * MainTask.java), reproduced on one thread:
 *   Fuse.<clinit>          System.loadLibrary("rthunder2lite") -> JNI_OnLoad
 *   new MainTask()         FuseTouch.getInstance(): OnEvent(1,1,0,0,0x10000)
 *                          new FuseSensor():        OnEvent(4,0,0,0,0)
 *                          FuseOnInit(activity, mainTask, sensor, utils,
 *                                     audio, egl)   -> JniTable lookups
 *   surfaceChanged         OnEvent(3,0,w,h,0)
 *   RenderThread, 1st run  OnCreate(apkPath, filesDir + "/")
 *                          OnEvent(1,4,keyboardVisible,0,0)
 *   RenderThread loop      OnEvent(0,1,0,0,0) until it returns 0
 *   onDestroy              OnEvent(0,0,0,0,0), OnDestroy()
 *
 * Before OnCreate there is no PEventQueue: every OnEvent is dropped except
 * OnEvent(3,0,w,h), whose size is stashed and handed to the
 * PAndroidSystemManager OnCreate builds (Java_com_polarbit_fuse_Jni_OnEvent /
 * _OnCreate). The first two events are still sent, as on Android.
 *
 * One OnEvent(0,1) is a full engine frame: FlushEvents() (input), then
 * PAndroidSystemManager::Run() (logic + render), which ends in
 * PAndroidDisplay::Update() -> FuseEgl.EglUpdateDisplay() -> vglSwapBuffers.
 *
 * Files: OnCreate opens the APK path with the engine's own zip reader
 * (PZipVFS) and uses filesDir as FUSEAPP_SAVEPATH. PFile::Open() looks in
 * FUSEAPP_SAVEPATH first, then the APK, then Data.vfs. No APK is needed: the
 * path below does not exist (PZipVFS::OpenZip just ends up empty), and
 * reimpl/io.c serves read-only misses in saves/ from assets/ (Data.vfs).
 */

#include "utils/init.h"
#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/settings.h"

#include "input.h"
#include "java.h"
#include "reimpl/audio.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

// The engine ran on a Java thread (RenderThread); give it room.
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

so_module so_mod;

#define SCREEN_W 960
#define SCREEN_H 544

#define APK_PATH   "/data/app/com.polarbit.rthunder2lite-1.apk"
#define FILES_PATH DATA_PATH "saves/"

// Placeholder Java objects. The engine stores them (JNIManager::Set*Object)
// and only uses them as Call*Method receivers; java.c never dereferences them.
static int activity_placeholder, maintask_placeholder, sensor_placeholder,
           utils_placeholder, audio_placeholder, egl_placeholder;

/* --- engine entry points ---------------------------------------------------- */

typedef int (*OnEvent_fn)(JNIEnv *, jclass, jint, jint, jint, jint, jint);
typedef int (*OnEventMessage_fn)(JNIEnv *, jclass, jint, jint, jint, jint, jstring);
typedef int (*OnEventMessage2_fn)(JNIEnv *, jclass, jint, jint, jint, jstring, jstring);
typedef int (*OnCreate_fn)(JNIEnv *, jclass, jstring, jstring);
typedef int (*OnDestroy_fn)(JNIEnv *, jclass);
typedef void (*FuseOnInit_fn)(JNIEnv *, jclass, jobject, jobject, jobject,
                              jobject, jobject, jobject);

static OnEvent_fn OnEvent;
static OnEventMessage_fn OnEventMessage;
static OnEventMessage2_fn OnEventMessage2;

static void *sym(const char *name) {
    void *p = (void *) so_symbol(&so_mod, name);
    if (!p)
        fatal_error("Error: %s not found in librthunder2lite.so", name);
    return p;
}

/* --- deferred Java -> native callbacks (java.h) ----------------------------- */

#define QUEUE_SIZE 32

typedef struct {
    int kind; // 0 OnEvent, 1 OnEventMessage, 2 OnEventMessage2
    int type, sub, a, b, c;
    char *msg1, *msg2;
} queued_call;

static queued_call queue[QUEUE_SIZE];
static int queue_len = 0;

static void enqueue(queued_call q) {
    if (queue_len >= QUEUE_SIZE) {
        l_error("java event queue full, dropping event %d/%d", q.type, q.sub);
        free(q.msg1);
        free(q.msg2);
        return;
    }
    queue[queue_len++] = q;
}

void fuse_queue_event(int type, int sub, int a, int b, int c) {
    enqueue((queued_call) { 0, type, sub, a, b, c, NULL, NULL });
}

void fuse_queue_message(int type, int sub, int a, int b, const char *msg) {
    enqueue((queued_call) { 1, type, sub, a, b, 0, strdup(msg ? msg : ""), NULL });
}

void fuse_queue_message2(int type, int sub, int a, const char *msg1, const char *msg2) {
    enqueue((queued_call) { 2, type, sub, a, 0, 0, strdup(msg1 ? msg1 : ""),
                            strdup(msg2 ? msg2 : "") });
}

void fuse_dispatch_queued(void) {
    // Calls may queue more calls; take a snapshot first.
    int n = queue_len;
    queued_call batch[QUEUE_SIZE];
    memcpy(batch, queue, n * sizeof(queued_call));
    queue_len = 0;

    for (int i = 0; i < n; i++) {
        queued_call *q = &batch[i];
        switch (q->kind) {
            case 0:
                OnEvent(&jni, NULL, q->type, q->sub, q->a, q->b, q->c);
                break;
            case 1:
                OnEventMessage(&jni, NULL, q->type, q->sub, q->a, q->b,
                               jni->NewStringUTF(&jni, q->msg1));
                break;
            case 2:
                OnEventMessage2(&jni, NULL, q->type, q->sub, q->a,
                                jni->NewStringUTF(&jni, q->msg1),
                                jni->NewStringUTF(&jni, q->msg2));
                break;
        }
        free(q->msg1);
        free(q->msg2);
    }
}

/* --- FalsoJNI fix-ups ------------------------------------------------------- */

// FalsoJNI has no java.nio: the "direct ByteBuffer" handed to Jni.AudioMix
// (reimpl/audio.c) is raw memory, so the object is its own address.
static void *GetDirectBufferAddress_soloader(JNIEnv *env, jobject buf) {
    return (void *) buf;
}

extern struct JNINativeInterface *_jni; // FalsoJNI.c

/* --- main ------------------------------------------------------------------- */

int main() {
    soloader_init_all();

    _jni->GetDirectBufferAddress = (void *) GetDirectBufferAddress_soloader;

    int (*JNI_OnLoad)(void *jvm) = sym("JNI_OnLoad");
    FuseOnInit_fn FuseOnInit = sym("Java_com_polarbit_fuse_MainTask_FuseOnInit");
    OnCreate_fn OnCreate = sym("Java_com_polarbit_fuse_Jni_OnCreate");
    OnDestroy_fn OnDestroy = sym("Java_com_polarbit_fuse_Jni_OnDestroy");
    OnEvent = sym("Java_com_polarbit_fuse_Jni_OnEvent");
    OnEventMessage = sym("Java_com_polarbit_fuse_Jni_OnEventMessage");
    OnEventMessage2 = sym("Java_com_polarbit_fuse_Jni_OnEventMessage2");

    l_info("JNI_OnLoad -> 0x%x", JNI_OnLoad(&jvm));

    // vitaGL must be up before OnCreate: CApplication::Init() creates the
    // display (FuseEgl.EglCreate) and loads textures from inside OnCreate.
    gl_init();
    l_success("vitaGL initialized (%dx%d).", SCREEN_W, SCREEN_H);

    // new MainTask(): field initializers, then the constructor.
    OnEvent(&jni, NULL, 1, 1, 0, 0, 0x10000);
    OnEvent(&jni, NULL, 4, 0, 0, 0, 0);
    FuseOnInit(&jni, NULL,
               (jobject) &activity_placeholder, (jobject) &maintask_placeholder,
               (jobject) &sensor_placeholder, (jobject) &utils_placeholder,
               (jobject) &audio_placeholder, (jobject) &egl_placeholder);
    l_success("FuseOnInit done.");

    // surfaceChanged(): the size OnCreate gives PAndroidSystemManager.
    OnEvent(&jni, NULL, 3, 0, SCREEN_W, SCREEN_H, 0);

    l_info("OnCreate(%s, %s)", APK_PATH, FILES_PATH);
    if (!OnCreate(&jni, NULL, jni->NewStringUTF(&jni, APK_PATH),
                  jni->NewStringUTF(&jni, FILES_PATH))) {
        l_fatal("OnCreate failed");
        fatal_error("Error: the game failed to start (OnCreate). "
                    "See %slogs/ for details.", DATA_PATH);
    }
    l_success("OnCreate returned.");

    // Xperia Play: hardKeyboardHidden != HARDKEYBOARDHIDDEN_YES means the
    // gamepad slider is open -> OnEvent(1, 4, 1, 0, 0) ("keypad visible",
    // PEVENT_DEVICE 45). With it the game expects physical controls.
    OnEvent(&jni, NULL, 1, 4, setting_xperiaPad ? 1 : 0, 0, 0);

    input_init((fuse_on_event_fn) OnEvent);

    l_info("Entering main loop.");
    log_set_buffered(1);

    uint64_t fps_t0 = sceKernelGetProcessTimeWide();
    unsigned frames = 0;

    while (1) {
        if (java_dialog_active()) {
            java_dialog_update();
        } else {
            input_update();
        }
        fuse_dispatch_queued();

        if (OnEvent(&jni, NULL, 0, 1, 0, 0, 0) == 0) {
            l_info("Engine requested exit.");
            break;
        }

        frames++;
        uint64_t now = sceKernelGetProcessTimeWide();
        if (now - fps_t0 >= 5000000) {
            if (setting_showFps)
                l_info("fps: %.1f", frames * 1000000.0 / (double) (now - fps_t0));
            log_flush();
            fps_t0 = now;
            frames = 0;
        }
    }

    // MainTask.onLeave()
    OnEvent(&jni, NULL, 0, 0, 0, 0, 0);
    OnDestroy(&jni, NULL);
    audio_shutdown();
    log_flush();

    sceKernelExitProcess(0);
    return 0;
}
