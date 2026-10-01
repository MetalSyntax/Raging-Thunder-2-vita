/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  input.c
 * @brief Vita controls -> Polarbit Fuse input events.
 *
 * Everything goes through Jni.OnEvent(type, sub, a, b, c), exactly what the
 * Java side sends (decompiled/apk_jadx/.../MainTask.java, FuseTouch.java,
 * FuseSensor.java). PEventQueue::OnEvent (0x2894xx) queues them and the
 * engine consumes the queue at the start of the next OnEvent(0, 1) frame.
 *
 * Keys: OnEvent(1, 0, android_keycode, 0, pressed). The engine turns the
 * keycode into a menu key (m_keycodes[]) and a game action bit
 * (m_keymasks[]). Tables from PAndroidSystemManager::Init() plus the
 * overrides in CApplication::Init() (Xperia Play layout):
 *
 *   keycode                 menu key   action bit
 *   19..22 DPAD U/D/L/R     3/4/1/2    4/8/1/2
 *   23 DPAD_CENTER (Xperia X)  0x98    0x400
 *   99 BUTTON_X (Xperia Square)        0x200
 *   100 BUTTON_Y (Xperia Triangle) 0x15 0x10
 *   4 BACK (Xperia Circle)     8       0x40
 *   102/103 L1/R1              0xe/0xf 0x200/0x400
 *
 * so Cross/R share one action and Square/L the other, like on the Xperia
 * Play. Start is BACK as well (the game's pause/back key).
 *
 * Touch: OnEvent(1, 1, x, y, action | (pointer_id + 1) << 16), Android
 * MotionEvent actions (0 down, 1 up, 2 move, 5/6 pointer down/up), in
 * surface pixels (960x544, the size given with OnEvent(3, 0, w, h)). Pointer
 * ids are our own stable slots, never the raw SceTouchReport id.
 *
 * Accelerometer: OnEvent(4, 0, x, y, z), m/s^2 * 6553 (FuseSensor), in the
 * axes FuseSensor reports for a landscape game: x points to the top of the
 * landscape screen, y to its left side, z out of the screen; a device at rest
 * reads +g against gravity (Android convention). Only sent while the game has
 * it enabled (FuseSensor.ActivateAccelerometer, java.c). Fed either by the
 * left stick (steering = turning a virtual wheel held at LEAN_DEG) or by the
 * Vita's own accelerometer (setting steering 1).
 */

#include "input.h"

#include "java.h"
#include "utils/logger.h"
#include "utils/settings.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include <falso_jni/FalsoJNI.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 960
#define SCREEN_H 544

#define KEY_BACK        4
#define KEY_DPAD_UP     19
#define KEY_DPAD_DOWN   20
#define KEY_DPAD_LEFT   21
#define KEY_DPAD_RIGHT  22
#define KEY_DPAD_CENTER 23
#define KEY_BUTTON_X    99
#define KEY_BUTTON_Y    100
#define KEY_BUTTON_L1   102
#define KEY_BUTTON_R1   103

#define ACTION_DOWN         0
#define ACTION_UP           1
#define ACTION_MOVE         2
#define ACTION_POINTER_DOWN 5
#define ACTION_POINTER_UP   6

#define GRAVITY      9.80665f
#define SENSOR_SCALE 6553.0f   // FuseSensor: (int)(value * 6553)
#define LEAN_DEG     50.0f     // screen tilted back 50 degrees from flat
#define MAX_STEER_DEG 35.0f    // full stick at steer_sensitivity 100

#define STICK_DEADZONE 0.15f   // fraction of full deflection
#define STICK_DIGITAL  0.50f   // stick -> d-pad threshold

#define TOUCH_SLOTS 6

static fuse_on_event_fn OnEvent;

static void send(int type, int sub, int a, int b, int c) {
    OnEvent(&jni, NULL, type, sub, a, b, c);
}

/* --- keys ------------------------------------------------------------------- */

static const struct {
    uint32_t button;
    int keycode;
} button_map[] = {
    { SCE_CTRL_UP,       KEY_DPAD_UP },
    { SCE_CTRL_DOWN,     KEY_DPAD_DOWN },
    { SCE_CTRL_LEFT,     KEY_DPAD_LEFT },
    { SCE_CTRL_RIGHT,    KEY_DPAD_RIGHT },
    { SCE_CTRL_CROSS,    KEY_DPAD_CENTER },
    { SCE_CTRL_SQUARE,   KEY_BUTTON_X },
    { SCE_CTRL_TRIANGLE, KEY_BUTTON_Y },
    { SCE_CTRL_CIRCLE,   KEY_BACK },
    { SCE_CTRL_START,    KEY_BACK },
    { SCE_CTRL_LTRIGGER, KEY_BUTTON_L1 },
    { SCE_CTRL_L1,       KEY_BUTTON_L1 },
    { SCE_CTRL_RTRIGGER, KEY_BUTTON_R1 },
    { SCE_CTRL_R1,       KEY_BUTTON_R1 },
};

// Several sources can hold the same keycode (Circle and Start, d-pad and the
// stick): only the first press and the last release reach the engine.
static uint8_t key_refs[128];

static void key_set(int keycode, int down) {
    if (down) {
        if (key_refs[keycode]++ == 0)
            send(1, 0, keycode, 0, 1);
    } else if (key_refs[keycode] > 0) {
        if (--key_refs[keycode] == 0)
            send(1, 0, keycode, 0, 0);
    }
}

static uint32_t buttons_held = 0;
static int stick_dir_held[4] = {0}; // left, right, up, down via the stick

/* --- touch ------------------------------------------------------------------ */

typedef struct {
    int active;
    int report_id; // SceTouchReport id currently owning the slot
    int x, y;
} touch_slot;

static touch_slot slots[TOUCH_SLOTS];

static int slots_active(void) {
    int n = 0;
    for (int i = 0; i < TOUCH_SLOTS; i++)
        n += slots[i].active;
    return n;
}

static void touch_event(int slot, int action) {
    send(1, 1, slots[slot].x, slots[slot].y, action | ((slot + 1) << 16));
}

static void touch_update(void) {
    SceTouchData touch;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0)
        touch.reportNum = 0;

    int seen[TOUCH_SLOTS] = {0};

    for (int r = 0; r < touch.reportNum && r < SCE_TOUCH_MAX_REPORT; r++) {
        int id = touch.report[r].id;
        // Front panel is 1920x1088.
        int x = touch.report[r].x / 2;
        int y = touch.report[r].y / 2;
        if (x >= SCREEN_W) x = SCREEN_W - 1;
        if (y >= SCREEN_H) y = SCREEN_H - 1;

        int s = -1;
        for (int i = 0; i < TOUCH_SLOTS; i++) {
            if (slots[i].active && slots[i].report_id == id) { s = i; break; }
        }
        if (s >= 0) {
            seen[s] = 1;
            if (slots[s].x != x || slots[s].y != y) {
                slots[s].x = x;
                slots[s].y = y;
                touch_event(s, ACTION_MOVE);
            }
            continue;
        }
        for (int i = 0; i < TOUCH_SLOTS; i++) {
            if (!slots[i].active) { s = i; break; }
        }
        if (s < 0)
            continue; // more fingers than slots
        int first = slots_active() == 0;
        slots[s].active = 1;
        slots[s].report_id = id;
        slots[s].x = x;
        slots[s].y = y;
        seen[s] = 1;
        touch_event(s, first ? ACTION_DOWN : ACTION_POINTER_DOWN);
    }

    for (int i = 0; i < TOUCH_SLOTS; i++) {
        if (slots[i].active && !seen[i]) {
            int last = slots_active() == 1;
            touch_event(i, last ? ACTION_UP : ACTION_POINTER_UP);
            slots[i].active = 0;
        }
    }
}

/* --- accelerometer ---------------------------------------------------------- */

static float stick_axis(uint8_t v) {
    float f = ((int) v - 128) / 127.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    if (fabsf(f) < STICK_DEADZONE)
        return 0.0f;
    // rescale so the output starts at 0 right outside the dead zone
    return (f - copysignf(STICK_DEADZONE, f)) / (1.0f - STICK_DEADZONE);
}

static void send_accel(float lx, float ly, float lz) {
    // lx/ly/lz: landscape axes (x right, y up, z out), m/s^2, +g against
    // gravity. FuseSensor's frame: x = landscape up, y = landscape left.
    send(4, 0, (int) (ly * SENSOR_SCALE), (int) (-lx * SENSOR_SCALE), (int) (lz * SENSOR_SCALE));
}

static void accel_from_stick(float steer) {
    float sens = setting_steerSensitivity / 100.0f;
    float theta = steer * sens * MAX_STEER_DEG * (float) M_PI / 180.0f;
    float lean = LEAN_DEG * (float) M_PI / 180.0f;
    // Device turned clockwise by theta (steering right) while tilted back by
    // `lean`: the "up" reaction vector seen in device coordinates.
    float lx = -GRAVITY * sinf(lean) * sinf(theta);
    float ly =  GRAVITY * sinf(lean) * cosf(theta);
    float lz =  GRAVITY * cosf(lean);
    send_accel(lx, ly, lz);
}

static void accel_from_motion(void) {
    SceMotionSensorState st;
    if (sceMotionGetSensorState(&st, 1) < 0)
        return;
    // The Vita reports the gravity vector itself (-1 G on z lying face up);
    // Android reports the reaction (+g). Units are G.
    float lx = -st.accelerometer.x * GRAVITY;
    float ly = -st.accelerometer.y * GRAVITY;
    float lz = -st.accelerometer.z * GRAVITY;
    if (setting_invertSteering)
        lx = -lx;
    send_accel(lx, ly, lz);
}

/* --- public ------------------------------------------------------------------ */

void input_init(fuse_on_event_fn on_event) {
    OnEvent = on_event;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    if (setting_steering == STEERING_MOTION)
        sceMotionStartSampling();

    memset(slots, 0, sizeof(slots));
    memset(key_refs, 0, sizeof(key_refs));
    l_info("input: ready (steering: %s, sensitivity %d%%, invert %d)",
           setting_steering == STEERING_MOTION ? "motion" : "left stick",
           setting_steerSensitivity, setting_invertSteering);
}

void input_release_all(void) {
    for (unsigned i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++) {
        if (buttons_held & button_map[i].button)
            key_set(button_map[i].keycode, 0);
    }
    buttons_held = 0;
    static const int dir_keys[4] = { KEY_DPAD_LEFT, KEY_DPAD_RIGHT, KEY_DPAD_UP, KEY_DPAD_DOWN };
    for (int i = 0; i < 4; i++) {
        if (stick_dir_held[i]) {
            key_set(dir_keys[i], 0);
            stick_dir_held[i] = 0;
        }
    }
    for (int i = 0; i < TOUCH_SLOTS; i++) {
        if (slots[i].active) {
            int last = slots_active() == 1;
            touch_event(i, last ? ACTION_UP : ACTION_POINTER_UP);
            slots[i].active = 0;
        }
    }
}

void input_update(void) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    // Buttons
    for (unsigned i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++) {
        uint32_t b = button_map[i].button;
        int now = (pad.buttons & b) != 0;
        int before = (buttons_held & b) != 0;
        if (now != before)
            key_set(button_map[i].keycode, now);
    }
    buttons_held = pad.buttons;

    // Left stick
    float sx = stick_axis(pad.lx);
    float sy = stick_axis(pad.ly);
    int stick_steers = accelerometer_enabled && setting_steering == STEERING_STICK;

    // When the game is not in tilt mode the stick doubles as the d-pad
    // (menus, and the "buttons" steering option).
    int dir_now[4] = {
        !stick_steers && sx < -STICK_DIGITAL,
        !stick_steers && sx >  STICK_DIGITAL,
        !stick_steers && sy < -STICK_DIGITAL,
        !stick_steers && sy >  STICK_DIGITAL,
    };
    static const int dir_keys[4] = { KEY_DPAD_LEFT, KEY_DPAD_RIGHT, KEY_DPAD_UP, KEY_DPAD_DOWN };
    for (int i = 0; i < 4; i++) {
        if (dir_now[i] != stick_dir_held[i]) {
            key_set(dir_keys[i], dir_now[i]);
            stick_dir_held[i] = dir_now[i];
        }
    }

    if (accelerometer_enabled) {
        if (setting_steering == STEERING_MOTION)
            accel_from_motion();
        else
            accel_from_stick(setting_invertSteering ? -sx : sx);
    }

    touch_update();
}
