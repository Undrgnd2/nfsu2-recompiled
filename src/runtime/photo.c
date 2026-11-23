/*
 * photo.c - photo mode (Android, the base of undrgnd2.eu's): the world stands still, the HUD goes, and the camera
 * comes loose from the car (an orbit about a point at the car, or a free camera) with its zoom and tilt, until the
 * picture is taken (the touch controls: GameActivity copies the game's window into a PNG in the gallery).
 *
 * Adapted from nfsu2-web's runtime/photo.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there):
 *  - [0x890084] is the developers' world freeze, which nothing in the game sets: the main loop takes its paused
 *    branch (no world step: the cars, the smoke and the race clock stand) while the cameras keep running;
 *  - Player 0x5F1FD0 (thiscall: type, 0) makes the player's camera mover: type 2 is the developers' debug camera,
 *    which every update looks from [0x7EC4D0] at [0x8286A0] (written here every frame); 0x5FA820 (0) gives the
 *    player's own camera back;
 *  - the field of view and the tilt go in where the view is made (0x5C7EC0, nfs_photo_ov_view);
 *  - the front end's frame (0x5378C0: the HUD and any screen) is skipped while photo mode is on;
 *  - the sound manager plays at the volumes 0x469050 points it to: a copy without effects, engine and tires while
 *    the world stands (it keeps every sound going), the Audio options' after.
 * Only in the world while the player drives (a race, free roam), not in a race over the network (LAN).
 * Not here yet: the web's panel (lens, look, crop, scene), the camera path, the frames added up into one picture.
 */
#ifdef NFS_DRIFT_MODE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* clock_gettime */
#endif
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_005378C0_orig(void);
void sub_005C7EC0_orig(void);

#define G_FLOW         0x008654a4u
#define G_RACE         0x00890118u
#define G_FENG         0x008384c4u
#define G_PLAYER1      0x008900acu   /* Player*: +4 its car, +0x9A4 its HUD, +0x238 its camera mover */
#define G_VIEW1        0x00832e50u   /* eView 1: +0x40 its camera (+0x40 position, +0x50 forward, +0xC4 field of view) */
#define G_FREEZE       0x00890084u
#define G_EYE          0x007ec4d0u
#define G_LOOK         0x008286a0u
#define G_FOLLOW       0x007eb5f4u
#define G_NUDGE        0x008286b0u
#define G_NET_HOST     0x00866a14u
#define G_NET_CLIENT   0x008669f4u
#define G_NET_ONLINE   0x0089cf69u
#define G_VOLUMES      0x0083aa28u
#define G_SOUND        0x0082b884u
#define G_LEAST_LOD_A  0x008a1c10u   /* the least of a car's models its render may draw (0: the full one at any distance) */
#define G_LEAST_LOD_B  0x008a1c14u
#define G_MOTION_BLUR  0x00870cdcu
#define FN_SET_MOVER   0x005f1fd0u   /* thiscall Player (type, 0), ret 8 */
#define FN_OWN_CAMERA  0x005fa820u   /* thiscall Player (0), ret 4 */
#define FN_VOLUMES     0x00469050u   /* thiscall sound manager (volumes*), ret 4 */
#define FN_NEW         0x00575620u   /* cdecl (size) */
#define VOL_BYTES      0x40u
#define PL_CAR         4u
#define PL_HUD         0x9a4u
#define PL_MOVER       0x238u
#define MV_TYPE        0xcu
#define MV_KEYS        0x2cu
#define CAM_POS        0x40u
#define CAM_FWD        0x50u
#define CAM_FOV        0xc4u
#define MOVER_DRIVE    1u
#define MOVER_DEBUG    2u

#define CENTRE_Z       0.5f
#define ORBIT_PAN_M    50.f
#define ORBIT_UP_M     30.f
#define DIST_MIN       1.2f
#define DIST_MAX       60.f
#define FREE_M         100.f
#define EYE_MIN_Z      -0.3f
#define MOVE_MPS       5.f
#define FOV_MIN        15.f
#define FOV_MAX        110.f
#define ROLL_MAX       45.f
#define ROLL_DPS       30.f
#define DEG            0.017453292f
#define PI             3.14159265f
#define SNAP_M         0.001f

/* what the touch controls send (any thread) */
static struct { atomic_int open, mx, my, mz, dx, dy, zoom, roll, mode, reset; } I;
static atomic_int s_status[4];

static struct {
    int on, free, n;
    double t;
    uint32_t freeze_was, quiet, least[2], mb_was;
    float yaw, pitch, dist, off[3], eye[3];
    float yaw0, pitch0, dist0, off0[3], fov0, fov, roll;
    float se[3], sl[3];
} P;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }
static void rd3(uint32_t a, float *v) { for (int i = 0; i < 3; i++) v[i] = rdf(a + 4u * (uint32_t)i); }
static void wr3(uint32_t a, const float *v) { for (int i = 0; i < 3; i++) wrf(a + 4u * (uint32_t)i, v[i]); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static float wrap_pi(float a) { while (a > PI) a -= 2.f * PI; while (a < -PI) a += 2.f * PI; return a; }
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}

static uint32_t player(void) { uint32_t p = rd(G_PLAYER1); return ok(p, PL_HUD + 4u) ? p : 0; }
static uint32_t car_of(uint32_t pl) { uint32_t c = pl ? rd(pl + PL_CAR) : 0; return ok(c, 0x70) ? c : 0; }
static uint32_t mover_type(uint32_t pl) { uint32_t m = pl ? rd(pl + PL_MOVER) : 0; return ok(m, MV_KEYS + 20u) ? MEM32(m + MV_TYPE) : 0; }
static uint32_t view1_camera(void) { uint32_t cam = rd(G_VIEW1 + 0x40u); return ok(cam, CAM_FOV + 2u) ? cam : 0; }
static int network_race(void) { return MEM8(G_NET_ONLINE) || (int32_t)MEM32(G_NET_CLIENT) > 0 || (int32_t)MEM32(G_NET_HOST) > 0; }
static int hud_on_top(uint32_t pl) {
    uint32_t hud = rd(pl + PL_HUD), fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0;
    char a[40], b[40];
    read_str(top ? rd(top + 0xcu) : 0, a, sizeof a); read_str(hud ? rd(hud + 0x10u) : 0, b, sizeof b);
    return a[0] && !strcasecmp(a, b);
}
/* why photo mode cannot be on now (0: it can); on: it is, with our camera */
static const char *not_now(int on) {
    uint32_t pl = player(), r = rd(G_RACE);
    if (MEM32(G_FLOW) != 6u || !pl || !car_of(pl)) return "not in the world";
    if (network_race()) return "a race over the network";
    if (!ok(r, 4) || MEM32(r) != 4u) return "not driving";
    if (!hud_on_top(pl)) return "a screen over the HUD";
    if (mover_type(pl) != (on ? MOVER_DEBUG : MOVER_DRIVE)) return on ? "the game changed the camera" : "not the drive camera";
    return 0;
}
static void centre(uint32_t car, float *v) { rd3(car + 0x60u, v); v[2] += CENTRE_Z; }
static void dir(float yaw, float pitch, float *d) { d[0] = cosf(pitch) * cosf(yaw); d[1] = cosf(pitch) * sinf(yaw); d[2] = sinf(pitch); }
static void orbit_from(const float *p, const float *e) {
    float v[3] = { p[0] - e[0], p[1] - e[1], p[2] - e[2] }, l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < 0.01f) { v[0] = 1; v[1] = 0; v[2] = 0; l = 1; }
    P.yaw = atan2f(v[1], v[0]);
    P.pitch = clampf(asinf(clampf(v[2] / l, -1.f, 1.f)), -85.f * DEG, 20.f * DEG);
    P.dist = clampf(l, DIST_MIN, DIST_MAX);
}
static void aim_point(const float *cc, const float *e, const float *f, float *p) {
    float v[3] = { cc[0] - e[0], cc[1] - e[1], cc[2] - e[2] }, along = v[0] * f[0] + v[1] * f[1] + v[2] * f[2];
    along = clampf(along, DIST_MIN, DIST_MAX);
    for (int i = 0; i < 3; i++) p[i] = e[i] + f[i] * along;
    float o[3] = { p[0] - cc[0], p[1] - cc[1], p[2] - cc[2] }, h = sqrtf(o[0] * o[0] + o[1] * o[1]);
    if (h > ORBIT_PAN_M) { o[0] *= ORBIT_PAN_M / h; o[1] *= ORBIT_PAN_M / h; }
    o[2] = clampf(o[2], -CENTRE_Z, ORBIT_UP_M);
    for (int i = 0; i < 3; i++) p[i] = cc[i] + o[i];
}
static void clear_input(void) {
    atomic_store(&I.dx, 0); atomic_store(&I.dy, 0); atomic_store(&I.zoom, 0);
    atomic_store(&I.mode, 0); atomic_store(&I.reset, 0);
}
static void volumes(uint32_t v) { uint32_t snd = rd(G_SOUND); if (snd && v) nfs_guest_call(FN_VOLUMES, snd, &v, 1, 0); }
static void hush(void) {
    if (!P.quiet) { uint32_t n = VOL_BYTES; P.quiet = nfs_guest_call(FN_NEW, 0, &n, 1, 4); }
    if (!ok(P.quiet, VOL_BYTES)) { P.quiet = 0; return; }
    memcpy((void *)ADDR(P.quiet), (const void *)ADDR(G_VOLUMES), VOL_BYTES);
    for (uint32_t o = 0x10u; o <= 0x18u; o += 4u) wrf(P.quiet + o, 0.f);
    volumes(P.quiet);
}

static void enter(void) {
    uint32_t pl = player(), car = car_of(pl), cam = view1_camera();
    float cc[3], ce[3], cf[3], p[3];
    centre(car, cc);
    if (cam) { rd3(cam + CAM_POS, ce); rd3(cam + CAM_FWD, cf); }
    else { ce[0] = cc[0] - 5.f; ce[1] = cc[1]; ce[2] = cc[2] + 1.5f; cf[0] = 1.f; cf[1] = 0; cf[2] = -0.2f; }
    float fl = sqrtf(cf[0] * cf[0] + cf[1] * cf[1] + cf[2] * cf[2]);
    if (!(fl > 0.5f && fl < 2.f)) { cf[0] = cc[0] - ce[0]; cf[1] = cc[1] - ce[1]; cf[2] = cc[2] - ce[2]; fl = sqrtf(cf[0] * cf[0] + cf[1] * cf[1] + cf[2] * cf[2]) + 1e-6f; }
    for (int i = 0; i < 3; i++) cf[i] /= fl;
    P.fov0 = cam ? clampf((float)MEM16(cam + CAM_FOV) * 360.f / 65536.f, FOV_MIN, FOV_MAX) : 71.5f;
    P.freeze_was = MEM32(G_FREEZE);
    MEM32(G_FREEZE) = 1u;
    uint32_t arg[2] = { MOVER_DEBUG, 0u };
    nfs_guest_call(FN_SET_MOVER, pl, arg, 2, 0);
    if (mover_type(pl) != MOVER_DEBUG) {
        MEM32(G_FREEZE) = P.freeze_was;
        LOGI("Photo: no free camera (mover type %u): not opened", mover_type(pl));
        return;
    }
    MEM32(G_FOLLOW) = 0xffffffffu;
    aim_point(cc, ce, cf, p);
    for (int i = 0; i < 3; i++) P.off[i] = P.off0[i] = p[i] - cc[i];
    orbit_from(p, ce);
    P.yaw0 = P.yaw; P.pitch0 = P.pitch; P.dist0 = P.dist;
    P.fov = P.fov0; P.roll = 0; P.free = 0;
    memcpy(P.se, ce, sizeof P.se); memcpy(P.sl, p, sizeof P.sl);
    clear_input();
    hush();
    P.least[0] = MEM32(G_LEAST_LOD_A); P.least[1] = MEM32(G_LEAST_LOD_B);
    MEM32(G_LEAST_LOD_A) = 0; MEM32(G_LEAST_LOD_B) = 0;
    P.mb_was = MEM32(G_MOTION_BLUR); MEM32(G_MOTION_BLUR) = 0;
    P.on = 1; P.n++;
    LOGI("Photo: on, the world stands, the orbit at %.1f m, field of view %.1f", (double)P.dist, (double)P.fov);
}
static void leave(const char *why) {
    uint32_t pl = player();
    if (MEM32(G_FLOW) == 6u && pl && mover_type(pl) == MOVER_DEBUG) { uint32_t z = 0; nfs_guest_call(FN_OWN_CAMERA, pl, &z, 1, 0); }
    MEM32(G_FREEZE) = P.freeze_was;
    volumes(G_VOLUMES);
    MEM32(G_LEAST_LOD_A) = P.least[0]; MEM32(G_LEAST_LOD_B) = P.least[1];
    MEM32(G_MOTION_BLUR) = P.mb_was;
    P.on = 0;
    LOGI("Photo: off (%s)", why);
}

static void steer(uint32_t car, float dt) {
    float mx = (float)atomic_load(&I.mx) / 1000.f, my = (float)atomic_load(&I.my) / 1000.f, mz = (float)atomic_load(&I.mz) / 1000.f;
    float ddx = (float)atomic_exchange(&I.dx, 0) / 10000.f, ddy = (float)atomic_exchange(&I.dy, 0) / 10000.f;
    float zoom = (float)atomic_exchange(&I.zoom, 0) / 1000.f, rr = (float)atomic_load(&I.roll);
    int md = atomic_exchange(&I.mode, 0), rst = atomic_exchange(&I.reset, 0);
    float cc[3], d[3], e[3], l[3];
    centre(car, cc);
    if (rst) {
        P.free = 0; memcpy(P.off, P.off0, sizeof P.off);
        P.yaw = P.yaw0; P.pitch = P.pitch0; P.dist = P.dist0; P.fov = P.fov0; P.roll = 0;
    }
    if (md) {
        dir(P.yaw, P.pitch, d);
        if (!P.free) for (int i = 0; i < 3; i++) P.eye[i] = P.se[i] - cc[i];   /* free from where the orbit looks from */
        else {                                                                /* an orbit about the point ahead */
            float ee[3], v[3];
            for (int i = 0; i < 3; i++) { ee[i] = cc[i] + P.eye[i]; v[i] = cc[i] - ee[i]; }
            float ll = clampf(sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), DIST_MIN, DIST_MAX), p[3];
            for (int i = 0; i < 3; i++) p[i] = ee[i] + d[i] * ll;
            for (int i = 0; i < 3; i++) P.off[i] = p[i] - cc[i];
            P.dist = ll;
        }
        P.free = !P.free;
        LOGI("Photo: the %s camera", P.free ? "free" : "orbit");
    }
    /* a drag across the view's height turns about 140 degrees */
    P.yaw = wrap_pi(P.yaw - ddx * 2.4f);
    P.pitch = P.free ? clampf(P.pitch - ddy * 2.4f, -85.f * DEG, 85.f * DEG) : clampf(P.pitch - ddy * 2.4f, -85.f * DEG, 20.f * DEG);
    dir(P.yaw, P.pitch, d);
    float f[3] = { cosf(P.yaw), sinf(P.yaw), 0 }, r[3] = { sinf(P.yaw), -cosf(P.yaw), 0 };
    float step = MOVE_MPS * dt;
    if (!P.free) {
        for (int i = 0; i < 3; i++) P.off[i] += (r[i] * mx + f[i] * my) * step;
        P.off[2] += mz * step;
        float h = sqrtf(P.off[0] * P.off[0] + P.off[1] * P.off[1]);
        if (h > ORBIT_PAN_M) { P.off[0] *= ORBIT_PAN_M / h; P.off[1] *= ORBIT_PAN_M / h; }
        P.off[2] = clampf(P.off[2], -CENTRE_Z, ORBIT_UP_M);
    } else {
        for (int i = 0; i < 3; i++) P.eye[i] += (d[i] * my + r[i] * mx) * step;
        P.eye[2] += mz * step;
        float ll = sqrtf(P.eye[0] * P.eye[0] + P.eye[1] * P.eye[1] + P.eye[2] * P.eye[2]);
        if (ll > FREE_M) for (int i = 0; i < 3; i++) P.eye[i] *= FREE_M / ll;
    }
    /* pinch: the zoom (field of view) */
    P.fov = clampf(P.fov * expf(zoom), FOV_MIN, FOV_MAX);
    P.roll = clampf(P.roll + rr * ROLL_DPS * dt, -ROLL_MAX, ROLL_MAX);
    if (!P.free) for (int i = 0; i < 3; i++) { l[i] = cc[i] + P.off[i]; e[i] = l[i] - d[i] * P.dist; }
    else for (int i = 0; i < 3; i++) { e[i] = cc[i] + P.eye[i]; l[i] = e[i] + d[i] * 10.f; }
    float zmin = cc[2] - CENTRE_Z + EYE_MIN_Z;
    if (e[2] < zmin) { l[2] += zmin - e[2]; e[2] = zmin; if (P.free && P.eye[2] < zmin - cc[2]) P.eye[2] = zmin - cc[2]; }
    float k = 1.f - expf(-dt * 14.f), far = 0.f;
    for (int i = 0; i < 3; i++) {
        P.se[i] += (e[i] - P.se[i]) * k; P.sl[i] += (l[i] - P.sl[i]) * k;
        far = fmaxf(far, fmaxf(fabsf(e[i] - P.se[i]), fabsf(l[i] - P.sl[i])));
    }
    if (far < SNAP_M) { memcpy(P.se, e, sizeof P.se); memcpy(P.sl, l, sizeof P.sl); }
}
static void place(uint32_t pl) {
    uint32_t m = rd(pl + PL_MOVER);
    if (ok(m, MV_KEYS + 20u)) memset((void *)ADDR(m + MV_KEYS), 0, 20);
    MEM32(G_FOLLOW) = 0xffffffffu;
    MEM32(G_NUDGE) = 0; MEM32(G_NUDGE + 4u) = 0; MEM32(G_NUDGE + 8u) = 0;
    wr3(G_EYE, P.se); wr3(G_LOOK, P.sl);
}

void nfs_photo_input(int cmd, int a, int b) {
    switch (cmd) {
    case PHOTO_OPEN:  atomic_store(&I.open, a); break;                  /* 1 open, 2 close, 3 the other */
    case PHOTO_MOVE:  atomic_store(&I.mx, a); atomic_store(&I.my, b); break;   /* 1/1000: right, forward */
    case PHOTO_LIFT:  atomic_store(&I.mz, a); break;                    /* 1/1000: up */
    case PHOTO_DRAG:  atomic_fetch_add(&I.dx, a); atomic_fetch_add(&I.dy, b); break;   /* 1/10000 of the view's height */
    case PHOTO_ZOOM:  atomic_fetch_add(&I.zoom, a); break;              /* 1/1000 of the log of the field of view */
    case PHOTO_ROLL:  atomic_store(&I.roll, a); break;                  /* -1, 0, 1 held */
    case PHOTO_MODE:  atomic_store(&I.mode, 1); break;
    case PHOTO_RESET: atomic_store(&I.reset, 1); break;
    default: break;
    }
}
void nfs_photo_status(int out[4]) { for (int i = 0; i < 4; i++) out[i] = atomic_load(&s_status[i]); }

void nfs_photo_frame(void) {
    double t = now_s();
    float dt = P.t > 0 ? (float)(t - P.t) : 0.f;
    P.t = t;
    if (dt < 0.f) dt = 0.f; else if (dt > 0.1f) dt = 0.1f;
    int req = atomic_exchange(&I.open, 0);
    if (req == 3) req = P.on ? 2 : 1;
    const char *why = not_now(P.on);
    if (P.on && (why || req == 2)) leave(req == 2 ? "closed" : why);
    else if (!P.on && !why && req == 1) enter();
    else if (!P.on && req == 1) LOGI("Photo: not opened (%s)", why);
    uint32_t pl = player(), car = car_of(pl);
    if (P.on && car) { steer(car, dt); place(pl); }
    atomic_store(&s_status[0], (why == 0 || P.on ? 1 : 0) | (P.on ? 2 : 0) | (P.free ? 4 : 0));
    atomic_store(&s_status[1], (int)lrintf(P.fov * 10.f));
    atomic_store(&s_status[2], (int)lrintf(P.roll * 10.f));
    atomic_store(&s_status[3], P.n);
}

/* The front end's frame (0x5378C0, cdecl, no arguments): the HUD and any screen; not while photo mode is on. */
void nfs_photo_ov_frontend(void) {
    if (!P.on) { sub_005378C0_orig(); return; }
    g_esp += 4;
}
/* The view made (0x5C7EC0, ret 0x10, the view at +4): player 1's with photo mode's field of view and tilt, the
 * camera's own back after. */
static void view_roll(uint32_t m, float a) {
    float c = cosf(a), s = sinf(a);
    for (uint32_t r = m; r < m + 0x40u; r += 0x10u) {
        float x = rdf(r), y = rdf(r + 4u);
        wrf(r, x * c + y * s); wrf(r + 4u, y * c - x * s);
    }
}
void nfs_photo_ov_view(void) {
    uint32_t view = MEM32(g_esp + 4u), cam = 0;
    uint16_t keep_fov = 0; uint8_t keep[0x40];
    if (P.on && ok(view, 0x44) && (MEM32(view + 4u) == 1u || MEM32(view + 4u) == 4u)) {
        cam = MEM32(view + 0x40u);
        if (ok(cam, CAM_FOV + 2u)) {
            keep_fov = MEM16(cam + CAM_FOV);
            MEM16(cam + CAM_FOV) = (uint16_t)lrintf(P.fov * 65536.f / 360.f);
            memcpy(keep, (const void *)ADDR(cam), sizeof keep);
            if (P.roll != 0.f) view_roll(cam, P.roll * DEG);
        } else cam = 0;
    }
    sub_005C7EC0_orig();
    if (cam) { memcpy((void *)ADDR(cam), keep, sizeof keep); MEM16(cam + CAM_FOV) = keep_fov; }
}
#endif
