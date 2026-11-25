/*
 * whine.c - the player's gearbox whine (Android, the ⚙ menu's "Sifflement de boîte"), as ropi's Underground 2
 * Transmission Whine has it (UG2WhineGen 1.0.2: NFS Carbon's gearbox whine on Underground 2, "No Rights Reserved").
 *
 * Adapted from nfsu2-web's runtime/whine.c (MIT, Copyright (c) 2026 th0mk; the measures and the mod's rules are
 * there), for the player's car alone. The mod's three sounds are in the APK (assets/whine: 16-bit mono WAVs), handed
 * here by main.cpp at the start (nfs_whine_sound); each looped from the start over the game's sound:
 *   car_whine.wav          the whine, its pitch rising with the speed
 *   car_whine_2.wav        a second layer, louder with speed
 *   car_whine_reverse.wav  in reverse instead of both
 * Each frame (game thread): the player's car's speed +0x430 (m/s; the engine sound's) and gear +0x434 (0 reverse,
 * 1 neutral, 2 first ..); nothing out of the world (GameFlowManager [0x8654A4] 6) and silent while it stands (the
 * frame's world step [0x865154] 0: paused, a menu, photo mode). The level: the game's Master times Engine Volume
 * (0x83AA28 +0, +0x14) x LEVEL. Under 1 km/h the whine at pitch 0.3 and the rest silent; else the whine and reverse
 * layer at 0.3 + 0.7 km/h / 330 + min(0.5 sqrt(km/h / 70), 0.6) + m, m creeping towards 0.02 (gear - 1), the second
 * layer at 1; volumes 0.030, 0.015 km/h / 140, both off in reverse, the reverse layer 0.050 there only. An upshift:
 * the whine and the second layer silent 0.3 s, m at its new gear's value.
 * The audio thread (audio_mixer.h's render: nfs_whine_mix) adds it to the game's mix, every change smoothed; the
 * game thread writes the targets, one word each; older than STALE_MS (a parked game): silence.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_FLOW      0x008654a4u
#define G_WORLDDT   0x00865154u
#define G_PLAYER1   0x008900acu
#define G_VOLUMES   0x0083aa28u
#define CAR_SPEED   0x430u
#define CAR_GEAR    0x434u
#define STALE_MS    250u
#define LEVEL       1.65f

typedef struct { uint8_t *file; const uint8_t *pcm; uint32_t n; float rate; } Wav;
enum { W_WHINE, W_WHINE2, W_REVERSE, W_N };
static Wav g_wav[W_N];
static atomic_int g_ready, g_on;              /* the sounds in (1); the menu's switch */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32b(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static Wav wav_parse(uint8_t *b, uint32_t len) {
    Wav w = { b, 0, 0, 0.f };
    if (len < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return w;
    uint32_t rate = 0, fmt_ok = 0;
    for (uint32_t o = 12; o + 8 <= len;) {
        uint32_t sz = rd32b(b + o + 4);
        if (sz > len - o - 8) sz = len - o - 8;
        if (!memcmp(b + o, "fmt ", 4) && sz >= 16) { fmt_ok = rd16(b + o + 8) == 1 && rd16(b + o + 10) == 1 && rd16(b + o + 22) == 16; rate = rd32b(b + o + 12); }
        else if (!memcmp(b + o, "data", 4) && fmt_ok && rate) { w.pcm = b + o + 8; w.n = sz / 2; w.rate = (float)rate; return w; }
        o += 8 + sz + (sz & 1u);
    }
    return w;
}
/* main.cpp, at the start: sound w (0 car_whine, 1 car_whine_2, 2 car_whine_reverse) of len bytes, copied */
void nfs_whine_sound(int w, const void *data, unsigned len) {
    if (w < 0 || w >= W_N || !data || len < 44 || atomic_load(&g_ready)) return;
    uint8_t *b = malloc(len);
    if (!b) return;
    memcpy(b, data, len);
    g_wav[w] = wav_parse(b, len);
    if (g_wav[w].n < 4) { free(b); g_wav[w].file = 0; g_wav[w].n = 0; LOGI("Whine: sound %d is not a 16-bit mono WAV", w); return; }
    int all = 1;
    for (int k = 0; k < W_N; k++) all = all && g_wav[k].n >= 4;
    if (all) { atomic_store(&g_ready, 1); LOGI("Whine: the mod's sounds in"); }
}
void nfs_whine_enable(int on) { atomic_store(&g_on, on != 0); }

static uint32_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static float rdf(uint32_t va) { return ok(va, 4) ? bitsf(MEM32(va)) : 0.f; }

static _Atomic uint32_t g_gain[W_N], g_pitch[W_N], g_stamp, g_level;
static struct { uint32_t car; int32_t gear_old; float mod, speed_last, shift; } S;

static void put(int w, float gain, float pitch) { atomic_store(&g_gain[w], fbits(gain)); atomic_store(&g_pitch[w], fbits(pitch)); }
static void quiet(void) { memset(&S, 0, sizeof S); for (int w = 0; w < W_N; w++) atomic_store(&g_gain[w], fbits(0.f)); }
static float volume(uint32_t off) { float v = rdf(G_VOLUMES + off); return v >= 0.f && v <= 1.f ? v : 1.f; }

static void step(uint32_t car, float dt) {
    if (car != S.car) { memset(&S, 0, sizeof S); S.car = car; }
    float kmh = bitsf(MEM32(car + CAR_SPEED)) * 3.6f;
    int32_t gear = (int32_t)MEM32(car + CAR_GEAR);
    if (!isfinite(kmh)) kmh = 0.f;
    int play = gear != 0;
    float ticks = dt * 60.f, pitch = 0.3f, pitch2 = 1.f;
    if (kmh < 1.f) pitch2 = 0.f;
    else {
        float target = (float)(gear - 1) * 0.020f;
        if (target < 0.f) target = 0.f;
        int up = S.gear_old != 0 && gear > S.gear_old;
        if (S.gear_old == 0 || gear != S.gear_old) S.gear_old = gear;
        if (up) { S.shift = 0.3f; S.mod = target; }
        if (S.speed_last > kmh && S.mod > target) { S.mod -= 0.001f * ticks; if (S.mod < target) S.mod = target; }
        if (S.speed_last < kmh && S.mod < target) { S.mod += 0.001f * ticks; if (S.mod > target) S.mod = target; }
        S.speed_last = kmh;
        float m = 0.5f * sqrtf(kmh / 70.f);
        pitch = 0.3f + kmh / 330.f * 0.7f + (m > 0.6f ? 0.6f : m) + S.mod;
    }
    float shifting = 1.f;
    if (S.shift > 0.f) { S.shift -= dt; shifting = 0.f; }
    float g1 = 0.030f * (float)play * shifting, g2 = (kmh > 0.f ? kmh / 140.f : 0.f) * 0.015f * (float)play * shifting;
    float gr = 0.050f * (float)!play;
    put(W_WHINE, g1, pitch);
    put(W_WHINE2, kmh < 1.f ? 0.f : g2, pitch2);
    put(W_REVERSE, kmh < 1.f ? 0.f : gr, pitch);
}

/* guest thread, after each Present */
void nfs_whine_frame(void) {
    if (atomic_load(&g_ready) != 1) return;
    int on = atomic_load(&g_on), flow6 = on && ok(G_FLOW, 4) && MEM32(G_FLOW) == 6u;
    float dt = flow6 ? rdf(G_WORLDDT) : 0.f;
    int run = flow6 && dt > 0.f && dt < 0.25f;
    atomic_store(&g_stamp, now_ms());
    atomic_store(&g_level, fbits(run ? volume(0x00u) * volume(0x14u) * LEVEL : 0.f));
    uint32_t pl = ok(G_PLAYER1, 4) ? MEM32(G_PLAYER1) : 0, car = flow6 && ok(pl + 4u, 4) ? MEM32(pl + 4u) : 0;
    if (!car || !ok(car + CAR_SPEED, 8)) { if (S.car || !flow6) quiet(); return; }
    if (run) step(car, dt);
}

/* ---- the audio thread: added to the game's mix (stereo floats, rate Hz) ---- */
static struct { double pos; float gain, pitch; } g_v[W_N];
static float g_lvl;
static inline float glide(float x, float t, float k) { x += (t - x) * k; return fabsf(t - x) < 1e-6f ? t : x; }
static inline float wav_s(const Wav *w, uint32_t i) { return (float)(int16_t)rd16(w->pcm + 2u * i) * (1.f / 32768.f); }

void nfs_whine_mix(float *acc, unsigned frames, unsigned rate) {
    if (!rate || !frames || atomic_load(&g_ready) != 1) return;
    float fs = (float)rate;
    int stale = now_ms() - atomic_load(&g_stamp) > STALE_MS;
    float k_lvl = 1.f - expf(-1.f / (0.070f * fs)), k_gain = 1.f - expf(-1.f / (0.035f * fs)), k_pitch = 1.f - expf(-1.f / (0.020f * fs));
    float lvl_t = stale ? 0.f : bitsf(atomic_load(&g_level));
    if (!(lvl_t >= 0.f && lvl_t <= 4.f)) lvl_t = 0.f;
    if (g_lvl == 0.f && lvl_t == 0.f) return;
    float gt[W_N], pt[W_N];
    for (int w = 0; w < W_N; w++) {
        gt[w] = bitsf(atomic_load(&g_gain[w])); pt[w] = bitsf(atomic_load(&g_pitch[w]));
        if (!(gt[w] >= 0.f && gt[w] < 1.f)) gt[w] = 0.f;
        if (!(pt[w] >= 0.f && pt[w] < 4.f)) pt[w] = 0.f;
    }
    for (unsigned i = 0; i < frames; i++) {
        g_lvl = glide(g_lvl, lvl_t, k_lvl);
        float out = 0.f;
        for (int w = 0; w < W_N; w++) {
            if (gt[w] <= 0.f && g_v[w].gain <= 0.f) continue;
            const Wav *W = &g_wav[w];
            float g = g_v[w].gain = glide(g_v[w].gain, gt[w], k_gain);
            float p = g_v[w].pitch = glide(g_v[w].pitch, pt[w], k_pitch);
            double pos = g_v[w].pos;
            uint32_t i0 = (uint32_t)pos, i1 = i0 + 1u >= W->n ? 0u : i0 + 1u;
            float t = (float)(pos - (double)i0), a = wav_s(W, i0);
            out += (a + (wav_s(W, i1) - a) * t) * g;
            pos += (double)(p * W->rate / fs);
            if (pos >= (double)W->n) pos = fmod(pos, (double)W->n);
            g_v[w].pos = pos;
        }
        out *= g_lvl;
        acc[2u * i] += out; acc[2u * i + 1u] += out;
    }
}
#endif
