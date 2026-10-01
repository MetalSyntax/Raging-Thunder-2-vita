/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>

#include <stdint.h>
#include <string.h>

#include "utils/logger.h"
#include "utils/settings.h"

extern so_module so_mod;

/*
 * Car ground tint (config.txt "car_ground_tint").
 * CCarActor::Render() (0x132928) tints every car with the collision colour of
 * the ground under its wheels (CCarActor::Track -> CCollision::Find, averaged
 * into CArcadeCar+0x58/0x5c/0x60): SShaderEnv rgb = ground * 0.95 + 0.05,
 * which multiplies the paint material (darker in tunnels, cooler at dusk).
 * Kept as the game does it by default; 0 replaces the three loads with 1.0
 * (mov r2, #0x10000) for the plain paint colours.
 *   0: off, 1: original (default), 2: original with R and B swapped.
 */
static const struct { uint32_t off, orig; } tint_loads[3] = {
    { 0x133168, 0xe5912058 }, // ldr r2, [r1, #0x58]  (R)
    { 0x133188, 0xe591205c }, // ldr r2, [r1, #0x5c]  (G)
    { 0x1331a4, 0xe5912060 }, // ldr r2, [r1, #0x60]  (B)
};
static int tint_patchable = -1;

/** Rewrite the three loads for `mode` (also at runtime, from the port menu). */
void patch_set_car_ground_tint(int mode) {
    if (tint_patchable < 0) {
        tint_patchable = 1;
        for (int i = 0; i < 3; i++) {
            uint32_t cur;
            memcpy(&cur, (void *) (so_mod.text_base + tint_loads[i].off), sizeof(cur));
            if (cur != tint_loads[i].orig) {
                l_warn("car_ground_tint: unexpected code at 0x%x (0x%08x), not patched",
                       tint_loads[i].off, cur);
                tint_patchable = 0;
            }
        }
    }
    if (!tint_patchable)
        return;
    for (int i = 0; i < 3; i++) {
        uint32_t insn;
        if (mode == 0)
            insn = 0xe3a02801;                // mov r2, #0x10000
        else if (mode == 2)
            insn = tint_loads[2 - i].orig;    // R <- B, G <- G, B <- R
        else
            insn = tint_loads[i].orig;
        void *addr = (void *) (so_mod.text_base + tint_loads[i].off);
        kuKernelCpuUnrestrictedMemcpy(addr, &insn, sizeof(insn));
        kuKernelFlushCaches(addr, sizeof(insn));
    }
    l_info("car_ground_tint %d applied", mode);
}

void so_patch(void) {
    // Read once at load; must run before the tint check above reads the code,
    // i.e. on the pristine module.
    patch_set_car_ground_tint(setting_carGroundTint);
}
