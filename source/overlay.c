/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  overlay.c
 * @brief 2D overlay for the port menu (see overlay.h).
 */

#include "overlay.h"

#include "utils/font8x8.h"
#include "utils/logger.h"

#include <vitaGL.h>

#include <stdlib.h>
#include <string.h>

#define FONT_TEX 128 // 16x16 glyphs of 8x8

static GLuint font_tex, frame_tex;
static int frame_valid;
static uint8_t *frame_buf;

/* --- saved engine state ----------------------------------------------------- */

static struct {
    GLint active_tex, bound_tex0, bound_tex1, blend_src, blend_dst, depth_mask;
    GLint viewport[4], env_mode;
    GLfloat color[4];
    GLboolean tex2d_0, tex2d_1, blend, depth, cull, lighting, fog, alpha, scissor;
} st;

static void set_enabled(GLenum cap, GLboolean on) {
    if (on)
        glEnable(cap);
    else
        glDisable(cap);
}

static void bind_unit(GLenum unit) {
    glActiveTexture(unit);
}

/* --- textures --------------------------------------------------------------- */

static void ensure_font(void) {
    if (font_tex)
        return;
    uint8_t *px = calloc(FONT_TEX * FONT_TEX, 4);
    if (!px)
        return;
    for (int c = 0; c < 256; c++) {
        int cx = (c % 16) * 8, cy = (c / 16) * 8;
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8[c * 8 + row];
            for (int col = 0; col < 8; col++) {
                uint8_t *p = px + ((cy + row) * FONT_TEX + cx + col) * 4;
                p[0] = p[1] = p[2] = 0xff;
                p[3] = (bits & (0x80 >> col)) ? 0xff : 0x00;
            }
        }
    }
    glGenTextures(1, &font_tex);
    glBindTexture(GL_TEXTURE_2D, font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX, FONT_TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);
}

void overlay_capture_frame(void) {
    if (!frame_buf)
        frame_buf = malloc(OVL_W * OVL_H * 4);
    if (!frame_buf)
        return;
    glReadPixels(0, 0, OVL_W, OVL_H, GL_RGBA, GL_UNSIGNED_BYTE, frame_buf);

    GLint active, bound;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    bind_unit(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (!frame_tex)
        glGenTextures(1, &frame_tex);
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OVL_W, OVL_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_buf);
    glBindTexture(GL_TEXTURE_2D, bound);
    bind_unit(active);
    frame_valid = 1;
}

int overlay_has_frame(void) {
    return frame_valid;
}

/* --- begin / end ------------------------------------------------------------ */

void overlay_begin(void) {
    glGetIntegerv(GL_ACTIVE_TEXTURE, &st.active_tex);
    bind_unit(GL_TEXTURE1);
    st.tex2d_1 = glIsEnabled(GL_TEXTURE_2D);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &st.bound_tex1);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    bind_unit(GL_TEXTURE0);
    st.tex2d_0 = glIsEnabled(GL_TEXTURE_2D);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &st.bound_tex0);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &st.env_mode);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    st.blend = glIsEnabled(GL_BLEND);
    st.depth = glIsEnabled(GL_DEPTH_TEST);
    st.cull = glIsEnabled(GL_CULL_FACE);
    st.lighting = glIsEnabled(GL_LIGHTING);
    st.fog = glIsEnabled(GL_FOG);
    st.alpha = glIsEnabled(GL_ALPHA_TEST);
    st.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_BLEND_SRC, &st.blend_src);
    glGetIntegerv(GL_BLEND_DST, &st.blend_dst);
    glGetIntegerv(GL_DEPTH_WRITEMASK, &st.depth_mask);
    glGetIntegerv(GL_VIEWPORT, st.viewport);
    glGetFloatv(GL_CURRENT_COLOR, st.color);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0.0f, OVL_W, OVL_H, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, OVL_W, OVL_H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    ensure_font();
}

void overlay_end(void) {
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    bind_unit(GL_TEXTURE0);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, st.bound_tex0);
    set_enabled(GL_TEXTURE_2D, st.tex2d_0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, st.env_mode);
    bind_unit(GL_TEXTURE1);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, st.bound_tex1);
    set_enabled(GL_TEXTURE_2D, st.tex2d_1);
    bind_unit(st.active_tex);
    glMatrixMode(GL_MODELVIEW);

    set_enabled(GL_BLEND, st.blend);
    set_enabled(GL_DEPTH_TEST, st.depth);
    set_enabled(GL_CULL_FACE, st.cull);
    set_enabled(GL_LIGHTING, st.lighting);
    set_enabled(GL_FOG, st.fog);
    set_enabled(GL_ALPHA_TEST, st.alpha);
    set_enabled(GL_SCISSOR_TEST, st.scissor);
    glBlendFunc(st.blend_src, st.blend_dst);
    glDepthMask(st.depth_mask ? GL_TRUE : GL_FALSE);
    glViewport(st.viewport[0], st.viewport[1], st.viewport[2], st.viewport[3]);
    glColor4f(st.color[0], st.color[1], st.color[2], st.color[3]);
}

/* --- primitives ------------------------------------------------------------- */

static void color(uint32_t c) {
    glColor4f(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f,
              (c & 0xff) / 255.0f, ((c >> 24) & 0xff) / 255.0f);
}

void overlay_rect(float x0, float y0, float x1, float y1, uint32_t c) {
    glDisable(GL_TEXTURE_2D);
    color(c);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

void overlay_frame(float x0, float y0, float x1, float y1, float t, uint32_t c) {
    overlay_rect(x0, y0, x1, y0 + t, c);
    overlay_rect(x0, y1 - t, x1, y1, c);
    overlay_rect(x0, y0, x0 + t, y1, c);
    overlay_rect(x1 - t, y0, x1, y1, c);
}

void overlay_background(uint32_t dim) {
    if (frame_valid) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, frame_tex);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        // glReadPixels rows are bottom-up.
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(OVL_W, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(OVL_W, OVL_H);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, OVL_H);
        glEnd();
    } else {
        overlay_rect(0.0f, 0.0f, OVL_W, OVL_H, 0xff000000);
    }
    overlay_rect(0.0f, 0.0f, OVL_W, OVL_H, dim);
}

float overlay_text_width(const char *s, float scale) {
    return strlen(s) * OVL_GLYPH * scale;
}

void overlay_text(float x, float y, float scale, uint32_t c, const char *s) {
    if (!font_tex)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_tex);
    color(c);
    const float g = OVL_GLYPH * scale, u = 8.0f / FONT_TEX;
    glBegin(GL_QUADS);
    for (; *s; s++, x += g) {
        unsigned ch = (unsigned char) *s;
        if (ch == ' ')
            continue;
        float s0 = (ch % 16) * u, t0 = (ch / 16) * u;
        glTexCoord2f(s0, t0);         glVertex2f(x, y);
        glTexCoord2f(s0 + u, t0);     glVertex2f(x + g, y);
        glTexCoord2f(s0 + u, t0 + u); glVertex2f(x + g, y + g);
        glTexCoord2f(s0, t0 + u);     glVertex2f(x, y + g);
    }
    glEnd();
}
