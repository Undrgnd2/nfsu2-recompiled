/*
 * cheats.c - the touch controls' solo cheats (Android, the ⚙ menu's "Triches"): money in the career's bank, the
 * player's nitrous refilled (once, or always full), every part in the career's shops unlocked. None in a LAN or Online
 * game ([0x850078] 0x200 / 8). Asked from the UI thread, done on the game's (nfs_cheats_frame, after each Present).
 *  - the bank: Career 0x85AD40 +0x7134 (garage.c K_BANK), career only ([0x850078] 1);
 *  - the nitrous: the player's car +0x40 (+0x414 a full shot, +0x41C what was left when it went on, +0x420 when, game
 *    ticks [0x86518C]; ghost.c nos_add): what is left set to a full shot;
 *  - the parts: uniques.c's wrap of 0x515500 (the career's half of a part's unlock test, the stage's bit 4) says yes
 *    to every part while it is on (nfs_cheat_parts);
 *  - the car lot: its list of the career's car types (0x513C50, thiscall Career (list): from all 46 types, each of
 *    the table [0x838438] (count [0x83843C], 12 bytes: the type's hash, its unlock's key) whose event is not won yet,
 *    0x500B80, taken off) is made with that table's count 0 while it is on: every car the car lot sells, from stage 1;
 *  - the stage: Career +0x7AF8 (1-5; 0 the prologue). The game ends a stage when its record (Career +0x72D8, 0x104
 *    bytes a stage: +0xFC done, +0x100 its settings, +0x28 their final event) has its final event won (0x532C60:
 *    +0xFC set, then 0x528320 the stage + 1). A jump to stage n marks the stages before it done and sets the stage;
 *    the garage gets 2 open slots a stage (garage.c). The career's lists (its races, shops, walls, sponsor) are made
 *    from the stage as the career loads, so the player saves and loads it again. In career free roam only;
 *  - every event: the career's available races (Career +0x71C8, 64 pointers) are made by 0x527D30 (thiscall Career:
 *    from the race table [0x8383E0] x [0x8383E4], 0x88 bytes: +8 hash, +0x34 kind (2 URL), +0x37 stage, +0x7F open;
 *    the stage's open races not won, then its URL races), as the career loads and after a race. While it is on, the
 *    other stages' races (1-5) not won are put in after the game's, the open ones and the URL ones, while there is
 *    room: the map shows them and their markers start them.
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
static atomic_int s_refill, s_nos_full, s_parts, s_cars, s_stage, s_events;
#define K_STAGE     0x7af8u
#define K_STAGE_REC 0x72d8u
#define STAGE_REC   0x104u
#define STAGE_DONE  0xfcu

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
    case 5: atomic_store(&s_cars, arg != 0); LOGI("Cheats: every car at the car lot %s", arg ? "on" : "off"); return 1;
    case 6: {   /* the career to stage arg (2-5): 0 not now (not career free roam, the prologue, a stage not ahead) */
        uint32_t st = rd(G_CAREER + K_STAGE);
        if (!solo() || rd(G_GAMEMODE) != 1u || rd(G_FLOW) != 6u || st < 1u || arg < 2 || arg > 5 || (uint32_t)arg <= st) return 0;
        atomic_store(&s_stage, arg); return 1;
    }
    case 7: return solo() && rd(G_GAMEMODE) == 1u ? (int)rd(G_CAREER + K_STAGE) : -1;   /* the career's stage */
    case 8: atomic_store(&s_events, arg != 0); LOGI("Cheats: every stage's events %s", arg ? "on" : "off"); return 1;
    }
    return 0;
}
int nfs_cheat_parts(void) { return atomic_load(&s_parts) && solo() && rd(G_GAMEMODE) == 1u; }

/* 0x527D30 (thiscall Career): the career's available races */
void sub_00527D30_orig(void);
#define G_RACES         0x008383e0u
#define G_NRACES        0x008383e4u
#define RACE_SZ         0x88u
#define K_AVAIL         0x71c8u
#define AVAIL_N         64u
#define K_NEVENTS       0x7afcu
#define K_EVENTS        0x7b00u
static int won(uint32_t career, uint32_t hash) {
    uint32_t n = rd(career + K_NEVENTS);
    if (n > 0x181u) return 0;
    for (uint32_t i = 0; i < n; i++) if (MEM32(career + K_EVENTS + 8u * i) == hash) return MEM8(career + K_EVENTS + 8u * i + 6u) == 1u;
    return 0;
}
void nfs_cheats_ov_races(void) {
    uint32_t career = g_ecx;
    sub_00527D30_orig();
    if (!atomic_load(&s_events) || !solo() || rd(G_GAMEMODE) != 1u || career != G_CAREER) return;
    uint32_t races = rd(G_RACES), n = rd(G_NRACES), st = rd(career + K_STAGE);
    if (!races || n > 4096u || !ok(races, n * RACE_SZ) || !ok(career + K_AVAIL, 4u * AVAIL_N)) return;
    unsigned added = 0, closed = 0, full = 0, had = 0;
    for (uint32_t i = 0; i < AVAIL_N; i++) had += MEM32(career + K_AVAIL + 4u * i) != 0;
    for (int pass = 1; pass <= 5; pass++)            /* stage by stage, the career's own first */
        for (uint32_t i = 0; i < n; i++) {
            uint32_t r = races + i * RACE_SZ, h = MEM32(r + 8u);
            int stage = (int8_t)MEM8(r + 0x37u);
            if (stage != pass || (uint32_t)stage == st || !h || won(career, h)) continue;
            if (!MEM8(r + 0x7fu) && MEM8(r + 0x34u) != 2u) { closed++; continue; }
            uint32_t free_at = AVAIL_N; int there = 0;
            for (uint32_t k = 0; k < AVAIL_N; k++) {
                uint32_t a = MEM32(career + K_AVAIL + 4u * k);
                if (a && ok(a + 8u, 4) && MEM32(a + 8u) == h) { there = 1; break; }
                if (!a && free_at == AVAIL_N) free_at = k;
            }
            if (there) continue;
            if (free_at == AVAIL_N) { full++; continue; }
            MEM32(career + K_AVAIL + 4u * free_at) = r;
            added++;
        }
    LOGI("Cheats: every stage's events: %u races added to the game's %u (stage %u; %u not open yet left out, %u no room)", added, had, st, closed, full);
}

/* 0x513C50 (thiscall Career (list): the car types the car lot may show) */
void sub_00513C50_orig(void);
#define G_CARLOCKS_N    0x0083843cu
void nfs_cheats_ov_cartypes(void) {
    int all = atomic_load(&s_cars) && solo() && rd(G_GAMEMODE) == 1u && ok(G_CARLOCKS_N, 4);
    uint32_t n = all ? MEM32(G_CARLOCKS_N) : 0;
    if (all) MEM32(G_CARLOCKS_N) = 0;
    sub_00513C50_orig();
    if (all) { MEM32(G_CARLOCKS_N) = n; LOGI("Cheats: the car lot's list with every car (%u locks left out)", n); }
}

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
    int to = atomic_exchange(&s_stage, 0);
    uint32_t st = rd(G_CAREER + K_STAGE);
    if (to && rd(G_GAMEMODE) == 1u && st >= 1u && (uint32_t)to > st && to <= 5) {
        for (uint32_t s = st; s < (uint32_t)to; s++) MEM8(G_CAREER + K_STAGE_REC + s * STAGE_REC + STAGE_DONE) = 1;
        MEM32(G_CAREER + K_STAGE) = (uint32_t)to;
        nfs_garage_open_slots(2u * (uint32_t)to);
        LOGI("Cheats: the career at stage %d (was %u): stages %u-%d marked done, %d garage slots open", to, st, st, to - 1, 2 * to);
    }
    if (atomic_exchange(&s_refill, 0)) { nos_fill(); LOGI("Cheats: nitrous refilled"); }
    if (atomic_load(&s_nos_full)) nos_fill();
}
#endif
