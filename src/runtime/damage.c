/*
 * damage.c - crash damage on the cars' own meshes (Android, the ⚙ menu's "Dégâts visuels"): the visual part of FRS
 * Mods' "Crashing Foreign Cars" (Damage.asi), as nfsu2-web has it (runtime/damage.c and damagecore.cpp, MIT,
 * Copyright (c) 2026 th0mk; the notes on the game are there). damagecore.cpp does the work; this is its link to the
 * game here:
 *   CarRenderInfo::Render 0x620AE0       photo.c's wrap calls nfs_damage_render first (the core's frame work, bent wheels)
 *   CarCollisionBody::ApplyImpulse        wrap 0x5A0B00: the game's, then the impact (its vtable slot +0x50 checked)
 *   CarDamage::SetDamageToZero            wrap 0x637D70: the repair (a race's start, the garage), then the game's
 *   Render's open flags                   hook 0x620C32 (the web's 0x620C2B is a branch target here; the cmp between
 *                                           them changes neither esi nor esp)
 *   Render's draws: the body, an exhaust  hooks at the calls 0x624499, 0x624C52, 0x625A27 and after the last two:
 *     tip, a plate                          0x624C5B, 0x625A30 (the web's 0x624C57 and 0x625A2C are branch targets here)
 *   each frame                            nfs_damage_frame (after each Present)
 * Without the web's free roam multiplayer (the core's part for friends' cars is compiled, never called) and without the
 * scratches' texture pack (a mod file: the dents come without scratches). The core's clones live in the game's heap
 * (operator new 0x575620 / delete 0x575630). The switch stops new impacts; the damage there stays until the game's own
 * repair (a race's start, the garage).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
double nfs_guest_last_st0(void);
void sub_005A0B00_orig(void);
void sub_00637D70_orig(void);

/* damagecore.cpp */
int  dmg_init(void);
void dmg_render_entry(uint32_t cri, uint32_t args);
void dmg_impulse(uint32_t body, uint32_t impulse, uint32_t point);
void dmg_zero(uint32_t car_damage, uint32_t caller);
void dmg_open_flags(uint32_t cri, uint32_t frame);
void dmg_body_draw(uint32_t model, uint32_t l2w);
void dmg_marker_pre(int slot, uint32_t model, uint32_t l2w);
void dmg_marker_post(void);
void dmg_main_loop(void);

#define FN_NEW            0x00575620u
#define FN_DELETE         0x00575630u
#define APPLY_IMPULSE     0x005a0b00u
#define VT_APPLY_IMPULSE  0x50u

static atomic_int s_want;            /* the menu's switch */
static int s_ready = -1;             /* the core: -1 not started, 0 failed, 1 running */

/* ---- for the core ---- */
uint8_t *dmg_base(void) { return (uint8_t *)ADDR(0); }
uint32_t dmg_call(uint32_t fn, int n, const uint32_t *a) { return nfs_guest_call(fn, 0, a, (unsigned)n, 4u * (unsigned)n); }
uint32_t dmg_stdcall(uint32_t fn, int n, const uint32_t *a) { return nfs_guest_call(fn, 0, a, (unsigned)n, 0); }
uint32_t dmg_thiscall(uint32_t fn, uint32_t self, int n, const uint32_t *a) { return nfs_guest_call(fn, self, a, (unsigned)n, 0); }
float dmg_thiscall_f(uint32_t fn, uint32_t self, int n, const uint32_t *a) {
    nfs_guest_call(fn, self, a, (unsigned)n, 0);
    return (float)nfs_guest_last_st0();
}
uint32_t dmg_alloc(uint32_t n) {
    uint32_t p = nfs_guest_call(FN_NEW, 0, &n, 1, 4);
    if (p && nfs_guest_readable(p, n)) memset((void *)ADDR(p), 0, n);
    return p;
}
void dmg_free(uint32_t p) { if (p) nfs_guest_call(FN_DELETE, 0, &p, 1, 4); }
int dmg_committed(uint32_t a, uint32_t n) { return a >= 0x10000u && nfs_guest_readable(a, n ? n : 1u); }
uint32_t dmg_va_free(void) { return 256u << 20; }
int dmg_big_memory(void) { return 0; }
uint32_t dmg_scratches_read(uint32_t *data, uint32_t *size) { *data = 0; *size = 0; return 0; }

/* ---- the switch ---- */
void nfs_damage_enable(int on) { atomic_store(&s_want, on != 0); }
static int running(void) { return s_ready == 1; }

/* guest thread, after each Present */
void nfs_damage_frame(void) {
    if (s_ready < 0 && atomic_load(&s_want)) {
        s_ready = dmg_init() ? 1 : 0;
        LOGI("Damage: %s", s_ready ? "on" : "could not start (no guest memory)");
    }
    if (running()) dmg_main_loop();
}
/* photo.c's wrap of CarRenderInfo::Render (0x620AE0), before the game's: the render info, its stack arguments */
void nfs_damage_render(uint32_t cri, uint32_t args) { if (running()) dmg_render_entry(cri, args); }

/* 0x5A0B00 (thiscall CarCollisionBody: impulse*, point*) */
void nfs_damage_ov_impulse(void) {
    uint32_t body = g_ecx, impulse = MEM32(g_esp + 4u), point = MEM32(g_esp + 8u);
    sub_005A0B00_orig();
    if (!running() || !atomic_load(&s_want) || !dmg_committed(body, 4) || !dmg_committed(MEM32(body) + VT_APPLY_IMPULSE, 4) ||
        MEM32(MEM32(body) + VT_APPLY_IMPULSE) != APPLY_IMPULSE) return;
    uint32_t ka = g_eax, kd = g_edx;
    dmg_impulse(body, impulse, point);
    g_eax = ka; g_edx = kd;
}
/* 0x637D70 (thiscall CarDamage): the repair */
void nfs_damage_ov_zero(void) {
    if (running()) dmg_zero(g_ecx, MEM32(g_esp));
    sub_00637D70_orig();
}
/* widescreen.c's recomp_hook: 1 when the site is ours */
int nfs_damage_hook(uint32_t va) {
    switch (va) {
    case 0x00620C32u: if (running()) dmg_open_flags(g_esi, g_esp); return 1;   /* Render, after the open flags: esi the render info */
    case 0x00624499u: if (running()) dmg_body_draw(MEM32(g_esp), MEM32(g_esp + 4u)); return 1;   /* a part's draw (model, l2w) */
    case 0x00624C52u: if (running()) dmg_marker_pre(16, MEM32(g_esp), MEM32(g_esp + 4u)); return 1;   /* an exhaust tip's */
    case 0x00625A27u: if (running()) dmg_marker_pre(33, MEM32(g_esp), MEM32(g_esp + 4u)); return 1;   /* a plate's */
    case 0x00624C5Bu: case 0x00625A30u: if (running()) dmg_marker_post(); return 1;   /* back from them */
    }
    return 0;
}
#endif
