/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_SOFTFLOAT_H
#define SOLOADER_SOFTFLOAT_H

#include <so_util/so_util.h>

// Redirects a module's libgcc soft-float helpers (linked in or imported) to
// VFP. Must run after resolve_imports() and before so_flush_caches().
void softfloat_patch(so_module *mod, const char *name);

#endif // SOLOADER_SOFTFLOAT_H
