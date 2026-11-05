/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - the network layer on Android: org.rockbox.Helper.SonoraNet.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
#include <jni.h>
#include <stdbool.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "debug.h"
#include "sonora/sonora.h"

extern JNIEnv *env_ptr;

static jclass cls;
static jmethodID m_supported, m_begin, m_begin_state, m_end, m_start, m_poll, m_cancel;

static void clear_exc(void)
{
    if ((*env_ptr)->ExceptionCheck(env_ptr))
    {
        (*env_ptr)->ExceptionDescribe(env_ptr);
        (*env_ptr)->ExceptionClear(env_ptr);
    }
}

static bool init(void)
{
    if (cls)
        return true;
    JNIEnv e = *env_ptr;
    jclass local = e->FindClass(env_ptr, "org/rockbox/Helper/SonoraNet");
    if (!local)
    {
        clear_exc();
        DEBUGF("sonora: SonoraNet not found\n");
        return false;
    }
    cls = e->NewGlobalRef(env_ptr, local);
    e->DeleteLocalRef(env_ptr, local);
    m_supported = e->GetStaticMethodID(env_ptr, cls, "supported", "()Z");
    m_begin     = e->GetStaticMethodID(env_ptr, cls, "beginAsync", "()V");
    m_begin_state = e->GetStaticMethodID(env_ptr, cls, "beginState", "()I");
    m_end       = e->GetStaticMethodID(env_ptr, cls, "end", "()V");
    m_start     = e->GetStaticMethodID(env_ptr, cls, "start", "(Ljava/lang/String;Ljava/lang/String;)Z");
    m_poll      = e->GetStaticMethodID(env_ptr, cls, "poll", "()[J");
    m_cancel    = e->GetStaticMethodID(env_ptr, cls, "cancel", "()V");
    clear_exc();
    if (!m_supported || !m_begin || !m_begin_state || !m_end || !m_start || !m_poll || !m_cancel)
    {
        cls = NULL;
        return false;
    }
    return true;
}

bool sonora_net_supported(void)
{
    if (!init())
        return false;
    bool r = (*env_ptr)->CallStaticBooleanMethod(env_ptr, cls, m_supported);
    clear_exc();
    return r;
}

bool sonora_net_begin(void)
{
    if (!init())
        return false;
    /* polled, so playback keeps decoding while Wi-Fi comes up */
    (*env_ptr)->CallStaticVoidMethod(env_ptr, cls, m_begin);
    clear_exc();
    for (int waited = 0; waited < 45 * HZ; waited += HZ / 5)
    {
        sleep(HZ / 5);
        int state = (*env_ptr)->CallStaticIntMethod(env_ptr, cls, m_begin_state);
        clear_exc();
        if (state != 0)
            return state == 1;
    }
    return false;
}

void sonora_net_end(void)
{
    if (!init())
        return;
    (*env_ptr)->CallStaticVoidMethod(env_ptr, cls, m_end);
    clear_exc();
}

int sonora_net_fetch(const char *url, const char *out_path,
                     bool (*progress)(long long done, long long total, void *ctx),
                     void *ctx)
{
    if (!init())
        return -1;
    JNIEnv e = *env_ptr;
    jstring ju = e->NewStringUTF(env_ptr, url);
    jstring jo = e->NewStringUTF(env_ptr, out_path);
    bool ok = e->CallStaticBooleanMethod(env_ptr, cls, m_start, ju, jo);
    clear_exc();
    e->DeleteLocalRef(env_ptr, ju);
    e->DeleteLocalRef(env_ptr, jo);
    if (!ok)
        return -1;
    while (1)
    {
        sleep(HZ / 5);
        jlongArray arr = (jlongArray)e->CallStaticObjectMethod(env_ptr, cls, m_poll);
        clear_exc();
        if (!arr)
            return -1;
        jlong v[4];
        e->GetLongArrayRegion(env_ptr, arr, 0, 4, v);
        e->DeleteLocalRef(env_ptr, arr);
        if (v[0] != 0) /* not running any more */
            return v[0] == 1 ? 200 : (v[3] > 0 ? (int)v[3] : -1);
        if (progress && !progress(v[1], v[2], ctx))
        {
            e->CallStaticVoidMethod(env_ptr, cls, m_cancel);
            clear_exc();
        }
    }
}
