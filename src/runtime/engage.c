/*
 * engage.c - Engage at a marker in the career's free roam (a shop or garage door, a race start, a cover shoot): mended
 * when the game would refuse it for good, and logged when it refuses (Android).
 *
 * Adapted from nfsu2-web's runtime/engage.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there): the
 * prompt asks only whether the car is in the marker's trigger; Engage (Player::HandleAction 0x602BD0) also wants the
 * career not engaging already (Career +0x7138, a byte) and the showcase preview's byte 0x85EF6C clear. Left set, every
 * marker after shows its prompt and Enter does nothing until the next load. Each frame of the career's free roam with
 * the HUD on top and the engage manager free ([[0x8900D8]] 4): either byte set that long (STUCK frames) is cleared.
 * An Engage at a marker that brings no screen up within a second logs what Engage tests.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_GAMEMODE      0x00850078u
#define G_RACETYPE      0x0089e7b0u
#define G_CAREER        0x0085ad40u
#define G_PLAYER1       0x008900acu
#define G_FENG          0x008384c4u
#define C_ENGAGING      0x7138u
#define G_PREVIEW       0x0085ef6cu
#define G_ENGAGE_MGR    0x008900d8u
#define ENGAGE_FREE     4u
#define G_ACTIONS       0x0086eaf0u
#define ACT_ENGAGE      15u
#define PL_MARKER       0x9acu
#define STUCK           120
#define NO_SCREEN       60

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static int top_is(const char *name) {
    uint32_t fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0, s = top ? rd(top + 0xcu) : 0;
    char b[40]; unsigned i = 0;
    for (; s && i + 1 < sizeof b && ok(s + i, 1) && MEM8(s + i); i++) b[i] = (char)MEM8(s + i);
    b[i] = 0;
    return i && !strcasecmp(b, name);
}

void nfs_engage_frame(void) {
    static long f, stuck_eng, stuck_prev, pressed_at = -1;
    static int was_held;
    static uint32_t snap[6];
    f++;
    uint32_t mgr = rd(G_ENGAGE_MGR), pl = rd(G_PLAYER1);
    int roam = rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && top_is("HUD_SingleRace.fng");
    int idle = roam && ok(mgr, 4) && MEM32(mgr) == ENGAGE_FREE;
    if (idle && MEM8(G_CAREER + C_ENGAGING)) {
        if (!stuck_eng) stuck_eng = f;
        else if (f - stuck_eng >= STUCK) {
            MEM8(G_CAREER + C_ENGAGING) = 0; stuck_eng = 0;
            LOGI("Engage: the career was left engaging with nothing engaged: cleared, Enter at a marker works again");
        }
    } else stuck_eng = 0;
    if (idle && MEM8(G_PREVIEW)) {
        if (!stuck_prev) stuck_prev = f;
        else if (f - stuck_prev >= STUCK) {
            MEM8(G_PREVIEW) = 0; stuck_prev = 0;
            LOGI("Engage: the showcase preview's mode was left on in free roam: cleared, Enter at a marker works again");
        }
    } else stuck_prev = 0;
    int held = !(rd(G_ACTIONS) & (1u << ACT_ENGAGE));
    uint32_t marker = pl ? rd(pl + PL_MARKER) : 0u;
    if (held && !was_held && roam && marker && pressed_at < 0) {
        pressed_at = f;
        snap[0] = marker; snap[1] = MEM8(G_CAREER + C_ENGAGING); snap[2] = MEM8(G_PREVIEW);
        snap[3] = ok(mgr, 4) ? MEM32(mgr) : 0xffffffffu; snap[4] = ok(mgr + 0x4cu, 4) ? MEM32(mgr + 0x4cu) : 0xffffffffu;
        snap[5] = rd(G_GAMEMODE);
    }
    was_held = held;
    if (pressed_at >= 0 && f - pressed_at >= NO_SCREEN) {
        if (roam)
            LOGI("Engage: Enter at marker %08x brought nothing up: engaging %u, preview %u, engage manager %u (kind %u), mode %u",
                 snap[0], snap[1], snap[2], snap[3], snap[4], snap[5]);
        pressed_at = -1;
    }
}
#endif
