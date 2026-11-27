/*
 * look.c - the touch controls' look stick (Android): the view turns round the car to where the stick points (right
 * looks right, half way out half as far, straight down behind) and swings back behind the car once it is let go.
 *
 * Adapted from nfsu2-web's runtime/look.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there), without
 * its gamepad parts (the Look Back snap: the touch controls have their own Look Back button):
 *   - the drive camera places the camera each frame in the main loop's update; the frame is drawn after. So the
 *     camera is turned from the main view's setup (photo.c's wrap of 0x5C7EC0, view 1, the first of the frame; view 4,
 *     the road's reflection, when it has a camera of its own) until after Present (the frame chain: nfs_look_frame),
 *     when its bytes go back as the game made them. Everything drawn reads the turned camera: the views, their
 *     culling, and the car renderer (0x620AE0), which takes the eye from the view's camera for the car's parts it
 *     draws and its reflections;
 *   - the turn: the world about a vertical line through the car's centre (car +0x60), the camera going round the car;
 *     with the eye in or on the car (the bumper and hood cameras) about the eye, a turn of the head. z is up. Each
 *     frame the turn moves towards the stick's (TAU_S), back at rest the short way round.
 * Only while the player drives with the drive camera and the HUD on top (free roam, races); not in photo mode or the
 * cinema camera (theirs), a replay, a cutscene or a screen over the HUD: there the view is straight behind at once.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_FLOW       0x008654a4u
#define G_PLAYER1    0x008900acu   /* Player*: +4 its car, +0x9A4 its HUD, +0x238 its camera mover */
#define G_RACE       0x00890118u
#define G_FENG       0x008384c4u
#define PL_CAR       4u
#define PL_HUD       0x9a4u
#define PL_MOVER     0x238u
#define MV_TYPE      0xcu
#define MOVER_DRIVE  1u
#define CAR_POS      0x60u         /* the car's centre, world */
#define CAM_POS      0x40u         /* the camera's eye, world */
#define CAM_FWD      0x50u         /*   its forward */
#define CAM_TARGET   0x60u         /*   the point it looks at */
#define CAM_TURNED   0x70u         /* its bytes the turn changes: the matrix, those three */
#define NCAM         2             /* the cameras turned in a frame: view 1's, view 4's if its own */

#define STICK_ON     0.2f          /* the stick starts a look past this far out */
#define STICK_FULL   0.85f         /* and turns the whole way from this far */
#define TAU_S        0.09f         /* the turn's time constant towards the stick's */
#define BACK_RAD     2.4f          /* a stick pointing back past this (rad) keeps the turn's side */
#define IN_CAR_M     3.f           /* the eye this near the car's centre (m, flat): the bumper and hood cameras */
#define REST_RAD     0.0005f
#define PI_F         3.14159265f

static atomic_int s_x, s_y;       /* the stick, thousandths: x + right, y + up (forward) */

static struct {
    float yaw, px, py;             /* the turn now (rad, + looks right), the line it turns about */
    int on, n;                     /* turned this frame; the cameras turned now (until nfs_look_frame) */
    double t;
    uint32_t cam[NCAM];
    uint8_t keep[NCAM][CAM_TURNED], made[NCAM][CAM_TURNED];
} L;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t u = MEM32(va); float f; memcpy(&f, &u, 4); return f; }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static float wrap(float a) { while (a > PI_F) a -= 2.f * PI_F; while (a < -PI_F) a += 2.f * PI_F; return a; }
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}

/* any thread (the touch controls): the stick, thousandths, x + right, y + up; 0 0 let go */
void nfs_look_stick(int x, int y) { atomic_store(&s_x, x); atomic_store(&s_y, y); }

/* the player's car while it drives with the drive camera and the HUD on top (0: no look now) */
static uint32_t driving_car(void) {
    uint32_t pl = rd(G_PLAYER1), r = rd(G_RACE);
    if (rd(G_FLOW) != 6u || !ok(pl, PL_HUD + 4u) || !ok(r, 4) || MEM32(r) != 4u) return 0;
    if (nfs_photo_active() || nfs_cinema_active()) return 0;
    uint32_t car = rd(pl + PL_CAR), mv = rd(pl + PL_MOVER);
    if (!ok(car, CAR_POS + 12u) || !ok(mv, MV_TYPE + 4u) || MEM32(mv + MV_TYPE) != MOVER_DRIVE) return 0;
    uint32_t hud = rd(pl + PL_HUD), fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0;
    char a[40], b[40];
    read_str(top ? rd(top + 0xcu) : 0, a, sizeof a); read_str(hud ? rd(hud + 0x10u) : 0, b, sizeof b);
    return a[0] && !strcasecmp(a, b) ? car : 0;
}

/* the turn the stick asks for now (rad, + right; 0 at rest), its back side kept as the turn now has it */
static float stick_yaw(void) {
    float x = atomic_load(&s_x) / 1000.f, y = atomic_load(&s_y) / 1000.f, r = hypotf(x, y);
    if (r < STICK_ON) return 0.f;
    float k = fminf(1.f, (r - STICK_ON) / (STICK_FULL - STICK_ON));
    float dir = atan2f(x, y);                        /* up 0, right +pi/2, down pi, left -pi/2 */
    int side = fabsf(L.yaw) > 0.05f ? (L.yaw > 0.f ? 1 : -1) : (x >= 0.f ? 1 : -1);
    if (fabsf(dir) > BACK_RAD && (dir > 0.f ? 1 : -1) != side) dir += side * 2.f * PI_F;
    return dir * k;
}

/* p (a point, or a direction with pt 0) turned as the view is: clockwise seen from above about the line */
static void turn_xy(float *p, int pt) {
    float c = cosf(L.yaw), s = sinf(L.yaw), ox = pt ? L.px : 0.f, oy = pt ? L.py : 0.f, x = p[0] - ox, y = p[1] - oy;
    p[0] = ox + x * c + y * s; p[1] = oy - x * s + y * c;
}
/* the camera's matrix (rows, the translation in the last) with the world turned about the line first */
static void turn_matrix(float *M) {
    float c = cosf(L.yaw), s = -sinf(L.yaw);
    float tx = L.px - (L.px * c + L.py * s), ty = L.py - (-L.px * s + L.py * c), r0[4], r1[4];
    for (int k = 0; k < 4; k++) { r0[k] = c * M[k] - s * M[4 + k]; r1[k] = s * M[k] + c * M[4 + k]; }
    for (int k = 0; k < 4; k++) { M[12 + k] += tx * M[k] + ty * M[4 + k]; M[k] = r0[k]; M[4 + k] = r1[k]; }
}
static void turn_camera(uint32_t cam) {
    if (!ok(cam, CAM_TURNED) || L.n >= NCAM) return;
    for (int i = 0; i < L.n; i++) if (L.cam[i] == cam) return;
    uint8_t *p = (uint8_t *)ADDR(cam);
    float m[CAM_TURNED / 4];
    memcpy(L.keep[L.n], p, CAM_TURNED);
    memcpy(m, p, CAM_TURNED);
    turn_matrix(m);
    turn_xy(m + CAM_POS / 4, 1); turn_xy(m + CAM_FWD / 4, 0); turn_xy(m + CAM_TARGET / 4, 1);
    memcpy(p, m, CAM_TURNED);
    memcpy(L.made[L.n], p, CAM_TURNED);
    L.cam[L.n++] = cam;
}

/* the frame chain, after Present: the cameras back as the game made them (unless something wrote them since) */
void nfs_look_frame(void) {
    for (int i = 0; i < L.n; i++) {
        uint32_t cam = L.cam[i];
        if (ok(cam, CAM_TURNED) && !memcmp((const void *)ADDR(cam), L.made[i], CAM_TURNED))
            memcpy((void *)ADDR(cam), L.keep[i], CAM_TURNED);
    }
    L.n = 0;
}

/* photo.c's wrap of 0x5C7EC0, before the view is made (view at +4 of it: its id, +0x40 its camera) */
void nfs_look_view(uint32_t view) {
    if (!ok(view, 0x44u)) return;
    uint32_t id = MEM32(view + 4u), cam = MEM32(view + 0x40u);
    if (id == 4u) { if (L.on) turn_camera(cam); return; }   /* the road's reflection: a camera of its own */
    if (id != 1u) return;
    for (int i = 0; i < L.n; i++) if (L.cam[i] == cam) return;   /* made again this frame: turned already */
    if (L.n) nfs_look_frame();                                   /* a frame that ended without the chain */
    double t = now_s();
    float dt = L.t > 0. ? (float)(t - L.t) : 0.f;
    L.t = t;
    if (dt > 0.25f) dt = 0.25f;
    uint32_t car = driving_car();
    float want = car ? stick_yaw() : 0.f, d = want - L.yaw;
    if (d > PI_F) d -= 2.f * PI_F; else if (d < -PI_F) d += 2.f * PI_F;
    L.yaw = wrap(L.yaw + d * (1.f - expf(-dt / TAU_S)));
    if (!car || (want == 0.f && fabsf(L.yaw) < REST_RAD)) { L.yaw = car ? L.yaw : 0.f; if (fabsf(L.yaw) < REST_RAD) L.yaw = 0.f; L.on = 0; return; }
    float cx = rdf(car + CAR_POS), cy = rdf(car + CAR_POS + 4u);
    if (ok(cam, CAM_POS + 12u)) {
        float ex = rdf(cam + CAM_POS), ey = rdf(cam + CAM_POS + 4u);
        if (hypotf(ex - cx, ey - cy) < IN_CAR_M) { cx = ex; cy = ey; }
    }
    L.px = cx; L.py = cy; L.on = 1;
    turn_camera(cam);
}
#endif
