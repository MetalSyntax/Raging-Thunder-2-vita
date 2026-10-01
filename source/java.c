/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  java.c
 * @brief FalsoJNI implementation of the Java side of the Polarbit Fuse engine.
 *
 * librthunder2lite.so resolves every Java method it ever calls from one table,
 * `JniTable` (.data, 0x2a1ea4, 54 entries of {class, name, signature, jclass,
 * jmethodID, isStatic}), in Java_com_polarbit_fuse_MainTask_FuseOnInit ->
 * JNIManager::InitJni(). The entries below are exactly that table (dumped from
 * the real .so), with the behaviour of the matching class in
 * decompiled/apk_jadx/sources/com/polarbit/fuse/.
 *
 * FalsoJNI matches methods by name only (constructors as "<class>/<init>"), and
 * all 54 names are unique.
 *
 * Nothing here dereferences the Java "objects": the engine only keeps them as
 * Call*Method receivers (see main.c for the placeholders it gets).
 */

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/system_param.h>

#include <stdio.h>
#include <string.h>

#include "java.h"
#include "reimpl/audio.h"
#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/settings.h"

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.FuseEgl                                                */
/* ------------------------------------------------------------------------ */

// EglCreate(int[] attribs): vitaGL is already up (main.c, gl_init()) before
// OnCreate, so there is nothing to create. The attribute list is the one
// PAndroidDisplay::Init() builds (RGB565, depth 16, EGL_RENDERABLE_TYPE only
// for GLES2) -- logged for reference, vitaGL's framebuffer is fixed.
jboolean FuseEgl_EglCreate(jmethodID id, va_list args) {
    jintArray arr = va_arg(args, jintArray);
    int len = arr ? jni->GetArrayLength(&jni, arr) : 0;
    jint *v = arr ? jni->GetIntArrayElements(&jni, arr, NULL) : NULL;
    char buf[256] = {0};
    size_t off = 0;
    for (int i = 0; v && i + 1 < len && off < sizeof(buf) - 24; i += 2) {
        if (v[i] == 0x3038) break; // EGL_NONE
        off += snprintf(buf + off, sizeof(buf) - off, " %x=%d", v[i], v[i + 1]);
    }
    if (v) jni->ReleaseIntArrayElements(&jni, arr, v, JNI_ABORT);
    l_info("FuseEgl.EglCreate:%s", buf);
    return JNI_TRUE;
}

// Called by PAndroidDisplay::Update() once per engine frame: the buffer swap.
jboolean FuseEgl_EglUpdateDisplay(jmethodID id, va_list args) {
    gl_swap();
    return JNI_TRUE;
}

// Surface re-creation after pause/resize. vitaGL keeps its context.
jboolean FuseEgl_Activate(jmethodID id, va_list args) {
    int on = va_arg(args, int);
    l_debug("FuseEgl.Activate(%d)", on);
    return JNI_TRUE;
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.FuseSystem                                             */
/* ------------------------------------------------------------------------ */

jobject FuseSystem_GetIMEI(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "PSVITA00000000");
}

jobject FuseSystem_GetPhoneNr(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "null");
}

// The engine uses the *country* code as the language code (GetLanguageJNI():
// "fr" 1, "de" 2, "it" 3, "es" 4, "sv" 5, "zh" 0x16, "ja" 0x17, "ko" 0x30,
// anything else English). Only the European languages are offered: the game
// ships no CJK font.
static const char *language_code(void) {
    static const char *codes[] = { NULL, "us", "fr", "de", "it", "es", "sv" };
    if (setting_language > 0)
        return codes[setting_language];

    int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
    switch (lang) {
        case SCE_SYSTEM_PARAM_LANG_FRENCH:  return "fr";
        case SCE_SYSTEM_PARAM_LANG_GERMAN:  return "de";
        case SCE_SYSTEM_PARAM_LANG_ITALIAN: return "it";
        case SCE_SYSTEM_PARAM_LANG_SPANISH: return "es";
        case SCE_SYSTEM_PARAM_LANG_SWEDISH: return "sv";
        default:                            return "us";
    }
}

jobject FuseSystem_GetCountryCode(jmethodID id, va_list args) {
    const char *code = language_code();
    l_info("FuseSystem.GetCountryCode -> %s", code);
    return jni->NewStringUTF(&jni, code);
}

jobject FuseSystem_GetOperatorName(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "null");
}

// CApplication::DeterminePlatform() looks for "ipad"/"iphone"/"android" in
// the model name and ends up with the same platform (4) either way.
jobject FuseSystem_GetModelName(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "PS Vita");
}

jobject FuseSystem_GetPlatformVersion(jmethodID id, va_list args) {
    return jni->NewStringUTF(&jni, "19");
}

void FuseSystem_LaunchUrl(jmethodID id, va_list args) {
    jstring url = va_arg(args, jstring);
    const char *s = url ? jni->GetStringUTFChars(&jni, url, NULL) : NULL;
    l_info("FuseSystem.LaunchUrl(%s): no browser on this port", s ? s : "(null)");
    if (s) jni->ReleaseStringUTFChars(&jni, url, s);
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.FuseAudio -> reimpl/audio.c                            */
/* ------------------------------------------------------------------------ */

void FuseAudio_AudioStop(jmethodID id, va_list args) {
    audio_stop();
}

jint FuseAudio_AudioStart(jmethodID id, va_list args) {
    return audio_start();
}

jint FuseAudio_AudioCreate(jmethodID id, va_list args) {
    int rate = va_arg(args, int);
    int channels = va_arg(args, int);
    int bits = va_arg(args, int);
    int mixbufsize = va_arg(args, int);
    return audio_create(rate, channels, bits, mixbufsize);
}

jint FuseAudio_AudioGetVolume(jmethodID id, va_list args) {
    return 1; // FuseAudio.AudioGetVolume() always answers 1
}

void FuseAudio_AudioSetVolume(jmethodID id, va_list args) {
    int left = va_arg(args, int);
    int right = va_arg(args, int);
    audio_set_volume(left, right);
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.FuseSensor                                             */
/* ------------------------------------------------------------------------ */

volatile int accelerometer_enabled = 0;

// Returns mHasSensor && mSensorOn. The accelerometer always "exists" here
// (main.c announces it with OnEvent(4,0,...) like the FuseSensor constructor):
// it is fed by the left stick or by the Vita's motion sensor (input.c).
//
// JNIManager::JniSensorActivate (0x204528) never forwards its argument: it
// reuses r1 for InitJni(15) and calls CallBooleanMethod with whatever is left
// in r3 (an address inside the .so). The JVM only keeps the low byte of a
// jboolean, which is non-zero there, so on Android the sensor always ends up
// enabled. Do the same.
jboolean FuseSensor_ActivateAccelerometer(jmethodID id, va_list args) {
    int on = (va_arg(args, int) & 0xff) != 0;
    if (accelerometer_enabled != on)
        l_info("FuseSensor.ActivateAccelerometer(%d)", on);
    accelerometer_enabled = on;
    return on ? JNI_TRUE : JNI_FALSE;
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.license.Drm                                            */
/* ------------------------------------------------------------------------ */

static int drm_placeholder;

jobject Drm_init(jmethodID id, va_list args) {
    return (jobject) &drm_placeholder;
}

// Google Play licensing of the (free) app. The native side never starts a
// check (PDrm::startCheck() returns 0); if it ever does, answer LICENSED the
// way MyLicenseCheckerCallback.allow() does: OnEvent(6, 44, 0, 0, 0).
jint Drm_doLicenseCheck(jmethodID id, va_list args) {
    l_info("Drm.doLicenseCheck -> LICENSED");
    fuse_queue_event(6, 44, 0, 0, 0);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.input.InputDialog -> Vita IME (main.c polls it)        */
/* ------------------------------------------------------------------------ */

static int input_dialog_placeholder;
static char input_dialog_title[128];
static char input_dialog_text[256];

// InputDialog.ResultCode: None 0, Ok 1, Cancel 2
static int input_dialog_status = 0;
static char input_dialog_result[SCE_IME_DIALOG_MAX_TEXT_LENGTH * 3 + 1];
static int input_dialog_active = 0;

static void copy_jstring(char *dst, size_t size, jstring js) {
    dst[0] = '\0';
    if (!js) return;
    const char *s = jni->GetStringUTFChars(&jni, js, NULL);
    if (s) {
        snprintf(dst, size, "%s", s);
        jni->ReleaseStringUTFChars(&jni, js, s);
    }
}

// <init>(Activity, int type, String title, String msg, String but1,
//        String but2, String text)
jobject InputDialog_init(jmethodID id, va_list args) {
    va_arg(args, jobject); // activity
    int type = va_arg(args, int);
    jstring title = va_arg(args, jstring);
    jstring msg = va_arg(args, jstring);
    va_arg(args, jstring); // but1
    va_arg(args, jstring); // but2
    jstring text = va_arg(args, jstring);

    copy_jstring(input_dialog_title, sizeof(input_dialog_title), title);
    if (!input_dialog_title[0])
        copy_jstring(input_dialog_title, sizeof(input_dialog_title), msg);
    copy_jstring(input_dialog_text, sizeof(input_dialog_text), text);
    input_dialog_status = 0;
    l_info("InputDialog(type %d, \"%s\", \"%s\")", type, input_dialog_title, input_dialog_text);
    return (jobject) &input_dialog_placeholder;
}

jint InputDialog_Show(jmethodID id, va_list args) {
    input_dialog_status = 0;
    if (init_ime_dialog(input_dialog_title, input_dialog_text) < 0) {
        l_error("InputDialog.Show: sceImeDialogInit failed");
        input_dialog_status = 2;
        fuse_queue_message(2, 2, input_dialog_status, 0, input_dialog_text);
        return 1;
    }
    input_dialog_active = 1;
    return 1;
}

jint InputDialog_GetStatus(jmethodID id, va_list args) {
    return input_dialog_status;
}

jobject InputDialog_GetMessage(jmethodID id, va_list args) {
    if (!input_dialog_status)
        return NULL;
    return jni->NewStringUTF(&jni, input_dialog_result);
}

int java_dialog_active(void) {
    return input_dialog_active;
}

// Called every frame by main.c while the IME is up. On close, reports the
// result like InputDialog.onEvent(): OnEventMessage(2, 2, status, 0, text).
void java_dialog_update(void) {
    if (!input_dialog_active)
        return;
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return;

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    int ok = result.button == SCE_IME_DIALOG_BUTTON_ENTER;
    char *text = get_ime_dialog_result(); // terminates the dialog
    snprintf(input_dialog_result, sizeof(input_dialog_result), "%s",
             ok && text ? text : input_dialog_text);
    input_dialog_status = ok ? 1 : 2;
    input_dialog_active = 0;
    l_info("InputDialog closed: status %d, \"%s\"", input_dialog_status, input_dialog_result);
    fuse_queue_message(2, 2, input_dialog_status, 0, input_dialog_result);
}

/* ------------------------------------------------------------------------ */
/* Facebook, ads, web view, media: not available on Vita                    */
/* ------------------------------------------------------------------------ */

static int generic_placeholder;

jobject Generic_init(jmethodID id, va_list args) {
    return (jobject) &generic_placeholder;
}

void Generic_void(jmethodID id, va_list args) {
}

jint Generic_int0(jmethodID id, va_list args) {
    return 0;
}

jint FuseWebView_open(jmethodID id, va_list args) {
    jstring url = va_arg(args, jstring);
    const char *s = url ? jni->GetStringUTFChars(&jni, url, NULL) : NULL;
    l_info("FuseWebView.open(%s): not available", s ? s : "(null)");
    if (s) jni->ReleaseStringUTFChars(&jni, url, s);
    return 0;
}

jint Media_play(jmethodID id, va_list args) {
    jstring path = va_arg(args, jstring);
    const char *s = path ? jni->GetStringUTFChars(&jni, path, NULL) : NULL;
    l_info("Media.play(%s): video playback not available", s ? s : "(null)");
    if (s) jni->ReleaseStringUTFChars(&jni, path, s);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.billing.InAppBilling                                   */
/* ------------------------------------------------------------------------ */

// The only IAP is the optional "unlock everything" shortcut; the whole game is
// playable with in-game credits. There is no store on Vita, so every request
// is answered like a failed one (BillingState_Failed = 3), which is what the
// Java side does when Google Play billing is unavailable:
// OnEventMessage2(2, 3, state, sku, message).
static void billing_fail(const char *what, va_list args) {
    va_arg(args, jobject); // context
    jstring sku = va_arg(args, jstring);
    char buf[128];
    copy_jstring(buf, sizeof(buf), sku);
    l_info("InAppBilling.%s(%s): no store on Vita, failing", what, buf);
    fuse_queue_message2(2, 3, 3, buf, "In-app purchases are not available");
}

void InAppBilling_initiatePurchase(jmethodID id, va_list args) { billing_fail("initiatePurchase", args); }
void InAppBilling_refreshPurchases(jmethodID id, va_list args) { billing_fail("refreshPurchases", args); }
void InAppBilling_infoPurchase(jmethodID id, va_list args) { billing_fail("infoPurchase", args); }

/* ------------------------------------------------------------------------ */
/* com.polarbit.fuse.Fuse                                                   */
/* ------------------------------------------------------------------------ */

void Fuse_TelemetrySubmit(jmethodID id, va_list args) {
    jstring s = va_arg(args, jstring);
    char buf[128];
    copy_jstring(buf, sizeof(buf), s);
    l_debug("Fuse.TelemetrySubmit(%s)", buf);
}

/* ------------------------------------------------------------------------ */
/* Tables                                                                   */
/* ------------------------------------------------------------------------ */

enum {
    M_EglCreate = 1, M_EglUpdateDisplay, M_Activate,
    M_GetIMEI, M_GetPhoneNr, M_GetCountryCode, M_GetOperatorName,
    M_GetModelName, M_GetPlatformVersion, M_LaunchUrl,
    M_AudioStop, M_AudioStart, M_AudioCreate, M_AudioGetVolume, M_AudioSetVolume,
    M_ActivateAccelerometer,
    M_Drm_init, M_doLicenseCheck,
    M_InputDialog_init, M_Show, M_GetStatus, M_GetMessage,
    M_FuseFaceBook_init, M_Login, M_Logout, M_PostOnWall, M_Like, M_GetFriends,
    M_AdViewMain_init, M_getWidth, M_getHeight, M_createAdView, M_showAdView,
    M_hideAdView, M_refreshAdView, M_switchStream, M_postDisableAdsMessage,
    M_InterstitialManager_init, M_initInterstitialManager, M_showInterstitial,
    M_InAppBilling_init, M_initiatePurchase, M_stopPurchase, M_refreshPurchases,
    M_infoPurchase,
    M_Media_init, M_play, M_stop, M_pause, M_resume,
    M_FuseWebView_init, M_open, M_close,
    M_TelemetrySubmit,
};

NameToMethodID nameToMethodId[] = {
    { M_EglCreate,             "EglCreate",               METHOD_TYPE_BOOLEAN },
    { M_EglUpdateDisplay,      "EglUpdateDisplay",        METHOD_TYPE_BOOLEAN },
    { M_Activate,              "Activate",                METHOD_TYPE_BOOLEAN },
    { M_GetIMEI,               "GetIMEI",                 METHOD_TYPE_OBJECT },
    { M_GetPhoneNr,            "GetPhoneNr",              METHOD_TYPE_OBJECT },
    { M_GetCountryCode,        "GetCountryCode",          METHOD_TYPE_OBJECT },
    { M_GetOperatorName,       "GetOperatorName",         METHOD_TYPE_OBJECT },
    { M_GetModelName,          "GetModelName",            METHOD_TYPE_OBJECT },
    { M_GetPlatformVersion,    "GetPlatformVersion",      METHOD_TYPE_OBJECT },
    { M_LaunchUrl,             "LaunchUrl",               METHOD_TYPE_VOID },
    { M_AudioStop,             "AudioStop",               METHOD_TYPE_VOID },
    { M_AudioStart,            "AudioStart",              METHOD_TYPE_INT },
    { M_AudioCreate,           "AudioCreate",             METHOD_TYPE_INT },
    { M_AudioGetVolume,        "AudioGetVolume",          METHOD_TYPE_INT },
    { M_AudioSetVolume,        "AudioSetVolume",          METHOD_TYPE_VOID },
    { M_ActivateAccelerometer, "ActivateAccelerometer",   METHOD_TYPE_BOOLEAN },
    { M_Drm_init,              "com/polarbit/fuse/license/Drm/<init>", METHOD_TYPE_OBJECT },
    { M_doLicenseCheck,        "doLicenseCheck",          METHOD_TYPE_INT },
    { M_InputDialog_init,      "com/polarbit/fuse/input/InputDialog/<init>", METHOD_TYPE_OBJECT },
    { M_Show,                  "Show",                    METHOD_TYPE_INT },
    { M_GetStatus,             "GetStatus",               METHOD_TYPE_INT },
    { M_GetMessage,            "GetMessage",              METHOD_TYPE_OBJECT },
    { M_FuseFaceBook_init,     "com/polarbit/fuse/facebook/FuseFaceBook/<init>", METHOD_TYPE_OBJECT },
    { M_Login,                 "Login",                   METHOD_TYPE_VOID },
    { M_Logout,                "Logout",                  METHOD_TYPE_VOID },
    { M_PostOnWall,            "PostOnWall",              METHOD_TYPE_VOID },
    { M_Like,                  "Like",                    METHOD_TYPE_VOID },
    { M_GetFriends,            "GetFriends",              METHOD_TYPE_VOID },
    { M_AdViewMain_init,       "com/polarbit/fuse/ads/AdViewMain/<init>", METHOD_TYPE_OBJECT },
    { M_getWidth,              "getWidth",                METHOD_TYPE_INT },
    { M_getHeight,             "getHeight",               METHOD_TYPE_INT },
    { M_createAdView,          "createAdView",            METHOD_TYPE_VOID },
    { M_showAdView,            "showAdView",              METHOD_TYPE_VOID },
    { M_hideAdView,            "hideAdView",              METHOD_TYPE_VOID },
    { M_refreshAdView,         "refreshAdView",           METHOD_TYPE_VOID },
    { M_switchStream,          "switchStream",            METHOD_TYPE_VOID },
    { M_postDisableAdsMessage, "postDisableAdsMessage",   METHOD_TYPE_VOID },
    { M_InterstitialManager_init, "com/polarbit/fuse/ads/InterstitialManager/<init>", METHOD_TYPE_OBJECT },
    { M_initInterstitialManager, "initInterstitialManager", METHOD_TYPE_VOID },
    { M_showInterstitial,      "showInterstitial",        METHOD_TYPE_VOID },
    { M_InAppBilling_init,     "com/polarbit/fuse/billing/InAppBilling/<init>", METHOD_TYPE_OBJECT },
    { M_initiatePurchase,      "initiatePurchase",        METHOD_TYPE_VOID },
    { M_stopPurchase,          "stopPurchase",            METHOD_TYPE_VOID },
    { M_refreshPurchases,      "refreshPurchases",        METHOD_TYPE_VOID },
    { M_infoPurchase,          "infoPurchase",            METHOD_TYPE_VOID },
    { M_Media_init,            "com/polarbit/fuse/media/Media/<init>", METHOD_TYPE_OBJECT },
    { M_play,                  "play",                    METHOD_TYPE_INT },
    { M_stop,                  "stop",                    METHOD_TYPE_INT },
    { M_pause,                 "pause",                   METHOD_TYPE_INT },
    { M_resume,                "resume",                  METHOD_TYPE_INT },
    { M_FuseWebView_init,      "com/polarbit/fuse/FuseWebView/<init>", METHOD_TYPE_OBJECT },
    { M_open,                  "open",                    METHOD_TYPE_INT },
    { M_close,                 "close",                   METHOD_TYPE_VOID },
    { M_TelemetrySubmit,       "TelemetrySubmit",         METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { M_EglCreate,             FuseEgl_EglCreate },
    { M_EglUpdateDisplay,      FuseEgl_EglUpdateDisplay },
    { M_Activate,              FuseEgl_Activate },
    { M_ActivateAccelerometer, FuseSensor_ActivateAccelerometer },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { M_AudioStart,            FuseAudio_AudioStart },
    { M_AudioCreate,           FuseAudio_AudioCreate },
    { M_AudioGetVolume,        FuseAudio_AudioGetVolume },
    { M_doLicenseCheck,        Drm_doLicenseCheck },
    { M_Show,                  InputDialog_Show },
    { M_GetStatus,             InputDialog_GetStatus },
    { M_getWidth,              Generic_int0 },
    { M_getHeight,             Generic_int0 },
    { M_play,                  Media_play },
    { M_stop,                  Generic_int0 },
    { M_pause,                 Generic_int0 },
    { M_resume,                Generic_int0 },
    { M_open,                  FuseWebView_open },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { M_GetIMEI,               FuseSystem_GetIMEI },
    { M_GetPhoneNr,            FuseSystem_GetPhoneNr },
    { M_GetCountryCode,        FuseSystem_GetCountryCode },
    { M_GetOperatorName,       FuseSystem_GetOperatorName },
    { M_GetModelName,          FuseSystem_GetModelName },
    { M_GetPlatformVersion,    FuseSystem_GetPlatformVersion },
    { M_Drm_init,              Drm_init },
    { M_InputDialog_init,      InputDialog_init },
    { M_GetMessage,            InputDialog_GetMessage },
    { M_FuseFaceBook_init,     Generic_init },
    { M_AdViewMain_init,       Generic_init },
    { M_InterstitialManager_init, Generic_init },
    { M_InAppBilling_init,     Generic_init },
    { M_Media_init,            Generic_init },
    { M_FuseWebView_init,      Generic_init },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { M_LaunchUrl,             FuseSystem_LaunchUrl },
    { M_AudioStop,             FuseAudio_AudioStop },
    { M_AudioSetVolume,        FuseAudio_AudioSetVolume },
    { M_Login,                 Generic_void },
    { M_Logout,                Generic_void },
    { M_PostOnWall,            Generic_void },
    { M_Like,                  Generic_void },
    { M_GetFriends,            Generic_void },
    { M_createAdView,          Generic_void },
    { M_showAdView,            Generic_void },
    { M_hideAdView,            Generic_void },
    { M_refreshAdView,         Generic_void },
    { M_switchStream,          Generic_void },
    { M_postDisableAdsMessage, Generic_void },
    { M_initInterstitialManager, Generic_void },
    { M_showInterstitial,      Generic_void },
    { M_initiatePurchase,      InAppBilling_initiatePurchase },
    { M_stopPurchase,          Generic_void },
    { M_refreshPurchases,      InAppBilling_refreshPurchases },
    { M_infoPurchase,          InAppBilling_infoPurchase },
    { M_close,                 Generic_void },
    { M_TelemetrySubmit,       Fuse_TelemetrySubmit },
};

NameToFieldID nameToFieldId[] = {};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {};
FieldsObject fieldsObject[] = {};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
