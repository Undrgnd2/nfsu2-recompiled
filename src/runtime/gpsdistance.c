/*
 * gpsdistance.c - the distance to the GPS destination under its arrow (Android): in a straight line, not by road, in
 * m/km or ft/mi as the game's Speedometer Units option has it, in free roam while the GPS guides.
 *
 * Adapted from nfsu2-web's runtime/gpsdistance.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 *  - the HUD's step of Player::Update (0x600590) calls the HUD's update 0x4EC5A0 (player, car; ret 8, its only
 *    caller): wrapped here, the distance set right after it, as the web's hook at 0x6005E0 (a branch target, so
 *    not a hook site in this lift);
 *  - the text is a copy of the HUD's ONLINE_PLAYER_1 string named GPS_DISTANCE (the web's fe_clone: its class's
 *    Clone, linked after it in its package's list), the HUD package's own: freed with it, and looked up by its name
 *    every frame, so no pointer outlives a HUD (shops, races, pause);
 *  - the destination [0x8385A0] (x, y) against the car's position (+0x50); GPS::GetState (0x501100, this 0x838590):
 *    0 and 3 guide, 1/2 no satellite or path, 4 off.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_004EC5A0_orig(void);

#define G_PLAYER1      0x008900acu
#define G_RACETYPE     0x0089e7b0u
#define G_GPS          0x00838590u   /* the GPS: +0x10/+0x14 its destination x, y ([0x8385A0]); byte [0x8385B9] on */
#define G_GPS_ON       0x008385b9u
#define G_GPS_DEST     0x008385a0u
#define G_METRIC       0x0083aa1au   /* byte: Speedometer Units, 1 metric */
#define FN_NEW         0x00575620u   /* cdecl (size) */
#define FN_GPS_STATE   0x00501100u   /* thiscall GPS */
#define FN_FE_HASH     0x00505450u   /* cdecl (name) */
#define FN_FE_FIND     0x00537a10u   /* cdecl (package name, hash) */
#define FN_FE_PACKAGE  0x0052cef0u   /* cdecl (package name) -> FEPackage* (+0x48 count, +0x4C first, +0x50 last) */
#define FN_FE_SETSTR   0x00535fe0u   /* thiscall object (char*) */
#define FN_FE_COLOR    0x00517e20u   /* thiscall object (int bgra[4]*, 0) */
#define FN_FE_POS      0x00517c10u   /* thiscall object (float xyz*, 0) */
#define FN_FE_SCALE    0x00517db0u   /* thiscall object (float xyz*, 0) */
#define FN_FE_SHOW     0x0050ca50u   /* cdecl (object) */
#define FN_FE_HIDE     0x0050ca00u   /* cdecl (object) */
#define PL_CAR         4u
#define PL_HUD         0x9a4u
#define CAR_POS        0x50u
#define FE_WALK        16384

static uint32_t s_scr;               /* 64 bytes of guest memory: names, vectors, the text */
static uint32_t s_hash, s_src_hash;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = rd(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }
static void gstr(uint32_t a, const char *s) { uint32_t i = 0; for (; s[i] && i < 47u; i++) MEM8(a + i) = (uint8_t)s[i]; MEM8(a + i) = 0; }
static uint32_t c1(uint32_t fn, uint32_t arg) { return nfs_guest_call(fn, 0, &arg, 1, 4); }
static uint32_t c2(uint32_t fn, uint32_t a, uint32_t b) { uint32_t v[2] = { a, b }; return nfs_guest_call(fn, 0, v, 2, 8); }
static void obj2(uint32_t fn, uint32_t obj, uint32_t vec) { uint32_t v[2] = { vec, 0 }; nfs_guest_call(fn, obj, v, 2, 0); }
static uint32_t hash_of(const char *name) { gstr(s_scr, name); return c1(FN_FE_HASH, s_scr); }

/* the list (the address of its "first") that holds FE object o: the package's or a group's (type 5: +0x64) */
static uint32_t list_of(uint32_t first, uint32_t o, int depth) {
    int n = 0;
    for (uint32_t it = rd(first); it && n < FE_WALK; it = rd(it + 4u), n++) {
        if (it == o) return first;
        if (depth < 8 && rd(it + 0x18u) == 5u) { uint32_t l = list_of(it + 0x64u, o, depth + 1); if (l) return l; }
    }
    return 0;
}
/* a copy of string object o of package pkg (its name), named by hash, linked after o: 0 when not made */
static uint32_t clone_text(uint32_t pkg, uint32_t o, uint32_t hash) {
    if (!ok(o, 0x70) || rd(o + 0x18u) != 2u || !ok(rd(o), 8)) return 0;
    uint32_t pk = c1(FN_FE_PACKAGE, pkg), l = ok(pk, 0x54) ? list_of(pk + 0x4cu, o, 0) : 0;
    if (!l) return 0;
    uint32_t n = 0, last = 0;
    for (uint32_t it = rd(l); it && n < FE_WALK; it = rd(it + 4u)) { last = it; n++; }
    if (n != rd(l - 4u) || last != rd(l + 4u)) return 0;
    uint32_t one = 1, cl = nfs_guest_call(rd(rd(o) + 4u), o, &one, 1, 0);      /* its class's Clone (thiscall (1)) */
    if (!ok(cl, 0x70)) return 0;
    MEM32(cl + 0x58u) = 0;                                                    /* a render object of its own */
    uint32_t dn = rd(o + 0x30u), d0 = rd(o + 0x2cu), d1 = rd(cl + 0x2cu);      /* its drawing data as the original's */
    if (rd(cl + 0x30u) == dn && dn && dn <= 0x100u && ok(d0, dn) && ok(d1, dn) && d0 != d1)
        for (uint32_t i = 0; i < dn; i++) MEM8(d1 + i) = MEM8(d0 + i);
    MEM32(cl + 0x10u) = hash;
    uint32_t nx = rd(o + 4u);
    MEM32(cl + 4u) = nx; MEM32(cl + 8u) = o; MEM32(o + 4u) = cl;
    if (nx) MEM32(nx + 8u) = cl; else MEM32(l + 4u) = cl;
    MEM32(l - 4u) += 1;
    return cl;
}
static void format(char *text, size_t size, float metres, int metric) {
    double small = metric ? metres : metres / 0.3048, threshold = metric ? 1000.0 : 5280.0;
    if (small < threshold - 0.5) snprintf(text, size, "%.0f %s", small, metric ? "m" : "ft");
    else snprintf(text, size, "%.1f %s", small / threshold, metric ? "km" : "mi");
}

static void gps_hud(uint32_t pl) {
    if (!pl || pl != rd(G_PLAYER1)) return;
    uint32_t hud = rd(pl + PL_HUD), car = rd(pl + PL_CAR);
    if (!ok(hud, 0x60)) return;
    uint32_t pkg = MEM32(hud + 0x10u), part = MEM32(hud + 0x5cu);
    if (!pkg || !ok(part, 0x20)) return;
    if (!s_scr) {
        uint32_t sz = 64;
        s_scr = nfs_guest_call(FN_NEW, 0, &sz, 1, 4);
        if (!ok(s_scr, 64)) { s_scr = 0; return; }
    }
    if (!s_hash) { s_hash = hash_of("GPS_DISTANCE"); s_src_hash = hash_of("ONLINE_PLAYER_1"); }
    uint32_t obj = c2(FN_FE_FIND, pkg, s_hash);
    int on = ok(car, 0x60) && MEM8(G_GPS_ON) && !rd(G_RACETYPE) &&
             ((MEM32(part + 0x10u) & MEM32(part + 0x18u)) || (MEM32(part + 0x14u) & MEM32(part + 0x1cu)));
    if (on) { uint32_t st = nfs_guest_call(FN_GPS_STATE, G_GPS, 0, 0, 0); on = st == 0 || st == 3; }
    float d = on ? hypotf(rdf(G_GPS_DEST) - rdf(car + CAR_POS), rdf(G_GPS_DEST + 4u) - rdf(car + CAR_POS + 4u)) : 0.f;
    if (!on || !isfinite(d) || d >= 1000000.f) { if (obj) c1(FN_FE_HIDE, obj); return; }
    int made = !obj;
    if (!obj) {
        uint32_t src = c2(FN_FE_FIND, pkg, s_src_hash);
        obj = src ? clone_text(pkg, src, s_hash) : 0;
        if (!obj) return;
        MEM32(obj + 0x6cu) = (MEM32(obj + 0x6cu) & ~0xfu) | 5u;                /* centred */
        MEM32(obj + 0x1cu) |= 0x100u;
        for (int i = 0; i < 4; i++) MEM32(s_scr + 4u * (uint32_t)i) = 255;     /* white */
        obj2(FN_FE_COLOR, obj, s_scr);
        wrf(s_scr, 0.f); wrf(s_scr + 4u, -55.f); wrf(s_scr + 8u, 0.5f);       /* under the arrow */
        obj2(FN_FE_POS, obj, s_scr);
        wrf(s_scr, 0.7f); wrf(s_scr + 4u, 0.7f); wrf(s_scr + 8u, 1.f);
        obj2(FN_FE_SCALE, obj, s_scr);
    }
    char text[32];
    format(text, sizeof text, d, MEM8(G_METRIC) == 1);
    uint32_t str = rd(obj + 0x64u);                                           /* its UTF-16 text: set when it changed */
    int same = str != 0;
    for (uint32_t i = 0; same; i++) {
        if (!ok(str + i * 2u, 2) || MEM16(str + i * 2u) != (unsigned char)text[i]) same = 0;
        if (!text[i]) break;
    }
    if (!same) { gstr(s_scr, text); nfs_guest_call(FN_FE_SETSTR, obj, &s_scr, 1, 0); MEM32(obj + 0x1cu) |= 2u; }
    c1(FN_FE_SHOW, obj);
    if (made) LOGI("GPS: distance under the arrow: %s", text);
}

/* 0x4EC5A0 (player, car; ret 8): the HUD's update, from Player::Update */
void nfs_gps_ov_hud(void) {
    uint32_t pl = MEM32(g_esp + 4u);
    sub_004EC5A0_orig();
    gps_hud(pl);
}
#endif
