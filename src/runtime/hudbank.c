/*
 * hudbank.c - two parts of career free roam's HUD only when they say something (Android, as on undrgnd2.eu):
 *  - the Bank (top right, "Argent : 99 561 730"): BANK_START_S once free roam starts, BANK_BACK_S once the HUD is back
 *    after a menu (the pause menu, the map, a shop), BANK_CHANGE_S after the bank changed; else hidden;
 *  - the box at the top left (the D-pad and its icons: an SMS waiting, Engage at a marker or a door, an outrun's
 *    tuner): BOX_START_S once free roam starts, BOX_BACK_S after a menu, BOX_NEW_S after a message came, and while
 *    the car is at a marker, a door or a tuner (and BOX_HERE_S after); else hidden.
 *
 * Adapted from nfsu2-web's runtime/hudbank.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there):
 * Player::Update's HUD step (0x600590) has the HUD's part bits made by 0x600090 (thiscall Player: eax the low bits,
 * edx the high ones, its only caller) and hands them to the HUD when they change, which shows or hides the parts
 * whose bits changed. The Bank is the high bit 0x80000, the box the high bit 2: right after 0x600090 (wrapped here,
 * where the web hooks 0x60059A) a part's bit goes while it has nothing to say.
 */
#ifdef NFS_DRIFT_MODE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <time.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_00600090_orig(void);

#define BANK_CHANGE_S 20.0        /* shown after the bank changed */
#define BANK_START_S  10.0        /* ... after free roam starts */
#define BANK_BACK_S   8.0         /* ... after a menu */
#define BOX_NEW_S     20.0        /* the box shown after a message came */
#define BOX_HERE_S    2.0         /* ... while the car is at a marker, a door or a tuner, and this after */
#define BOX_START_S   10.0        /* ... after free roam starts */
#define BOX_BACK_S    8.0         /* ... after a menu */
#define GAP_S         1.0         /* the HUD step not run this long: the world stood (a menu, a loading screen) */
#define A_BANK        0x00861e74u /* the career's bank */
#define PL_HUD        0x9a4u
#define HUD_BOX       0x58u
#define BOX_SLOTS     0x60u       /* the box's messages: BOX_N of BOX_SLOT bytes (+0 state, +1 kind) */
#define BOX_N         8u
#define BOX_SLOT      0x14u
#define KIND_SMS      0x13u
#define KIND_NOTE     0x14u
#define BIT_BANK      0x80000u
#define BIT_BOX       0x2u

struct Item { uint32_t hud; int had, shown; double until, start; };
static struct { uint32_t pl, bank, came, here; int known; double last; struct Item b, x; } S;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static void show_for(struct Item *it, double t, double sec) { if (it->until < t + sec) it->until = t + sec; }
static const char *bit_came(struct Item *it, uint32_t hud, double t, int gap, double start, double back) {
    const char *why = 0;
    if (hud != it->hud) { it->hud = hud; it->start = t; show_for(it, t, start); why = "free roam"; }
    else if (!it->had || gap) { show_for(it, t, back); why = "back"; }
    it->had = 1;
    return why;
}
static void bank(uint32_t hud, double t, int gap) {
    struct Item *it = &S.b;
    if (!(g_edx & BIT_BANK)) { it->had = 0; return; }
    uint32_t b = ok(A_BANK, 4) ? MEM32(A_BANK) : 0;
    const char *why = bit_came(it, hud, t, gap, BANK_START_S, BANK_BACK_S);
    if (S.known && b != S.bank) { show_for(it, t, BANK_CHANGE_S); why = "changed"; }
    S.known = 1; S.bank = b;
    int on = t < it->until;
    if (why || on != it->shown) LOGI("HUD: bank %s (%s, %u)", on ? "shown" : "hidden", why ? why : "time's up", b);
    it->shown = on;
    if (!on) g_edx &= ~BIT_BANK;
}
static void box(uint32_t hud, double t, int gap) {
    struct Item *it = &S.x;
    if (!(g_edx & BIT_BOX)) { it->had = 0; return; }
    uint32_t part = ok(hud + HUD_BOX, 4) ? MEM32(hud + HUD_BOX) : 0, slots = part + BOX_SLOTS;
    if (!part || !ok(slots, BOX_N * BOX_SLOT)) return;
    const char *why = bit_came(it, hud, t, gap, BOX_START_S, BOX_BACK_S);
    uint32_t came = 0, here = 0;
    int pulse = 0;
    for (uint32_t k = 0; k < BOX_N; k++) {
        uint8_t st = MEM8(slots + k * BOX_SLOT), kind = MEM8(slots + k * BOX_SLOT + 1u);
        if (!st || !kind) continue;
        if (kind != KIND_SMS && kind != KIND_NOTE) here |= 1u << k;
        else if (st == 1 || st == 2) { came |= 1u << k; pulse |= st == 2; }
    }
    int settled = t - it->start >= BOX_START_S;
    if (settled && (came & ~S.came)) { show_for(it, t, BOX_NEW_S); why = "message"; }
    else if (settled && pulse) show_for(it, t, BOX_NEW_S);
    if (here) { show_for(it, t, BOX_HERE_S); if ((here & ~S.here) && !why) why = "here"; }
    S.came = came; S.here = here;
    int on = t < it->until;
    if (why || on != it->shown) LOGI("HUD: box %s (%s)", on ? "shown" : "hidden", why ? why : "time's up");
    it->shown = on;
    if (!on) g_edx &= ~BIT_BOX;
}

/* 0x600090 (thiscall Player): the HUD's part bits, eax low, edx high */
void nfs_hudbank_ov_bits(void) {
    uint32_t pl = g_ecx;
    sub_00600090_orig();
    if (!ok(pl + PL_HUD, 4)) return;
    uint32_t hud = MEM32(pl + PL_HUD);
    double t = now_s();
    if (S.pl != pl) { S = (__typeof__(S)){ 0 }; S.pl = pl; }
    int gap = S.last > 0 && t - S.last > GAP_S;
    S.last = t;
    bank(hud, t, gap);
    box(hud, t, gap);
}
#endif
