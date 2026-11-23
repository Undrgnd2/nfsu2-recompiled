/*
 * carspot.c - the career's kept spot moved to the new career's start when it is no place on the map (Android).
 *
 * Adapted from nfsu2-web's runtime/carspot.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 * leaving free roam (the pause menu's Quit, the garage) the game keeps in the career where the player's car is
 * (Career 0x85AD40 +0x8750 the place, +0x8760 the heading), the profile saves it, and the world's load puts the car
 * there without a look at the numbers. A spot once kept at NaN (or far off the map) put the car there at every load: a
 * black screen under the HUD. Every frame, a spot that is no place becomes the new career's start (the airport's car
 * park, which every career can reach), and the next save keeps that.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

#define G_CAREER        0x0085ad40u
#define C_SPOT          0x8750u
#define C_SPOT_HEADING  0x8760u
#define PLACE_MAX       100000.f
#define START_HEADING   0xf840u
static const float START[3] = { 1695.25f, -883.64f, 24.11f };

void nfs_carspot_frame(void) {
    float p[3]; int bad = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        uint32_t v = MEM32(G_CAREER + C_SPOT + 4u * i); memcpy(&p[i], &v, 4);
        if (!isfinite(p[i]) || fabsf(p[i]) > PLACE_MAX) bad = 1;
    }
    if (!bad) return;
    for (uint32_t i = 0; i < 3u; i++) { uint32_t v; memcpy(&v, &START[i], 4); MEM32(G_CAREER + C_SPOT + 4u * i) = v; }
    MEM16(G_CAREER + C_SPOT_HEADING) = START_HEADING;
    LOGI("Carspot: the career's kept spot is no place (%.1f %.1f %.1f, %s): the new career's start instead",
         (double)p[0], (double)p[1], (double)p[2], MEM8(G_CAREER + 0x8744u) ? "kept" : "not kept now");
}
#endif
