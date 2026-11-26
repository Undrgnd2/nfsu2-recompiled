/*
 * cinema.c - the cinema camera (Android, the ⚙ menu's "Caméra cinéma"): while the autopilot drives (autopilot.c),
 * the camera cuts every few seconds to another shot of the car, as a game's attract mode or a car film: a tracking
 * shot from its side, a low shot from its front, a wide chase from above, a shot on the road beside a wheel, a camera
 * standing by the road as the car goes by, a slow orbit, a helicopter's. The HUD goes while it runs.
 *
 * The camera is photo mode's (photo.c): Player 0x5F1FD0 (thiscall: type 2, 0) gives the player the developers' debug
 * camera mover, which looks from [0x7EC4D0] at [0x8286A0] every update (both written here each frame, worked out
 * from the car's place +0x60 and its axes +0x90 forward, +0xA0 left, +0xB0 up; [0x7EB5F4] -1 no car followed, the
 * mover's keys and [0x8286B0] cleared); 0x5FA820 (0) gives the player's own camera back. The field of view is the
 * shot's (photo.c's wrap of the view, 0x5C7EC0: nfs_cinema_fov). Unlike photo mode the world runs. Photo mode opened
 * meanwhile has the camera; the cinema takes it again after.
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
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);

#define G_FLOW        0x008654a4u
#define G_PLAYER1     0x008900acu
#define G_EYE         0x007ec4d0u
#define G_LOOK        0x008286a0u
#define G_FOLLOW      0x007eb5f4u
#define G_NUDGE       0x008286b0u
#define FN_SET_MOVER  0x005f1fd0u
#define FN_OWN_CAMERA 0x005fa820u
#define PL_CAR        4u
#define PL_MOVER      0x238u
#define MV_TYPE       0xcu
#define MV_KEYS       0x2cu
#define MOVER_DEBUG   2u
#define CAR_PLACE     0x60u
#define CAR_FWD       0x90u
#define CAR_LEFT      0xa0u
#define CAR_UP        0xb0u
#define YAW_TAU_S     0.35f         /* the shots' heading follows the car's this smoothly */
#define SHOT_MIN_S    5.f
#define SHOT_MAX_S    9.f

enum { S_SIDE, S_FRONT, S_CHASE, S_WHEEL, S_ROADSIDE, S_ORBIT, S_HELI, S_N };
static const char *const k_shot[S_N] = { "side tracking", "low front", "wide chase", "wheel", "roadside", "orbit", "helicopter" };
static atomic_int s_want;
static struct { int on, shot, shots; double t0, start, last; float dur, side, fov, fix[3], spin, yaw; int have_yaw; } C;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t u = MEM32(va); float f; memcpy(&f, &u, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t u; memcpy(&u, &f, 4); MEM32(va) = u; }
static void rd3(uint32_t a, float *v) { for (int i = 0; i < 3; i++) v[i] = rdf(a + 4u * (uint32_t)i); }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static uint32_t mover_type(uint32_t pl) { uint32_t m = rd(pl + PL_MOVER); return ok(m, MV_KEYS + 20u) ? MEM32(m + MV_TYPE) : 0; }
static float rnd(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

void nfs_cinema_enable(int on) { atomic_store(&s_want, on != 0); }
int nfs_cinema_active(void) { return C.on; }
float nfs_cinema_fov(void) { return C.on ? C.fov : 0.f; }

static void release(uint32_t pl, const char *why) {
    if (!C.on) return;
    if (rd(G_FLOW) == 6u && pl && mover_type(pl) == MOVER_DEBUG && !nfs_photo_active()) { uint32_t z = 0; nfs_guest_call(FN_OWN_CAMERA, pl, &z, 1, 0); }
    C.on = 0;
    LOGI("Cinema: off (%s), %d shots", why, C.shots);
}
static void next_shot(const float *c, const float *f, const float *l, double t) {
    int s;
    do s = rand() % S_N; while (s == C.shot && S_N > 1);
    C.shot = s; C.start = t; C.dur = rnd(SHOT_MIN_S, SHOT_MAX_S); C.side = rand() & 1 ? 1.f : -1.f; C.spin = rnd(0.f, 6.283f);
    C.shots++;
    if (s == S_ROADSIDE) {   /* a camera by the road ahead, the car goes by it */
        float a = rnd(30.f, 45.f), b = rnd(6.f, 10.f) * C.side;
        for (int i = 0; i < 3; i++) C.fix[i] = c[i] + f[i] * a + l[i] * b;
        C.fix[2] += 1.4f;
        C.dur = 8.f;
    }
    if (C.shots <= 8) LOGI("Cinema: shot %d: %s, %.1f s", C.shots, k_shot[s], (double)C.dur);   /* (the first ones only) */
}
static void shot(const float *c, const float *f, const float *l, const float *u, float k, float *eye, float *look) {
    float e[3], o[3];
    for (int i = 0; i < 3; i++) { e[i] = c[i]; o[i] = c[i] + u[i] * 0.5f; }
    switch (C.shot) {
    case S_SIDE:                       /* beside it, sliding from its back to its front */
        for (int i = 0; i < 3; i++) e[i] += l[i] * 6.f * C.side + f[i] * (-3.f + 6.f * k) + u[i] * 1.0f;
        C.fov = 45.f; break;
    case S_FRONT:                      /* ahead of it, low, the car coming at us */
        for (int i = 0; i < 3; i++) { e[i] += f[i] * 7.5f + l[i] * 1.2f * C.side + u[i] * 0.45f; o[i] = c[i] + u[i] * 0.6f; }
        C.fov = 42.f; break;
    case S_CHASE:                      /* behind and above, wide, the road ahead */
        for (int i = 0; i < 3; i++) { e[i] += f[i] * -13.f + u[i] * 5.5f; o[i] = c[i] + f[i] * 6.f; }
        C.fov = 62.f; break;
    case S_WHEEL:                      /* on the road by its back wheel, looking ahead */
        for (int i = 0; i < 3; i++) { e[i] += l[i] * 1.25f * C.side + f[i] * -1.4f + u[i] * 0.22f; o[i] = c[i] + f[i] * 8.f + u[i] * 0.25f; }
        C.fov = 72.f; break;
    case S_ROADSIDE:                   /* standing still, the car going by */
        for (int i = 0; i < 3; i++) e[i] = C.fix[i];
        C.fov = 38.f; break;
    case S_ORBIT: {                    /* round it, slowly */
        float a = C.spin + k * 1.6f * C.side, cs = cosf(a), sn = sinf(a);
        for (int i = 0; i < 3; i++) e[i] += (f[i] * cs + l[i] * sn) * 7.f + u[i] * 1.8f;
        C.fov = 50.f; break;
    }
    default:                           /* S_HELI: high above, behind */
        for (int i = 0; i < 3; i++) { e[i] += u[i] * 24.f + f[i] * -12.f; o[i] = c[i] + f[i] * 6.f; }
        C.fov = 55.f; break;
    }
    memcpy(eye, e, sizeof e); memcpy(look, o, sizeof o);
}

static void place_shot(uint32_t pl, uint32_t car) {
    double t = now_s();
    float c[3], fw[3], f[3], l[3], u[3] = { 0.f, 0.f, 1.f };
    rd3(car + CAR_PLACE, c); rd3(car + CAR_FWD, fw);
    if (!isfinite(c[0] + c[1] + c[2] + fw[0] + fw[1])) return;
    c[2] += 0.4f;
    float dt = C.last > 0 ? (float)(t - C.last) : 0.f;
    C.last = t;
    float yaw = atan2f(fw[1], fw[0]);
    if (!C.have_yaw || dt <= 0.f || dt > 0.5f) { C.yaw = yaw; C.have_yaw = 1; }
    else {
        float d = yaw - C.yaw;
        while (d > 3.14159265f) d -= 6.2831853f;
        while (d < -3.14159265f) d += 6.2831853f;
        C.yaw += d * (1.f - expf(-dt / YAW_TAU_S));
    }
    f[0] = cosf(C.yaw); f[1] = sinf(C.yaw); f[2] = 0.f;
    l[0] = -f[1]; l[1] = f[0]; l[2] = 0.f;
    float done = C.shot < 0 ? 1.f : (float)(t - C.start) / C.dur;
    if (C.shot == S_ROADSIDE && done < 1.f) {          /* the car gone far past the roadside camera: cut */
        float dx = c[0] - C.fix[0], dy = c[1] - C.fix[1], ahead = dx * f[0] + dy * f[1];
        if (ahead > 25.f || hypotf(dx, dy) > 70.f) done = 1.f;
    }
    if (done >= 1.f) { next_shot(c, f, l, t); done = 0.f; }
    float eye[3], look[3];
    shot(c, f, l, u, done, eye, look);
    uint32_t m = rd(pl + PL_MOVER);
    if (ok(m, MV_KEYS + 20u)) memset((void *)ADDR(m + MV_KEYS), 0, 20);
    MEM32(G_FOLLOW) = 0xffffffffu;
    MEM32(G_NUDGE) = 0; MEM32(G_NUDGE + 4u) = 0; MEM32(G_NUDGE + 8u) = 0;
    for (int i = 0; i < 3; i++) { wrf(G_EYE + 4u * (uint32_t)i, eye[i]); wrf(G_LOOK + 4u * (uint32_t)i, look[i]); }
}

/* guest thread, after each Present: the debug camera given (or taken back), the shots placed by the mover's wrap */
void nfs_cinema_frame(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + PL_CAR) : 0;
    int want = atomic_load(&s_want) && nfs_autopilot_active() && rd(G_FLOW) == 6u && ok(car, CAR_UP + 12u);
    if (!want) { release(pl, "the autopilot or the switch"); return; }
    if (nfs_photo_active()) return;                    /* (photo mode has the camera) */
    double t = now_s();
    if (mover_type(pl) != MOVER_DEBUG) {
        uint32_t arg[2] = { MOVER_DEBUG, 0u };
        nfs_guest_call(FN_SET_MOVER, pl, arg, 2, 0);
        if (mover_type(pl) != MOVER_DEBUG) return;
        if (!C.on) { C.on = 1; C.shots = 0; C.shot = -1; C.start = 0; C.have_yaw = 0; C.last = 0; srand((unsigned)(t * 1000.0)); LOGI("Cinema: on"); }
    }
    C.on = 1;
}

/* The debug camera mover's update (0x44D420, its vtable 0x787E8C: it looks from [0x7EC4D0] at [0x8286A0]): the shot
 * placed right before it, from where the car is as the camera looks, so the car stands where the camera sees it in
 * every frame (placed after the frame, the car was a world step ahead of the camera, more or less with each frame's
 * time: it shook back and forth). The shots are worked out in a level frame of the car's heading, smoothed: the
 * car's own axes pitch and roll with its suspension (the throttle and the governor's cuts rock it). */
void sub_0044D420_orig(void);
void nfs_cinema_ov_mover(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + PL_CAR) : 0;
    if (C.on && !nfs_photo_active() && ok(car, CAR_UP + 12u)) place_shot(pl, car);
    sub_0044D420_orig();
}
#endif
