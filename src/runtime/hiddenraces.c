/*
 * hiddenraces.c - the career's hidden races on the map like the others (Android, the ⚙ menu).
 *
 * Adapted from nfsu2-web's runtime/hiddenraces.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there), as
 * MaxHwoy's Show Hidden Races Mod has them: the career's race table ([0x8383E0] x [0x8383E4], 0x88 bytes, +0x7F 1
 * hidden: 33 races, STAGE_0_CIRCUIT_0 and S2_H_* to S5_H_*) and its stages ([0x83841C] x [0x838418], 0x50 bytes,
 * +0x30..+0x34 the most circuits, sprints, drags, drifts and Street X races open at once). The map and the radar leave
 * a hidden race out (0x532C20): only its marker on the street shows it. With the switch on, +0x7F is cleared on all
 * of them and each stage's caps raised to 8, so the races once hidden join the pick without taking others off the
 * map; off again, the tables as they were. The career's open races are made again after a race or a load (0x527D30).
 * Nothing of it is in the profile.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_RACES     0x008383e0u
#define G_NRACES    0x008383e4u
#define G_STAGES    0x0083841cu
#define G_NSTAGES   0x00838418u
#define RACE_SZ     0x88u
#define STAGE_SZ    0x50u
#define R_HIDDEN    0x7fu
#define S_CAPS      0x30u
#define CAP         8
#define MAX_RACES   1024u
#define MAX_STAGES  16u

static atomic_int s_on;
static struct {
    uint32_t races, stages;               /* the tables changed (0: as the game has them) */
    uint8_t hidden[MAX_RACES], caps[MAX_STAGES][5];
    uint32_t nr, ns;
} H;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && n && nfs_guest_readable(va, n); }

void nfs_hiddenraces_enable(int on) { atomic_store(&s_on, on != 0); }

/* guest thread, after each Present: the tables changed or put back when the switch says */
void nfs_hiddenraces_frame(void) {
    int on = atomic_load(&s_on);
    uint32_t races = ok(G_RACES, 4) ? MEM32(G_RACES) : 0, nr = ok(G_NRACES, 4) ? MEM32(G_NRACES) : 0;
    uint32_t stages = ok(G_STAGES, 4) ? MEM32(G_STAGES) : 0, ns = ok(G_NSTAGES, 4) ? MEM32(G_NSTAGES) : 0;
    if (H.races && (races != H.races || stages != H.stages)) H.races = H.stages = 0;   /* (tables loaded again) */
    if (on && !H.races) {
        if (!races || nr > MAX_RACES || ns > MAX_STAGES || !ok(races, nr * RACE_SZ) || !ok(stages, ns * STAGE_SZ)) return;
        int n = 0, caps = 0;
        for (uint32_t i = 0; i < nr; i++) {
            uint32_t a = races + i * RACE_SZ + R_HIDDEN;
            H.hidden[i] = MEM8(a);
            if (MEM8(a)) { MEM8(a) = 0; n++; }
        }
        for (uint32_t s = 0; s < ns; s++)
            for (uint32_t k = 0; k < 5u; k++) {
                uint32_t a = stages + s * STAGE_SZ + S_CAPS + k;
                int8_t v = (int8_t)MEM8(a);
                H.caps[s][k] = MEM8(a);
                if (v >= 0 && v < CAP) { MEM8(a) = CAP; caps++; }
            }
        H.races = races; H.stages = stages; H.nr = nr; H.ns = ns;
        LOGI("HiddenRaces: %d of %u races out of hiding, %d of the %u stages' caps raised to %d", n, nr, caps, ns, CAP);
    } else if (!on && H.races) {
        if (ok(H.races, H.nr * RACE_SZ) && ok(H.stages, H.ns * STAGE_SZ)) {
            for (uint32_t i = 0; i < H.nr; i++) MEM8(H.races + i * RACE_SZ + R_HIDDEN) = H.hidden[i];
            for (uint32_t s = 0; s < H.ns; s++)
                for (uint32_t k = 0; k < 5u; k++) MEM8(H.stages + s * STAGE_SZ + S_CAPS + k) = H.caps[s][k];
            LOGI("HiddenRaces: the races hidden again, the stages' caps as they were");
        }
        H.races = H.stages = 0;
    }
}
#endif
