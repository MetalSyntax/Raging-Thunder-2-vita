/*
 * Copyright (C) 2026 Raging Thunder 2 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  audio.h
 * @brief Native replacement for com.polarbit.fuse.FuseAudio (AudioTrack pump
 *        around Jni.AudioMix) on top of SceAudioOut.
 */

#ifndef SOLOADER_AUDIO_H
#define SOLOADER_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/** FuseAudio.AudioCreate(rate, channels, bits, mixbufsize). Returns 0. */
int audio_create(int rate, int channels, int bits, int mixbufsize);

/** FuseAudio.AudioStart(). 1 if it started now, 0 if already running. */
int audio_start(void);

/** FuseAudio.AudioStop(): stop pulling from the mixer (port stays open). */
void audio_stop(void);

/** FuseAudio.AudioSetVolume(left, right), percent (Java only uses left). */
void audio_set_volume(int left, int right);

/** Output silence without stopping the mixer (app suspended). */
void audio_set_muted(int muted);

/** Stop the pump thread and release the port (app exit). */
void audio_shutdown(void);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_AUDIO_H
