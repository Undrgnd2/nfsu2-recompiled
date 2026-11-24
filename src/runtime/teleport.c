/*
 * teleport.c - the world map's Téléporter (Android): from career free roam's world map, to the nearest shop of a kind
 * (the car lot, the body shop, the car specialties, the paint shop, the performance shop) or the garage, or to a
 * radar's straight road (radars.c), through the game's hot-position jump (as nfsu2-web's career.c shopgo and walls.c).
 *  - the shops: the career's shop table [0x8383F0] ([0x8383F4] of them, 0xA0 bytes: +0 its name, CC_CAR_LOT_1 ...,
 *    +0x3C its marker's hash), each marker found on the track's marker list (0x88F298 +0x43C, +0x438 its size; a
 *    marker +0x30 its hash, +0x42 its size, +4/+8 x y, +0x14 z, +0xC/+0x10 its way); the kind by the shop's name;
 *    the nearest one on this career's side of its walls (walls.c nfs_walls_open_at);
 *  - the jump: [0x890140] x y z, [0x8900D0] the heading (65536 a turn), [0x8900CE] set, [0x8900CD] jump (the player's
 *    next update);
 *  - the world map open: the top package UI_InGame_WorldMap.fng (nfs_teleport_status, for the touch controls).
 * Career free roam only, not LAN or Online.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
int nfs_walls_open_at(float x, float y, float z);
int nfs_radar_info(int i, float *start, float *dir, int *record);

#define G_GAMEMODE      0x00850078u
#define G_RACETYPE      0x0089e7b0u
#define G_FLOW          0x008654a4u
#define G_FENG          0x008384c4u
#define G_PLAYER1       0x008900acu
#define G_SHOPS         0x008383f0u
#define G_NSHOPS        0x008383f4u
#define SHOP_SIZE       0xa0u
#define G_PATHS         0x0088f298u
#define G_HOTPOS        0x00890140u
#define G_HOTPOS_ANGLE  0x008900d0u
#define G_HOTPOS_JUMP   0x008900cdu
#define G_HOTPOS_VALID  0x008900ceu
#define CAR_PLACE       0x60u

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = rd(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}
static int top_is(const char *name) {
    uint32_t fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0;
    char b[48]; read_str(top ? rd(top + 0xcu) : 0, b, sizeof b);
    return b[0] && !strcasecmp(b, name);
}
static int roam(void) { return rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u; }

/* 1 the world map is open in career free roam */
int nfs_teleport_status(void) { return roam() && top_is("UI_InGame_WorldMap.fng"); }

static uint32_t marker_by_hash(uint32_t h) {
    uint32_t m = rd(G_PATHS + 0x43cu), end = m + rd(G_PATHS + 0x438u);
    for (uint32_t k = 0; m && m < end && k < 4096 && ok(m, 0x44); k++) {
        if (MEM32(m + 0x30u) == h) return m;
        int16_t sz = (int16_t)MEM16(m + 0x42u);
        if (sz <= 0) break;
        m += (uint32_t)sz;
    }
    return 0;
}
/* the kind of a shop by its name: 1 car lot, 2 body shop, 3 car specialties, 4 paint, 5 performance, 6 garage */
static int kind_of(const char *n) {
    char u[64]; size_t i = 0;
    for (; n[i] && i + 1 < sizeof u; i++) u[i] = (char)(n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]);
    u[i] = 0;
    if (strstr(u, "CAR_LOT")) return 1;
    if (strstr(u, "BODY")) return 2;
    if (strstr(u, "SPECIAL") || strstr(u, "ICE")) return 3;
    if (strstr(u, "GRAPHIC") || strstr(u, "PAINT")) return 4;
    if (strstr(u, "PERF")) return 5;
    if (strstr(u, "CRIB") || strstr(u, "GARAGE") || strstr(u, "SAFE") || strstr(u, "HOME")) return 6;
    return 0;
}
static void jump(float x, float y, float z, float dx, float dy) {
    float a = atan2f(dy, dx); if (a < 0.f) a += 6.2831853f;
    wrf(G_HOTPOS, x); wrf(G_HOTPOS + 4u, y); wrf(G_HOTPOS + 8u, z + 0.5f);
    MEM16(G_HOTPOS_ANGLE) = (uint16_t)(int)(a * (65536.f / 6.2831853f));
    MEM8(G_HOTPOS_VALID) = 1; MEM8(G_HOTPOS_JUMP) = 1;
}

static atomic_int s_ask;              /* the kind asked for (1..6, or 100 + a radar), 0 none */

/* any thread: 1..6 a shop's kind (as kind_of), 100 + i radar i; 0 when not in career free roam's world map */
int nfs_teleport(int what) {
    if (!roam()) return 0;
    atomic_store(&s_ask, what);
    return 1;
}

void nfs_teleport_frame(void) {
    int what = atomic_exchange(&s_ask, 0);
    if (!what || !roam()) return;
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    float cx = ok(car, CAR_PLACE + 12u) ? rdf(car + CAR_PLACE) : 0.f, cy = ok(car, CAR_PLACE + 12u) ? rdf(car + CAR_PLACE + 4u) : 0.f;
    if (what >= 100) {
        float s[3], d[2]; int rec;
        if (!nfs_radar_info(what - 100, s, d, &rec)) { LOGI("Teleport: no radar %d", what - 100 + 1); return; }
        jump(s[0], s[1], s[2], d[0], d[1]);
        LOGI("Teleport: to radar %d's road (%.0f %.0f)", what - 100 + 1, (double)s[0], (double)s[1]);
        return;
    }
    uint32_t t = rd(G_SHOPS), n = rd(G_NSHOPS), best = 0;
    float bd = 1e30f;
    char name[48], bestname[48] = "";
    for (uint32_t i = 0; t && i < n && i < 256u; i++) {
        uint32_t sh = t + i * SHOP_SIZE;
        if (!ok(sh, SHOP_SIZE)) break;
        read_str(sh, name, sizeof name);
        static int listed;
        if (!listed && i + 1 == n) listed = 1;
        if (kind_of(name) != what) continue;
        uint32_t m = marker_by_hash(MEM32(sh + 0x3cu));
        if (!m) continue;
        float x = rdf(m + 4u), y = rdf(m + 8u), z = rdf(m + 0x14u);
        if (!isfinite(x + y + z) || !nfs_walls_open_at(x, y, z + 0.5f)) continue;
        float d = hypotf(x - cx, y - cy);
        if (d < bd) { bd = d; best = m; memcpy(bestname, name, sizeof name); }
    }
    if (!best) {
        LOGI("Teleport: no shop of kind %d open here; the shops:", what);
        for (uint32_t i = 0; t && i < n && i < 256u && ok(t + i * SHOP_SIZE, SHOP_SIZE); i++) {
            read_str(t + i * SHOP_SIZE, name, sizeof name);
            LOGI("Teleport:   %s (kind %d)", name, kind_of(name));
        }
        return;
    }
    float z = rdf(best + 0x14u);
    if (z == 0.f && ok(car, CAR_PLACE + 12u)) z = rdf(car + CAR_PLACE + 8u);
    jump(rdf(best + 4u), rdf(best + 8u), z, rdf(best + 0xcu), rdf(best + 0x10u));
    LOGI("Teleport: to %s (%.0f m away)", bestname, (double)bd);
}
#endif
