/*
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 * Copyright (C) 2026      Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include "settings.h"

#define CONFIG_FILE_PATH DATA_PATH"config.txt"

int  setting_language;
int  setting_steering;
int  setting_steerSensitivity;
bool setting_invertSteering;
bool setting_showFps;
int  setting_msaa;
bool setting_engineLog;
bool setting_vfpFloat;
bool setting_xperiaPad;
int  setting_carGroundTint;

void settings_reset() {
    setting_language         = 0;              // system language
    setting_steering         = STEERING_STICK;
    setting_steerSensitivity = 100;            // 100 = full stick = ~35 degrees of tilt
    setting_invertSteering   = false;
    setting_showFps          = false;
    setting_msaa             = 2;              // 4x: 2010-era GLES1 scenes, cheap for the SGX543
    setting_engineLog        = false;
    setting_vfpFloat         = true;
    setting_xperiaPad        = true;
    setting_carGroundTint    = 1;
}

void settings_load() {
    settings_reset();

    char buffer[64];
    int value;

    FILE *config = fopen(CONFIG_FILE_PATH, "r");

    if (config) {
        while (EOF != fscanf(config, "%63[^ ] %d\n", buffer, &value)) {
            if      (strcmp("language", buffer) == 0)          setting_language         = value;
            else if (strcmp("steering", buffer) == 0)          setting_steering         = value;
            else if (strcmp("steer_sensitivity", buffer) == 0) setting_steerSensitivity = value;
            else if (strcmp("invert_steering", buffer) == 0)   setting_invertSteering   = (bool)value;
            else if (strcmp("show_fps", buffer) == 0)          setting_showFps          = (bool)value;
            else if (strcmp("msaa", buffer) == 0)              setting_msaa             = value;
            else if (strcmp("engine_log", buffer) == 0)        setting_engineLog        = (bool)value;
            else if (strcmp("vfp_float", buffer) == 0)         setting_vfpFloat         = (bool)value;
            else if (strcmp("xperia_pad", buffer) == 0)        setting_xperiaPad        = (bool)value;
            else if (strcmp("car_ground_tint", buffer) == 0)   setting_carGroundTint    = value;
        }
        fclose(config);
    }

    if (setting_language < 0 || setting_language > 6) setting_language = 0;
    if (setting_steering != STEERING_MOTION) setting_steering = STEERING_STICK;
    if (setting_steerSensitivity < 25) setting_steerSensitivity = 25;
    if (setting_steerSensitivity > 400) setting_steerSensitivity = 400;
    if (setting_msaa < 0 || setting_msaa > 2) setting_msaa = 2;
    if (setting_carGroundTint < 0 || setting_carGroundTint > 2) setting_carGroundTint = 1;

    // Always rewrite: writes the defaults on first boot and adds keys that
    // are new in this version to an existing config.txt.
    settings_save();
}

void settings_save() {
    FILE *config = fopen(CONFIG_FILE_PATH, "w+");

    if (config) {
        fprintf(config, "%s %d\n", "language", setting_language);
        fprintf(config, "%s %d\n", "steering", setting_steering);
        fprintf(config, "%s %d\n", "steer_sensitivity", setting_steerSensitivity);
        fprintf(config, "%s %d\n", "invert_steering", (int)setting_invertSteering);
        fprintf(config, "%s %d\n", "show_fps", (int)setting_showFps);
        fprintf(config, "%s %d\n", "msaa", setting_msaa);
        fprintf(config, "%s %d\n", "engine_log", (int)setting_engineLog);
        fprintf(config, "%s %d\n", "vfp_float", (int)setting_vfpFloat);
        fprintf(config, "%s %d\n", "xperia_pad", (int)setting_xperiaPad);
        fprintf(config, "%s %d\n", "car_ground_tint", setting_carGroundTint);
        fclose(config);
    }
}
