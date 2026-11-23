/*
 * showoff.c - the exhibition mode (Android): with the player's car standing in free roam, the SHOWOFF touch button
 * opens its parts one step at a time: the hood, then the trunk alone, then both doors, then all of them, then closes
 * them all. They close by themselves as soon as the car drives off. Made for photo mode: open, then take the picture.
 *
 * Adapted from nfsu2-web's runtime/showoff.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there),
 * the player's car only (no friends' cars, no EAGL heap changes: one car type's animations at a time are well within
 * the game's own animation heap):
 *  - the parts are the game's cover shoot's: CARS\<car>\PARTS_ANIMATIONS.BIN, loaded here (0x57CFB0) and kept;
 *  - each part is a node (0x28 bytes) made by 0x433A80 and moved by 0x433D50, in a block of ours, put on the car
 *    type's bank list only while the car is drawn (photo.c's wraps of 0x620AE0 and 0x615A10 call
 *    nfs_showoff_put / nfs_showoff_take), so the game's own cars of that type are not touched;
 *  - the trunk's audio pieces are drawn as the cover shoot draws them while the trunk is open near the camera;
 *  - the game throws its animation banks away (0x438140 with 0, after a cover shoot, a garage...): ours are unloaded
 *    first and the walk reads the next bank before deleting one (the web's crash fix), wrap nfs_showoff_ov_banks.
 */
#ifdef NFS_DRIFT_MODE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_00438140_orig(void);

#define G_FLOW         0x008654a4u
#define G_RACE         0x00890118u
#define G_CAREER_RACE  0x00863470u
#define G_RACETYPE     0x0089e7b0u
#define G_GAMEMODE     0x00850078u
#define G_FENG         0x008384c4u
#define G_FREEZE       0x00890084u
#define G_PLAYER1      0x008900acu
#define G_BANKS        0x00827c9cu
#define G_SOUND        0x0082b884u
#define G_SHOWCASE     0x00836352u
#define G_VIEW1        0x00832e50u
#define FN_NEW         0x00575620u   /* cdecl (size) */
#define FN_NODE_INIT   0x00433a80u   /* thiscall node (type, speed, part, kind, style 22), ret 0x14 */
#define FN_NODE_GO     0x00433d50u   /* cdecl (node, 2 open / 3 close) */
#define FN_ATTR        0x0060fef0u   /* thiscall part (hash, 0) */
#define FN_HASH        0x0043db50u   /* cdecl (string) */
#define FN_HASH2       0x0043db80u   /* cdecl (string, seed) */
#define FN_LOAD        0x0057cfb0u   /* cdecl (file, 0, 0, 1, callback, arg) */
#define FN_UNLOAD      0x00580e90u   /* cdecl (resource file) */
#define FN_NOP         0x004022c0u
#define FN_SOUND       0x00481500u   /* thiscall world's sounds (which) */
#define FN_BANK_FREE   0x004380f0u   /* thiscall bank */
#define S_ATTR_DOOR    0x007a8750u
#define S_ATTR_HOOD    0x007a8780u
#define S_STYLE22      0x007a8770u
#define S_STYLE22CF    0x007a875cu
#define PL_CAR         4u
#define PL_HUD         0x9a4u
#define CAR_REC        0x14u
#define CAR_POS        0x60u
#define CAR_FWD_SPEED  0x42cu
#define CAR_RI         0x554u
#define RI_RIDE        0x04u
#define RI_TYPE        0x08u
#define RIDE_HOOD      0x5b4u
#define RIDE_WIDE      0x5a8u
#define RIDE_DOORS     0x810u
#define NODE_SZ        0x28u
#define OPEN_MPS       1.0f
#define SHUT_MPS       2.0f
#define TRUNK_NEAR_M   40.f
#define SO_TYPES       48
#define OPEN_HOOD 1u
#define OPEN_TRUNK 2u
#define OPEN_LDOOR 4u
#define OPEN_RDOOR 8u
#define OPEN_ALL 15u

typedef struct { uint32_t type, file, name; uint8_t st; int tries; long since; } Bank;
static struct {
    uint32_t mem;                     /* our block: 4 nodes, then SO_TYPES file names of 64 bytes */
    uint32_t car, ri, type, node[4];
    uint8_t want, has, live;
    Bank bank[SO_TYPES]; int nbank;
    int step, failed;
    long frame;
} S;
static atomic_int s_press, s_status;
static int s_presses, s_note;          /* presses seen; why the last did nothing (8 moving, 16 not free roam, 32 no animations) */
#define NSTEPS 5
static const uint8_t k_step[NSTEPS] = { 0, OPEN_HOOD, OPEN_TRUNK, OPEN_LDOOR | OPEN_RDOOR, OPEN_ALL };
static const char *const k_stepname[NSTEPS] = { "all closed", "the hood", "the trunk", "the doors", "all open" };
static const char *const k_part[4] = { "hood", "trunk", "left door", "right door" };

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = rd(va); float f; memcpy(&f, &v, 4); return f; }
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}
static uint32_t call(uint32_t va, uint32_t ecx, const uint32_t *a, unsigned n, int cdecl_) {
    return nfs_guest_call(va, ecx, a, n, cdecl_ ? 4u * n : 0u);
}

static uint32_t bank_of(uint32_t type) {
    uint32_t e = rd(G_BANKS), b = e ? e - 4u : 0;
    for (int n = 0; b && b != G_BANKS - 4u && n < 256 && ok(b, 0x28); n++) {
        if (MEM32(b + 0x24u) == type) return b;
        e = MEM32(b + 4u); b = e ? e - 4u : 0;
    }
    return 0;
}
static int roam_world(void) {
    uint32_t r = rd(G_RACE), pl = rd(G_PLAYER1);
    return rd(G_FLOW) == 6u && ok(r, 0x28) && MEM32(r + 0x24u) == 1u && (MEM32(r) == 4u || MEM32(r) == 6u) &&
           !rd(G_CAREER_RACE) && rd(G_RACETYPE) == 0u && !(rd(G_GAMEMODE) & 0x208u) && pl && rd(pl + PL_CAR);
}
static uint32_t top_package(void) {
    uint32_t fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0;
    return mg ? rd(mg + 0xe4u) : 0;
}
static int hud_on_top(void) {
    uint32_t pl = rd(G_PLAYER1), hud = pl ? rd(pl + PL_HUD) : 0, top = top_package();
    char a[40], b[40];
    read_str(top ? rd(top + 0xcu) : 0, a, sizeof a); read_str(hud ? rd(hud + 0x10u) : 0, b, sizeof b);
    return a[0] && !strcasecmp(a, b);
}
static int shoot_on(void) {
    uint32_t top = top_package(); char a[40];
    read_str(top ? rd(top + 0xcu) : 0, a, sizeof a);
    return !strncasecmp(a, "UI_Showcase", 11);
}

static int mem_ok(void) {
    if (S.mem) return 1;
    uint32_t sz = 4u * NODE_SZ + SO_TYPES * 64u;
    S.mem = call(FN_NEW, 0, &sz, 1, 1);
    if (!S.mem || !ok(S.mem, sz)) { S.mem = 0; LOGI("Showoff: guest malloc failed"); return 0; }
    for (uint32_t i = 0; i < sz; i += 4) MEM32(S.mem + i) = 0;
    return 1;
}
/* the car type's animations: 1 in, 0 on the way, -1 can't be had */
static int bank_ready(uint32_t ti) {
    uint32_t type = ok(ti, 0xd4) ? MEM32(ti + 0xd0u) : 0;
    if (!type) return -1;
    if (bank_of(type)) return 1;
    if (!roam_world()) return 0;
    Bank *k = 0;
    for (int i = 0; i < S.nbank && !k; i++) if (S.bank[i].type == type) k = &S.bank[i];
    if (!k) {
        if (S.nbank == SO_TYPES || !mem_ok()) return -1;
        k = &S.bank[S.nbank]; memset(k, 0, sizeof *k); k->type = type;
        k->name = S.mem + 4u * NODE_SZ + 64u * (uint32_t)S.nbank++;
    }
    if (k->st == 1 && S.frame - k->since < 600) return 0;
    char nm[24], path[64];
    read_str(ti, nm, sizeof nm);
    if (k->st) { k->file = 0; k->st = 0; LOGI("Showoff: %s part animations not in, loading again", nm); }
    if (k->tries >= 3) return -1;
    snprintf(path, sizeof path, "CARS\\%s\\PARTS_ANIMATIONS.BIN", nm);
    for (int i = 0; i < 64; i++) MEM8(k->name + (uint32_t)i) = (uint8_t)path[i];
    MEM8(k->name + 63u) = 0;
    uint32_t a[6] = { k->name, 0, 0, 1, FN_NOP, 0 };
    k->file = call(FN_LOAD, 0, a, 6, 1);
    k->st = 1; k->since = S.frame; k->tries++;
    LOGI("Showoff: loading %s (%s)", path, k->file ? "under way" : "FAILED");
    return 0;
}
static uint32_t so_kind(uint32_t ri, int part, uint32_t *mark) {
    uint32_t ride = rd(ri + RI_RIDE), ti = rd(ri + RI_TYPE), type = ok(ti, 0xd4) ? MEM32(ti + 0xd0u) : 0, kind;
    *mark = 0;
    if (!ok(ride, 0x8f0)) return 0;
    if (part == 0) {
        uint32_t h = rd(ride + RIDE_HOOD);
        if (!ok(h, 0xe) || (MEM8(h + 5u) & 0x1fu) != 5u) return 0;
        uint32_t s = S_ATTR_HOOD, ah = call(FN_HASH, 0, &s, 1, 1), aa[2] = { ah, 0 }, at = call(FN_ATTR, h, aa, 2, 0);
        kind = ok(at, 8) && MEM32(at + 4u) ? 4u : 2u;
        uint32_t key = (uint32_t)MEM16(h + 2u) << 16 | MEM16(h), s1[2] = { S_STYLE22, type }, s2[2] = { S_STYLE22CF, type };
        *mark = key == call(FN_HASH2, 0, s1, 2, 1) || key == call(FN_HASH2, 0, s2, 2, 1);
        return kind;
    }
    if (part == 1) return 0;
    uint32_t d = rd(ride + RIDE_DOORS);
    if (!ok(d, 0xe)) return 0;
    uint32_t s = S_ATTR_DOOR, ah = call(FN_HASH, 0, &s, 1, 1), aa[2] = { ah, 0 }, at = call(FN_ATTR, d, aa, 2, 0);
    if (!ok(at, 8)) return 0;
    kind = MEM32(at + 4u);
    if (kind == 1u) {
        uint32_t w = rd(ride + RIDE_WIDE);
        if (ok(w, 6)) { uint8_t b = MEM8(w + 5u); if ((b >> 5) && (b & 0x1fu)) kind = (b & 0x1fu) + 2u; }
    }
    return kind;
}
static void so_sound(int part, int open) {
    if (part == 3) return;
    uint32_t snd = rd(G_SOUND), w = snd ? rd(snd + 0x78u) : 0;
    if (!w || rd(snd + 0x90u) == 1u) return;
    uint32_t which = part == 0 ? (open ? 3u : 2u) : part == 1 ? (open ? 5u : 4u) : (open ? 1u : 0u);
    call(FN_SOUND, w, &which, 1, 0);
}
static void count_live(void) {
    S.live = 0;
    for (int p = 0; p < 4; p++) if (S.node[p] && MEM32(S.node[p] + 0x10u) != 1u) S.live = 1;
}
static void car_reset(void) {
    for (int p = 0; p < 4; p++) if (S.node[p]) MEM32(S.node[p] + 0x10u) = 1u;
    S.car = S.ri = S.type = 0; S.want = S.has = 0; S.live = 0;
}
static void car_apply(void) {
    if (S.want == S.has || !ok(S.ri, 0x2d48)) return;
    int r = bank_ready(rd(S.ri + RI_TYPE));
    if (r <= 0) {
        if (r < 0) {
            for (int p = 0; p < 4; p++) if (S.node[p]) MEM32(S.node[p] + 0x10u) = 1u;
            S.has = 0; S.want = 0; S.failed = 1;
        }
        return;
    }
    if (!mem_ok()) return;
    for (int p = 0; p < 4; p++) {
        uint8_t bit = (uint8_t)(1u << p);
        int w = (S.want & bit) != 0, h = (S.has & bit) != 0;
        if (w == h) continue;
        uint32_t mark = 0, kind = w ? so_kind(S.ri, p, &mark) : 0, n = S.node[p];
        if (w && (!n || MEM32(n + 0x20u) != S.type)) {
            n = S.node[p] = S.mem + (uint32_t)p * NODE_SZ;
            uint32_t a[5] = { S.type, 0x3f800000u, (uint32_t)p, kind, mark };
            call(FN_NODE_INIT, n, a, 5, 0);
        } else if (!w && !bank_of(MEM32(n + 0x20u))) {
            MEM32(n + 0x10u) = 1u;
        } else {
            if (w) { MEM32(n + 8u) = 0x3f800000u; MEM32(n + 0x14u) = kind; MEM8(n + 0x24u) = (uint8_t)mark; }
            uint32_t a[2] = { n, w ? 2u : 3u };
            call(FN_NODE_GO, 0, a, 2, 1);
        }
        so_sound(p, w);
        S.has ^= bit;
        LOGI("Showoff: %s %s (kind %u)", k_part[p], w ? "opens" : "closes", kind);
    }
}
static void car_set(uint32_t car, uint8_t want) {
    uint32_t ri = ok(car, 0x9e0) ? rd(car + CAR_RI) : 0, ti = ok(ri, 0x2d48) ? rd(ri + RI_TYPE) : 0;
    uint32_t type = ok(ti, 0xd4) ? MEM32(ti + 0xd0u) : 0;
    if (!type) { if (S.car) car_reset(); return; }
    if (S.car != car || S.ri != ri || S.type != type) { car_reset(); S.car = car; S.ri = ri; S.type = type; }
    S.want = want & OPEN_ALL;
}

void nfs_showoff_press(void) { atomic_fetch_add(&s_press, 1); }
int nfs_showoff_status(void) { return atomic_load(&s_status); }

void nfs_showoff_frame(void) {
    S.frame++;
    int world = roam_world(), top = world && hud_on_top() && !rd(G_FREEZE);
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + PL_CAR) : 0;
    float v = ok(car, 0x9e0) ? fabsf(rdf(car + CAR_FWD_SPEED)) : 0.f;
    if (!(v == v)) v = 0.f;
    int stands = top && v < OPEN_MPS;
    for (int n = atomic_exchange(&s_press, 0); n > 0; n--) {
        s_presses++;
        if (!stands) {
            s_note = !world ? 16 : !top ? 16 : 8;
            LOGI("Showoff: press, nothing (%s)", !world ? "not in free roam" : !top ? "a screen over the HUD" : "the car moves");
            continue;
        }
        s_note = 0;
        S.step = (S.step + 1) % NSTEPS;
        LOGI("Showoff: step %d (%s)", S.step, k_stepname[S.step]);
    }
    int shoot = world && shoot_on();
    if (S.step && (!world || shoot || v > SHUT_MPS)) {
        LOGI("Showoff: all closed (%s)", !world ? "out of free roam" : shoot ? "a cover shoot" : "the car drives off");
        S.step = 0;
    }
    if (!world || shoot) { if (S.car) car_reset(); }
    else if (S.step || S.car) { car_set(car, k_step[S.step]); car_apply(); }
    if (S.failed) {
        if (S.step) { LOGI("Showoff: this car's part animations can't be had"); s_note = 32; }
        S.failed = 0; S.step = 0;
    }
    count_live();
    atomic_store(&s_status, S.step | s_note | (s_presses & 0xff) << 8);
}

/* ---- the drawing (photo.c's wraps of 0x620AE0 and 0x615A10, render info in ecx) ---- */
static uint32_t trunk_rec(void) {
    uint32_t n = S.node[1], car = S.car, rec = ok(car, 0x9e0) ? rd(car + CAR_REC) : 0, cam = rd(G_VIEW1 + 0x40u);
    if (!n || MEM32(n + 0x10u) == 1u || !ok(rec, 8) || rd(S.ri) != car || !ok(cam, 0x4c)) return 0;
    float dx = rdf(car + CAR_POS) - rdf(cam + 0x40u), dy = rdf(car + CAR_POS + 4u) - rdf(cam + 0x44u),
          dz = rdf(car + CAR_POS + 8u) - rdf(cam + 0x48u), d2 = dx * dx + dy * dy + dz * dz;
    return d2 == d2 && d2 < TRUNK_NEAR_M * TRUNK_NEAR_M ? rec : 0;
}
int nfs_showoff_put(uint32_t ri, int body, uint32_t keep[6]) {
    if (!S.live || ri != S.ri) return 0;
    uint32_t b = bank_of(S.type);
    if (!b) return 0;
    uint32_t head = b + 0xcu, prev = head;
    keep[0] = b; keep[1] = MEM32(head); keep[2] = MEM32(head + 4u); keep[3] = 0;
    for (int p = 0; p < 4; p++) {
        uint32_t n = S.node[p];
        if (!n) continue;
        MEM32(prev) = n; MEM32(n + 4u) = prev; prev = n;
    }
    MEM32(prev) = head; MEM32(head + 4u) = prev;
    uint32_t rec = body ? trunk_rec() : 0;
    if (rec) { keep[3] = rec; keep[4] = MEM8(G_SHOWCASE); keep[5] = MEM32(rec + 4u); MEM8(G_SHOWCASE) = 1; MEM32(rec + 4u) = 1u; }
    return 1;
}
void nfs_showoff_take(const uint32_t keep[6]) {
    if (keep[3]) { MEM8(G_SHOWCASE) = (uint8_t)keep[4]; MEM32(keep[3] + 4u) = keep[5]; }
    MEM32(keep[0] + 0xcu) = keep[1]; MEM32(keep[0] + 0x10u) = keep[2];
}

/* ---- 0x438140 (cdecl type, 0 every bank): the game throws the animation banks away; ours unloaded first ---- */
static void drop_ours(void) {
    int n = 0;
    for (int i = 0; i < S.nbank; i++) {
        Bank *k = &S.bank[i];
        if (!k->file || !bank_of(k->type)) continue;
        uint32_t a = k->file;
        call(FN_UNLOAD, 0, &a, 1, 1);
        k->file = 0; k->st = 0; k->tries = 0; n++;
    }
    if (!n) return;
    for (int p = 0; p < 4; p++) if (S.node[p]) MEM32(S.node[p] + 0x10u) = 1u;
    S.has = 0;
    count_live();
    LOGI("Showoff: the game throws the part animations away: %d of ours unloaded first", n);
}
void nfs_showoff_ov_banks(void) {
    uint32_t type = MEM32(g_esp + 4u);
    drop_ours();
    uint32_t e = rd(G_BANKS), b = e ? e - 4u : 0;
    for (int n = 0; b && b != G_BANKS - 4u && n < 4096 && ok(b, 0x28); n++) {
        uint32_t ne = MEM32(b + 4u), next = ne ? ne - 4u : 0;
        if (!type || MEM32(b + 0x24u) == type) {
            uint32_t pv = MEM32(b + 8u), nx = MEM32(b + 4u), one = 1;
            MEM32(pv) = nx; MEM32(nx + 4u) = pv;
            call(FN_BANK_FREE, b, 0, 0, 0);
            call(MEM32(MEM32(b)), b, &one, 1, 0);
            LOGI("Showoff: a bank was still on the list: deleted");
        }
        b = next;
    }
    g_esp += 4;                                   /* (cdecl: the return slot) */
}
#endif
