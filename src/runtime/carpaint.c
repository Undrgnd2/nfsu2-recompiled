/*
 * carpaint.c - a car's exhaust tips, brake calipers and rims keep their own paint (or none), whatever paint other cars
 * with the same parts have (Android).
 *
 * Adapted from nfsu2-web's runtime/carpaint.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there):
 * a part's paint is a light material in its model's solid's table (solid +0x3C, +0x1A entries), which the car drawing
 * (0x620AE0) writes with the car's own paint only when the car has one (render info +0x394 tips, +0x398 brakes, +0x390
 * rims), and the other drawing (0x6162E0) not at all for the tips and calipers. With several cars drawing the same
 * solid (LAN races), an unpainted part took the paint of the last car drawn with a painted one of that style, and kept
 * it into the garage. So before a car is drawn (photo.c's wraps of 0x620AE0 and 0x6162E0) its tips' and calipers'
 * models get the material their solid loaded with, then the car's own paint if it has one, and its rims, if it has no
 * rim paint, the ones they loaded with.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define CRI_MODELS     0x17a8u
#define CRI_RIM_PAINT  0x390u
#define CRI_TIP_PAINT  0x394u
#define CRI_BRK_PAINT  0x398u
#define MODEL_SOLID    0xcu
#define SOLID_NLM      0x1au
#define SOLID_LM       0x3cu
#define G_LM_LIST      0x008358c0u
#define G_LM_DEFAULT   0x007f399cu
#define LM_HASH        0xcu

enum { H_TIP, H_CALIPER, H_CALIPER2, H_MAGSILVER, H_MAGCHROME, H_MAGGUNMETAL, NH };
static const uint32_t g_hash[NH] = { 0x3b3a1f8du, 0xd6640dffu, 0xa3186e2bu, 0x22719fa9u, 0xfd102a92u, 0x12049251u };

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }

static uint32_t g_def[NH], g_def_key[3];
static void defaults(void) {
    uint32_t head = rd(G_LM_LIST), tail = rd(G_LM_LIST + 4u), dflt = rd(G_LM_DEFAULT);
    if (g_def_key[0] == head && g_def_key[1] == tail && g_def_key[2] == dflt && head) return;
    g_def_key[0] = head; g_def_key[1] = tail; g_def_key[2] = dflt;
    for (int i = 0; i < NH; i++) g_def[i] = 0;
    const uint32_t end = G_LM_LIST - 4u;
    uint32_t m = head ? head - 4u : 0u;
    for (int n = 0; m && m != end && ok(m, LM_HASH + 4u) && n < 65536; n++) {
        uint32_t h = MEM32(m + LM_HASH);
        for (int i = 0; i < NH; i++) if (h == g_hash[i] && !g_def[i]) g_def[i] = m;
        uint32_t nx = MEM32(m + 4u);
        m = nx ? nx - 4u : 0u;
    }
    for (int i = 0; i < NH; i++) if (!g_def[i]) g_def[i] = dflt;
}
static void put(uint32_t ri, uint32_t sl, int h0, int h1, const uint32_t *mat) {
    for (uint32_t j = 0; j < 8u; j++) {
        uint32_t mdl = MEM32(ri + CRI_MODELS + 0x20u * sl + 4u * j), so = mdl ? rd(mdl + MODEL_SOLID) : 0u;
        if (!ok(so, SOLID_LM + 4u)) continue;
        int n = (int8_t)MEM8(so + SOLID_NLM);
        uint32_t t = MEM32(so + SOLID_LM);
        if (n <= 0 || !ok(t, 8u * (uint32_t)n)) continue;
        for (int e = 0; e < n; e++) {
            uint32_t h = MEM32(t + 8u * (uint32_t)e);
            for (int i = h0; i < h1; i++) if (h == g_hash[i] && mat[i]) MEM32(t + 8u * (uint32_t)e + 4u) = mat[i];
        }
    }
}
void nfs_carpaint_draw(uint32_t ri) {
    if (!ok(ri, CRI_MODELS + 0x20u * 0x40u)) return;
    defaults();
    uint32_t tip = MEM32(ri + CRI_TIP_PAINT), brk = MEM32(ri + CRI_BRK_PAINT), rim = MEM32(ri + CRI_RIM_PAINT), own[NH];
    for (int i = 0; i < NH; i++) own[i] = g_def[i];
    put(ri, 0x10u, H_TIP, H_TIP + 1, own);
    put(ri, 0x1bu, H_CALIPER, H_CALIPER2 + 1, own);
    put(ri, 0x1cu, H_CALIPER, H_CALIPER2 + 1, own);
    if (!rim) { put(ri, 0x1du, H_MAGSILVER, NH, own); put(ri, 0x1eu, H_MAGSILVER, NH, own); }
    for (int i = 0; i < NH; i++) own[i] = 0;
    own[H_TIP] = tip; own[H_CALIPER] = own[H_CALIPER2] = brk;
    if (tip) put(ri, 0x10u, H_TIP, H_TIP + 1, own);
    if (brk) put(ri, 0x1bu, H_CALIPER, H_CALIPER2 + 1, own);
}
#endif
