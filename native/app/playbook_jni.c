/*
 * HakuXP app front end - playbooks (native/dev/padplay.h) for the activity:
 * the core's playbook=, record= and playrun= options, and PlaybookNative's
 * JNI (android/.../playbook/PlaybookNative.kt): status, stop, mark, the
 * PC's debug.hakux.playbook commands and debug.hakux.automation.
 */
#include "native/app/app.h"
#include "native/core/nc.h"
#include "native/dev/padplay.h"
#include "native/dev/padstate.h"
#include "native/state/save.h"
#include "hw/xbox/hle/prop.h"

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

#define EXPORT __attribute__((visibility("default")))
#define TAG "hakuX-native-playbook"

static char g_play[512], g_record[512], g_run[512], g_ids[40];
static unsigned g_segment;
static char g_failed[160];      /* why the playbook couldn't play */

bool nc_app_playbook_arg(const char *a)
{
    if (!strncmp(a, "playbook=", 9)) {
        snprintf(g_play, sizeof(g_play), "%s", a + 9);
    } else if (!strncmp(a, "record=", 7)) {
        snprintf(g_record, sizeof(g_record), "%s", a + 7);
    } else if (!strncmp(a, "playrun=", 8)) {
        snprintf(g_run, sizeof(g_run), "%s", a + 8);
    } else if (!strncmp(a, "playseg=", 8)) {
        g_segment = (unsigned)strtoul(a + 8, NULL, 10);    /* the part after a load */
    } else if (!strncmp(a, "playid=", 7)) {
        snprintf(g_ids, sizeof(g_ids), "%s", a + 7);        /* "<title ID>_<XMID>" */
    } else {
        return false;
    }
    return true;
}

void nc_app_playbook_begin(void)
{
    if ((g_play[0] || g_record[0]) && !g_run[0]) {
        nc_log(TAG, "playbook= and record= need playrun=: nothing started");
        snprintf(g_failed, sizeof(g_failed), "no run folder");
        return;
    }
    if (g_play[0]) {
        nc_padplay_play(g_play, g_run, g_segment, g_ids, g_failed, sizeof(g_failed));
    } else if (g_record[0] && !nc_padplay_record(g_record, g_run, g_segment, g_ids)) {
        snprintf(g_failed, sizeof(g_failed), "can't write the playbook");
    }
}

static jstring jstr(JNIEnv *env, const char *s)
{
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}

/* {mode, was, frame, last, events taken or kept, events in it, checks,
 * differs, worst, part, saves} (NcPadplayStatus). */
EXPORT JNIEXPORT jlongArray JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeStatus(JNIEnv *env,
                                                                                                jclass cls)
{
    NcPadplayStatus s;
    nc_padplay_status(&s);
    const jlong v[11] = {s.mode, s.was, s.frame, s.last, (jlong)s.events, (jlong)s.total,
                         s.checks, s.differs, s.worst, s.segment, s.saved};
    jlongArray a = (*env)->NewLongArray(env, 11);
    if (a) {
        (*env)->SetLongArrayRegion(env, a, 0, 11, v);
    }
    return a;
}

/* Why playback or recording ended ("" while on; "load <state ID>": the
 * next part starts in that state), or why it never started. */
EXPORT JNIEXPORT jstring JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeEnded(JNIEnv *env,
                                                                                            jclass cls)
{
    char why[64 + NC_PB_STATE_MAX];
    if (g_failed[0]) {
        snprintf(why, sizeof(why), "not started: %s", g_failed);
        return jstr(env, why);
    }
    NcPadplayStatus s;
    nc_padplay_status(&s);
    if (!strcmp(s.ended, "load") && s.load_id[0]) {
        snprintf(why, sizeof(why), "load %s", s.load_id);
        return jstr(env, why);
    }
    return jstr(env, s.ended);
}

/* Recording: the guide's Save state, as a save line with this label. */
EXPORT JNIEXPORT void JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeNoteSave(JNIEnv *env,
                                                                                            jclass cls,
                                                                                            jstring jlabel)
{
    const char *label = jlabel ? (*env)->GetStringUTFChars(env, jlabel, NULL) : NULL;
    nc_padplay_note_save(label);
    if (label) {
        (*env)->ReleaseStringUTFChars(env, jlabel, label);
    }
}

/* Recording: the guide's Load state, kept as this state ID: the part ends with its load line. */
EXPORT JNIEXPORT void JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeNoteLoad(JNIEnv *env,
                                                                                            jclass cls,
                                                                                            jstring jid)
{
    const char *id = (*env)->GetStringUTFChars(env, jid, NULL);
    nc_padplay_note_load(id);
    (*env)->ReleaseStringUTFChars(env, jid, id);
}

/* A playbook's state just saved at path: its cache files opened to the
 * folder's group, for the PC (native/dev/padstate.h). */
EXPORT JNIEXPORT void JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeShareState(JNIEnv *env,
                                                                                              jclass cls,
                                                                                              jstring jpath)
{
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    nc_padstate_share(path);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
}

/* The state at src copied to dst with the cache files it uses (not on the
 * UI thread: a cache can be large): null, or why not. */
EXPORT JNIEXPORT jstring JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeExportState(JNIEnv *env,
                                                                                                  jclass cls,
                                                                                                  jstring jsrc,
                                                                                                  jstring jdst)
{
    const char *src = (*env)->GetStringUTFChars(env, jsrc, NULL);
    const char *dst = (*env)->GetStringUTFChars(env, jdst, NULL);
    const char *err = nc_cache_export(src, dst);
    (*env)->ReleaseStringUTFChars(env, jsrc, src);
    (*env)->ReleaseStringUTFChars(env, jdst, dst);
    return err ? jstr(env, err) : NULL;
}

/* Ends playback or recording, its files written before it returns. */
EXPORT JNIEXPORT void JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeStop(JNIEnv *env, jclass cls,
                                                                                        jstring jwhy)
{
    const char *why = jwhy ? (*env)->GetStringUTFChars(env, jwhy, NULL) : NULL;
    nc_padplay_stop(why);
    if (why) {
        (*env)->ReleaseStringUTFChars(env, jwhy, why);
    }
}

/* Recording: a checkpoint at the next frame. */
EXPORT JNIEXPORT void JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeMark(JNIEnv *env, jclass cls,
                                                                                        jstring jname)
{
    const char *name = jname ? (*env)->GetStringUTFChars(env, jname, NULL) : NULL;
    nc_padplay_mark(name);
    if (name) {
        (*env)->ReleaseStringUTFChars(env, jname, name);
    }
}

/* Null: the playbook at path can play; else "line N: why". */
EXPORT JNIEXPORT jstring JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeCheck(JNIEnv *env,
                                                                                            jclass cls,
                                                                                            jstring jpath)
{
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    char why[160];
    const bool ok = nc_padplay_check_file(path, why, sizeof(why));
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return ok ? NULL : jstr(env, why);
}

/* A new value of debug.hakux.playbook (the PC's: "play:NAME", "stop",
 * "record:start", "record:here", "mark:NAME", "exit", each with an
 * "@nonce" so the same command twice is a new value); the value found at
 * the first call, a new core's, does nothing. Null: none. */
EXPORT JNIEXPORT jstring JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeTakeCommand(JNIEnv *env,
                                                                                                  jclass cls)
{
    static char last[PROP_VALUE_MAX];
    static bool primed;
    char v[PROP_VALUE_MAX] = "";
    hle_prop_get("debug.hakux.playbook", v);
    if (!primed || !strcmp(v, last)) {
        primed = true;
        snprintf(last, sizeof(last), "%s", v);
        return NULL;
    }
    snprintf(last, sizeof(last), "%s", v);
    if (!v[0] || !strcmp(v, "none")) {
        return NULL;
    }
    nc_log(TAG, "debug.hakux.playbook: %s", v);
    return jstr(env, v);
}

/* debug.hakux.automation: set by the PC while it drives the device ("" or
 * "0" when it doesn't). */
EXPORT JNIEXPORT jstring JNICALL Java_com_rfandango_haku_1x_playbook_PlaybookNative_nativeAutomation(JNIEnv *env,
                                                                                                 jclass cls)
{
    char v[PROP_VALUE_MAX] = "";
    hle_prop_get("debug.hakux.automation", v);
    return jstr(env, v);
}
