/*
 * uniques.c - once Caleb is beaten, the unique visual parts (the wide body kits and the other visual rewards of the
 * stages' outruns) are in every career car's shops (Android).
 *
 * Adapted from nfsu2-web's runtime/uniques.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there): the
 * game gives each unique visual part (unlock level 4, part byte +5's top 3 bits) only to the car driven to its star,
 * and its ending opens them all after Caleb in Customize mode only, whose cars free roam never drives. 0x515500
 * (cdecl filter, slot, part -> al: the career's half of a part's unlock test) also says yes to a level-4 part with the
 * stage's bit (4) asked once stage 5's final event is won. Such a part is then bought like any other, and is the car's
 * in the profile too.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_00515500_orig(void);

#define G_CAREER       0x0085ad40u
#define C_NEVENTS      0x7afcu
#define C_EVENTS       0x7b00u
#define C_MAXEVENTS    0x181u
#define G_S5_INFO      0x0086262cu
#define SI_FINAL       0x28u
#define P_LEVEL        5u
#define LEVEL_UNIQUE   4u
#define F_STAGE        4u

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }

static int caleb_beaten(void) {
    uint32_t si = MEM32(G_S5_INFO);
    if (!ok(si, SI_FINAL + 4u)) return 0;
    uint32_t h = MEM32(si + SI_FINAL), n = MEM32(G_CAREER + C_NEVENTS);
    if (!h || n > C_MAXEVENTS) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = G_CAREER + C_EVENTS + 8u * i;
        if (MEM32(r) == h) return MEM8(r + 6u) == 1u;
    }
    return 0;
}

/* 0x515500 (cdecl filter, slot, part -> al) */
void nfs_uniques_ov_unlocked(void) {
    static int said;
    uint32_t filter = MEM32(g_esp + 4u), slot = MEM32(g_esp + 8u), part = MEM32(g_esp + 12u);
    sub_00515500_orig();
    if ((g_eax & 0xffu) || !(filter & F_STAGE) || !ok(part, P_LEVEL + 1u)) return;
    if (nfs_cheat_parts()) { g_eax = (g_eax & ~0xffu) | 1u; return; }   /* cheats.c: every part, while asked */
    if ((MEM8(part + P_LEVEL) >> 5) != LEVEL_UNIQUE || !caleb_beaten()) return;
    g_eax = (g_eax & ~0xffu) | 1u;
    if (!said) { said = 1; LOGI("Uniques: Caleb beaten: the unique visual parts are in every car's shops (first asked: slot %u)", slot); }
}
#endif
