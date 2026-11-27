/*
 * guards.c - guards against the game's own null pointers that crash the Android build (Android).
 *
 *  - the world's update (0x609CE0, thiscall World* [0x890080], 1 argument the frame's time, ret 4), called by the main
 *    loop (0x581407) whenever the track loader [0x8650B8] says the world is not loading (+0x1C 0). The world is made
 *    by 0x5FF4xx (it writes [0x890080]) and torn down by 0x60A9A0 (0 written there and in its destructor 0x5FF6F0); a
 *    crash report of 2026-11-27 (OnePlus 6T, free roam after a stage's ending cutscene) had the update called with
 *    [0x890080] still 0, the read of World +0x10 faulting. The update is skipped while there is no world (logged with
 *    the game's state), as the game would have had nothing to update;
 *  - the game's allocator (0x440390, cdecl (size, name, line, flags: the pool in the low bits) -> memory, 0 when the
 *    pool has no room): its callers mostly write through what it gives (the bPNode slot pool's growth 0x440D40, then
 *    the list push 0x479130 links node 0: crash reports at guest addresses 0x0..0x10). A refusal is logged (pool,
 *    size, the caller's return address), the first ones of each pool, so a crash after it names the cause.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_WARN, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
void sub_00609CE0_orig(void);
void sub_00440390_orig(void);

#define G_FLOW    0x008654a4u
#define G_LOADER  0x008650b8u
#define G_RACE    0x00890118u

static uint32_t rd(uint32_t va) { return va >= 0x10000u && nfs_guest_readable(va, 4) ? MEM32(va) : 0; }

/* wraps 0x440390 (bMalloc): a refusal logged */
void nfs_guard_ov_bmalloc(void) {
    uint32_t ret = MEM32(g_esp), size = MEM32(g_esp + 4u), flags = MEM32(g_esp + 16u);
    sub_00440390_orig();
    if (g_eax || !size) return;
    static unsigned said[16];
    unsigned pool = flags & 15u;
    if (said[pool]++ < 5)
        LOGI("Guard: the game's memory pool %u has no room for %u bytes (flags %08x, from %08x): the caller gets 0", pool, size, flags, ret);
}

/* wraps 0x609CE0: the world's update, skipped while there is no world */
void nfs_guard_ov_world_update(void) {
    if (g_ecx) { sub_00609CE0_orig(); return; }
    static unsigned said;
    if (said++ < 20) {
        uint32_t ld = rd(G_LOADER);
        LOGI("Guard: the world's update called with no world (flow %u, loader %08x loading %u, race %08x): skipped",
             rd(G_FLOW), ld, ld ? rd(ld + 0x1cu) : 0u, rd(G_RACE));
    }
    g_esp += 4u + 4u;                                   /* (ret 4) */
}
#endif
