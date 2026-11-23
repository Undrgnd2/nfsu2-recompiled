/*
 * garage.c - the career garage's 10 slots, and Trade Car: a car of yours taken from the garage for 80 % of the money
 * spent on it (Android).
 *
 * Adapted from nfsu2-web's runtime/garage.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there),
 * without the web's split screen, mod parts and add-on car types:
 *  - the game's 5 career slots stay where they are (FEDatabase +0x3C0 +0x5954); 5 more (slots 6-10, CAREER_SLOT_5..9)
 *    are records of the same class in guest memory of ours, found after the game's own by its lookups (0x503510,
 *    0x503680, 0x5035C0), walk (0x5162D0) and count (0x534850); the race setups (0x525ED0, 0x526970 in cameras.c's
 *    wraps, 0x53FEB0) find the car driven in the table's last entry while they run (swap_in); the shop's buys find it
 *    at hook 0x547689 (the web's 0x547685 is a branch target here);
 *  - 2 slots open a stage (10 at stage 5): each opening the game makes opens one more;
 *  - slots 6-10 are saved beside the profile, "NFS Underground 2/<NAME>/GARAGE.DAT", written when the game has written
 *    its profile (the host's CloseHandle: nfs_garage_file_written), from the payload the game made for it (0x52A280,
 *    0x52A450 the save's); read back when the profile's cars load (0x516850, 0x534770);
 *  - the profile names a car of the 5 for a car of slots 6-10 that is driven (the career's, the Quick Race one), put
 *    back after the load (0x5344D0);
 *  - Trade Car: the garage's car select (UI_CareerCarSelect) gets the help bar's button 1 (0x54E6E0, its label
 *    0x52D330) with 2 cars or more; the game's own question "You really want to trade this in? ... 80% of its cost."
 *    (0xB18C1000), Yes: the bank +80 % of the money spent on the car, the slot empty (0x4DFC50, its messages).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
int nfs_save_host_path(const char *rel, char *out, size_t n);
void sub_00503510_orig(void);
void sub_00503680_orig(void);
void sub_005035C0_orig(void);
void sub_005162D0_orig(void);
void sub_00534850_orig(void);
void sub_0053FEB0_orig(void);
void sub_0052A5D0_orig(void);
void sub_00516480_orig(void);
void sub_00532830_orig(void);
void sub_00551320_orig(void);
void sub_00532CF0_orig(void);
void sub_0052A280_orig(void);
void sub_0052A4C0_orig(void);
void sub_00516850_orig(void);
void sub_00534770_orig(void);
void sub_005344D0_orig(void);
void sub_0054E6E0_orig(void);
void sub_0052D330_orig(void);
void sub_004DFC50_orig(void);

#define G_GAMEMODE      0x00850078u
#define G_CAREER        0x0085ad40u
#define G_QR_CAR        0x0083aab8u
#define G_FECARS        0x0083ad90u
#define G_LANG          0x008383d8u
#define G_LANG_N        0x008383d0u
#define FEDB            0x0083a9d0u
#define FEDB_KEPT       0x16778u
#define CARS_SZ         0xab4cu
#define C_SLOTS         0x5954u
#define C_TABLE         0x9bd8u
#define TABLE_N         91u
#define REC_SZ          0x7f8u
#define R_HANDLE        0x08u
#define R_USED          0x10u
#define R_FLAGS         0x14u
#define R_PARTS         0x434u
#define R_SPENT         0x7f0u
#define R_LOCKED        0x7f4u
#define R_OWNED         0x7f5u
#define XN              5u
#define SLOTS           10u
#define SAVE_REC        2034u
#define CARS_PART       39230u
#define P_NAME          53761u
#define PROFILE_SIZE    54966u
#define PAYLOAD_AT      36u
#define K_BANK          0x7134u
#define K_STAGES        0x73c0u
#define K_STAGE_SZ      0x104u
#define K_STAGES_N      8u
#define K_STAGE         0x7af8u
#define K_CAR           0x8740u
#define G_HELPBAR       0x008384d8u
#define FN_NEW          0x00575620u
#define FN_REC_NEW      0x00525cf0u
#define FN_PARTS_CLEAR  0x00503e70u
#define FN_SLOT_HASH    0x0050b990u
#define S_SLOT_FMT      0x00797e74u
#define FN_REC_WRITE    0x00516620u
#define FN_PARTS_READ   0x00504040u
#define FN_SET_CAR      0x00500bd0u
#define FN_DIALOG       0x00558750u
#define FN_SHOW         0x00537c00u
#define FN_SELECT_MAKE  0x004ee210u
#define FN_HELPBAR_SET  0x0054e6e0u
#define SELECT_PKG      "UI_CareerCarSelect.fng"
#define SC_PACKAGE      0x04u
#define SC_BUTTONS      0x20u
#define SC_ITEM         0x54u
#define SC_AT           0x5cu
#define IT_LOCKED       0x930u
#define IT_FREE         0x931u
#define IT_HANDLE       0x920u
#define HB_PKG          0x58u
#define HB_TRADE        0x40000u
#define TXT_TRADE_CAR   0xe0f68e78u
#define TXT_TRADE_ASK   0xb18c1000u
#define MSG_BUTTON1     0xc519bfc0u
#define MSG_NO          0x34dc1bcfu
#define MSG_TRADE_YES   0x6a7e1d05u
#define S_GROUP         0x47ff4e7cu
#define S_UNDIM         0x0079031cu
#define PR_CARS         0x3b0u
#define GARAGE_FILE     "GARAGE.DAT"
#define GARAGE_OLD      "GARAGE.OLD"
#define GARAGE_MAGIC    0x4147324eu
#define TRADE_PCT       80u

static uint32_t g_blk;
#define X_AT(p)         (g_blk + (uint32_t)(p) * XN * REC_SZ)
#define TEMPLATE        (g_blk + 2u * XN * REC_SZ)
#define SCRATCH         (TEMPLATE + REC_SZ)
#define SCRATCH_LEFT    (SCRATCH + XN * SAVE_REC)
#define BLK_SZ          (2u * XN * REC_SZ + REC_SZ + XN * SAVE_REC + 16u)

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static uint32_t gcall(uint32_t fn, uint32_t self, unsigned n, const uint32_t *a) { return nfs_guest_call(fn, self, a, n, 0); }
static uint32_t ccall(uint32_t fn, unsigned n, const uint32_t *a) { return nfs_guest_call(fn, 0, a, n, 4u * n); }

static int pkg_is(uint32_t s, const char *name) {
    char b[64]; uint32_t i = 0;
    for (; s && i + 1 < sizeof b && ok(s + i, 1) && MEM8(s + i); i++) b[i] = (char)MEM8(s + i);
    b[i] = 0;
    return i && !strcasecmp(b, name);
}
static uint64_t fnv_g(uint32_t a, uint32_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < n; i++) { h ^= MEM8(a + i); h *= 0x100000001b3ull; }
    return h;
}
static uint32_t game_text(uint32_t hash) {
    uint32_t tab = rd(G_LANG), n = rd(G_LANG_N);
    if (!tab || !n || n > 0x100000u || !ok(tab, 8u * n)) return 0;
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2, h = MEM32(tab + 8u * mid);
        if (h == hash) return MEM32(tab + 8u * mid + 4u);
        if (h < hash) lo = mid + 1; else hi = mid;
    }
    return 0;
}

/* --- the slots --- */
static int player_of(uint32_t db) { return db == G_FECARS ? 0 : db == G_FECARS + CARS_SZ ? 1 : -1; }
static uint32_t slot(uint32_t db, unsigned i) {
    int p = player_of(db);
    return i < 5u ? db + C_SLOTS + i * REC_SZ : X_AT(p) + (i - 5u) * REC_SZ;
}
static uint32_t xfind(uint32_t db, uint32_t h) {
    int p = player_of(db);
    if (p < 0 || !g_blk || !h) return 0;
    for (unsigned i = 0; i < XN; i++) { uint32_t r = X_AT(p) + i * REC_SZ; if (MEM32(r + R_HANDLE) == h) return r; }
    return 0;
}
static int xis(uint32_t db, uint32_t r) {
    int p = player_of(db);
    return p >= 0 && g_blk && r >= X_AT(p) && r < X_AT(p) + XN * REC_SZ;
}
static unsigned count(uint32_t db, uint32_t field, int set) {
    unsigned n = 0;
    for (unsigned i = 0; i < SLOTS; i++) n += (MEM8(slot(db, i) + field) != 0) == set;
    return n;
}
static int slot_of(uint32_t db, uint32_t r) {
    for (unsigned i = 0; i < SLOTS; i++) if (slot(db, i) == r) return (int)i;
    return -1;
}
#define open_n(db)      count(db, R_LOCKED, 0)
#define owned_n(db)     count(db, R_OWNED, 1)
static int open_next(uint32_t db) {
    for (unsigned i = 0; i < SLOTS; i++) if (MEM8(slot(db, i) + R_LOCKED)) { MEM8(slot(db, i) + R_LOCKED) = 0; return 1; }
    return 0;
}
static void open_to(uint32_t db, unsigned n) { while (open_n(db) < (n > SLOTS ? SLOTS : n) && open_next(db)) {} }
static void fresh(uint32_t r, uint32_t handle, int locked) {
    for (uint32_t o = 0; o < REC_SZ; o += 4u) MEM32(r + o) = MEM32(TEMPLATE + o);
    MEM32(r + R_HANDLE) = handle; MEM32(r + R_USED) = 1u; MEM32(r + R_FLAGS) = 4u;
    MEM8(r + R_LOCKED) = (uint8_t)locked; MEM8(r + R_OWNED) = 0;
}
static void xinit(uint32_t db) {
    int p = player_of(db);
    for (unsigned i = 0; i < XN; i++) {
        uint32_t a[2] = { S_SLOT_FMT, 5u + i };
        fresh(X_AT(p) + i * REC_SZ, ccall(FN_SLOT_HASH, 2, a), 1);
    }
}
static int ensure(void) {
    if (g_blk) return 1;
    uint32_t sz = BLK_SZ, b = ccall(FN_NEW, 1, &sz);
    if (!b || !ok(b, sz)) { LOGI("Garage: no guest memory for slots 6-10"); return 0; }
    for (uint32_t i = 0; i < sz; i += 4u) MEM32(b + i) = 0;
    g_blk = b;
    gcall(FN_REC_NEW, TEMPLATE, 0, 0);
    gcall(FN_PARTS_CLEAR, TEMPLATE + R_PARTS, 0, 0);
    xinit(G_FECARS); xinit(G_FECARS + CARS_SZ);
    LOGI("Garage: slots 6-10 ready");
    return 1;
}
static void xsave(uint32_t db, uint8_t *out) {
    int p = player_of(db);
    MEM32(SCRATCH_LEFT) = XN * SAVE_REC;
    for (unsigned i = 0; i < XN; i++) { uint32_t a[2] = { SCRATCH + i * SAVE_REC, SCRATCH_LEFT }; gcall(FN_REC_WRITE, X_AT(p) + i * REC_SZ, 2, a); }
    for (uint32_t i = 0; i < XN * SAVE_REC; i++) out[i] = MEM8(SCRATCH + i);
}
static void xload(uint32_t db, const uint8_t *in) {
    int p = player_of(db);
    for (uint32_t i = 0; i < XN * SAVE_REC; i++) MEM8(SCRATCH + i) = in[i];
    MEM32(SCRATCH_LEFT) = XN * SAVE_REC;
    for (unsigned i = 0; i < XN; i++) {
        uint32_t r = X_AT(p) + i * REC_SZ, s = SCRATCH + i * SAVE_REC;
        for (uint32_t o = 0; o < 20u; o += 4u) MEM32(r + 4u + o) = MEM32(s + o);
        for (uint32_t o = 0; o < 0x418u; o += 4u) MEM32(r + 0x18u + o) = MEM32(s + 20u + o);
        MEM32(r + 0x430u) = MEM32(s + 20u + 0x418u);
        MEM8(r + R_LOCKED) = MEM8(s + 1072u); MEM8(r + R_OWNED) = MEM8(s + 1073u);
        uint32_t a[2] = { s + 1074u, SCRATCH_LEFT };
        gcall(FN_PARTS_READ, r + R_PARTS, 2, a);
    }
}

/* --- the lists --- */
void nfs_garage_ov_find(void)   { uint32_t db = g_ecx, h = MEM32(g_esp + 4u); sub_00503510_orig(); if (!g_eax) g_eax = xfind(db, h); }
void nfs_garage_ov_find2(void)  { uint32_t db = g_ecx, h = MEM32(g_esp + 4u); sub_00503680_orig(); if (!g_eax) g_eax = xfind(db, h); }
void nfs_garage_ov_find3(void)  { uint32_t db = g_ecx, h = MEM32(g_esp + 4u); sub_005035C0_orig(); if (!g_eax) g_eax = xfind(db, h); }
static uint32_t xnext(uint32_t db, uint32_t kinds, uint32_t after) {
    int p = player_of(db);
    unsigned i = after ? (after - X_AT(p)) / REC_SZ + 1u : 0u;
    for (; i < XN; i++) { uint32_t r = X_AT(p) + i * REC_SZ; if (MEM32(r + R_USED) && (MEM32(r + R_FLAGS) & kinds)) return r; }
    return 0;
}
/* thiscall cars (kinds, after) -> the next record in use of those kinds (ret 8) */
void nfs_garage_ov_walk(void) {
    uint32_t db = g_ecx, kinds = MEM32(g_esp + 4u), after = MEM32(g_esp + 8u);
    if (player_of(db) < 0 || !g_blk) { sub_005162D0_orig(); return; }
    if (xis(db, after)) { g_esp += 4u + 8u; g_eax = xnext(db, kinds, after); return; }
    sub_005162D0_orig();
    if (!g_eax) g_eax = xnext(db, kinds, 0);
}
/* thiscall cars (kinds, what) -> how many in use of those kinds */
void nfs_garage_ov_count(void) {
    uint32_t db = g_ecx, kinds = MEM32(g_esp + 4u);
    int p = player_of(db);
    sub_00534850_orig();
    if (p < 0 || !g_blk || !(kinds & 4u)) return;
    uint32_t n = g_eax, keep = g_edx;
    for (unsigned i = 0; i < XN; i++) {
        uint32_t r = X_AT(p) + i * REC_SZ;
        if (!MEM32(r + R_USED) || !(MEM32(r + R_FLAGS) & kinds) || !MEM8(r + R_OWNED)) continue;
        if (gcall(MEM32(MEM32(r) + 4u), r, 0, 0) != 0xffffffffu) n++;
    }
    g_edx = keep;
    g_eax = n;
}

/* the race setups: the car driven from slots 6-10 in the table's last entry while they run */
static uint32_t g_sw_at[2], g_sw_was[2];
void nfs_garage_swap_in(void) {
    int career = MEM32(G_GAMEMODE) == 1u;
    for (int p = 0; p < 2; p++) {
        uint32_t db = G_FECARS + (uint32_t)p * CARS_SZ;
        uint32_t h = career ? MEM32(G_CAREER + K_CAR) : MEM32(G_QR_CAR + 4u * (uint32_t)p);
        uint32_t x = career && p ? 0 : xfind(db, h);
        g_sw_at[p] = 0;
        if (!x) continue;
        g_sw_at[p] = db + C_TABLE + 4u * (TABLE_N - 1u);
        g_sw_was[p] = MEM32(g_sw_at[p]);
        MEM32(g_sw_at[p]) = x;
    }
}
void nfs_garage_swap_out(void) {
    for (int p = 0; p < 2; p++) if (g_sw_at[p]) { MEM32(g_sw_at[p]) = g_sw_was[p]; g_sw_at[p] = 0; }
}
void nfs_garage_ov_setup(void) { nfs_garage_swap_in(); sub_0053FEB0_orig(); nfs_garage_swap_out(); }

static uint32_t garage_find(uint32_t db, uint32_t h) {
    if (!h) return 0;
    for (uint32_t i = 0; i < TABLE_N; i++) { uint32_t r = rd(db + C_TABLE + 4u * i); if (r && MEM32(r + R_HANDLE) == h) return r; }
    return xfind(db, h);
}

/* --- new careers, stages --- */
/* (guest calls after the original's: its eax and edx kept, they are scratch registers to the calls) */
#define KEEP_RET(body) do { uint32_t ka_ = g_eax, kd_ = g_edx; body; g_eax = ka_; g_edx = kd_; } while (0)
void nfs_garage_ov_new_profile(void) { sub_0052A5D0_orig(); KEEP_RET(ensure()); }
void nfs_garage_ov_new_slots(void) {
    uint32_t db = g_ecx;
    sub_00516480_orig();
    KEEP_RET(if (player_of(db) >= 0 && ensure()) xinit(db));
}
void nfs_garage_ov_new_career(void) { sub_00532830_orig(); if (g_blk) open_to(G_FECARS, 2u); }
static int g_in_stage;
static unsigned g_loads;
static void stages(void (*orig)(void)) {
    if (g_in_stage || !g_blk) { orig(); return; }
    uint32_t was[K_STAGES_N]; unsigned n = 0, open0 = open_n(G_FECARS), loads = g_loads;
    for (unsigned k = 0; k < K_STAGES_N; k++) was[k] = MEM32(G_CAREER + K_STAGES + k * K_STAGE_SZ);
    g_in_stage = 1; orig(); g_in_stage = 0;
    for (unsigned k = 0; k < K_STAGES_N; k++) n += !was[k] && MEM32(G_CAREER + K_STAGES + k * K_STAGE_SZ);
    if (!n || loads != g_loads) return;
    open_to(G_FECARS, open0 + 2u * n);
    LOGI("Garage: a new stage: %u of %u slots open", open_n(G_FECARS), SLOTS);
}
void nfs_garage_ov_career_update(void) { stages(sub_00551320_orig); }
void nfs_garage_ov_stage(void) { stages(sub_00532CF0_orig); }

/* --- the profile --- */
typedef struct { uint32_t drive, qr, stand; uint8_t cars[XN * SAVE_REC]; } State;
static struct { char name[33]; uint64_t fp; State st; unsigned seq; } g_built[4];
static unsigned g_seq;
static struct { int on, p; char name[33]; uint64_t fp; State st; } g_kept;
static State g_now, g_read, g_st0;
static struct { int on; uint32_t drive, qr, stand; } g_back;
static int g_topup;
static char g_topup_name[33];

static void name_of(uint32_t payload, char *out) {
    unsigned i = 0;
    for (; i < 32u && ok(payload + P_NAME + i, 1) && MEM8(payload + P_NAME + i); i++) out[i] = (char)MEM8(payload + P_NAME + i);
    out[i] = 0;
}
static uint32_t xowned(uint32_t db, uint32_t h) { uint32_t r = xfind(db, h); return r && MEM8(r + R_OWNED) ? r : 0; }
static uint32_t first_car(uint32_t db, unsigned n) {
    for (unsigned i = 0; i < n; i++) { uint32_t r = slot(db, i); if (MEM8(r + R_OWNED)) return MEM32(r + R_HANDLE); }
    return 0;
}
static void state_of(uint32_t db, State *st) {
    xsave(db, st->cars);
    st->drive = st->qr = st->stand = 0;
    if (db != G_FECARS) return;
    uint32_t stand = first_car(G_FECARS, 5u);
    if (!stand) return;
    if (xowned(G_FECARS, MEM32(G_CAREER + K_CAR))) st->drive = MEM32(G_CAREER + K_CAR);
    if (xowned(G_FECARS, MEM32(G_QR_CAR))) st->qr = MEM32(G_QR_CAR);
    if (st->drive || st->qr) st->stand = stand;
}
static int differ(const State *a, const State *b) {
    return a->drive != b->drive || a->qr != b->qr || memcmp(a->cars, b->cars, sizeof a->cars);
}
/* thiscall profile (out, size): the payload, naming the stand-in for a car of ours driven */
static void payload(void) {
    uint32_t db = g_ecx + PR_CARS, out = MEM32(g_esp + 4u), size = MEM32(g_esp + 8u);
    int p = player_of(db);
    if (!g_blk || p < 0 || size < P_NAME + 32u) { sub_0052A280_orig(); return; }
    State *st = &g_st0;
    state_of(db, st);
    uint32_t k = MEM32(G_CAREER + K_CAR), q = MEM32(G_QR_CAR);
    if (st->drive) MEM32(G_CAREER + K_CAR) = st->stand;
    if (st->qr) MEM32(G_QR_CAR) = st->stand;
    sub_0052A280_orig();
    MEM32(G_CAREER + K_CAR) = k; MEM32(G_QR_CAR) = q;
    char name[33]; name_of(out, name);
    uint64_t fp = fnv_g(out, CARS_PART);
    unsigned at = 0;
    for (unsigned i = 1; i < 4u; i++) if (g_built[i].seq < g_built[at].seq) at = i;
    for (unsigned i = 0; i < 4u; i++) if (g_built[i].seq && g_built[i].fp == fp && !strcmp(g_built[i].name, name)) { at = i; break; }
    memcpy(g_built[at].name, name, sizeof name);
    g_built[at].fp = fp; g_built[at].st = *st; g_built[at].seq = ++g_seq;
    if (out == rd(FEDB + FEDB_KEPT)) {
        g_kept.st = *st; g_kept.p = p; g_kept.fp = fp; g_kept.on = 1;
        memcpy(g_kept.name, name, sizeof g_kept.name);
    }
}
void nfs_garage_ov_payload(void) { payload(); }
/* thiscall FEDatabase (out, size): add ecx, 0x10; jmp 0x52A280 (the lift copies the jump's target: wrapped too) */
void nfs_garage_ov_save_payload(void) { g_ecx += 0x10u; payload(); }
/* thiscall FEDatabase -> al: anything to save */
void nfs_garage_ov_dirty(void) {
    uint32_t kept = MEM32(g_ecx + FEDB_KEPT);
    sub_0052A4C0_orig();
    if ((g_eax & 0xffu) || !kept || !g_kept.on || !g_blk) return;
    uint32_t ret = g_eax, ret_d = g_edx;
    state_of(G_FECARS + (uint32_t)g_kept.p * CARS_SZ, &g_now);
    g_eax = ret; g_edx = ret_d;
    if (differ(&g_now, &g_kept.st)) { g_eax = (g_eax & ~0xffu) | 1u; LOGI("Garage: slots 6-10 changed: something to save"); }
}

/* the host: a file the game wrote closed (a profile: its GARAGE file beside it) */
static void dir_file(const char *path, const char *file, char *out, size_t n) {
    const char *sl = strrchr(path, '/');
    snprintf(out, n, "%.*s/%s", sl ? (int)(sl - path) : 1, sl ? path : ".", file);
}
void nfs_garage_file_written(const char *path) {
    static uint8_t f[PROFILE_SIZE + 1];
    FILE *in = fopen(path, "rb");
    if (!in) return;
    size_t n = fread(f, 1, sizeof f, in); fclose(in);
    if (n != PROFILE_SIZE || memcmp(f, "20CM", 4)) return;
    const uint8_t *pl = f + PAYLOAD_AT;
    char name[33]; unsigned i = 0;
    for (; i < 32u && pl[P_NAME + i]; i++) name[i] = (char)pl[P_NAME + i];
    name[i] = 0;
    uint64_t fp = 0xcbf29ce484222325ull;
    for (uint32_t k = 0; k < CARS_PART; k++) { fp ^= pl[k]; fp *= 0x100000001b3ull; }
    int best = -1;
    for (int k = 0; k < 4; k++)
        if (g_built[k].seq && g_built[k].fp == fp && !strcasecmp(g_built[k].name, name) && (best < 0 || g_built[k].seq > g_built[best].seq)) best = k;
    if (best < 0) { LOGI("Garage: %s written: no payload of ours for it, its GARAGE file left as it is", name); return; }
    const State *st = &g_built[best].st;
    char g[1100]; dir_file(path, GARAGE_FILE, g, sizeof g);
    FILE *o = fopen(g, "wb");
    if (!o) { LOGI("Garage: %s: cannot write %s", name, g); return; }
    uint32_t hd[8] = { GARAGE_MAGIC, 1u, (uint32_t)fp, (uint32_t)(fp >> 32), st->drive, st->qr, st->stand, 0 };
    int good = fwrite(hd, sizeof hd, 1, o) == 1 && fwrite(st->cars, sizeof st->cars, 1, o) == 1;
    good = fclose(o) == 0 && good;
    unsigned cars = 0;
    for (unsigned k = 0; k < XN; k++) cars += st->cars[k * SAVE_REC + 1073u] != 0;
    LOGI("Garage: %s: slots 6-10 saved (%u cars)%s", name, cars, good ? "" : ": WRITE FAILED");
}
/* the host: a file the game deleted (a profile: <dir>/<NAME> in its folder <NAME>: its GARAGE files go too) */
void nfs_garage_file_deleted(const char *path) {
    const char *sl = strrchr(path, '/');
    if (!sl) return;
    const char *base = sl + 1, *dsl = sl - 1;
    while (dsl > path && *dsl != '/') dsl--;
    if (*dsl == '/') dsl++;
    if ((size_t)(sl - dsl) != strlen(base) || strncasecmp(dsl, base, strlen(base))) return;
    char g[1100];
    dir_file(path, GARAGE_FILE, g, sizeof g); if (remove(g) == 0) LOGI("Garage: %s deleted with its profile", g);
    dir_file(path, GARAGE_OLD, g, sizeof g); remove(g);
}
static int read_garage(const char *name, uint64_t fp, State *st) {
    char rel[128], path[1100];
    snprintf(rel, sizeof rel, "NFS Underground 2\\%s\\%s", name, GARAGE_FILE);
    if (!nfs_save_host_path(rel, path, sizeof path)) return 0;
    FILE *in = fopen(path, "rb");
    if (!in) return 0;
    uint32_t hd[8] = { 0 };
    int good = fread(hd, sizeof hd, 1, in) == 1 && fread(st->cars, sizeof st->cars, 1, in) == 1;
    fclose(in);
    if (!good || hd[0] != GARAGE_MAGIC || hd[1] != 1u) { LOGI("Garage: %s: its GARAGE file is not one of ours", name); return 0; }
    if (hd[2] != (uint32_t)fp || hd[3] != (uint32_t)(fp >> 32)) {
        char old[1100]; dir_file(path, GARAGE_OLD, old, sizeof old);
        int kept = rename(path, old) == 0;
        LOGI("Garage: %s: its GARAGE file is of another save of it: not used, %s", name, kept ? "kept as " GARAGE_OLD : "COULD NOT KEEP IT ASIDE");
        return 0;
    }
    st->drive = hd[4]; st->qr = hd[5]; st->stand = hd[6];
    return 1;
}
static void load_state(uint32_t db, const State *st) {
    xload(db, st->cars);
    if (db == G_FECARS && (st->drive || st->qr)) { g_back.on = 1; g_back.drive = st->drive; g_back.qr = st->qr; g_back.stand = st->stand; }
}
/* thiscall cars (in, size): a profile's cars into these; ours from its GARAGE file, or as the kept payload had them */
static void cars_loaded_rest(uint32_t db, uint32_t in, uint32_t size);
void nfs_garage_ov_cars_loaded(void) {
    uint32_t db = g_ecx, in = MEM32(g_esp + 4u), size = MEM32(g_esp + 8u);
    sub_00516850_orig();
    KEEP_RET(cars_loaded_rest(db, in, size));
}
static void cars_loaded_rest(uint32_t db, uint32_t in, uint32_t size) {
    g_loads++;
    int p = player_of(db);
    if (p < 0 || !ensure()) return;
    if (!p) g_back.on = 0;
    xinit(db);
    char name[33] = "";
    uint64_t fp = ok(in, CARS_PART) ? fnv_g(in, CARS_PART) : 0;
    if (size >= P_NAME + 32u || (size >= CARS_PART && ok(in + P_NAME, 32))) name_of(in, name);
    if (g_kept.on && g_kept.p == p && in == rd(FEDB + FEDB_KEPT) && fp == g_kept.fp && (!name[0] || !strcmp(name, g_kept.name))) {
        load_state(db, &g_kept.st);
        LOGI("Garage: back to the kept payload: %u cars, %u of %u slots open", owned_n(db), open_n(db), SLOTS);
        return;
    }
    if (name[0]) {
        if (read_garage(name, fp, &g_read)) {
            load_state(db, &g_read);
            LOGI("Garage: %s: slots 6-10 from its GARAGE file, %u cars, %u of %u slots open", name, owned_n(db), open_n(db), SLOTS);
            return;
        }
        if (!p) { g_topup = 1; memcpy(g_topup_name, name, sizeof g_topup_name); }
        return;
    }
    open_to(db, 2u * open_n(db));
}
/* thiscall FEDatabase (in, size): a profile loaded; [+0x156A8] 4: only the cars of player [+0x20358], by a jump to
 * 0x516850 (a copy of it in the lift) */
void nfs_garage_ov_profile_cars(void) {
    uint32_t fe = g_ecx;
    if (MEM32(fe + 0x156a8u) != 4u) { sub_00534770_orig(); return; }
    g_ecx = fe + 0x10u + MEM32(fe + 0x20358u) * CARS_SZ + PR_CARS;
    MEM32(g_esp + 8u) = CARS_PART;
    nfs_garage_ov_cars_loaded();
}
static void not_empty(uint32_t at, const char *what) {
    uint32_t h = MEM32(at), r = xfind(G_FECARS, h), to;
    if (!r || MEM8(r + R_OWNED) || !(to = first_car(G_FECARS, SLOTS))) return;
    MEM32(at) = to;
    LOGI("Garage: %s was in an empty slot: now the first car", what);
}
/* thiscall profile (in, size, 1): a profile loaded; the cars driven as they were */
static void profile_rest(uint32_t db);
void nfs_garage_ov_profile(void) {
    uint32_t db = g_ecx + PR_CARS;
    g_topup = 0;
    sub_005344D0_orig();
    KEEP_RET(profile_rest(db));
}
static void profile_rest(uint32_t db) {
    if (!g_blk || db != G_FECARS) return;
    if (g_topup) {
        uint32_t stage = MEM32(G_CAREER + K_STAGE);
        open_to(G_FECARS, stage <= 1u ? 2u : 2u * stage);
        LOGI("Garage: %s: no slots 6-10 saved, stage %u: %u of %u slots open", g_topup_name, stage, open_n(G_FECARS), SLOTS);
        g_topup = 0;
    }
    if (g_back.on) {
        uint32_t r;
        if (g_back.drive && MEM32(G_CAREER + K_CAR) == g_back.stand && (r = xowned(G_FECARS, g_back.drive))) {
            MEM32(G_CAREER + K_CAR) = g_back.drive;
            LOGI("Garage: the career drives the car in slot %d", slot_of(G_FECARS, r) + 1);
        }
        if (g_back.qr && MEM32(G_QR_CAR) == g_back.stand && xowned(G_FECARS, g_back.qr)) MEM32(G_QR_CAR) = g_back.qr;
        g_back.on = 0;
    }
    not_empty(G_CAREER + K_CAR, "the career's car");
    not_empty(G_QR_CAR, "the Quick Race car");
    uint32_t h = MEM32(G_CAREER + K_CAR), to;
    if (MEM32(G_CAREER + K_STAGE) >= 1u && slot_of(G_FECARS, garage_find(G_FECARS, h)) < 0 && (to = first_car(G_FECARS, SLOTS))) {
        gcall(FN_SET_CAR, G_CAREER, 1, &to);
        LOGI("Garage: the career's car was in none of the slots: now the first car");
    }
}

/* the shop's buys (0x547570) add each part to the career car found among the 5 by handle; none: esi 0 */
void nfs_garage_hook_shop(void) {
    if (g_esi || MEM32(G_GAMEMODE) != 1u) return;
    uint32_t x = xfind(G_FECARS, MEM32(G_CAREER + K_CAR));
    if (x) { g_esi = x; LOGI("Garage: the shop's buy: for the car in slot %u", 6u + (x - X_AT(0)) / REC_SZ); }
}

/* --- Trade Car --- */
/* thiscall help bar (buttons, package), ret 8 */
void nfs_garage_ov_helpbar(void) {
    if (g_blk && MEM32(G_GAMEMODE) == 1u && pkg_is(MEM32(g_esp + 8u), SELECT_PKG)) {
        unsigned cars = owned_n(G_FECARS);
        if (cars >= 2u) MEM32(g_esp + 4u) |= HB_TRADE;
        LOGI("Garage: %u cars, %u of %u slots open%s", cars, open_n(G_FECARS), SLOTS, cars >= 2u ? ", Trade Car" : "");
    }
    sub_0054E6E0_orig();
}
/* thiscall help bar (button) -> its label's text */
void nfs_garage_ov_helplabel(void) {
    uint32_t hb = g_ecx, b = MEM32(g_esp + 4u);
    sub_0052D330_orig();
    if (b == HB_TRADE && pkg_is(rd(hb + HB_PKG), SELECT_PKG)) g_eax = TXT_TRADE_CAR;
}
static uint32_t g_trade;
static void handles_move(uint32_t from, uint32_t to) {
    if (MEM32(G_CAREER + K_CAR) == from) MEM32(G_CAREER + K_CAR) = to;
    if (MEM32(G_QR_CAR) == from) MEM32(G_QR_CAR) = to;
}
static int trade_ask(uint32_t scr) {
    uint32_t it = rd(scr + SC_ITEM);
    if (!g_blk || !ok(it, IT_LOCKED + 2u) || MEM8(it + IT_LOCKED) || MEM8(it + IT_FREE) || owned_n(G_FECARS) < 2u) return 0;
    uint32_t h = MEM32(it + IT_HANDLE), r = garage_find(G_FECARS, h);
    if (!r || slot_of(G_FECARS, r) < 0 || !MEM8(r + R_OWNED)) return 0;
    g_trade = h;
    uint32_t q = game_text(TXT_TRADE_ASK), t = SCRATCH + 4u, n = 0;
    if (!q) return 0;
    MEM8(SCRATCH) = '%'; MEM8(SCRATCH + 1u) = 's'; MEM8(SCRATCH + 2u) = 0;
    for (uint32_t i = 0; ok(q + i, 1) && MEM8(q + i) && n + 3u < 0x400u; i++) {
        if (MEM8(q + i) == '%') MEM8(t + n++) = '%';
        MEM8(t + n++) = MEM8(q + i);
    }
    MEM8(t + n) = 0;
    uint32_t a[10] = { MEM32(scr + SC_PACKAGE), 0x00783fe5u, 0xf770u, 0x61cu, MSG_TRADE_YES, MSG_NO, MSG_NO, 0, SCRATCH, t };
    ccall(FN_DIALOG, 10, a);
    return 1;
}
static void trade(uint32_t scr) {
    uint32_t r = garage_find(G_FECARS, g_trade);
    int s = r ? slot_of(G_FECARS, r) : -1;
    uint32_t pkg = MEM32(scr + SC_PACKAGE);
    uint32_t a[4] = { pkg, S_GROUP, S_UNDIM, 1u };
    ccall(FN_SHOW, 4, a);
    if (s < 0 || !MEM8(r + R_OWNED) || owned_n(G_FECARS) < 2u) return;
    uint32_t spent = MEM32(r + R_SPENT), pay = (uint32_t)((uint64_t)spent * TRADE_PCT / 100u);
    MEM32(G_CAREER + K_BANK) += pay;
    fresh(r, g_trade, 0);
    if (s == 0) {   /* the game counts on slot 1 having a car: the last car moves there */
        for (int k = (int)SLOTS - 1; k > 0; k--) {
            uint32_t q = slot(G_FECARS, (unsigned)k);
            if (!MEM8(q + R_OWNED)) continue;
            uint32_t hq = MEM32(q + R_HANDLE);
            for (uint32_t o = 4u; o < REC_SZ; o += 4u) if (o != R_HANDLE) MEM32(r + o) = MEM32(q + o);
            fresh(q, hq, 0);
            handles_move(hq, g_trade);
            LOGI("Garage: the car in slot %d moved to slot 1", k + 1);
            break;
        }
    }
    uint32_t kr = garage_find(G_FECARS, MEM32(G_CAREER + K_CAR)), qr = garage_find(G_FECARS, MEM32(G_QR_CAR));
    if (kr && slot_of(G_FECARS, kr) >= 0 && !MEM8(kr + R_OWNED)) { uint32_t h = first_car(G_FECARS, SLOTS); gcall(FN_SET_CAR, G_CAREER, 1, &h); }
    if (qr && slot_of(G_FECARS, qr) >= 0 && !MEM8(qr + R_OWNED)) MEM32(G_QR_CAR) = first_car(G_FECARS, SLOTS);
    LOGI("Garage: traded the car in slot %d for $%u (%u %% of $%u spent on it), %u cars left, the bank $%u",
         s + 1, pay, TRADE_PCT, spent, owned_n(G_FECARS), MEM32(G_CAREER + K_BANK));
    MEM32(scr + SC_AT) = 1u;
    gcall(FN_SELECT_MAKE, scr, 0, 0);
    uint32_t hb[2] = { MEM32(scr + SC_BUTTONS), pkg };
    gcall(FN_HELPBAR_SET, MEM32(G_HELPBAR), 2, hb);
}
/* UI_CareerCarSelect's messages (thiscall msg, a, b, c; ret 0x10): C or the help bar's Trade Car asks, Yes trades */
void nfs_garage_ov_select(void) {
    uint32_t m = MEM32(g_esp + 4u), scr = g_ecx;
    if (m == MSG_TRADE_YES || (m == MSG_BUTTON1 && trade_ask(scr))) {
        if (m == MSG_TRADE_YES) trade(scr);
        g_esp += 4u + 0x10u;
        return;
    }
    sub_004DFC50_orig();
}
#endif
