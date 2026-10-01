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
 * Play. The physical buttons are remappable (controls.txt / port menu):
 * every action below sends one of these keycodes. START is not bindable: it
 * is the BACK key (sent on release) and, with SELECT, opens the port menu.
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
#include "vita_menu.h"
#include "utils/logger.h"
#include "utils/settings.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include <falso_jni/FalsoJNI.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
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

/* Remappable actions, saved in DATA_PATH controls.txt (written with the
 * defaults if missing) and edited in game with the port menu (vita_menu.c). */
#define CONTROLS_PATH    DATA_PATH "controls.txt"
#define CONTROLS_VERSION 1

typedef struct {
    const char *name;           // controls.txt key
    const char *label;          // port menu
    int keycode;                // Android keycode sent to the engine
    uint32_t default_buttons;
    uint32_t buttons;           // current binding
    int down;                   // keycode currently held by this action
} Action;

enum { ACT_ACCELERATE, ACT_BRAKE, ACT_NITRO, ACT_BACK, ACT_L, ACT_R,
       ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_COUNT };

static Action actions[ACT_COUNT] = {
    [ACT_ACCELERATE] = { "ACCELERATE", "Accelerate / OK",       KEY_DPAD_CENTER, SCE_CTRL_CROSS },
    [ACT_BRAKE]      = { "BRAKE",      "Brake / reverse",       KEY_BUTTON_X,    SCE_CTRL_SQUARE },
    [ACT_NITRO]      = { "NITRO",      "Nitro / action",        KEY_BUTTON_Y,    SCE_CTRL_TRIANGLE },
    [ACT_BACK]       = { "BACK",       "Back / pause",          KEY_BACK,        SCE_CTRL_CIRCLE },
    [ACT_L]          = { "L",          "L: brake, menu page",   KEY_BUTTON_L1,   SCE_CTRL_LTRIGGER },
    [ACT_R]          = { "R",          "R: accel., menu page",  KEY_BUTTON_R1,   SCE_CTRL_RTRIGGER },
    [ACT_UP]         = { "UP",         "Up (menus)",            KEY_DPAD_UP,     SCE_CTRL_UP },
    [ACT_DOWN]       = { "DOWN",       "Down (menus)",          KEY_DPAD_DOWN,   SCE_CTRL_DOWN },
    [ACT_LEFT]       = { "LEFT",       "Left (menus/steer)",    KEY_DPAD_LEFT,   SCE_CTRL_LEFT },
    [ACT_RIGHT]      = { "RIGHT",      "Right (menus/steer)",   KEY_DPAD_RIGHT,  SCE_CTRL_RIGHT },
};

typedef struct {
    const char *name;   // controls.txt spelling (first one per mask is canonical)
    const char *label;  // port menu
    uint32_t mask;
} ButtonName;

static const ButtonName button_names[] = {
    { "CROSS",      "Cross",    SCE_CTRL_CROSS },
    { "CIRCLE",     "Circle",   SCE_CTRL_CIRCLE },
    { "SQUARE",     "Square",   SCE_CTRL_SQUARE },
    { "TRIANGLE",   "Triangle", SCE_CTRL_TRIANGLE },
    { "L1",         "L",        SCE_CTRL_LTRIGGER },
    { "R1",         "R",        SCE_CTRL_RTRIGGER },
    { "UP",         "Up",       SCE_CTRL_UP },
    { "DOWN",       "Down",     SCE_CTRL_DOWN },
    { "LEFT",       "Left",     SCE_CTRL_LEFT },
    { "RIGHT",      "Right",    SCE_CTRL_RIGHT },
    { "SELECT",     "Select",   SCE_CTRL_SELECT },
    { "START",      "Start",    SCE_CTRL_START },
    // Aliases, only read.
    { "X",          NULL,       SCE_CTRL_CROSS },
    { "O",          NULL,       SCE_CTRL_CIRCLE },
    { "L",          NULL,       SCE_CTRL_LTRIGGER },
    { "LTRIGGER",   NULL,       SCE_CTRL_LTRIGGER },
    { "R",          NULL,       SCE_CTRL_RTRIGGER },
    { "RTRIGGER",   NULL,       SCE_CTRL_RTRIGGER },
    { "DPAD_UP",    NULL,       SCE_CTRL_UP },
    { "DPAD_DOWN",  NULL,       SCE_CTRL_DOWN },
    { "DPAD_LEFT",  NULL,       SCE_CTRL_LEFT },
    { "DPAD_RIGHT", NULL,       SCE_CTRL_RIGHT },
};
#define BUTTON_NAMES_COUNT (sizeof(button_names) / sizeof(button_names[0]))

// Several sources can hold the same keycode (two actions, START and BACK,
// d-pad and the stick): only the first press and the last release reach the
// engine.
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
static int start_combo;             // START held and used for START + SELECT
static int start_tap;               // BACK pressed for START, released next frame

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
    input_reload_controls();
    l_info("input: ready (steering: %s, sensitivity %d%%, invert %d)",
           setting_steering == STEERING_MOTION ? "motion" : "left stick",
           setting_steerSensitivity, setting_invertSteering);
}

void input_release_all(void) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (actions[i].down) {
            key_set(actions[i].keycode, 0);
            actions[i].down = 0;
        }
    }
    if (start_tap) {
        key_set(KEY_BACK, 0);
        start_tap = 0;
    }
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

    uint32_t buttons = pad.buttons;
    uint32_t pressed = buttons & ~buttons_held;
    uint32_t released = buttons_held & ~buttons;
    buttons_held = buttons;

    // BACK sent for a START tap last frame: release it now.
    if (start_tap) {
        key_set(KEY_BACK, 0);
        start_tap = 0;
    }

    if (vita_menu_active()) {
        if (!(buttons & SCE_CTRL_START))
            start_combo = 0;
        vita_menu_update(buttons, pressed);
        // Closed with START: its release must not count as BACK.
        if (!vita_menu_active() && (buttons & SCE_CTRL_START))
            start_combo = 1;
        return;
    }

    // START + SELECT (either order) opens the port menu. START alone is the
    // BACK key, sent on release so that the combo doesn't also pause.
    if (((buttons & SCE_CTRL_START) && (pressed & SCE_CTRL_SELECT)) ||
        ((buttons & SCE_CTRL_SELECT) && (pressed & SCE_CTRL_START))) {
        start_combo = 1;
        input_release_all();
        vita_menu_open();
        return;
    }
    if (released & SCE_CTRL_START) {
        if (!start_combo) {
            key_set(KEY_BACK, 1);
            start_tap = 1;
        }
        start_combo = 0;
    }

    // Remappable actions. SELECT is left out while START is held (combo).
    uint32_t act_buttons = buttons & ~SCE_CTRL_START;
    if (buttons & SCE_CTRL_START)
        act_buttons &= ~SCE_CTRL_SELECT;
    for (int i = 0; i < ACT_COUNT; i++) {
        int now = (act_buttons & actions[i].buttons) != 0;
        if (now != actions[i].down) {
            key_set(actions[i].keycode, now);
            actions[i].down = now;
        }
    }

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

/* --- bindings (controls.txt, port menu) ------------------------------------- */

int input_action_count(void) {
    return ACT_COUNT;
}

const char *input_action_label(int action) {
    return action >= 0 && action < ACT_COUNT ? actions[action].label : "";
}

uint32_t input_action_buttons(int action) {
    return action >= 0 && action < ACT_COUNT ? actions[action].buttons : 0;
}

void input_action_bind(int action, uint32_t button, int add) {
    if (action < 0 || action >= ACT_COUNT)
        return;
    for (int i = 0; i < ACT_COUNT; i++)
        actions[i].buttons &= ~button;
    if (add)
        actions[action].buttons |= button;
    else
        actions[action].buttons = button;
}

void input_action_clear(int action) {
    if (action >= 0 && action < ACT_COUNT)
        actions[action].buttons = 0;
}

void input_controls_defaults(void) {
    for (int i = 0; i < ACT_COUNT; i++)
        actions[i].buttons = actions[i].default_buttons;
}

uint32_t input_bindable_buttons(void) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++)
        if (button_names[i].label)
            mask |= button_names[i].mask;
    return mask & ~SCE_CTRL_START;
}

static void buttons_join(uint32_t mask, char *out, size_t size, int labels) {
    size_t len = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++) {
        const ButtonName *b = &button_names[i];
        if (!b->label || !(mask & b->mask))
            continue;
        int n = snprintf(out + len, size - len, "%s%s", len ? ", " : "", labels ? b->label : b->name);
        if (n < 0 || (size_t) n >= size - len)
            break;
        len += n;
    }
    if (!len)
        snprintf(out, size, "%s", labels ? "-" : "NONE");
}

void input_buttons_text(uint32_t mask, char *out, size_t size) {
    buttons_join(mask, out, size, 1);
}

// Copies the first word of `s` (up to whitespace, ',', '=', ':', '#', ';'),
// upper-cased.
static void word(const char *s, char *out, size_t size) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = 0;
    while (*s && !strchr(" \t,=:#;\r\n", *s) && len < size - 1)
        out[len++] = (char) toupper((unsigned char) *s++);
    out[len] = '\0';
}

static uint32_t parse_button_token(const char *tok) {
    char clean[32];
    word(tok, clean, sizeof(clean));
    if (!clean[0] || strcmp(clean, "NONE") == 0)
        return 0;
    for (unsigned i = 0; i < BUTTON_NAMES_COUNT; i++) {
        if (strcmp(clean, button_names[i].name) == 0)
            return button_names[i].mask & ~SCE_CTRL_START;
    }
    l_warn("input: unknown button '%s'", clean);
    return 0;
}

static uint32_t parse_button_list(const char *p) {
    uint32_t mask = 0;
    while (*p && *p != '#' && *p != ';' && *p != '\r' && *p != '\n') {
        mask |= parse_button_token(p);
        while (*p && *p != ',' && *p != '#' && *p != ';' && *p != '\r' && *p != '\n')
            p++;
        if (*p == ',')
            p++;
    }
    return mask;
}

static int find_action_index(const char *name) {
    char clean[32];
    word(name, clean, sizeof(clean));
    for (int i = 0; i < ACT_COUNT; i++) {
        if (strcmp(clean, actions[i].name) == 0)
            return i;
    }
    return -1;
}

void input_controls_save(void) {
    FILE *f = fopen(CONTROLS_PATH, "w");
    if (!f) {
        l_error("input: cannot write %s", CONTROLS_PATH);
        return;
    }
    fprintf(f,
        "# Raging Thunder 2 - PS Vita controls\n"
        "# Also editable in game: START + SELECT.\n"
        "#\n"
        "# Buttons: CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, UP, DOWN, LEFT, RIGHT,\n"
        "#   SELECT, NONE. START is always back / pause.\n"
        "#\n"
        "# Actions:\n"
        "#   ACCELERATE   Accelerate, OK in the menus\n"
        "#   BRAKE        Brake / reverse\n"
        "#   NITRO        Nitro / action\n"
        "#   BACK         Back / pause\n"
        "#   L, R         Shoulder keys: brake / accelerate in the race, page\n"
        "#                switch in the menus\n"
        "#   UP, DOWN, LEFT, RIGHT   D-pad (menus; LEFT/RIGHT also steer with the\n"
        "#                buttons steering option)\n"
        "# The left stick always steers (or moves in the menus).\n"
        "#\n"
        "# ACTION = BUTTON, BUTTON ...   (or BUTTON = ACTION)\n"
        "\n"
        "VERSION = %d\n", CONTROLS_VERSION);
    for (int i = 0; i < ACT_COUNT; i++) {
        char list[128];
        buttons_join(actions[i].buttons, list, sizeof(list), 0);
        fprintf(f, "%s = %s\n", actions[i].name, list);
    }
    fclose(f);
}

void input_reload_controls(void) {
    input_controls_defaults();

    FILE *f = fopen(CONTROLS_PATH, "r");
    if (!f) {
        input_controls_save();
        l_info("input: generated default %s", CONTROLS_PATH);
        return;
    }

    uint32_t parsed[ACT_COUNT] = {0};
    int seen[ACT_COUNT] = {0};
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == '\0')
            continue;
        char *eq = strpbrk(p, "=:");
        if (!eq)
            continue;
        *eq = '\0';
        char *right = eq + 1;

        char key[32];
        word(p, key, sizeof(key));
        if (strcmp(key, "VERSION") == 0)
            continue;

        int act = find_action_index(p);
        if (act >= 0) {
            seen[act] = 1;
            parsed[act] |= parse_button_list(right);
        } else {
            act = find_action_index(right);
            uint32_t btn = parse_button_token(p);
            if (act >= 0 && btn) {
                seen[act] = 1;
                parsed[act] |= btn;
            }
        }
    }
    fclose(f);

    for (int i = 0; i < ACT_COUNT; i++)
        if (seen[i])
            actions[i].buttons = parsed[i];
    for (int i = 0; i < ACT_COUNT; i++)
        l_info("input: %-10s -> 0x%08X", actions[i].name, actions[i].buttons);
}

void input_apply_settings(void) {
    if (setting_steering == STEERING_MOTION)
        sceMotionStartSampling();
}
