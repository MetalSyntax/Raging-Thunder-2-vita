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

#include <stddef.h>
#include <stdint.h>

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

/** Reload the button bindings from controls.txt (written with the defaults
 *  if missing). */
void input_reload_controls(void);
/** Apply settings changed in the port menu (steering mode). */
void input_apply_settings(void);

/* --- bindings, used by the port menu (vita_menu.c) ---------------------- */

int         input_action_count(void);
const char *input_action_label(int action);
uint32_t    input_action_buttons(int action);
/** Bind `button` (one bit) to `action`, removing it from every other action.
 *  `add` keeps the buttons the action already had. */
void        input_action_bind(int action, uint32_t button, int add);
void        input_action_clear(int action);
void        input_controls_defaults(void);
void        input_controls_save(void);
/** Buttons that can be bound (everything but START). */
uint32_t    input_bindable_buttons(void);
/** "R, Cross" style list of the buttons in `mask` ("-" if none). */
void        input_buttons_text(uint32_t mask, char *out, size_t size);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_INPUT_H
