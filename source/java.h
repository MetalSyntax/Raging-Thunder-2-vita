/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  java.h
 * @brief Glue between the FalsoJNI "Java" side (java.c) and the main loop.
 *
 * On Android the Java callbacks that answer the engine (InputDialog result,
 * billing, DRM) arrive later, on the UI thread, never from inside the native
 * call that asked for them. java.c runs *inside* that native call, so it only
 * queues the Jni.OnEvent / OnEventMessage / OnEventMessage2 call; main.c
 * delivers the queue between two engine frames (fuse_dispatch_queued()).
 */

#ifndef SOLOADER_JAVA_H
#define SOLOADER_JAVA_H

#ifdef __cplusplus
extern "C" {
#endif

/** Queue Jni.OnEvent(type, sub, a, b, c). */
void fuse_queue_event(int type, int sub, int a, int b, int c);

/** Queue Jni.OnEventMessage(type, sub, a, b, msg). */
void fuse_queue_message(int type, int sub, int a, int b, const char *msg);

/** Queue Jni.OnEventMessage2(type, sub, a, msg1, msg2). */
void fuse_queue_message2(int type, int sub, int a, const char *msg1, const char *msg2);

/** Deliver every queued call to the engine (main.c, between frames). */
void fuse_dispatch_queued(void);

/** 1 while the IME dialog opened by InputDialog.Show() is on screen. */
int java_dialog_active(void);

/** Poll the IME dialog; reports the result to the engine when it closes. */
void java_dialog_update(void);

/** FuseSensor.ActivateAccelerometer() state (read by input.c). */
extern volatile int accelerometer_enabled;

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_JAVA_H
