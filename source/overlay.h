/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  overlay.h
 * @brief Minimal 2D drawing on top of the game (port menu): rectangles and
 *        8x8 bitmap text through vitaGL immediate mode, plus a frozen copy of
 *        the last game frame used as the menu background.
 *
 * The Polarbit engine caches GL state (fuseGL::P3DStateMan), so everything
 * overlay_begin() changes is restored by overlay_end().
 */

#ifndef SOLOADER_OVERLAY_H
#define SOLOADER_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OVL_W 960
#define OVL_H 544

/** Glyph cell size at scale 1 (pixels). */
#define OVL_GLYPH 8

/** Copy the frame about to be presented (call right before the swap). */
void overlay_capture_frame(void);
int  overlay_has_frame(void);

void overlay_begin(void);
void overlay_end(void);

/** Colours are 0xAARRGGBB. Coordinates: pixels, origin top-left. */
void  overlay_background(uint32_t dim);
void  overlay_rect(float x0, float y0, float x1, float y1, uint32_t color);
void  overlay_frame(float x0, float y0, float x1, float y1, float t, uint32_t color);
void  overlay_text(float x, float y, float scale, uint32_t color, const char *s);
float overlay_text_width(const char *s, float scale);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_OVERLAY_H
