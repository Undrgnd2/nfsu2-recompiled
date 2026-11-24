/*
 * maxperf.c - the solo cheats' "Performances au maximum" (Android, the ⚙ menu's Triches): every performance package
 * of every category put in a career car of the player's choice (or all of them), up to the last level the game has
 * (the unique ones included, if the game has them past Extreme).
 *
 * A car's performance is a struct of the game's (0x94 bytes: +0 ten floats, +0x28 ten flags, +0x50 a byte for each
 * of the 68 packages: installed), the same in the career's car record (+0x2CC, saved with the profile) and in a ride
 * (RideInfo +0x2F0, the car driven: its physics info is ride +0x10). The game's own 0x5A64A0 (thiscall on it: level,
 * category or -1 for every one) takes the packages off and puts in each level from 1 to the one asked (0x500100
 * category, level: the level's packages, none past the last), each with what it needs (0x5A6320). For the car driven
 * in free roam its ride gets them too and its physics are made again (0x5B4DC0 on the physics info: 0, 1, 1; as a
 * race's car messages make them): the car drives with them at once. The record is saved when the profile is.
 * Career only, not LAN or Online. Asked from the UI thread, done on the game's (nfs_maxperf_frame).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);

#define G_GAMEMODE    0x00850078u
#define G_FLOW        0x008654a4u
#define G_PLAYER1     0x008900acu
#define G_CARTYPES    0x008a1cccu
#define CT_SIZE       0x890u
#define CT_N          0x2eu
#define CT_TEXNAME    0x20u
#define CT_MAKE       0xc0u
#define CT_HASH       0xd0u
#define REC_TYPE      0x20u
#define REC_PERF      0x2ccu
#define RIDE_PERF     0x2f0u
#define RIDE_PI       0x10u
#define PERF_SIZE     0x94u
#define CAR_RIDE      0x10u
#define FN_PERF_LEVEL 0x005a64a0u
#define FN_PI_BUILD   0x005b4dc0u
#define TOP_LEVEL     5u            /* (a level the game has not puts in nothing) */
#define SLOTS         10u

static atomic_int s_req;            /* 0 none, 1 every car, 2 + i slot i */

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static int solo_career(void) { return rd(G_GAMEMODE) == 1u; }

static void str_at(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    while (i + 1 < n && ok(va + i, 1) && MEM8(va + i) >= 0x20 && MEM8(va + i) < 0x7f) { out[i] = (char)MEM8(va + i); i++; }
    out[i] = 0;
}
static void car_name(uint32_t rec, char *out, size_t n) {
    uint32_t h = rd(rec + REC_TYPE), types = rd(G_CARTYPES);
    snprintf(out, n, "Voiture");
    if (!h || !ok(types, CT_SIZE * CT_N)) return;
    for (uint32_t i = 0; i < CT_N; i++) {
        uint32_t t = types + CT_SIZE * i;
        if (MEM32(t + CT_HASH) != h) continue;
        char make[32], model[32];
        str_at(t + CT_MAKE, make, sizeof make); str_at(t + CT_TEXNAME, model, sizeof model);
        if (!model[0]) str_at(t, model, sizeof model);
        snprintf(out, n, "%s%s%s", make, make[0] ? " " : "", model);
        return;
    }
}

/* the career's cars, one a line: "slot<TAB>name<TAB>1 when driven" (any thread: names only); their number */
int nfs_maxperf_list(char *out, int n) {
    int count = 0, at = 0;
    if (n > 0) out[0] = 0;
    if (!solo_career()) return 0;
    uint32_t driven = nfs_garage_driven();
    for (unsigned i = 0; i < SLOTS && at < n; i++) {
        uint32_t r = nfs_garage_slot_car(i);
        if (!r) continue;
        char name[72]; car_name(r, name, sizeof name);
        int w = snprintf(out + at, (size_t)(n - at), "%u\t%s\t%d\n", i, name, r == driven);
        if (w < 0 || w >= n - at) break;
        at += w; count++;
    }
    return count;
}
/* -1 every car, else a slot (any thread): 0 not now (not a solo career), 1 asked */
int nfs_maxperf(int slot) {
    if (!solo_career() || slot < -1 || slot >= (int)SLOTS) return 0;
    atomic_store(&s_req, slot < 0 ? 1 : 2 + slot);
    return 1;
}

static uint32_t level(uint32_t perf, uint32_t lv) {
    uint32_t a[2] = { lv, 0xffffffffu };
    return nfs_guest_call(FN_PERF_LEVEL, perf, a, 2, 0);
}
static void maxed(uint32_t perf, const char *what) {
    uint32_t n3 = level(perf, 3u), n4 = level(perf, 4u), n = level(perf, TOP_LEVEL);
    LOGI("MaxPerf: %s: %u packages (%u to Extreme, %u to level 4)", what, n, n3, n4);
}
void nfs_maxperf_frame(void) {
    int req = atomic_exchange(&s_req, 0);
    if (!req || !solo_career()) return;
    uint32_t driven = nfs_garage_driven();
    int done = 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        uint32_t r = nfs_garage_slot_car(i);
        if (!r || (req >= 2 && (unsigned)(req - 2) != i) || !ok(r + REC_PERF, PERF_SIZE)) continue;
        char name[72], what[96]; car_name(r, name, sizeof name);
        snprintf(what, sizeof what, "slot %u (%s)", i + 1u, name);
        maxed(r + REC_PERF, what);
        done++;
        if (r != driven || rd(G_FLOW) != 6u) continue;
        /* the car driven: its ride and physics now */
        uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0, ride = car ? rd(car + CAR_RIDE) : 0;
        if (!ok(ride + RIDE_PERF, PERF_SIZE)) continue;
        maxed(ride + RIDE_PERF, "the car driven");
        uint32_t a[3] = { 0, 1, 1 };
        nfs_guest_call(FN_PI_BUILD, ride + RIDE_PI, a, 3, 0);
        LOGI("MaxPerf: the car driven's physics made again");
    }
    if (!done) LOGI("MaxPerf: no car (asked %d)", req);
}
#endif
