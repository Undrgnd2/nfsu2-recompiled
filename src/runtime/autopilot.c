/*
 * autopilot.c - the player's car driven by the game's own AI in career free roam (Android, the touch button "Pilote
 * auto"), to look at the city while the car drives itself; with the cruise control on, at its speed at most.
 *
 * The game's Player::AutoPilotOn (0x5FAE20, thiscall Player) hands the player's car to its AI driver (the car's driver
 * kind +0x24: 3), as the game does at a race's end; Player::AutoPilotOff (0x5FAF10) gives it back to a human driver
 * (1), and SetInputMode (0x605C70, thiscall Player (2)) the player's controls back to it (nfsu2-web career.c's
 * "autopilot"). The speed: the car's governor, its physics info's +0x2C0 (mph; ride +0x10; copied to the chassis every
 * frame, 0x5A49A0; 0 none: any ECU part), held at the cruise control's speed while both are on (cruise.c's buttons
 * set it), put back after. The AI drives at a pace of its own (about 60 km/h in free roam), its target speed times
 * its driver's factor +0x770 (1 its own pace; 0x41C5E0 reads it, 0x4172D0 keeps it outside a race's catch-up, as
 * nfsu2-web catchup.c has it); the driver: the car's +0x2C (its +4 the car). With the cruise control on, that factor
 * is steered each frame so the car goes at the cruise control's speed (the governor its ceiling), put back after. Off: the button again, a pedal or the steering touched (the touch controls), a jump of
 * the car, leaving career free roam (a race, a shop, a menu's load). Not in LAN or Online.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);

#define G_GAMEMODE       0x00850078u
#define G_RACETYPE       0x0089e7b0u
#define G_FLOW           0x008654a4u
#define G_RACEPARAMS     0x0089e7a0u
#define G_PLAYER1        0x008900acu
#define FN_AUTOPILOT_ON  0x005fae20u
#define FN_AUTOPILOT_OFF 0x005faf10u
#define FN_SETINPUTMODE  0x00605c70u
#define CAR_DRIVER       0x24u         /* 1 human, 3 AI */
#define CAR_RIDE         0x10u
#define CAR_PLACE        0x60u
#define CAR_SPEED        0x42cu
#define PI_LIMIT_MPH     (0x10u + 0x2c0u)   /* ride +0x10 the physics info, +0x2C0 its governor (mph) */
#define DRIVER_AI        3u
#define CAR_AI           0x2cu          /* the car's AI driver (+4 the car) */
#define DRV_PACE         0x770u         /* the driver's target speed factor, 1 its own pace */
#define PACE_MIN         0.3f
#define PACE_MAX         6.f
#define PACE_K           0.8f           /* the factor's change a second per (km/h short) / (km/h asked) */
#define JUMP_M           60.f

enum { EV_ON = 1, EV_OFF, EV_LEFT, EV_NOT_HERE, EV_TOUCHED };
static atomic_int s_toggle, s_touched, s_active, s_avail, s_ev, s_ev_seq;
static struct { int on; uint32_t pl, car, ride, keep_limit, drv, keep_pace; int limited, paced; float lx, ly, pace; double t; } A;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
#include <time.h>
static uint64_t nfs_now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t u = rd(va); float f; memcpy(&f, &u, 4); return f; }
static int roam(void) {
    return rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u && rd(G_RACEPARAMS) == 4000u;
}
static void event(int ev) { atomic_store(&s_ev, ev); atomic_fetch_add(&s_ev_seq, 1); }

int nfs_autopilot_active(void) { return atomic_load(&s_active); }
/* the touch button (any thread) */
void nfs_autopilot_toggle(void) { atomic_store(&s_toggle, 1); }
/* a pedal or the steering touched while it drives (any thread): the player takes the car back */
void nfs_autopilot_touched(void) { if (atomic_load(&s_active)) atomic_store(&s_touched, 1); }
/* out: on, the button offered, last event (1 on, 2 off, 3 left, 4 not here, 5 the player took it), its number */
void nfs_autopilot_status(int out[4]) {
    out[0] = atomic_load(&s_active); out[1] = atomic_load(&s_avail); out[2] = atomic_load(&s_ev); out[3] = atomic_load(&s_ev_seq);
}

static void limit_off(void) {
    if (A.limited && A.ride && ok(A.ride + PI_LIMIT_MPH, 4)) MEM32(A.ride + PI_LIMIT_MPH) = A.keep_limit;
    A.limited = 0;
    if (A.paced && ok(A.drv + DRV_PACE, 4)) MEM32(A.drv + DRV_PACE) = A.keep_pace;
    A.paced = 0;
}
static void stop(int ev, const char *why) {
    if (!A.on) return;
    limit_off();
    if (ok(A.pl, 8) && rd(A.pl + 4u) == A.car && ok(A.car, CAR_DRIVER + 4u) && MEM32(A.car + CAR_DRIVER) == DRIVER_AI) {
        nfs_guest_call(FN_AUTOPILOT_OFF, A.pl, 0, 0, 0);
        uint32_t mode = 2;
        nfs_guest_call(FN_SETINPUTMODE, A.pl, &mode, 1, 0);
    }
    A.on = 0; atomic_store(&s_active, 0);
    event(ev);
    LOGI("Autopilot: off (%s)", why);
}

/* guest thread, after each Present */
void nfs_autopilot_frame(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    int here = roam() && ok(car, CAR_SPEED + 4u);
    atomic_store(&s_avail, here);
    int toggle = atomic_exchange(&s_toggle, 0), touched = atomic_exchange(&s_touched, 0);
    if (!A.on) {
        if (!toggle) return;
        if (!here) { event(EV_NOT_HERE); return; }
        nfs_guest_call(FN_AUTOPILOT_ON, pl, 0, 0, 0);
        if (rd(car + CAR_DRIVER) != DRIVER_AI) { event(EV_NOT_HERE); LOGI("Autopilot: the game did not take the car"); return; }
        A.on = 1; A.pl = pl; A.car = car; A.ride = rd(car + CAR_RIDE); A.limited = 0;
        A.lx = rdf(car + CAR_PLACE); A.ly = rdf(car + CAR_PLACE + 4u);
        atomic_store(&s_active, 1); event(EV_ON);
        LOGI("Autopilot: on");
        return;
    }
    if (toggle) { stop(EV_OFF, "button"); return; }
    if (touched) { stop(EV_TOUCHED, "the player took the car"); return; }
    if (!here || car != A.car || pl != A.pl) { stop(EV_LEFT, "not in free roam"); return; }
    if (rd(car + CAR_DRIVER) != DRIVER_AI) { stop(EV_LEFT, "the game gave the car back"); return; }
    float x = rdf(car + CAR_PLACE), y = rdf(car + CAR_PLACE + 4u);
    if (isfinite(x + y) && hypotf(x - A.lx, y - A.ly) > JUMP_M) { stop(EV_LEFT, "the car jumped"); return; }
    A.lx = x; A.ly = y;
    /* the cruise control's speed: the AI's pace steered to it, the car's governor its ceiling */
    float kmh = nfs_cruise_held();
    double now = (double)nfs_now_ms() * 0.001;
    float dt = A.t > 0 ? (float)(now - A.t) : 0.f;
    A.t = now;
    if (dt < 0.f || dt > 0.25f) dt = 0.f;
    if (kmh > 0.f && ok(A.ride + PI_LIMIT_MPH, 4)) {
        if (!A.limited) {
            A.keep_limit = MEM32(A.ride + PI_LIMIT_MPH); A.limited = 1;
            uint32_t drv = rd(car + CAR_AI);
            A.paced = ok(drv + DRV_PACE, 4) && rd(drv + 4u) == car;
            if (A.paced) { A.drv = drv; A.keep_pace = MEM32(drv + DRV_PACE); memcpy(&A.pace, &A.keep_pace, 4); if (!(A.pace >= PACE_MIN && A.pace <= PACE_MAX)) A.pace = 1.f; }
            LOGI("Autopilot: at %.0f km/h (%s)", (double)kmh, A.paced ? "its pace steered, the governor at that" : "the governor at that: no driver's pace found");
        }
        float mph = (kmh + 3.f) / 1.609344f; uint32_t u; memcpy(&u, &mph, 4);
        MEM32(A.ride + PI_LIMIT_MPH) = u;
        if (A.paced && ok(A.drv + DRV_PACE, 4)) {
            float v = fabsf(rdf(car + CAR_SPEED)) * 3.6f;
            if (isfinite(v)) A.pace += PACE_K * (kmh - v) / kmh * dt;
            A.pace = A.pace < PACE_MIN ? PACE_MIN : A.pace > PACE_MAX ? PACE_MAX : A.pace;
            memcpy(&u, &A.pace, 4); MEM32(A.drv + DRV_PACE) = u;
        }
    } else if (A.limited || A.paced) limit_off();
}
#endif
