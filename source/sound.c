/* sound.c - a background thread keeps the audio port fed; sound_play only sets a flag,
   so nothing in here can stall the screen. Sounds are generated (sine tones with a soft
   envelope), quiet, and short. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <lv2/thread.h>
#include <sysmodule/sysmodule.h>

#include "sound.h"
#include "log.h"

#define RATE 48000.0f
#define MAXV 4

typedef struct { float f0, f1, t, len, amp; } Voice;

static volatile int enabled = 0, quit = 0, started = 0;
/* sounds asked for but not started yet: a small queue so quick presses aren't lost
   (the screen adds at the head, the sound thread takes from the tail) */
#define QN 16
static volatile int queue[QN];
static volatile u32 q_head = 0, q_tail = 0;
static sys_ppu_thread_t th;
static Voice voices[MAXV];

static void start_voice(int which)
{
    static const struct { float f0, f1, len, amp; } def[] = {
        { 1180, 1100, 0.065f, 0.16f },     /* move: short soft tick */
        { 880,  1320, 0.16f,  0.14f },     /* ok: rising chime */
        { 700,  440,  0.12f,  0.12f },     /* back: falling */
        { 1000, 1350, 0.08f,  0.15f },     /* tab */
        { 1320, 1980, 0.22f,  0.12f },     /* star */
    };
    int i, v = 0;
    float oldest = -1;
    for (i = 0; i < MAXV; i++) {
        if (voices[i].t >= voices[i].len) { v = i; break; }
        if (voices[i].t / voices[i].len > oldest) { oldest = voices[i].t / voices[i].len; v = i; }   /* all busy: replace the one nearly done */
    }
    voices[v].f0 = def[which].f0; voices[v].f1 = def[which].f1;
    voices[v].len = def[which].len; voices[v].amp = def[which].amp; voices[v].t = 0;
}

static void fill(float *buf)
{
    int i, v;
    for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        float s = 0;
        for (v = 0; v < MAXV; v++) {
            Voice *vo = &voices[v];
            if (vo->t >= vo->len) continue;
            float k = vo->t / vo->len;
            float f = vo->f0 + (vo->f1 - vo->f0) * k;
            float env = (k < 0.08f ? k / 0.08f : 1.0f) * (1.0f - k) * (1.0f - k);
            s += sinf(6.2831853f * f * vo->t) * env * vo->amp;
            vo->t += 1.0f / RATE;
        }
        buf[i * 2] = buf[i * 2 + 1] = s;
    }
}

static void audio_thread(void *arg)
{
    audioPortParam params;
    audioPortConfig cfg;
    u32 port;
    int r;
    (void) arg;
    sysModuleLoad(SYSMODULE_AUDIO);
    r = audioInit();
    if (r) { log_step("sound: audioInit failed %d", r); sysThreadExit(0); }
    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = 0;
    r = audioPortOpen(&params, &port);
    if (r) { log_step("sound: audioPortOpen failed %d", r); audioQuit(); sysThreadExit(0); }
    audioGetPortConfig(port, &cfg);
    audioPortStart(port);
    log_step("sound: on");

    u32 next = ((u32) *(volatile u64 *)(u64) cfg.readIndex + 1) % AUDIO_BLOCK_8;
    float *data = (float *)(u64) cfg.audioDataStart;
    volatile u64 *read_index = (volatile u64 *)(u64) cfg.readIndex;
    while (!quit) {
        while (q_tail != q_head) {
            int p = queue[q_tail % QN];
            q_tail++;
            if (enabled) start_voice(p);
        }
        u32 cur = (u32) *read_index;
        /* keep the blocks just ahead of the one playing filled; new sounds go into the
           nearest block so they start within a few milliseconds */
        u32 want = (cur + 4) % AUDIO_BLOCK_8;
        while (next != want) {
            fill(data + 2 * AUDIO_BLOCK_SAMPLES * next);
            next = (next + 1) % AUDIO_BLOCK_8;
        }
        sysUsleep(1000);
    }
    audioPortStop(port);
    audioPortClose(port);
    audioQuit();
    sysThreadExit(0);
}

void sound_enable(int on)
{
    enabled = on;
    if (on && !started) {
        started = 1;
        if (sysThreadCreate(&th, audio_thread, NULL, 900, 0x4000, THREAD_JOINABLE, "sound") != 0) {
            log_step("sound: thread not started");
            started = 0;
        }
    }
}

void sound_play(int which)
{
    if (!enabled || !started) return;
    if (q_head - q_tail >= QN) return;        /* thread is behind: drop rather than block */
    queue[q_head % QN] = which;
    __sync_synchronize();
    q_head++;
}

void sound_shutdown(void)
{
    u64 rv;
    if (!started) return;
    quit = 1;
    sysThreadJoin(th, &rv);
    started = 0;
}
