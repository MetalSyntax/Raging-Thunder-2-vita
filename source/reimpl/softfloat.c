/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  softfloat.c
 * @brief VFP replacements for the soft-float helpers linked into the game.
 *
 * librthunder2lite.so is armeabi (v5TE, soft-float code): every float/double
 * operation is a call into libgcc's ieee754 emulation, statically linked into
 * the module (floats at 0x1d5288..0x1d5c9c, doubles and compares at
 * 0x281790..0x281cc4; 802 call sites). The Polarbit engine is mostly 16.16
 * fixed point, but physics/camera code still goes through these helpers.
 * Their entries get overwritten with a branch to the functions below. The
 * module imports no float helper, so override_imports() is a no-op here (kept
 * for symmetry with the Carnivores port this file comes from).
 *
 * This file is built with -mfloat-abi=softfp (whole project) so the calling
 * convention is exactly the AEABI helper one: floats/doubles in r0-r3, result
 * in r0/r0:r1. It is built WITHOUT -ffast-math (CMakeLists.txt) so NaN
 * comparisons behave like libgcc.
 *
 * Hook safety (checked against the real .so with objdump, 2026-09-30,
 * 55292 direct branches scanned):
 * - 22 entries hooked, all ARM. Each hook writes 8 bytes (LDR PC,[PC,#-4] +
 *   address). No two hooked entries are closer than 8 bytes (closest pairs:
 *   ui2f/i2f at 0x1d58f0/0x1d58f8, gesf2/lesf2/cmpsf2 at 0x281bdc/e4/ec).
 * - The only branches into the first 8 bytes of a hooked entry are
 *   __gesf2/__lesf2 -> __cmpsf2+4 (0x281bf0); both are hooked too, so that
 *   code never runs. __aeabi_ui2f branches to i2f+8, past the hooked window.
 * - __aeabi_fsub/frsub/dsub/drsub are NOT hooked: they flip a sign bit and
 *   fall through / branch to the start of fadd/dadd, which is hooked.
 * - __aeabi_cf*cmp* (result in CPSR flags) are left alone; they call
 *   __cmpsf2 through the PLT, which resolves to the hooked local definition.
 */

#include <kubridge.h>
#include <stdint.h>
#include <string.h>
#include <so_util/so_util.h>

#include "utils/logger.h"


/* --- float ----------------------------------------------------------------- */

static float vfp_fadd(float a, float b) { return a + b; }
static float vfp_fsub(float a, float b) { return a - b; }
static float vfp_frsub(float a, float b) { return b - a; }
static float vfp_fmul(float a, float b) { return a * b; }
static float vfp_fdiv(float a, float b) { return a / b; }

static float vfp_i2f(int a) { return (float) a; }
static float vfp_ui2f(unsigned a) { return (float) a; }
static int vfp_f2iz(float a) { return (int) a; }
static unsigned vfp_f2uiz(float a) { return (unsigned) a; }

static int vfp_fcmpeq(float a, float b) { return a == b; }
static int vfp_fcmplt(float a, float b) { return a < b; }
static int vfp_fcmple(float a, float b) { return a <= b; }
static int vfp_fcmpge(float a, float b) { return a >= b; }
static int vfp_fcmpgt(float a, float b) { return a > b; }

// libgcc three-way compares: <0, 0, >0; the value for unordered (NaN)
// operands is what makes each variant differ.
static int vfp_gesf2(float a, float b) { return a < b ? -1 : a > b ? 1 : a == b ? 0 : -1; }
static int vfp_lesf2(float a, float b) { return a < b ? -1 : a > b ? 1 : a == b ? 0 : 1; }

/* --- double ---------------------------------------------------------------- */

static double vfp_dadd(double a, double b) { return a + b; }
static double vfp_dsub(double a, double b) { return a - b; }
static double vfp_drsub(double a, double b) { return b - a; }
static double vfp_dmul(double a, double b) { return a * b; }
static double vfp_ddiv(double a, double b) { return a / b; }

static double vfp_i2d(int a) { return (double) a; }
static double vfp_ui2d(unsigned a) { return (double) a; }
static int vfp_d2iz(double a) { return (int) a; }
static unsigned vfp_d2uiz(double a) { return (unsigned) a; }

static double vfp_f2d(float a) { return (double) a; }
static float vfp_d2f(double a) { return (float) a; }

static int vfp_dcmpeq(double a, double b) { return a == b; }
static int vfp_dcmplt(double a, double b) { return a < b; }
static int vfp_dcmple(double a, double b) { return a <= b; }
static int vfp_dcmpge(double a, double b) { return a >= b; }
static int vfp_dcmpgt(double a, double b) { return a > b; }

static int vfp_gedf2(double a, double b) { return a < b ? -1 : a > b ? 1 : a == b ? 0 : -1; }
static int vfp_ledf2(double a, double b) { return a < b ? -1 : a > b ? 1 : a == b ? 0 : 1; }

/* --- helpers linked into the module: entry hooks --------------------------- */

// Every name the entry can be exported as; the first one found is hooked.
// libfmodex.so exports some helpers only under their GNU name.
static const struct {
    const char *names[3];
    void *fn;
} entries[] = {
    { { "__aeabi_fadd", "__addsf3" },           vfp_fadd   },  // fsub/frsub fall into it
    { { "__aeabi_fmul", "__mulsf3" },           vfp_fmul   },
    { { "__aeabi_fdiv", "__divsf3" },           vfp_fdiv   },
    { { "__aeabi_i2f", "__floatsisf" },         vfp_i2f    },
    { { "__aeabi_ui2f", "__floatunsisf" },      vfp_ui2f   },
    { { "__aeabi_f2iz", "__fixsfsi" },          vfp_f2iz   },
    { { "__aeabi_f2uiz", "__fixunssfsi" },      vfp_f2uiz  },
    { { "__aeabi_fcmpeq" },                     vfp_fcmpeq },
    { { "__aeabi_fcmplt" },                     vfp_fcmplt },
    { { "__aeabi_fcmple" },                     vfp_fcmple },
    { { "__aeabi_fcmpge" },                     vfp_fcmpge },
    { { "__aeabi_fcmpgt" },                     vfp_fcmpgt },
    { { "__gesf2", "__gtsf2" },                 vfp_gesf2  },
    { { "__lesf2", "__ltsf2" },                 vfp_lesf2  },
    { { "__cmpsf2", "__eqsf2", "__nesf2" },     vfp_lesf2  },  // unordered -> 1

    { { "__aeabi_dadd", "__adddf3" },           vfp_dadd   },  // dsub/drsub fall into it
    { { "__aeabi_dmul", "__muldf3" },           vfp_dmul   },
    { { "__aeabi_ddiv", "__divdf3" },           vfp_ddiv   },
    { { "__aeabi_i2d", "__floatsidf" },         vfp_i2d    },
    { { "__aeabi_ui2d", "__floatunsidf" },      vfp_ui2d   },
    { { "__aeabi_d2iz", "__fixdfsi" },          vfp_d2iz   },
    { { "__aeabi_d2uiz", "__fixunsdfsi" },      vfp_d2uiz  },
    { { "__aeabi_f2d", "__extendsfdf2" },       vfp_f2d    },
    { { "__aeabi_d2f", "__truncdfsf2" },        vfp_d2f    },
    { { "__aeabi_dcmpeq" },                     vfp_dcmpeq },
    { { "__aeabi_dcmplt" },                     vfp_dcmplt },
    { { "__aeabi_dcmple" },                     vfp_dcmple },
    { { "__aeabi_dcmpge" },                     vfp_dcmpge },
    { { "__aeabi_dcmpgt" },                     vfp_dcmpgt },
    { { "__gedf2", "__gtdf2" },                 vfp_gedf2  },
    { { "__ledf2", "__ltdf2" },                 vfp_ledf2  },
    { { "__cmpdf2", "__eqdf2", "__nedf2" },     vfp_ledf2  },
};

/* --- helpers the module imports: GOT overrides ------------------------------ */

static const struct {
    const char *name;
    void *fn;
} imports[] = {
    { "__aeabi_fadd",  vfp_fadd  }, { "__aeabi_fsub",  vfp_fsub  }, { "__aeabi_frsub", vfp_frsub },
    { "__aeabi_fmul",  vfp_fmul  }, { "__aeabi_fdiv",  vfp_fdiv  },
    { "__aeabi_i2f",   vfp_i2f   }, { "__aeabi_ui2f",  vfp_ui2f  },
    { "__aeabi_f2iz",  vfp_f2iz  }, { "__aeabi_f2uiz", vfp_f2uiz },
    { "__aeabi_fcmpeq", vfp_fcmpeq }, { "__aeabi_fcmplt", vfp_fcmplt }, { "__aeabi_fcmple", vfp_fcmple },
    { "__aeabi_fcmpge", vfp_fcmpge }, { "__aeabi_fcmpgt", vfp_fcmpgt },
    { "__aeabi_dadd",  vfp_dadd  }, { "__aeabi_dsub",  vfp_dsub  }, { "__aeabi_drsub", vfp_drsub },
    { "__aeabi_dmul",  vfp_dmul  }, { "__aeabi_ddiv",  vfp_ddiv  },
    { "__aeabi_i2d",   vfp_i2d   }, { "__aeabi_ui2d",  vfp_ui2d  },
    { "__aeabi_d2iz",  vfp_d2iz  }, { "__aeabi_d2uiz", vfp_d2uiz },
    { "__aeabi_f2d",   vfp_f2d   }, { "__aeabi_d2f",   vfp_d2f   },
    { "__aeabi_dcmpeq", vfp_dcmpeq }, { "__aeabi_dcmplt", vfp_dcmplt }, { "__aeabi_dcmple", vfp_dcmple },
    { "__aeabi_dcmpge", vfp_dcmpge }, { "__aeabi_dcmpgt", vfp_dcmpgt },
};

// Same walk as so_resolve(), but only rewrites the slots named above and
// leaves every other import alone.
static int override_imports(so_module *mod) {
    int n = 0;
    for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
        Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
        int type = ELF32_R_TYPE(rel->r_info);
        if (type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT)
            continue;
        Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
        if (sym->st_shndx != SHN_UNDEF)
            continue;
        const char *name = mod->dynstr + sym->st_name;
        for (unsigned j = 0; j < sizeof(imports) / sizeof(imports[0]); j++) {
            if (strcmp(name, imports[j].name) == 0) {
                uintptr_t val = (uintptr_t) imports[j].fn;
                kuKernelCpuUnrestrictedMemcpy((void *) (mod->text_base + rel->r_offset), &val, sizeof(val));
                n++;
                break;
            }
        }
    }
    return n;
}

static int hook_entries(so_module *mod) {
    int n = 0;
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        uintptr_t addr = 0;
        for (int k = 0; k < 3 && entries[i].names[k] && !addr; k++)
            addr = so_symbol(mod, entries[i].names[k]);
        if (!addr)
            continue; // not linked into this module (e.g. fmod's doubles)
        // The helpers are ARM code; a Thumb (odd) address would mean a
        // different build than the one the hook table was checked on.
        if (addr & 1) {
            l_warn("softfloat: %s is Thumb, not hooked", entries[i].names[0]);
            continue;
        }
        hook_arm(addr, (uintptr_t) entries[i].fn);
        n++;
    }
    return n;
}

void softfloat_patch(so_module *mod, const char *name) {
    int hooked = hook_entries(mod);
    int got = override_imports(mod);
    l_success("softfloat: %s: %d helpers hooked, %d imports redirected to VFP.", name, hooked, got);
}
