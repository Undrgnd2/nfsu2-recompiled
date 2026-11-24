/*
 * cruise.c - a cruise control for career free roam (Android).
 *
 * The touch button (scancode 0xF6) holds the speed the car has (at least MIN_KMH, forward); − and + (0xF7, 0xF8) move
 * it by STEP_KMH. While it holds, the throttle is the cruise's: the player's driving input (0x407A30, thiscall on the
 * car's input: it reads the player's controls, +0x2D4 throttle, +0x2DC brake, +0x2EC e-brake, and sets the car's,
 * +0x20C throttle, +0x214 brake, through 0x5EB910 and 0x5EB8F0, and copies the throttle to the car's +0x438) runs, then
 * the throttle and brake it set are replaced by a PI controller's on the car's speed (+0x42C, m/s), a light brake
 * downhill. The player's Gas still accelerates (the larger throttle wins, the cruise holds again when it is let go);
 * Brake or the handbrake turns it off, as a real car's, and so do a crash (the car slowed far under the speed held),
 * a jump of the car (a reset, a teleport), leaving free roam. Career free roam only, not LAN or Online.
 */
#ifdef NFS_DRIFT_MODE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_00407A30_orig(void);

#define G_GAMEMODE    0x00850078u
#define G_RACETYPE    0x0089e7b0u
#define G_FLOW        0x008654a4u
#define G_RACEPARAMS  0x0089e7a0u
#define G_PLAYER1     0x008900acu
#define IN_CAR        0x2a8u      /* the input's car */
#define IN_GAS_KEY    0x2d4u      /* the player's controls (0..1) */
#define IN_BRAKE_KEY  0x2dcu
#define IN_EBRAKE_KEY 0x2ecu
#define IN_GAS        0x20cu      /* what the car drives with */
#define IN_BRAKE      0x214u
#define CAR_PLACE     0x60u
#define CAR_VEL       0x70u
#define CAR_FWD       0x90u
#define CAR_SPEED     0x42cu
#define CAR_GAS       0x438u      /* (0x407A30 copies the throttle here) */
#define MIN_KMH       20.f
#define MAX_KMH       300.f
#define STEP_KMH      5.f
#define KP            0.06f       /* throttle per km/h under */
#define KI            0.04f       /* ... per km/h·s */
#define BRAKE_FROM    5.f         /* km/h over: a light brake */
#define BRAKE_K       0.03f
#define BRAKE_MAX     0.35f
#define PEDAL_OFF     0.1f        /* the player's brake or e-brake past this turns it off */
#define SLOW_S        1.f         /* under SLOW_FRAC of the speed held (or SLOW_KMH) this long: a crash, off */
#define SLOW_FRAC     0.5f
#define SLOW_KMH      15.f
#define JUMP_M        60.f

enum { EV_ON = 1, EV_BRAKE, EV_CRASH, EV_LEFT, EV_TOO_SLOW, EV_OFF, EV_NOT_HERE, EV_SET };
static atomic_int s_toggle, s_step, s_on, s_set, s_ev, s_ev_seq, s_avail;
static struct { uint32_t ctl; float set, integ, slow, lx, ly; int on; } C;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = rd(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t u; memcpy(&u, &f, 4); if (ok(va, 4)) MEM32(va) = u; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int roam(void) {
    return rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u && rd(G_RACEPARAMS) == 4000u;
}
static void event(int ev) { atomic_store(&s_ev, ev); atomic_fetch_add(&s_ev_seq, 1); }
static void off(int ev, const char *why) {
    if (C.on) LOGI("Cruise: off (%s)", why);
    C.on = 0; atomic_store(&s_on, 0);
    if (ev) event(ev);
}

/* the touch buttons (any thread): 0xF6 on or off, 0xF7 slower, 0xF8 faster */
void nfs_cruise_key(int scan) {
    if (scan == 0xf6) atomic_store(&s_toggle, 1);
    else if (scan == 0xf7) atomic_fetch_sub(&s_step, 1);
    else if (scan == 0xf8) atomic_fetch_add(&s_step, 1);
}
void nfs_cruise_status(int out[5]) {
    out[0] = atomic_load(&s_on); out[1] = atomic_load(&s_set); out[2] = atomic_load(&s_ev);
    out[3] = atomic_load(&s_ev_seq); out[4] = atomic_load(&s_avail);
}
/* guest thread, after each Present: whether the button is offered, and its presses where the input does not run */
void nfs_cruise_frame(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    int here = roam() && ok(car, CAR_GAS + 4u);
    atomic_store(&s_avail, here);
    if (!here) {
        if (C.on) off(EV_LEFT, "not in free roam");
        if (atomic_exchange(&s_toggle, 0)) event(EV_NOT_HERE);
        atomic_store(&s_step, 0);
    }
}

/* 0x407A30 (thiscall: the input; the frame's step, s): the player's controls into the car */
void nfs_cruise_ov_input(void) {
    uint32_t in = g_ecx;
    float dt; { uint32_t v = MEM32(g_esp + 4); memcpy(&dt, &v, 4); }
    sub_00407A30_orig();
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    if (!car || rd(in + IN_CAR) != car || !ok(in, IN_EBRAKE_KEY + 4u) || !ok(car, CAR_GAS + 4u)) return;
    if (!isfinite(dt) || dt <= 0.f || dt > 0.25f) dt = 1.f / 60.f;
    float x = rdf(car + CAR_PLACE), y = rdf(car + CAR_PLACE + 4u);
    float v = rdf(car + CAR_SPEED) * 3.6f;
    float fwd = rdf(car + CAR_VEL) * rdf(car + CAR_FWD) + rdf(car + CAR_VEL + 4u) * rdf(car + CAR_FWD + 4u)
              + rdf(car + CAR_VEL + 8u) * rdf(car + CAR_FWD + 8u);
    if (!isfinite(x + y + v + fwd)) return;
    float kmh = fwd >= 0.f ? fabsf(v) : -fabsf(v);
    int here = roam();
    /* the buttons */
    if (atomic_exchange(&s_toggle, 0)) {
        if (C.on) off(EV_OFF, "button");
        else if (!here) event(EV_NOT_HERE);
        else if (kmh < MIN_KMH) event(EV_TOO_SLOW);
        else {
            C.on = 1; C.ctl = in; C.set = clampf(roundf(kmh), MIN_KMH, MAX_KMH); C.slow = 0;
            C.integ = clampf(rdf(in + IN_GAS), 0.2f, 0.8f);   /* (from the throttle it had: no jolt) */
            C.lx = x; C.ly = y;
            atomic_store(&s_on, 1); atomic_store(&s_set, (int)C.set); event(EV_ON);
            LOGI("Cruise: on at %.0f km/h", (double)C.set);
        }
    }
    int steps = atomic_exchange(&s_step, 0);
    if (C.on && steps) {
        float to = clampf(STEP_KMH * roundf((C.set + steps * STEP_KMH) / STEP_KMH), MIN_KMH, MAX_KMH);
        if (to != C.set) { C.set = to; atomic_store(&s_set, (int)to); event(EV_SET); LOGI("Cruise: %.0f km/h", (double)to); }
    }
    if (!C.on) return;
    if (in != C.ctl || !here) { off(EV_LEFT, "not in free roam"); return; }
    if (hypotf(x - C.lx, y - C.ly) > JUMP_M) { off(EV_LEFT, "the car jumped"); return; }
    C.lx = x; C.ly = y;
    if (rdf(in + IN_BRAKE_KEY) > PEDAL_OFF || rdf(in + IN_EBRAKE_KEY) > PEDAL_OFF) { off(EV_BRAKE, "brake"); return; }
    C.slow = kmh < fmaxf(SLOW_KMH, C.set * SLOW_FRAC) ? C.slow + dt : 0;
    if (C.slow > SLOW_S) { off(EV_CRASH, "slowed down"); return; }
    /* the throttle to hold C.set: a PI controller, a light brake when over (downhill) */
    float err = C.set - kmh, gas, brake = 0;
    if (err < -BRAKE_FROM) {
        gas = 0; brake = clampf((-err - BRAKE_FROM) * BRAKE_K, 0.f, BRAKE_MAX);
        C.integ = clampf(C.integ - KI * dt * 10.f, 0.f, 1.f);
    } else {
        float p = KP * err, u = C.integ + p;
        if ((u < 1.f || err < 0) && (u > 0.f || err > 0)) C.integ = clampf(C.integ + KI * err * dt, 0.f, 1.f);
        gas = clampf(C.integ + p, 0.f, 1.f);
    }
    float mine = rdf(in + IN_GAS);   /* the player's Gas: the larger wins */
    if (rdf(in + IN_GAS_KEY) > PEDAL_OFF && isfinite(mine) && mine > gas) { gas = mine; brake = 0; }
    wrf(in + IN_GAS, gas); wrf(in + IN_BRAKE, brake); wrf(car + CAR_GAS, gas);
}
#endif
