/*
 * dyno.c - Performance Tuning / Dyno keeps the setups it saves: Save Profile and Quit Tuning (with the game's Save
 * option Off) put them into the car first (Android).
 *
 * Adapted from nfsu2-web's runtime/dyno.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there):
 * the dyno object 0x839C40 (+0 the car's record, +0x900 the setup being tuned, +0x9FC its kind); the dyno menu's
 * messages (0x5647E0): Save Profile's Yes saved the profile with the record as it was, without the setups, and Quit
 * Tuning's Yes with the Save option Off left past the putting in: the next visit had the setups of before. Here both
 * put them in first (0x50FAE0), Save Profile storing the one being tuned into the visit's copy before (0x50FC20).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_005647E0_orig(void);

#define DYNO            0x00839c40u
#define D_RECORD        0x000u
#define D_TUNED         0x900u
#define D_KIND          0x9fcu
#define REC_SETUPS      0x360u
#define SETUP_SZ        0x29u
#define G_SAVE_OPTION   0x00844ad8u
#define FN_PUT_IN       0x0050fae0u   /* thiscall dyno (kind), ret 4 */
#define FN_STORE        0x0050fc20u   /* thiscall dyno (kind), ret 4 */
#define FN_CHANGED      0x0050fd10u   /* thiscall dyno -> al */
#define MSG_QUIT_YES    0x30ed2368u
#define MSG_SAVE_YES    0x51123592u

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }

/* 0x5647E0 (thiscall the dyno's menu: msg, obj, a3, a4; ret 0x10) */
void nfs_dyno_ov_msg(void) {
    uint32_t m = MEM32(g_esp + 4u), rec = MEM32(DYNO + D_RECORD), kind = MEM32(DYNO + D_KIND);
    if ((m == MSG_SAVE_YES || (m == MSG_QUIT_YES && !MEM8(G_SAVE_OPTION))) && ok(rec + REC_SETUPS, 5u * SETUP_SZ) &&
        MEM32(DYNO + D_TUNED) && kind < 5u && (nfs_guest_call(FN_CHANGED, DYNO, 0, 0, 0) & 0xffu)) {
        if (m == MSG_SAVE_YES) nfs_guest_call(FN_STORE, DYNO, &kind, 1, 0);
        nfs_guest_call(FN_PUT_IN, DYNO, &kind, 1, 0);
        LOGI("Dyno: %s: the setups into the car (kind %u)", m == MSG_SAVE_YES ? "Save Profile" : "Quit Tuning, Save option Off", kind);
    }
    sub_005647E0_orig();
}
#endif
