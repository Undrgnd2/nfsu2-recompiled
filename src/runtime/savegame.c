/*
 * savegame.c - the pause menu's Save (Android): in career free roam the career saved where the car stands, without a
 * trip to the garage.
 *
 * Adapted from nfsu2-web's runtime/savegame.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 *  - career free roam's pause menu (0x4C2900) starts with Return to Explore Mode, added through the menu's AddOption
 *    (0x50DAB0, wrapped here): Save goes in after it, an item of Replay's kind (0x51F670, vtable 0x7954F0, +0x20
 *    greyed out) with the game's own label MU_PAUSE_SAVE_GAME ("Save" in every language, which the PC game never
 *    shows); greyed out while the car moves, with no profile loaded, in a new career's prologue and while a save runs;
 *  - its action is Replay's (0x4C26B0, thiscall: package, message, 3 more; ret 0x14, wrapped here): Accept puts the
 *    car's place and heading where leaving free roam keeps them (Career +0x8750, +0x8760, +0x8744 1), then runs the
 *    game's own save screen (0x4A3100 with the crib's job 0x151, MC_Main.fng over the pause menu); with Autosave on
 *    it saves at once (no "overwrite?" question, whose No would turn Autosave off), with Prompt or Off it asks first;
 *  - once the save is over (the job's word [0x836514] back to 0, MC_Main gone) the career's flag goes back as it was.
 * The icon: INGAME_ICON_VOIP_MUTE, as the web's (whose page draws the crib's Save icon there; here it is the game's).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_0050DAB0_orig(void);
void sub_004C26B0_orig(void);

#define G_PLAYER1      0x008900acu
#define G_GAMEMODE     0x00850078u   /* 1 career */
#define G_RACETYPE     0x0089e7b0u   /* 0 free roam */
#define G_PROFILE_NAME 0x0083a9e0u   /* +0x20 1: a profile is loaded */
#define G_CAREER       0x0085ad40u
#define C_SPOT_SET     0x8744u
#define C_SPOT         0x8750u
#define C_SPOT_HEADING 0x8760u
#define G_CAREER_CAR   0x00863480u
#define G_FECARS       0x0083ad90u
#define C_TABLE        0x9bd8u
#define TABLE_N        91u
#define R_HANDLE       0x08u
#define G_FENG         0x008384c4u
#define G_MC_JOB       0x00836514u
#define G_MC_MANAGER   0x00836498u
#define MGR_YES        6u
#define MGR_LIB        0x5cu
#define FN_LIB_FLAGS   0x006fbfccu   /* thiscall (on, bits; ret 8) */
#define LIB_ASKED      0x100u
#define G_AUTOSAVE     0x00844ad9u
#define CAR_PLACE      0x60u
#define CAR_VELOCITY   0x70u
#define CAR_HEADING    0x40cu
#define FN_NEW         0x00575620u   /* cdecl (size) */
#define FN_ITEM        0x0051f670u   /* thiscall (icon, label, 0; ret 0xC) */
#define FN_ADD_ITEM    0x0050dab0u   /* thiscall (item; ret 4) */
#define FN_MC_JOB      0x004a3100u   /* cdecl (package, package to tell, job, 0, 0, 0, 0) */
#define VT_EXPLORE     0x007954d8u
#define VT_REPLAY      0x007954f0u
#define LBL_EXPLORE    0xc3e5bcdcu
#define LBL_SAVE       0x37d13ae5u
#define ICON_SAVE      0x12f47b2fu
#define ITEM_BYTES     0x48u
#define IT_GREY        0x20u
#define IT_LABEL       0x2cu
#define MSG_ACCEPT     0x0c407210u
#define MC_JOB_SAVE    0x151u
#define STILL_MPS      1.0f
#define PLACE_MAX      100000.f
#define WAIT_FRAMES    14400
#define MC_PACKAGE     "MC_Main.fng"

static struct { int pending, keep_was; long at; } S;
static long g_frame;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }
static uint32_t player_car(void) { uint32_t p = rd(G_PLAYER1), car = p ? rd(p + 4u) : 0; return ok(car, CAR_HEADING + 2u) ? car : 0; }
/* the career has a car of its own (not a new career's prologue) */
static int career_car(void) {
    uint32_t h = MEM32(G_CAREER_CAR);
    if (!h) return 0;
    for (uint32_t i = 0; i < TABLE_N; i++) { uint32_t r = rd(G_FECARS + C_TABLE + 4u * i); if (ok(r, R_HANDLE + 4u) && MEM32(r + R_HANDLE) == h) return 1; }
    return 0;
}
/* a front-end package of this name is up (the package manager's two lists, +0xE4 and +0xF4) */
static int fe_active(const char *name) {
    uint32_t fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0;
    if (!ok(mg, 0xf8)) return 0;
    for (uint32_t head = 0xe4u; head <= 0xf4u; head += 0x10u) {
        uint32_t p = MEM32(mg + head);
        for (int k = 0; p && k < 64 && ok(p, 0x10); k++, p = MEM32(p + 4u)) {
            uint32_t n = MEM32(p + 0xcu);
            char s[48]; int i = 0;
            for (; n && i < 47 && ok(n + (uint32_t)i, 1) && MEM8(n + (uint32_t)i); i++) s[i] = (char)MEM8(n + (uint32_t)i);
            s[i] = 0;
            if (i > 4 && !strcasecmp(s, name)) return 1;
        }
    }
    return 0;
}
/* why Save cannot run now (0: it can) */
static const char *not_now(void) {
    if (MEM32(G_GAMEMODE) != 1u || MEM32(G_RACETYPE) != 0u) return "not career free roam";
    if (!MEM8(G_PROFILE_NAME + 0x20u)) return "no profile loaded";
    if (!career_car()) return "no car of the career's own yet";
    if (S.pending || fe_active(MC_PACKAGE)) return "a save runs";
    uint32_t car = player_car();
    if (!car) return "no car";
    float v2 = 0.f;
    for (uint32_t i = 0; i < 3u; i++) {
        float p = rdf(car + CAR_PLACE + 4u * i), v = rdf(car + CAR_VELOCITY + 4u * i);
        if (!isfinite(p) || fabsf(p) > PLACE_MAX || !isfinite(v)) return "no place on the map";
        v2 += v * v;
    }
    if (!(v2 < STILL_MPS * STILL_MPS)) return "moving";
    return 0;
}

/* AddOption (0x50DAB0, thiscall menu (item)): after Return to Explore Mode, Save */
void nfs_save_ov_add_item(void) {
    uint32_t menu = g_ecx, it = MEM32(g_esp + 4u);
    sub_0050DAB0_orig();
    if (!ok(it, IT_LABEL + 4u) || MEM32(it) != VT_EXPLORE || MEM32(it + IT_LABEL) != LBL_EXPLORE) return;
    if (MEM32(G_GAMEMODE) != 1u || MEM32(G_RACETYPE) != 0u) return;
    uint32_t saved_eax = g_eax, n = ITEM_BYTES, sv = nfs_guest_call(FN_NEW, 0, &n, 1, 4);
    if (!ok(sv, ITEM_BYTES)) { g_eax = saved_eax; return; }
    uint32_t arg[3] = { ICON_SAVE, LBL_SAVE, 0 };
    nfs_guest_call(FN_ITEM, sv, arg, 3, 0);
    MEM32(sv) = VT_REPLAY;
    const char *why = not_now();
    MEM8(sv + IT_GREY) = why ? 1 : 0;
    nfs_guest_call(FN_ADD_ITEM, menu, &sv, 1, 0);
    g_eax = saved_eax;
    LOGI("Save: the pause menu's Save%s%s%s", why ? " (greyed out: " : "", why ? why : "", why ? ")" : "");
}

/* Replay's action (0x4C26B0, thiscall item (package, message, 3 more), ret 0x14): Save's Accept saves */
void nfs_save_ov_accept(void) {
    uint32_t it = g_ecx, pkg = MEM32(g_esp + 4u), msg = MEM32(g_esp + 8u);
    if (!ok(it, IT_LABEL + 4u) || MEM32(it + IT_LABEL) != LBL_SAVE || MEM32(it) != VT_REPLAY) { sub_004C26B0_orig(); return; }
    if (msg == MSG_ACCEPT && !MEM8(it + IT_GREY)) {
        const char *why = not_now();
        if (why) LOGI("Save: not now (%s)", why);
        else {
            uint32_t car = player_car(), k = G_CAREER;
            S.keep_was = MEM8(k + C_SPOT_SET);
            for (uint32_t i = 0; i < 3u; i++) wrf(k + C_SPOT + 4u * i, rdf(car + CAR_PLACE + 4u * i));
            MEM16(k + C_SPOT_HEADING) = MEM16(car + CAR_HEADING);
            MEM8(k + C_SPOT_SET) = 1;
            uint32_t mgr = rd(G_MC_MANAGER), lib = ok(mgr, MGR_LIB + 4u) ? rd(mgr + MGR_LIB) : 0;
            if (MEM8(G_AUTOSAVE) && lib) {                  /* no "overwrite?", whose No turns Autosave off */
                MEM8(mgr + MGR_YES) = 1;
                uint32_t on[2] = { 1u, LIB_ASKED };
                nfs_guest_call(FN_LIB_FLAGS, lib, on, 2, 0);
            }
            uint32_t arg[7] = { pkg, pkg, MC_JOB_SAVE, 0, 0, 0, 0 };
            S.pending = 1; S.at = g_frame;
            LOGI("Save: the career at %.1f %.1f %.1f (heading %04x), the save screen", (double)rdf(k + C_SPOT),
                 (double)rdf(k + C_SPOT + 4u), (double)rdf(k + C_SPOT + 8u), MEM16(k + C_SPOT_HEADING));
            nfs_guest_call(FN_MC_JOB, 0, arg, 7, 28);
        }
    }
    g_esp += 4 + 0x14;                                      /* thiscall, 5 arguments */
}

/* each frame: the save over (its job done, MC_Main gone), the career's flag as it was */
void nfs_save_frame(void) {
    g_frame++;
    if (!S.pending) return;
    int up = MEM32(G_MC_JOB) || fe_active(MC_PACKAGE);
    if ((up || g_frame - S.at < 10) && g_frame - S.at < WAIT_FRAMES) return;
    MEM8(G_CAREER + C_SPOT_SET) = (uint8_t)S.keep_was;
    S.pending = 0;
    LOGI("Save: %s after %ld frames", up ? "the save screen still up" : "over", g_frame - S.at);
}
#endif
