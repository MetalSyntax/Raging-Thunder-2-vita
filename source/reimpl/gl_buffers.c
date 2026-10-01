/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  gl_buffers.c
 * @brief GLES1 buffer-object names for an engine that invents its own.
 *
 * bite::CVertexBuffer::BindStatic() and bite::CIndexBuffer::BindStatic() never
 * call glGenBuffers: they take a name from bite::GenBufferID() (a plain
 * counter, 1, 2, 3...) and go straight to glBindBuffer + glBufferData, which
 * GLES 1.x allows. vitaGL's buffer names are `vbo *` pointers, so binding "1"
 * and then glBufferData() dereferenced address 1 (crash log 001: data abort
 * in glNamedBufferData, buffers.c:397, called from
 * P3DBackendES11::glBufferData while CPolyMesh::Read loaded the menu).
 *
 * Every buffer entry point the engine resolves is routed through a table
 * engine name -> vitaGL name; an unknown name gets a real vitaGL buffer the
 * first time it is bound (what a GLES1 driver does). glGenBuffers hands out
 * names from the same table, so both kinds of names can coexist.
 *
 * glIsBuffer was a ret0 stub: BindStatic() checks it after uploading and, on
 * 0, deletes the buffer and falls back to client-side arrays.
 *
 * Only the GL thread touches buffers (the engine is single threaded).
 */

#include "reimpl/gl_buffers.h"

#include <vitaGL.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils/logger.h"

typedef struct {
    GLuint key;   // engine name, 0 = empty slot
    GLuint value; // vitaGL name
} buf_entry;

static buf_entry *table = NULL;
static uint32_t table_cap = 0;   // power of two
static uint32_t table_used = 0;  // live + tombstones

#define TOMBSTONE_VALUE 0xFFFFFFFFu

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d;
    x ^= x >> 15; x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static buf_entry *find_slot(GLuint key, int for_insert) {
    uint32_t mask = table_cap - 1;
    uint32_t i = hash_u32(key) & mask;
    buf_entry *tomb = NULL;
    for (;;) {
        buf_entry *e = &table[i];
        if (e->key == 0)
            return for_insert && tomb ? tomb : e;
        if (e->key == key && e->value != TOMBSTONE_VALUE)
            return e;
        if (e->value == TOMBSTONE_VALUE && !tomb)
            tomb = e;
        i = (i + 1) & mask;
    }
}

static void grow(void) {
    buf_entry *old = table;
    uint32_t old_cap = table_cap;
    table_cap = old_cap ? old_cap * 2 : 1024;
    table = calloc(table_cap, sizeof(buf_entry));
    table_used = 0;
    for (uint32_t i = 0; i < old_cap; i++) {
        if (old[i].key && old[i].value != TOMBSTONE_VALUE) {
            buf_entry *e = find_slot(old[i].key, 1);
            *e = old[i];
            table_used++;
        }
    }
    free(old);
}

static GLuint lookup(GLuint name) {
    if (!name || !table)
        return 0;
    buf_entry *e = find_slot(name, 0);
    return e->key ? e->value : 0;
}

static void insert(GLuint name, GLuint real) {
    if (!table || (table_used + 1) * 4 >= table_cap * 3)
        grow();
    buf_entry *e = find_slot(name, 1);
    if (!e->key)
        table_used++;
    e->key = name;
    e->value = real;
}

static void erase(GLuint name) {
    if (!name || !table)
        return;
    buf_entry *e = find_slot(name, 0);
    if (e->key)
        e->value = TOMBSTONE_VALUE;
}

// Engine name -> vitaGL name, creating the buffer on first use.
static GLuint resolve(GLuint name) {
    if (!name)
        return 0;
    GLuint real = lookup(name);
    if (!real) {
        glGenBuffers(1, &real);
        insert(name, real);
    }
    return real;
}

/* --- entry points handed to the engine (dynlib.c) -------------------------- */

static GLuint next_gen_name = 0x40000000u; // far away from GenBufferID()'s counter

void glGenBuffers_soloader(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint name;
        do {
            name = next_gen_name++;
        } while (!name || lookup(name));
        GLuint real;
        glGenBuffers(1, &real);
        insert(name, real);
        buffers[i] = name;
    }
}

void glBindBuffer_soloader(GLenum target, GLuint buffer) {
    glBindBuffer(target, resolve(buffer));
}

void glDeleteBuffers_soloader(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint real = lookup(buffers[i]);
        if (real) {
            glDeleteBuffers(1, &real);
            erase(buffers[i]);
        }
    }
}

GLboolean glIsBuffer_soloader(GLuint buffer) {
    return lookup(buffer) ? GL_TRUE : GL_FALSE;
}
