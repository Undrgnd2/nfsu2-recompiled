/*
 * cheats.c - the touch controls' solo cheats (Android, the ⚙ menu's "Triches"): money in the career's bank, the
 * player's nitrous refilled (once, or always full), every part in the career's shops unlocked. None in a LAN or Online
 * game ([0x850078] 0x200 / 8). Asked from the UI thread, done on the game's (nfs_cheats_frame, after each Present).
 *  - the bank: Career 0x85AD40 +0x7134 (garage.c K_BANK), career only ([0x850078] 1);
 *  - the nitrous: the player's car +0x40 (+0x414 a full shot, +0x41C what was left when it went on, +0x420 when, game
 *    ticks [0x86518C]; ghost.c nos_add): what is left set to a full shot;
 *  - the parts: uniques.c's wrap of 0x515500 (the career's half of a part's unlock test, the stage's bit 4) says yes
 *    to every part while it is on (nfs_cheat_parts).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_GAMEMODE  0x00850078u
#define G_FLOW      0x008654a4u
#define G_CAREER    0x0085ad40u
#define K_BANK      0x7134u
#define G_PLAYER1   0x008900acu
#define G_TICKS     0x0086518cu
#define CAR_NOS     0x40u
#define BANK_MAX    999999999u

static atomic_uint s_money;          /* money asked for, not given yet */
static atomic_int s_refill, s_nos_full, s_parts;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static int solo(void) { return !(rd(G_GAMEMODE) & 0x208u); }

/* 1 money (arg dollars), 2 nitrous refilled, 3 nitrous always full (arg 0/1), 4 parts unlocked (arg 0/1);
 * 0: not now (LAN or Online, money outside the career), 1 asked */
int nfs_cheat(int cmd, int arg) {
    switch (cmd) {
    case 1: if (!solo() || rd(G_GAMEMODE) != 1u || arg <= 0) return 0; atomic_fetch_add(&s_money, (unsigned)arg); return 1;
    case 2: if (!solo()) return 0; atomic_store(&s_refill, 1); return 1;
    case 3: atomic_store(&s_nos_full, arg != 0); return 1;
    case 4: atomic_store(&s_parts, arg != 0); LOGI("Cheats: every part unlocked %s", arg ? "on" : "off"); return 1;
    }
    return 0;
}
int nfs_cheat_parts(void) { return atomic_load(&s_parts) && solo() && rd(G_GAMEMODE) == 1u; }

static void nos_fill(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0, n = car + CAR_NOS;
    if (!car || !ok(n + 0x414u, 16)) return;
    int32_t full = (int32_t)MEM32(n + 0x414u), left = (int32_t)MEM32(n + 0x41cu);
    uint32_t since = MEM32(n + 0x420u);
    if (full <= 0 || full == 0x7fffffff || left == 0x7fffffff) return;
    int on = since && since != 0x7fffffffu;
    int32_t used = on ? (int32_t)(MEM32(G_TICKS) - since) : 0;
    if (left - used >= full) return;
    MEM32(n + 0x41cu) = (uint32_t)(full + used);
}

void nfs_cheats_frame(void) {
    if (!solo()) { atomic_store(&s_money, 0u); atomic_store(&s_refill, 0); return; }
    unsigned money = atomic_exchange(&s_money, 0u);
    if (money && rd(G_GAMEMODE) == 1u && ok(G_CAREER + K_BANK, 4)) {
        uint32_t b = MEM32(G_CAREER + K_BANK), to = b + money > BANK_MAX || b + money < b ? BANK_MAX : b + money;
        MEM32(G_CAREER + K_BANK) = to;
        LOGI("Cheats: the bank %u -> %u", b, to);
    }
    if (rd(G_FLOW) != 6u) return;
    if (atomic_exchange(&s_refill, 0)) { nos_fill(); LOGI("Cheats: nitrous refilled"); }
    if (atomic_load(&s_nos_full)) nos_fill();
}
#endif
