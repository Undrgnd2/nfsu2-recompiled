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
 * Without the web's free roam multiplayer (the core's part for friends' cars is compiled, never called). The scratches:
 * Crashing Foreign Cars' own texture pack (DAMAGE_SCRATCH1..4, DXT5; nfsu2-web's tools/damage/scratches.tpk) in the
 * APK (assets/damage/scratches.tpk), handed here by main.cpp at the start (nfs_damage_scratches); the core gives it to
 * the game's LoadChunks once, from a block of the game's heap, its data 128-aligned, checked first as the game's loader
 * trusts it (dmg_tpk_ok, as the web's). The core's clones live in the game's heap
 * (operator new 0x575620 / delete 0x575630). The switch stops new impacts; the damage there stays until the game's own
 * repair (a race's start, the garage).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <stdlib.h>
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
/* ---- the scratches' texture pack ---- */
static uint8_t *s_tpk;                /* the pack as the APK has it (main.cpp), 0 none */
static uint32_t s_tpk_n;
void nfs_damage_scratches(const void *data, unsigned len) {
    if (s_tpk || !data || len < 16 || len > (16u << 20)) return;
    if (!(s_tpk = malloc(len))) return;
    memcpy(s_tpk, data, len); s_tpk_n = len;
}
#define TPK_CHUNK 0xb3300000u
static int64_t tpk_chunk(uint32_t b, uint32_t end) { return b + 8u <= end && MEM32(b + 4u) <= end - b - 8u ? (int64_t)MEM32(b + 4u) : -1; }
static uint64_t tex_bytes(uint32_t w, uint32_t h, uint32_t levels, uint32_t fmt) {
    uint32_t blk = 1, bpb;
    switch (fmt) {
    case 21: case 22: bpb = 4; break;
    case 28: case 41: bpb = 1; break;
    case 0x31545844u: blk = 4; bpb = 8; break;
    case 0x33545844u: case 0x35545844u: blk = 4; bpb = 16; break;
    default: return 0;
    }
    uint64_t n = 0;
    for (uint32_t lv = 0; lv < (levels ? levels : 1u) && lv < 16u; lv++) {
        uint32_t lw = w >> lv ? w >> lv : 1u, lh = h >> lv ? h >> lv : 1u;
        n += (uint64_t)((lw + blk - 1u) / blk) * ((lh + blk - 1u) / blk) * bpb;
    }
    return n;
}
/* the TPK at d (n bytes) as the game's loader trusts it (nfsu2-web damage.c dmg_tpk_ok, tools/packcheck.py) */
static int tpk_ok(uint32_t d, uint32_t n) {
    static const uint32_t want[3] = { 0x33310002u, 0x33310004u, 0x33310005u };
    uint32_t at[3] = { 0 }, sz[3] = { 0 }, pix = 0, pend = 0, end = d + n;
    for (uint32_t c = d + 8u; c < end;) {
        int64_t s = tpk_chunk(c, end);
        if (s < 0) return 0;
        uint32_t ce = c + 8u + (uint32_t)s;
        if (MEM32(c) == 0xb3310000u || MEM32(c) == 0xb3320000u)
            for (uint32_t k = c + 8u; k < ce;) {
                int64_t ks = tpk_chunk(k, ce);
                if (ks < 0) return 0;
                for (int j = 0; j < 3; j++) if (MEM32(k) == want[j]) { at[j] = k + 8u; sz[j] = (uint32_t)ks; }
                if (MEM32(k) == 0x33320002u) { pix = (k + 8u + 0x7fu) & ~0x7fu; pend = k + 8u + (uint32_t)ks; }
                k += 8u + (uint32_t)ks;
            }
        c = ce;
    }
    uint32_t cnt = sz[0] / 8u;
    if (!at[0] || !at[1] || !at[2] || !pix || pix > pend || sz[0] != cnt * 8u || sz[1] != cnt * 0x7cu || sz[2] != cnt * 0x20u) return 0;
    for (uint32_t k = 0; k < cnt; k++) {
        uint32_t ti = at[1] + k * 0x7cu, img = MEM32(ti + 0x30u), pal = MEM32(ti + 0x34u), isz = MEM32(ti + 0x38u), psz = MEM32(ti + 0x3cu);
        uint64_t need = tex_bytes(MEM16(ti + 0x44u), MEM16(ti + 0x46u), MEM8(ti + 0x4eu), MEM32(at[2] + k * 0x20u + 0x14u));
        if (MEM32(ti + 0x24u) != MEM32(at[0] + 8u * k) || !MEM16(ti + 0x44u) || !MEM16(ti + 0x46u) || !need || isz < need ||
            img > pend - pix || isz > pend - pix - img || (psz && (pal > pend - pix || psz > pend - pix - pal))) return 0;
    }
    return 1;
}
/* the core, once: the pack into a block of the game's heap (its data 128-aligned) -> the block, 0 none */
uint32_t dmg_scratches_read(uint32_t *data, uint32_t *size) {
    *data = 0; *size = 0;
    if (!s_tpk) { LOGI("Damage: no scratches' pack in the APK: the dents without scratches"); return 0; }
    uint32_t n = s_tpk_n, want = n + 128u, block = nfs_guest_call(FN_NEW, 0, &want, 1, 4);
    if (!dmg_committed(block, want)) { LOGI("Damage: no memory for the scratches' pack"); return 0; }
    uint32_t d = (block + 127u) & ~127u;
    memcpy((void *)ADDR(d), s_tpk, n);
    if (MEM32(d) != TPK_CHUNK || MEM32(d + 4u) + 8u != n || !tpk_ok(d, n)) {
        dmg_free(block);
        LOGI("Damage: the scratches' pack is not one the game's loader takes: the dents without scratches");
        return 0;
    }
    *data = d; *size = n;
    LOGI("Damage: the scratches' pack (%u KB) to the game", n >> 10);
    return block;
}

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
