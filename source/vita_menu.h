/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  vita_menu.h
 * @brief "PS Vita controls" port menu (button remapping, steering, car tint).
 *
 * START + SELECT opens it. The game is frozen while it is open: main() stops
 * running engine frames, the clock the engine reads is held (see
 * vita_menu_paused_us()) and the audio is muted.
 */

#ifndef SOLOADER_VITA_MENU_H
#define SOLOADER_VITA_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  vita_menu_active(void);
/** True once the background frame has been captured: from then on main()
 *  only draws the menu (no engine frames). */
int  vita_menu_frozen(void);
void vita_menu_open(void);
/** Buttons of this frame (held / newly pressed), from input_update(). */
void vita_menu_update(uint32_t held, uint32_t pressed);
/** Draw the menu on top of the frozen frame and present it. */
void vita_menu_render(void);
/** Called right before every swap: grabs the background frame once. */
void vita_menu_on_swap(void);
/** Total time the menu has been open (us), subtracted from the engine clock. */
uint64_t vita_menu_paused_us(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_VITA_MENU_H
