/*
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 * Copyright (C) 2026      Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  settings.h
 * @brief Loader settings, stored as "key value" lines in DATA_PATH/config.txt.
 */

#ifndef SOLOADER_SETTINGS_H
#define SOLOADER_SETTINGS_H

#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STEERING_STICK  0 ///< left stick drives the emulated accelerometer
#define STEERING_MOTION 1 ///< the Vita's own accelerometer (tilt the console)

extern int  setting_language;       ///< 0 system, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 sv
extern int  setting_steering;       ///< STEERING_STICK / STEERING_MOTION
extern int  setting_steerSensitivity; ///< percent, 25..400
extern bool setting_invertSteering;
extern bool setting_showFps;
extern int  setting_msaa;           ///< 0 off, 1 2x, 2 4x
extern bool setting_engineLog;      ///< engine __android_log_print/_PDebug -> log
extern bool setting_vfpFloat;       ///< run the engine's soft-float helpers on VFP
extern bool setting_xperiaPad;      ///< tell the game the Xperia Play gamepad is open
extern int  setting_carGroundTint;  ///< 0 off, 1 original (default), 2 original with R/B swapped

void settings_load();
void settings_save();
void settings_reset();

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_SETTINGS_H
