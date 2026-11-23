/*
 * specialties.c - the Car Specialties shop (UI_ICEMAIN.fng): Stock gauges can be picked again, the gauge list opens on
 * the gauge fitted, and Back out of Trunk Audio keeps the shop's list on Trunk Audio (Android).
 *
 * Adapted from nfsu2-web's runtime/specialties.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are
 * there):
 *  - Custom Gauges (its messages 0x563CF0): Enter on Stock in the style list runs as the game's install of a style
 *    without colours (the stage [0x838560] 0 for it), and its Yes puts the colour slots back to each slot's first part
 *    (0x61BA30: WHITE): the gauges as before any purchase, free;
 *  - the gauge list (0x56C420) opens on the style fitted (the front end's ride 0x8392C0, slot 163);
 *  - Trunk Audio's Back (its screen's messages 0x568740, the screens' factory 0x560550) wrote 0 for the shop list's
 *    place (the screen table's row 0x7F890C +0x10): the place it was opened from is written back first.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_00563CF0_orig(void);
void sub_0056C420_orig(void);
void sub_00568740_orig(void);
void sub_00560550_orig(void);

#define G_FE_RIDE       0x008392c0u
#define G_CARPARTDB     0x008a2b68u
#define FN_FE_HIDE      0x0050ca00u
#define FN_FE_SHOW      0x0050ca50u
#define FN_ATTR         0x0061b500u   /* thiscall part (hash, default) -> attribute */
#define FN_LIST_SELECT  0x005386a0u   /* thiscall list (item) */
#define FN_HUD_SHOW     0x00548d20u   /* thiscall gauge screen () */
#define FN_PART_FIRST   0x0061ba30u   /* thiscall parts (car type, slot, 0, -1) */
#define G_HUD_STAGE     0x00838560u
#define G_COL_BACKING   0x00839c24u
#define G_COL_CHAR      0x00839c28u
#define G_COL_NEEDLE    0x00839c2cu
#define G_LIST_KIND     0x00838998u
#define ROW_ICE_PLACE   0x007f891cu
#define RIDE_PARTS      0x590u
#define SLOT_HUD        0xa3u
#define SLOT_BACKING    0xa4u
#define SLOT_NEEDLE     0xa5u
#define SLOT_CHAR       0xa6u
#define SLOT_TRUNK      0x22u
#define ATTR_STYLE      0x311151f8u
#define MSG_PAD_ACCEPT  0x406415e3u
#define MSG_INSTALL_YES 0x51123592u
#define MSG_BUTTON      0x0c407210u
#define S_FLAG_SEEN     0x129u
#define S_FLAG_BUSY     0x12au
#define S_LIST          0x50u
#define S_CATEGORY      0x110u
#define S_UP            0xfcu
#define S_DOWN          0x100u
#define I_PART          0x2cu
#define ICE_ITEM        0x58u

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static int pkg_is(uint32_t s, const char *name) {
    size_t n = strlen(name);
    if (!ok(s, (uint32_t)n + 1u)) return 0;
    for (size_t i = 0; i < n; i++) if ((MEM8(s + i) | 0x20) != (name[i] | 0x20)) return 0;
    return MEM8(s + n) == 0;
}

/* --- Custom Gauges --- */
static uint32_t stock_item(uint32_t scr) {
    if (!ok(scr, 0x130u)) return 0;
    uint32_t cat = MEM32(scr + S_CATEGORY), item = MEM32(scr + S_LIST + 0xcu);
    if (MEM32(scr + S_LIST + 4u) == scr + S_LIST + 4u || !ok(cat, 0x50u) || MEM32(cat + 0x48u) != SLOT_HUD) return 0;
    if (!ok(item, I_PART + 4u)) return 0;
    uint32_t part = MEM32(item + I_PART), a[2] = { ATTR_STYLE, 0 };
    if (!ok(part, 14u)) return 0;
    return nfs_guest_call(FN_ATTR, part, a, 2, 0) == 0 ? part : 0;
}
static uint32_t first_part(uint32_t type, uint32_t slot) {
    uint32_t a[4] = { type, slot, 0, 0xffffffffu };
    return nfs_guest_call(FN_PART_FIRST, G_CARPARTDB, a, 4, 0);
}
/* 0x563CF0 (thiscall gauge screen: msg, a, b, c; ret 0x10) */
void nfs_shop_ov_hud_msg(void) {
    uint32_t scr = g_ecx, msg = MEM32(g_esp + 4u), stage = MEM32(G_HUD_STAGE);
    int lifted = 0;
    if (msg == MSG_PAD_ACCEPT && stage == 1u && ok(scr, 0x130u) && MEM8(scr + S_FLAG_SEEN) && !MEM8(scr + S_FLAG_BUSY) && stock_item(scr)) {
        MEM32(G_HUD_STAGE) = 0;
        lifted = 1;
        LOGI("Shop: Custom Gauges: Stock picked");
    }
    if (msg == MSG_INSTALL_YES && stock_item(scr)) {
        uint32_t type = MEM32(G_FE_RIDE);
        MEM32(G_COL_BACKING) = first_part(type, SLOT_BACKING);
        MEM32(G_COL_CHAR) = first_part(type, SLOT_CHAR);
        MEM32(G_COL_NEEDLE) = first_part(type, SLOT_NEEDLE);
        LOGI("Shop: Custom Gauges: Stock fitted, the colours as a new car's, free");
    }
    g_ecx = scr;
    sub_00563CF0_orig();
    if (lifted && MEM32(G_HUD_STAGE) == 0u) MEM32(G_HUD_STAGE) = stage;
}
/* 0x56C420 (cdecl (arg) -> the gauge screen): its list on the style fitted */
void nfs_shop_ov_hud_new(void) {
    sub_0056C420_orig();
    uint32_t scr = g_eax, ret_d = g_edx, fitted = MEM32(G_FE_RIDE + RIDE_PARTS + 4u * SLOT_HUD), cat = ok(scr, 0x130u) ? MEM32(scr + S_CATEGORY) : 0;
    if (!fitted || !ok(cat, 0x50u) || MEM32(cat + 0x48u) != SLOT_HUD) return;
    uint32_t head = scr + S_LIST + 4u, node = MEM32(head), item = 0;
    for (int k = 0; node && node != head && k < 64 && ok(node, 8u); k++, node = MEM32(node))
        if (ok(node - 4u, I_PART + 4u) && MEM32(node - 4u + I_PART) == fitted) { item = node - 4u; break; }
    if (!item || item == MEM32(scr + S_LIST + 0xcu)) return;
    nfs_guest_call(FN_LIST_SELECT, scr + S_LIST, &item, 1, 0);
    MEM32(cat + 0x4cu) = fitted;
    nfs_guest_call(FN_HUD_SHOW, scr, 0, 0, 0);
    uint32_t up = MEM32(scr + S_UP), down = MEM32(scr + S_DOWN);
    uint32_t first = MEM32(scr + S_LIST + 4u) - 4u, last = MEM32(scr + S_LIST + 8u) - 4u;
    nfs_guest_call(FN_FE_SHOW, 0, &up, 1, 4);
    nfs_guest_call(FN_FE_SHOW, 0, &down, 1, 4);
    if (item == first) nfs_guest_call(FN_FE_HIDE, 0, &up, 1, 4);
    else if (item == last) nfs_guest_call(FN_FE_HIDE, 0, &down, 1, 4);
    g_eax = scr; g_edx = ret_d;
    LOGI("Shop: Custom Gauges: the list on the gauge fitted");
}

/* --- Trunk Audio's Back --- */
static struct { int state; uint32_t at, kind; } g_trunk;
/* 0x568740 (thiscall UI_ICEMAIN screen: msg, a, b, c; ret 0x10) */
void nfs_shop_ov_ice_msg(void) {
    uint32_t scr = g_ecx, msg = MEM32(g_esp + 4u);
    if (msg == MSG_BUTTON && g_trunk.state != 2 && ok(scr, 0x170u) && pkg_is(MEM32(scr + 4u), "UI_ICEMAIN.fng")) {
        uint32_t it = MEM32(scr + ICE_ITEM);
        if (ok(it, 0x4cu) && MEM32(it + 0x48u) == SLOT_TRUNK) {
            g_trunk.state = 1; g_trunk.at = MEM32(ROW_ICE_PLACE); g_trunk.kind = MEM32(G_LIST_KIND);
            LOGI("Shop: Trunk Audio from the list's place %u", g_trunk.at);
        } else g_trunk.state = 0;
    }
    sub_00568740_orig();
}
/* 0x560550 (cdecl (arg) -> UI_ICEMAIN.fng's screen) */
void nfs_shop_ov_ice_new(void) {
    uint32_t kind = rd(G_LIST_KIND);
    if (g_trunk.state == 1) g_trunk.state = kind != g_trunk.kind ? 2 : 0;
    else if (g_trunk.state == 2 && kind == g_trunk.kind) {
        if (MEM32(ROW_ICE_PLACE) == 0u && g_trunk.at) {
            MEM32(ROW_ICE_PLACE) = g_trunk.at;
            LOGI("Shop: Trunk Audio's Back: the list stays on its place %u", g_trunk.at);
        }
        g_trunk.state = 0;
    }
    sub_00560550_orig();
}
#endif
