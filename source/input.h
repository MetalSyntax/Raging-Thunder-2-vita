/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  input.h
 * @brief Vita controls -> Polarbit Fuse input events (Jni.OnEvent).
 */

#ifndef SOLOADER_INPUT_H
#define SOLOADER_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*fuse_on_event_fn)(void *env, void *clazz, int type, int sub,
                                int a, int b, int c);

/** Set up sampling; on_event is Java_com_polarbit_fuse_Jni_OnEvent. */
void input_init(fuse_on_event_fn on_event);

/** Sample once and send the changes to the engine (call once per frame). */
void input_update(void);

/** Release everything that is held (before suspending / when the IME opens). */
void input_release_all(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_INPUT_H
