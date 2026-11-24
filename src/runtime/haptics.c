/*
 * haptics.c - what the phone feels of the player's car (Android): its knocks, its gear changes, its nitrous going on.
 * Read once a frame (after each Present, while the world runs and the car drives); the touch controls vibrate for each
 * (RacingControlsView.setHaptics, the ⚙ menu's "Vibrations de retour"). Nothing of the game's is changed.
 *  - a knock: the car's velocity (+0x70) changed by more than HIT_MS m/s in one frame of the world (braking hard is
 *    some 0.3 m/s a frame): a wall, a car, a landing; its strength from HIT_MS to HIT_FULL. A jump of the car (a reset,
 *    a teleport) is none;
 *  - a gear: its gear (+0x434: 0 reverse, 1 neutral, 2 first...) changed;
 *  - the nitrous: on (+0x40 +0x420, when it went on: 0 or 0x7FFFFFFF none) from off.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_FLOW      0x008654a4u
#define G_PLAYER1   0x008900acu
#define G_WORLDDT   0x00865154u
#define G_FREEZE    0x00890084u
#define CAR_PLACE   0x60u
#define CAR_VEL     0x70u
#define CAR_GEAR    0x434u
#define CAR_NOS_ON  (0x40u + 0x420u)
#define HIT_MS      3.f
#define HIT_FULL    15.f
#define JUMP_M      30.f

static atomic_int s_hit_seq, s_hit, s_gear_seq, s_nos_seq;
static struct { uint32_t car; float v[3], p[3]; int gear, nos, have; } H;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static float f32(uint32_t va) { uint32_t u = MEM32(va); float f; memcpy(&f, &u, 4); return f; }

void nfs_haptics_status(int out[4]) {
    out[0] = atomic_load(&s_hit_seq); out[1] = atomic_load(&s_hit);
    out[2] = atomic_load(&s_gear_seq); out[3] = atomic_load(&s_nos_seq);
}
void nfs_haptics_frame(void) {
    uint32_t pl = ok(G_PLAYER1, 4) ? MEM32(G_PLAYER1) : 0, car = ok(pl + 4u, 4) ? MEM32(pl + 4u) : 0;
    if (!ok(G_FLOW, 4) || MEM32(G_FLOW) != 6u || !ok(car, CAR_NOS_ON + 4u) || !ok(G_WORLDDT, 4)) { H.have = 0; return; }
    float wdt = f32(G_WORLDDT);
    if (!(wdt > 0.f) || wdt > 0.1f || (ok(G_FREEZE, 4) && MEM32(G_FREEZE))) return;   /* (the world stands: nothing) */
    float v[3], p[3];
    for (int k = 0; k < 3; k++) { v[k] = f32(car + CAR_VEL + 4u * k); p[k] = f32(car + CAR_PLACE + 4u * k); }
    if (!isfinite(v[0] + v[1] + v[2] + p[0] + p[1] + p[2])) { H.have = 0; return; }
    int gear = (int)MEM32(car + CAR_GEAR);
    uint32_t since = MEM32(car + CAR_NOS_ON);
    int nos = since && since != 0x7fffffffu;
    if (H.have && H.car == car) {
        float dv = sqrtf((v[0] - H.v[0]) * (v[0] - H.v[0]) + (v[1] - H.v[1]) * (v[1] - H.v[1]) + (v[2] - H.v[2]) * (v[2] - H.v[2]));
        float dp = sqrtf((p[0] - H.p[0]) * (p[0] - H.p[0]) + (p[1] - H.p[1]) * (p[1] - H.p[1]) + (p[2] - H.p[2]) * (p[2] - H.p[2]));
        if (dv > HIT_MS && dp < JUMP_M) {
            float s = (dv - HIT_MS) / (HIT_FULL - HIT_MS);
            atomic_store(&s_hit, (int)lrintf(100.f * (s < 0.f ? 0.f : s > 1.f ? 1.f : s)));
            atomic_fetch_add(&s_hit_seq, 1);
        }
        if (gear != H.gear && gear >= 0 && gear < 10 && H.gear >= 0 && H.gear < 10) atomic_fetch_add(&s_gear_seq, 1);
        if (nos && !H.nos) atomic_fetch_add(&s_nos_seq, 1);
    }
    H.car = car; H.gear = gear; H.nos = nos; H.have = 1;
    memcpy(H.v, v, sizeof v); memcpy(H.p, p, sizeof p);
}
#endif
