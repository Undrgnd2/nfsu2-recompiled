/*
 * ghost.c - Ghost mode in free roam (Android): the GHOST touch button lets the
 * game's own cars (traffic and AI racers) pass through the player's car, and
 * each car passed through gives back some nitrous.
 *
 * The pass-through is nfsu2-web's "Ghost mode: Everything" (runtime/roam.c and
 * roam_game.c ghost_pair / roam_game_camera_cars, MIT, Copyright (c) 2026
 * th0mk), for the player's car alone:
 *  - a touching pair of collision bodies (0x5B2610, thiscall, from the
 *    collision step 0x5FFA50 only) between the player's car and one of the
 *    game's cars (car +0x24: 3 an AI racer, 4 traffic) is let go without the
 *    game's resolve (no rewind, impulse, sparks or crash sound); the pair's
 *    contact count (+0x240) is made full ([0x802084] a step) so the step
 *    does not test it again;
 *  - the drive camera's test for a car its eye is in (0x44AC10, thiscall,
 *    ret 8) leaves out the cars passing through, so the camera does not hop
 *    over them (they are marked inactive, Car +0x550, for the call).
 * The nitrous (Car +0x40: +0x414 a full shot, +0x41C what was left when it
 * went on, +0x420 when it went on, game ticks [0x86518C] at 4000 a second):
 * each new car passed through adds GAIN of a full shot to what is left,
 * never more than a full shot.
 * Only in free roam itself, driving (not in a race, a career race or LAN).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_005B2610_orig(void);
void sub_0044AC10_orig(void);

#define G_FLOW          0x008654a4u
#define G_GAMEMODE      0x00850078u
#define G_RACETYPE      0x0089e7b0u
#define G_RACE          0x00890118u
#define G_CAREER_RACE   0x00863470u
#define G_PLAYER1       0x008900acu
#define G_WORLD         0x00890080u   /* World*: +0x10 cars, +0x1C cars[] */
#define G_TICKS         0x0086518cu
#define G_PAIR_PASSES   0x00802084u
#define WORLD_INDEX     22u
#define CAR_ACTIVE      0x550u
#define CAR_DRIVER      0x24u
#define CAR_BODY        0x3cu
#define CAR_NOS         0x40u
#define DRIVER_RACER    3u
#define DRIVER_TRAFFIC  4u
#define CONTACT_PAIR    0x14u
#define GP_N            8
#define GAIN            0.20f         /* of a full shot, each car passed through */
#define COOLDOWN        120u          /* frames before the same car gives nitrous again */
#define FLASH_FRAMES    60
#define NOTE_FRAMES     180

static struct {
    int want, note, flash;
    uint32_t step, passes;
    uint32_t car[GP_N], seen[GP_N], paid[GP_N]; int at;
} G;
static atomic_int s_presses;
static atomic_int s_status[3];

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static uint32_t player_car(void) { uint32_t p = rd(G_PLAYER1); return p ? rd(p + 4u) : 0; }
static int roaming(void) {
    uint32_t race = rd(G_RACE);
    return MEM32(G_FLOW) == 6u && ok(race, 0xc44) && MEM32(race) == 4u && MEM32(race + 0x24u) == 1u &&
           !MEM32(G_CAREER_RACE) && MEM32(G_RACETYPE) == 0u && !(MEM32(G_GAMEMODE) & 0x208u) && player_car();
}
static int active(void) { return G.want && roaming(); }
/* a collision body's car (body +0x130), if that car has this body */
static uint32_t body_car(uint32_t body) {
    uint32_t car = ok(body, 0x134) ? MEM32(body + 0x130u) : 0;
    return ok(car, 0x9e0) && MEM32(car + CAR_BODY) == body ? car : 0;
}
static int game_car(uint32_t car) { uint32_t d = MEM32(car + CAR_DRIVER); return d == DRIVER_RACER || d == DRIVER_TRAFFIC; }
static int recent(uint32_t car) {
    for (int j = 0; j < GP_N; j++) if (G.car[j] == car && G.step - G.seen[j] <= 1u) return j;
    return -1;
}

/* a full shot's GAIN more nitrous, at most a full shot left */
static void nos_add(uint32_t car) {
    uint32_t n = car + CAR_NOS;
    if (!ok(n + 0x414u, 16)) return;
    int32_t full = (int32_t)MEM32(n + 0x414u), left = (int32_t)MEM32(n + 0x41cu);
    uint32_t since = MEM32(n + 0x420u);
    if (full <= 0 || full == 0x7fffffff || left == 0x7fffffff) return;
    int on = since && since != 0x7fffffffu;
    int32_t used = on ? (int32_t)(MEM32(G_TICKS) - since) : 0, now = left - used;
    if (now < 0) now = 0;
    int32_t want = now + (int32_t)(GAIN * (float)full);
    if (want > full) want = full;
    if (want <= now) return;
    MEM32(n + 0x41cu) = (uint32_t)(want + used);
    G.flash = FLASH_FRAMES;
    LOGI("Ghost: +%.0f%% nitrous (%.0f%% -> %.0f%%)", (double)(GAIN * 100.f), 100.0 * now / full, 100.0 * want / full);
}

/* 1: the pair is let through */
static int ghost_pair(uint32_t contact) {
    if (!active() || !ok(contact, CONTACT_PAIR + 4u)) return 0;
    uint32_t a = body_car(MEM32(contact + 0xcu)), b = a ? body_car(MEM32(contact + 0x10u)) : 0, me = player_car();
    if (!b || (a != me && b != me)) return 0;
    uint32_t theirs = a == me ? b : a;
    if (!game_car(theirs)) return 0;
    int i = recent(theirs);
    if (i < 0) {
        int j = -1;
        for (int k = 0; k < GP_N && j < 0; k++) if (G.car[k] == theirs) j = k;
        if (j < 0) { j = G.at; G.at = (G.at + 1) % GP_N; G.car[j] = theirs; G.paid[j] = G.step - COOLDOWN - 1u; }
        i = j;
        G.passes++;
        if (G.step - G.paid[i] > COOLDOWN) { G.paid[i] = G.step; nos_add(me); }
        if (G.passes <= 16 || G.passes % 100u == 0)
            LOGI("Ghost: through %s %08x (%u so far)", MEM32(theirs + CAR_DRIVER) == DRIVER_RACER ? "an AI racer" : "a traffic car", theirs, G.passes);
    }
    G.seen[i] = G.step;
    uint32_t pr = MEM32(contact + CONTACT_PAIR);
    int32_t most = (int32_t)MEM32(G_PAIR_PASSES);
    if (ok(pr, 0x244) && most > 0 && (int32_t)MEM32(pr + 0x240u) < most - 1) MEM32(pr + 0x240u) = (uint32_t)(most - 1);
    return 1;
}

void nfs_ghost_press(void) { atomic_fetch_add(&s_presses, 1); }
void nfs_ghost_status(int out[3]) { for (int i = 0; i < 3; i++) out[i] = atomic_load(&s_status[i]); }

void nfs_ghost_frame(void) {
    int n = atomic_exchange(&s_presses, 0), roam = roaming();
    G.step++;
    if (G.note > 0) G.note--;
    if (G.flash > 0) G.flash--;
    for (int i = 0; i < n; i++) {
        uint32_t flow = MEM32(G_FLOW), race = rd(G_RACE);
        int free_roam = flow == 6u && ok(race, 0xc44) && MEM32(race + 0x24u) == 1u && !MEM32(G_CAREER_RACE) &&
                        MEM32(G_RACETYPE) == 0u && !(MEM32(G_GAMEMODE) & 0x208u);
        if (free_roam) { G.want = !G.want; G.note = 0; LOGI("Ghost: ghost mode %s", G.want ? "on" : "off"); }
        else { G.note = NOTE_FRAMES; LOGI("Ghost: button outside free roam: nothing"); }
    }
    atomic_store(&s_status[0], G.want | (G.want && roam ? 2 : 0) | (G.note > 0 ? 4 : 0) | (G.flash > 0 ? 8 : 0));
    atomic_store(&s_status[1], (int)G.passes);
    atomic_store(&s_status[2], (int)(GAIN * 100.f));
}

/* 0x5B2610 (thiscall, ret): a touching pair of collision bodies */
void nfs_ghost_ov_contact(void) {
    if (ghost_pair(g_ecx)) { g_esp += 4; return; }
    sub_005B2610_orig();
}
/* 0x44AC10 (thiscall (eye, flag), ret 8): the drive camera's car under its eye; the cars passing through left out */
void nfs_ghost_ov_camera_cars(void) {
    uint32_t w = active() ? rd(G_WORLD) : 0, cnt = ok(w, 0x80) ? MEM32(w + 0x10u) : 0, hid[WORLD_INDEX], me = player_car();
    uint8_t was[WORLD_INDEX];
    unsigned nh = 0;
    for (uint32_t i = 0; i < cnt && i < WORLD_INDEX; i++) {
        uint32_t car = MEM32(w + 0x1cu + 4u * i);
        if (!ok(car, 0x9e0) || car == me || !MEM8(car + CAR_ACTIVE) || !game_car(car) || recent(car) < 0) continue;
        hid[nh] = car; was[nh++] = MEM8(car + CAR_ACTIVE);
        MEM8(car + CAR_ACTIVE) = 0;
    }
    sub_0044AC10_orig();
    while (nh > 0) { nh--; MEM8(hid[nh] + CAR_ACTIVE) = was[nh]; }
}
#endif
