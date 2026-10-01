/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  audio.c
 * @brief Native replacement for com.polarbit.fuse.FuseAudio.
 *
 * The engine owns the mixer (PAudioPlayer) but not the output: JNIManager
 * calls FuseAudio.AudioCreate(rate, channels, bits, mixbufsize) and
 * AudioStart(), and the Java class then pulls PCM from
 * Jni.AudioMix(ByteBuffer, frames) every time its AudioTrack asks for a period
 * (decompiled/apk_jadx/.../FuseAudio.java onPeriodicNotification()). The
 * native side mixes frames * bytes_per_frame bytes into the buffer it gets
 * from env->GetDirectBufferAddress() (Java_com_polarbit_fuse_Jni_AudioMix at
 * 0x209b58). This file is that pump with SceAudioOut instead of AudioTrack:
 * sceAudioOutOutput() blocks until the previous granule played, which paces
 * the thread like AudioTrack.write() does.
 *
 * FalsoJNI has no java.nio, so the "direct ByteBuffer" is raw memory and
 * main.c makes GetDirectBufferAddress() return the object itself.
 */

#include "reimpl/audio.h"
#include "utils/logger.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern so_module so_mod;

// FuseAudio's notification period is getMinBufferSize() frames (~20-45 ms);
// 512 frames is 23 ms at 22050 Hz. Must be a multiple of 64 for SceAudioOut.
#define GRAIN 512

static void (*AudioMix)(JNIEnv *env, jclass clazz, jobject buf, jint frames);

static SceUID audio_thid = -1;
static volatile int audio_running = 0;  // pump thread alive
static volatile int audio_playing = 0;  // AudioStart() .. AudioStop()
static volatile int audio_muted = 0;
static volatile int audio_volume = SCE_AUDIO_VOLUME_0DB;
static volatile int audio_volume_dirty = 1;

static int a_rate, a_channels, a_bits;

static int valid_rate(int rate) {
    switch (rate) {
        case 8000: case 11025: case 12000: case 16000: case 22050:
        case 24000: case 32000: case 44100: case 48000:
            return 1;
        default:
            return 0;
    }
}

static int audio_thread(SceSize args, void *argp) {
    int bpf = a_channels * a_bits / 8;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, a_rate,
                                   a_channels == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                                   : SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("audio: sceAudioOutOpenPort(%d, %d Hz, %d ch) failed: 0x%08X",
                GRAIN, a_rate, a_channels, port);
        audio_running = 0;
        return sceKernelExitDeleteThread(0);
    }

    // mix: what AudioMix() writes (bits as created); out: S16 for SceAudioOut.
    uint8_t *mix = calloc(GRAIN, bpf);
    int16_t *out = calloc(GRAIN * a_channels, sizeof(int16_t));
    if (!mix || !out) {
        l_error("audio: out of memory");
        goto done;
    }

    l_info("audio: SceAudioOut port %d up: %d Hz, %d ch, %d bit, %d frames/granule",
           port, a_rate, a_channels, a_bits, GRAIN);

    while (audio_running) {
        if (audio_volume_dirty) {
            int vol[2] = { audio_volume, audio_volume };
            sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
            audio_volume_dirty = 0;
        }

        if (audio_playing && !audio_muted) {
            AudioMix(&jni, NULL, (jobject) mix, GRAIN);
            if (a_bits == 16) {
                memcpy(out, mix, GRAIN * bpf);
            } else { // 8-bit PCM is unsigned
                for (int i = 0; i < GRAIN * a_channels; i++)
                    out[i] = (int16_t) ((mix[i] - 128) << 8);
            }
        } else {
            memset(out, 0, GRAIN * a_channels * sizeof(int16_t));
        }

        sceAudioOutOutput(port, out);
    }

done:
    sceAudioOutOutput(port, NULL);
    sceAudioOutReleasePort(port);
    free(mix);
    free(out);
    l_info("audio: thread finished");
    return sceKernelExitDeleteThread(0);
}

int audio_create(int rate, int channels, int bits, int mixbufsize) {
    l_info("FuseAudio.AudioCreate(rate %d, channels %d, bits %d, mixbuf %d)",
           rate, channels, bits, mixbufsize);

    if (audio_running) // created twice: keep the running port
        return 0;

    if (!AudioMix)
        AudioMix = (void *) so_symbol(&so_mod, "Java_com_polarbit_fuse_Jni_AudioMix");
    if (!AudioMix) {
        l_error("audio: Java_com_polarbit_fuse_Jni_AudioMix not found");
        return 0;
    }
    if (!valid_rate(rate) || (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
        l_error("audio: format unsupported by SceAudioOut, audio disabled");
        return 0;
    }

    a_rate = rate;
    a_channels = channels;
    a_bits = bits;

    audio_running = 1;
    // AudioTrack callbacks run on a high priority thread; stay above the game.
    audio_thid = sceKernelCreateThread("fuse_audio", audio_thread, 0x10000100 - 10,
                                       64 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (audio_thid < 0) {
        l_error("audio: sceKernelCreateThread failed: 0x%08X", audio_thid);
        audio_running = 0;
        return 0;
    }
    sceKernelStartThread(audio_thid, 0, NULL);
    return 0;
}

int audio_start(void) {
    l_info("FuseAudio.AudioStart");
    if (audio_playing)
        return 0;
    audio_playing = 1;
    return 1;
}

void audio_stop(void) {
    l_info("FuseAudio.AudioStop");
    audio_playing = 0;
}

void audio_set_volume(int left, int right) {
    // Java: setStereoVolume(left * (max - min) / 100 + min) on both channels.
    if (left < 0) left = 0;
    if (left > 100) left = 100;
    audio_volume = SCE_AUDIO_VOLUME_0DB * left / 100;
    audio_volume_dirty = 1;
}

void audio_set_muted(int muted) {
    audio_muted = muted;
}

void audio_shutdown(void) {
    if (!audio_running)
        return;
    audio_running = 0;
    sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
    audio_thid = -1;
}
