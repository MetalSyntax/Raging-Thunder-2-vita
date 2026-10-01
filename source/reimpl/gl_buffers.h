/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  gl_buffers.h
 * @brief GLES1 buffer names chosen by the engine -> vitaGL buffer objects.
 */

#ifndef SOLOADER_GL_BUFFERS_H
#define SOLOADER_GL_BUFFERS_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void glGenBuffers_soloader(GLsizei n, GLuint *buffers);
void glBindBuffer_soloader(GLenum target, GLuint buffer);
void glDeleteBuffers_soloader(GLsizei n, const GLuint *buffers);
GLboolean glIsBuffer_soloader(GLuint buffer);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GL_BUFFERS_H
