/*
 * sponsors.c - the career's sponsor screens take a tap to go on (Android).
 *
 * Adapted from nfsu2-web's runtime/sponsors.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 * "Select a sponsor!" (UI_SponsorPopup.fng) and the sponsor screen (UI_Sponsorship_new.fng) have Back alone in their
 * PC help bar (the screen table 0x7F7DC8, rows of 0x1C bytes, +0xC the help bar's buttons), so a tap only went Back
 * and round to the race reward, and nothing chose a sponsor. Both get Continue (0x404, as the race reward has).
 * Done once, at the first frame, on the data of the exe this port is made for only.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define ROW         0x1cu
#define ROW_MASK    0x0cu
#define HB_BACK     0x400u
#define HB_CONTINUE 0x004u

static int row_is(uint32_t row, const char *name, uint32_t ctor) {
    uint32_t s = MEM32(row);
    if (MEM32(row + 4u) != ctor || s < 0x10000u || !nfs_guest_readable(s, 32)) return 0;
    for (uint32_t i = 0; ; i++) {
        if ((char)MEM8(s + i) != name[i]) return 0;
        if (!name[i]) return 1;
    }
}

void nfs_sponsors_frame(void) {
    static int done;
    if (done) return;
    done = 1;
    static const struct { const char *name; uint32_t ctor, row; } S[] = {
        { "UI_SponsorPopup.fng",    0x004c1280u, 0x007f89d0u },
        { "UI_Sponsorship_new.fng", 0x004f4730u, 0x007f89ecu },
    };
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++)
        if (!nfs_guest_readable(S[i].row, ROW) || !row_is(S[i].row, S[i].name, S[i].ctor) ||
            (MEM32(S[i].row + ROW_MASK) & ~HB_CONTINUE) != HB_BACK) {
            LOGI("Sponsors: the exe's screen table is not the one expected, left as it is");
            return;
        }
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) MEM32(S[i].row + ROW_MASK) = HB_BACK | HB_CONTINUE;
    LOGI("Sponsors: Continue on the sponsor screens");
}
#endif
