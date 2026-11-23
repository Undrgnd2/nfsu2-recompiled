/*
 * cameras.c - the drift camera (the drift races' own, high behind the car) in every drive, after the four views the
 * camera key goes through (Android). Nothing else changes: the views keep their order and their look, Look Back
 * works with it as with the others, and a profile keeps it when picked.
 *
 * Adapted from nfsu2-web's runtime/cameras.c (MIT, Copyright (c) 2026 th0mk; its notes on the game are there, the
 * facts as ExtraOptions' EnableHiddenCameraModes names them):
 *  - PlayerSettings (0x83AA64, 0x28 each) +0x10 the view; 0x502540 ScrollDriveCam (thiscall settings, step +1 / -1,
 *    ret 4) the next or previous view, wrapping: the last is 3 (four views), 4 only in a drift event. Here it is 4
 *    everywhere (replaced, nfs_cam_ov_scroll).
 *  - Every race's setup but a drift event's turns a view 4 into 3: after a drift event (or with the drift camera
 *    picked) the player drove with view 3, not the one they had. Here the view the player had is given back after a
 *    setup that did that: the setups are seen through the race's settings 0x4FE9F0 (free roam's and the career's),
 *    the human racers 0x525ED0, the dyno's test run 0x526970, free roam's 0x540780 and the loads' 0x581820.
 * drift.c's drift mode puts view 4 on and gives the one before back itself (nfs_drift_camera_view).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

void sub_004FE9F0_orig(void);
void sub_00525ED0_orig(void);
void sub_00526970_orig(void);
void sub_00540780_orig(void);
void sub_00581820_orig(void);

#define G_PSETTINGS  0x0083aa64u
#define PS_SIZE      0x28u
#define PS_VIEW      0x10u
#define G_DRIFTRACE  0x0089e7d9u
#define VIEW_LAST    4u              /* the drift camera */
#define VIEW_TO      3u              /* what the setups make of it */
#define VIEW_NONE    0xffffffffu

static struct {
    int setup, known;
    uint32_t seen[2], own[2], keyed[2];
} V = { .keyed = { VIEW_NONE, VIEW_NONE } };

/* ScrollDriveCam (0x502540, thiscall settings (step), ret 4): as the game's, with the last view 4 */
void nfs_cam_ov_scroll(void) {
    uint32_t ps = g_ecx, step = MEM32(g_esp + 4u), v = MEM32(ps + PS_VIEW);
    if (step == 1u) { v++; if ((int32_t)v > (int32_t)VIEW_LAST) v = 0; }
    else if (step == 0xffffffffu) { v--; if ((int32_t)v < 0) v = VIEW_LAST; }
    MEM32(ps + PS_VIEW) = v;
    for (uint32_t n = 0; n < 2u; n++) if (ps == G_PSETTINGS + PS_SIZE * n) V.keyed[n] = v;
    g_eax = v; g_edx = VIEW_LAST;
    g_esp += 4u + 4u;
}
/* the race's setups: seen, then the game's own */
void nfs_cam_ov_settings(void) { V.setup = 1; sub_004FE9F0_orig(); }
void nfs_cam_ov_humans(void)   { V.setup = 1; sub_00525ED0_orig(); }
void nfs_cam_ov_dyno(void)     { V.setup = 1; sub_00526970_orig(); }
void nfs_cam_ov_roam(void)     { V.setup = 1; sub_00540780_orig(); }
void nfs_cam_ov_load(void)     { V.setup = 1; sub_00581820_orig(); }

/* each frame: the views as the player picks them, and the player's back where a setup put the drift camera to 3 */
void nfs_cam_frame(void) {
    int setup = V.setup, drift = MEM8(G_DRIFTRACE) != 0;
    uint32_t ours = nfs_drift_camera_view();
    V.setup = 0;
    for (uint32_t n = 0; n < 2u; n++) {
        uint32_t at = G_PSETTINGS + PS_SIZE * n + PS_VIEW, v = MEM32(at);
        if (!V.known) { V.seen[n] = v; V.own[n] = v < VIEW_LAST ? v : VIEW_NONE; continue; }
        if (setup && !drift && V.seen[n] == VIEW_LAST && (v == VIEW_TO || v == VIEW_LAST) && V.own[n] <= VIEW_LAST && v != V.own[n]) {
            LOGI("Cameras: player %u's view %u again (a race's setup had view %u after the drift camera)", n + 1u, V.own[n], v);
            v = V.own[n];
            MEM32(at) = v;
        } else if (!setup && !drift && at != ours) {
            /* the drift camera only as the camera key picked it (a profile's is no one's) */
            if (v < VIEW_LAST || V.keyed[n] == v) V.own[n] = v;
            else if (v != V.seen[n]) V.own[n] = VIEW_NONE;
        }
        if (v != VIEW_LAST) V.keyed[n] = VIEW_NONE;
        V.seen[n] = v;
    }
    V.known = 1;
}
#endif
