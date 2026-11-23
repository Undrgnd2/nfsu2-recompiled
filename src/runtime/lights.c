/*
 * lights.c - the headlights on a touch button (Android): tapped, off or back on (the game's own lights, as they
 * were); held (HOLD_S), high beam for as long as it is held, and as they were again when let go (off again, if they
 * were off: a flash). Only while the player drives (the world running, the drive camera); not paused, in menus or
 * photo mode.
 *
 * Adapted from nfsu2-web's runtime/lightkey.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 *  - car +0x93D (byte): its headlights are on. 0x5F6190 (thiscall car, from the car's update) sets it each step; its
 *    readers draw: the flares (0x615630, the two headlight flares, hashes 0x9DB90133 and 0xD09091C6), the glow and
 *    the light trails. Off: the byte put to 0 after that step (the wrap of 0x5F6190), for the player's car.
 *  - the road: the player's car's projector lights it with HEADLIGHTCOLOR, the car's headlight colour that 0x60F510
 *    (cdecl car) returns ([[car +0x554] +0x380]): black while off (the wrap of 0x60F510). (The web's page also lights
 *    the road farther on high beam with a light of its own; here high beam is the flares'.)
 *  - high beam: each headlight flare of the player's car drawn brighter and bigger: its strength (0x490920, cdecl:
 *    view, light, matrix, strength, ...) times HI_FLARE while photo.c's flares wrap draws that car's.
 */
#ifdef NFS_DRIFT_MODE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime */
#endif
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_005F6190_orig(void);
void sub_0060F510_orig(void);
void sub_00490920_orig(void);

#define G_FLOW       0x008654a4u
#define G_RACE       0x00890118u
#define G_FREEZE     0x00890084u
#define G_PLAYER1    0x008900acu
#define PL_CAR       4u
#define PL_MOVER     0x238u
#define MV_TYPE      0xcu
#define MOVER_DRIVE  1u
#define CAR_HL_ON    0x93du
#define HASH_HL_A    0x9db90133u
#define HASH_HL_B    0xd09091c6u
#define HOLD_S       0.2
#define HI_FLARE     3.0f

static struct {
    int off, flash, held, flashed;
    double t0;
    uint32_t flare_ri;
} L;
static atomic_int s_down = -1;       /* the button: 1 pressed, 0 let go, -1 nothing new */
static atomic_int s_state;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static uint32_t my_car(void) { uint32_t pl = rd(G_PLAYER1); return pl ? rd(pl + PL_CAR) : 0; }
static int driving(void) {
    uint32_t pl = rd(G_PLAYER1), r = rd(G_RACE);
    if (MEM32(G_FLOW) != 6u || !ok(pl, PL_MOVER + 4u) || !ok(r, 4) || MEM32(r) != 4u || MEM32(G_FREEZE)) return 0;
    uint32_t mv = rd(pl + PL_MOVER);
    return ok(rd(pl + PL_CAR), 0x9e0) && ok(mv, MV_TYPE + 4u) && MEM32(mv + MV_TYPE) == MOVER_DRIVE;
}
/* our lights now: 0 as the game has them, 1 off, 2 high beam */
static int state(void) { return L.flash ? 2 : L.off ? 1 : 0; }

void nfs_lights_key(int down) { atomic_store(&s_down, down ? 1 : 0); }
int nfs_lights_state(void) { return atomic_load(&s_state); }

void nfs_lights_frame(void) {
    double t = now_s();
    int can = driving(), d = atomic_exchange(&s_down, -1);
    if (d == 1 && can) { L.held = 1; L.t0 = t; L.flashed = 0; }
    else if (d == 0 && L.held) {
        L.held = 0;
        if (!L.flashed && can) { L.off = !L.off; LOGI("Lights: %s", L.off ? "off" : "on"); }
        else if (L.flashed) LOGI("Lights: high beam let go, lights %s", L.off ? "off" : "on");
    }
    if (!can) L.held = 0;
    int flash = can && L.held && t - L.t0 >= HOLD_S;
    if (flash && !L.flash) { L.flashed = 1; LOGI("Lights: high beam"); }
    L.flash = flash;
    atomic_store(&s_state, state());
}

/* the car's step (0x5F6190, thiscall car): the headlights' byte; ours 0 while off */
void nfs_lights_ov_step(void) {
    uint32_t car = g_ecx;
    sub_005F6190_orig();
    if (L.off && !L.flash && car && car == my_car() && ok(car + CAR_HL_ON, 1)) MEM8(car + CAR_HL_ON) = 0;
}
/* the car's headlight colour (0x60F510, cdecl car): the road lit by it; ours black while off */
void nfs_lights_ov_colour(void) {
    uint32_t car = MEM32(g_esp + 4u);
    sub_0060F510_orig();
    if (L.off && !L.flash && car && car == my_car()) g_eax = 0;
}
/* photo.c's flares wrap (0x615630): the car whose flares are drawn now (its render info; 0 done) */
void nfs_lights_flares(uint32_t ri) {
    L.flare_ri = 0;
    if (!ri || !L.flash || !ok(ri, 4)) return;
    uint32_t mine = my_car();
    if (mine && MEM32(ri) == mine) L.flare_ri = ri;
}
/* a light's flare (0x490920, cdecl: view, light, matrix, strength, ...): our headlights' brighter on high beam */
void nfs_lights_ov_flare(void) {
    if (L.flare_ri) {
        uint32_t light = MEM32(g_esp + 8u), h = ok(light, 0xc) ? MEM32(light + 8u) : 0u;
        if (h == HASH_HL_A || h == HASH_HL_B) {
            uint32_t v = MEM32(g_esp + 0x10u); float f; memcpy(&f, &v, 4); f *= HI_FLARE; memcpy(&v, &f, 4); MEM32(g_esp + 0x10u) = v;
        }
    }
    sub_00490920_orig();
}
#endif
