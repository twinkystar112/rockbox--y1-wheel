/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (c) 2011 Michael Sevakis
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "system.h"
#include "settings.h"
#include "pcm.h"
#include "pcm_mixer.h"
#include "misc.h"
#include "fixedpoint.h"

/** Beep generation, CPU optimized **/
#include "asm/beep.c"

static uint32_t beep_phase;     /* Phase of square wave generator */
static uint32_t beep_step;      /* Step of square wave generator on each sample */
#ifdef BEEP_GENERIC
static int16_t  beep_amplitude; /* Amplitude of square wave generator */
#else
/* Optimized routines do XOR with phase sign bit in both channels at once */
static uint32_t beep_amplitude; /* Amplitude of square wave generator */
#endif
static int beep_count;          /* Number of samples remaining to generate */

#define BEEP_COUNT(fs, duration) ((fs) / 1000 * (duration))

/* Reserve enough static space for keyclick to fit in worst case */
#define BEEP_BUF_COUNT  BEEP_COUNT(PLAY_SAMPR_MAX, KEYCLICK_DURATION)
static int16_t beep_buf[BEEP_BUF_COUNT*2] IBSS_ATTR __attribute__((aligned(4)));

/* Callback to generate the beep frames - also don't want inlining of
   call below in beep_play */
static void __attribute__((noinline))
beep_get_more(const void **start, size_t *size)
{
    int count = beep_count;

    if (count > 0)
    {
        count = MIN(count, BEEP_BUF_COUNT);
        beep_count -= count;
        *start = beep_buf;
        *size = count * 2 * sizeof (int16_t);
        beep_generate((void *)beep_buf, count, &beep_phase,
                      beep_step, beep_amplitude);
    }
}

/* Generates a constant square wave sound with a given frequency in Hertz for
   a duration in milliseconds */
void beep_play(unsigned int frequency, unsigned int duration,
               unsigned int amplitude)
{
    mixer_channel_stop(PCM_MIXER_CHAN_BEEP);

    if (frequency == 0 || duration == 0 || amplitude == 0)
        return;

    if (amplitude > INT16_MAX)
        amplitude = INT16_MAX;

    /* Setup the parameters for the square wave generator */
    uint32_t fout = mixer_get_frequency();
    beep_phase = 0;
    beep_step = fp_div(frequency, fout, 32);
    beep_count = BEEP_COUNT(fout, duration);

#ifdef BEEP_GENERIC
    beep_amplitude = amplitude;
#else
    /* Optimized routines do XOR with phase sign bit in both channels at once */
    beep_amplitude = amplitude | (amplitude << 16); /* Word:|AMP16|AMP16| */
#endif

    /* If it fits - avoid cb overhead */
    const void *start;
    size_t size;

    /* Generate first frame here */
    beep_get_more(&start, &size);

    mixer_channel_set_amplitude(PCM_MIXER_CHAN_BEEP, MIX_AMP_UNITY);
    static struct mixer_play_cbs cbs;
    cbs.get_more = beep_count ? beep_get_more : NULL;
    mixer_channel_play_data(PCM_MIXER_CHAN_BEEP, &cbs, start, size);
}

/* Y1 Wheel: shaped click sounds for the wheel (Settings > Appearance > Wheel
 * and click): 1 clack, 2 bubble, 3 drop, 4 wood. Generated once per click
 * into their own buffer, short enough to fit. */
#define CLICK_MS 24
static int16_t click_buf[(PLAY_SAMPR_MAX / 1000) * CLICK_MS * 2] __attribute__((aligned(4)));

void beep_play_click(int sound, unsigned int amplitude)
{
    mixer_channel_stop(PCM_MIXER_CHAN_BEEP);
    if (sound <= 0 || amplitude == 0)
        return;
    if (amplitude > INT16_MAX)
        amplitude = INT16_MAX;
    uint32_t fs = mixer_get_frequency();
    /* length, start/end pitch (Hz), decay (1/1000 per ms), second partial */
    static const struct { short ms, f0, f1, decay, f2; } shapes[5] = {
        { 0, 0, 0, 0, 0 },
        { 7, 2600, 1900, 380, 5200 },   /* clack: a bright little tap */
        { 22, 700, 1900, 120, 0 },      /* bubble: rising, round */
        { 18, 2300, 650, 150, 0 },      /* drop: falling */
        { 14, 1100, 1050, 260, 2750 },  /* wood: hollow block */
    };
    int s = sound < 5 ? sound : 1;
    int n = (int)(fs / 1000) * shapes[s].ms;
    if (n > (int)(ARRAYLEN(click_buf) / 2))
        n = ARRAYLEN(click_buf) / 2;
    uint32_t phase = 0, phase2 = 0;
    long env = 0x7fffffff;                 /* Q31 */
    /* per-sample envelope factor for "decay per ms" */
    long per_ms = 0x7fffffff - (long)((0x7fffffffLL * shapes[s].decay) / 1000);
    int spm = fs / 1000;
    for (int i = 0; i < n; i++)
    {
        int f = shapes[s].f0 + (shapes[s].f1 - shapes[s].f0) * i / (n ? n : 1);
        phase += (uint32_t)(((uint64_t)f << 32) / fs);
        long cosv;
        long v = fp_sincos(phase, &cosv) >> 1;
        if (shapes[s].f2)
        {
            phase2 += (uint32_t)(((uint64_t)shapes[s].f2 << 32) / fs);
            v += fp_sincos(phase2, &cosv) >> 2;
        }
        /* a 0.3 ms attack, then the decay */
        long a = i < spm / 3 ? (long)((0x7fffffffLL * i) / (spm / 3 ? spm / 3 : 1)) : env;
        if (i >= spm / 3 && spm && (i % spm) == 0)
            env = (long)(((long long)env * per_ms) >> 31);
        int32_t out = (int32_t)((((long long)v * a) >> 31) * (long long)amplitude >> 30);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < -INT16_MAX) out = -INT16_MAX;
        click_buf[i * 2] = click_buf[i * 2 + 1] = out;
    }
    mixer_channel_set_amplitude(PCM_MIXER_CHAN_BEEP, MIX_AMP_UNITY);
    mixer_channel_play_data(PCM_MIXER_CHAN_BEEP, NULL, click_buf, n * 2 * sizeof(int16_t));
}
