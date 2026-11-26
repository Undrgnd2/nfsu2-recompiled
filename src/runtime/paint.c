/*
 * paint.c - the player's car painted any colour (Android, the ⚙ menu's "Peinture perso"): a colour picked freely or
 * typed as a hex code, and a finish, instead of the paint shop's list.
 *
 * How the game paints a car's body (SPEED2.EXE):
 *   - the body's colour is in its skin texture, which the game composes from the ride's parts (0x61D790, cdecl, the
 *     RideInfo: called once per car load from 0x633C10); its base colour is the base paint part's (ride +0x68C, a
 *     part of kind 0x30) RED, GREEN and BLUE attributes (0..255), read with 0x60FEF0 (thiscall part (hash, 0) -> its
 *     { hash, value }), packed with its GLOSS as A8B8G8R8; the body coloured parts (ride +0x590 slots 0x44..0x4C)
 *     read the same part's when they carry it;
 *   - the finish is a light material (the paint part's LIGHT_MATERIAL_NAME: REGPAINT*, METPAINT*, PEARL*), put in
 *     the render info's +0x384 when it is made (0x638D9E) and read from there while the car is drawn (0x620AE0).
 * So while the skin of the player's car is composed (the wrap of 0x61D790) the base paint part's RED, GREEN and
 * BLUE are ours (the wrap of 0x60FEF0 answers them from a block of ours), and while the car is drawn (photo.c's
 * wraps of 0x620AE0 and 0x6162E0) its render info's +0x384 is the finish's material, put back after. The colour
 * shows when the car's skin is next composed: when the car loads (the garage, a shop, a race, free roam).
 * Whose car: the player's (the career's car, else Quick Race's), in the world and on the front end's screens about it
 * (plates.c says which), never in a LAN game. Kept per car beside the profiles (AppData/Local/NFS Underground 2/
 * PAINT.TXT: profile, handle, car type, RRGGBB, finish).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
int nfs_save_host_path(const char *rel, char *out, size_t n);
int nfs_plates_fe_ours(void);
void sub_0061D790_orig(void);
void sub_0060FEF0_orig(void);

#define G_GAMEMODE     0x00850078u
#define G_FLOW         0x008654a4u
#define G_PLAYER1      0x008900acu   /* Player*: +4 its car */
#define G_CAREER_CAR   0x00863480u
#define G_QR_CAR       0x0083aab8u
#define G_PROFILE_NAME 0x0083a9e0u
#define G_CARTYPES     0x008a1cccu
#define G_LM_LIST      0x008358c0u   /* the light materials (a material's link at its +4, its name hash at +0xC) */
#define FN_NEW         0x00575620u
#define CAR_RENDER     0x554u
#define RI_CAR         0u
#define RI_RIDE        4u
#define RI_PAINT_LM    0x384u
#define RIDE_PAINT     0x68cu
#define CARTYPES_N     46u
#define CARTYPE_SZ     0x890u
#define CARTYPE_HASH   0xd0u
#define REC_TYPE       0x20u
#define H_RED          0x0000d99au
#define H_GREEN        0x02ddc8f0u
#define H_BLUE         0x00136707u
#define PAINT_FILE     "NFS Underground 2\\PAINT.TXT"
#define MAX_ENTRIES    128
#define FINISHES       5             /* 0 the paint's own, then gloss, metallic, pearl, matte */

static const char *const k_finish[FINISHES] = { 0, "REGPAINTGRAY", "METPAINTSILVER", "PEARL", "MATTEPLASTIC" };

typedef struct { char profile[33]; uint32_t handle, type, rgb; int finish; } Entry;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static Entry s_e[MAX_ENTRIES];
static int s_n, s_loaded;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n ? n : 1u); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static uint32_t bhash(const char *s) { uint32_t h = 0xffffffffu; for (; *s; s++) h = h * 33u + (uint8_t)*s; return h; }

static void profile(char out[33]) {
    out[0] = 0;
    if (!ok(G_PROFILE_NAME, 0x24) || !MEM8(G_PROFILE_NAME + 0x20u)) return;
    int i = 0;
    for (; i < 32 && MEM8(G_PROFILE_NAME + (uint32_t)i) >= 0x20 && MEM8(G_PROFILE_NAME + (uint32_t)i) < 0x7f; i++) out[i] = (char)MEM8(G_PROFILE_NAME + (uint32_t)i);
    out[i] = 0;
}
static uint32_t driven(void) { return rd(G_GAMEMODE) == 1u ? rd(G_CAREER_CAR) : rd(G_QR_CAR); }
static uint32_t car_type(uint32_t h) {
    uint32_t r = h ? nfs_garage_find_car(h) : 0;
    return r && ok(r + REC_TYPE, 4) ? MEM32(r + REC_TYPE) : 0;
}
static uint32_t ride_type(uint32_t ride) {
    uint32_t types = rd(G_CARTYPES), t = ride ? rd(ride) : 0xffffffffu;
    return t < CARTYPES_N && ok(types + t * CARTYPE_SZ + CARTYPE_HASH, 4) ? MEM32(types + t * CARTYPE_SZ + CARTYPE_HASH) : 0;
}

static void load(void) {
    s_loaded = 1; s_n = 0;
    char path[1100];
    if (!nfs_save_host_path(PAINT_FILE, path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (s_n < MAX_ENTRIES && fgets(line, sizeof line, f)) {
        Entry e; char name[33];
        if (sscanf(line, "%32[^\t]\t%x\t%x\t%x\t%d", name, &e.handle, &e.type, &e.rgb, &e.finish) != 5) continue;
        if (e.rgb > 0xffffffu || e.finish < 0 || e.finish >= FINISHES) continue;
        snprintf(e.profile, sizeof e.profile, "%s", name);
        s_e[s_n++] = e;
    }
    fclose(f);
    LOGI("Paint: %d cars' own paint read", s_n);
}
static void save(void) {
    char path[1100];
    if (!nfs_save_host_path(PAINT_FILE, path, sizeof path)) return;
    FILE *f = fopen(path, "w");
    if (!f) { LOGI("Paint: cannot write %s", path); return; }
    for (int i = 0; i < s_n; i++)
        fprintf(f, "%s\t%08x\t%08x\t%06x\t%d\n", s_e[i].profile, s_e[i].handle, s_e[i].type, s_e[i].rgb, s_e[i].finish);
    fclose(f);
}
static Entry *find(const char *prof, uint32_t h) {
    for (int i = 0; i < s_n; i++) if (s_e[i].handle == h && !strcasecmp(s_e[i].profile, prof)) return &s_e[i];
    return 0;
}
/* the player's car's paint (lock held): 1 when it has its own */
static int mine(Entry *out) {
    char prof[33]; profile(prof);
    uint32_t h = driven(), t = car_type(h);
    if (!prof[0] || !h || !t) return 0;
    if (!s_loaded) load();
    Entry *e = find(prof, h);
    if (!e || e->type != t) return 0;
    *out = *e;
    return 1;
}

/* the menu (any thread): out = { a car (1) or none (0), has its own, 0xRRGGBB, finish } */
void nfs_paint_get(int out[4]) {
    pthread_mutex_lock(&s_lock);
    char prof[33]; profile(prof);
    uint32_t h = driven();
    Entry e = { "", 0, 0, 0xffa31au, 0 };
    out[0] = prof[0] && h && car_type(h);
    out[1] = out[0] && mine(&e);
    out[2] = (int)e.rgb; out[3] = e.finish;
    pthread_mutex_unlock(&s_lock);
}
/* the menu (any thread): the player's car's own paint (has 0: the game's); 0 no car */
int nfs_paint_set(int has, int rgb, int finish) {
    pthread_mutex_lock(&s_lock);
    char prof[33]; profile(prof);
    uint32_t h = driven(), t = car_type(h);
    int done = 0;
    if (prof[0] && h && t) {
        if (!s_loaded) load();
        Entry *e = find(prof, h);
        if (!has || finish < 0 || finish >= FINISHES) {
            if (e) { int i = (int)(e - s_e); memmove(e, e + 1, sizeof *e * (size_t)(s_n - 1 - i)); s_n--; }
        } else {
            if (!e && s_n < MAX_ENTRIES) { e = &s_e[s_n++]; memset(e, 0, sizeof *e); snprintf(e->profile, sizeof e->profile, "%s", prof); e->handle = h; }
            if (e) { e->type = t; e->rgb = (uint32_t)rgb & 0xffffffu; e->finish = finish; }
        }
        done = 1;
    }
    pthread_mutex_unlock(&s_lock);
    return done;
}
void nfs_paint_save(void) {
    pthread_mutex_lock(&s_lock);
    if (s_loaded) { save(); LOGI("Paint: %d cars' own paint kept", s_n); }
    pthread_mutex_unlock(&s_lock);
}

/* whether the car of ride (and render info ri, 0 none yet) is the player's: 1 with its paint in e */
static int ours(uint32_t ride, uint32_t ri, Entry *e) {
    if (!ride || (rd(G_GAMEMODE) & 0x208u)) return 0;             /* (not in a LAN or Online game) */
    int got = 0;
    pthread_mutex_lock(&s_lock);
    if (mine(e) && ride_type(ride) == e->type) {
        if (rd(G_FLOW) == 3u) got = nfs_plates_fe_ours();          /* the front end: the car its screens are about */
        else {
            uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0, pri = car ? rd(car + CAR_RENDER) : 0;
            if (ri) got = car && rd(ri + RI_CAR) == car;
            else got = !pri || rd(pri + RI_RIDE) == ride;          /* (loading: the player's car of that type) */
        }
    }
    pthread_mutex_unlock(&s_lock);
    return got;
}

/* ---- the skin: the base paint's colour ---- */
static struct { int on; uint32_t part, attr; } S;
static uint32_t attr_block(void) {
    static uint32_t blk;
    if (!blk) {
        uint32_t n = 3u * 8u;
        blk = nfs_guest_call(FN_NEW, 0, &n, 1, 4);
        if (!ok(blk, n)) blk = 0;
    }
    return blk;
}
/* wraps 0x61D790 (cdecl, the RideInfo): the skin composed, our colour as the base paint's */
void nfs_paint_ov_compose(void) {
    uint32_t ride = rd(g_esp + 4u);
    Entry e;
    if (ok(ride, RIDE_PAINT + 4u) && MEM32(ride + RIDE_PAINT) && ours(ride, 0, &e) && (S.attr = attr_block())) {
        const uint32_t h[3] = { H_RED, H_GREEN, H_BLUE }, v[3] = { e.rgb >> 16 & 0xffu, e.rgb >> 8 & 0xffu, e.rgb & 0xffu };
        for (int i = 0; i < 3; i++) { MEM32(S.attr + 8u * (uint32_t)i) = h[i]; MEM32(S.attr + 8u * (uint32_t)i + 4u) = v[i]; }
        S.part = MEM32(ride + RIDE_PAINT); S.on = 1;
        LOGI("Paint: the player's car's skin composed in #%06X", e.rgb);
    }
    sub_0061D790_orig();
    S.on = 0;
}
/* wraps 0x60FEF0 (thiscall part (hash, after) -> its attribute): the base paint's RED, GREEN, BLUE ours while composed */
void nfs_paint_ov_attr(void) {
    if (S.on && g_ecx == S.part && !MEM32(g_esp + 8u)) {
        uint32_t h = MEM32(g_esp + 4u);
        int i = h == H_RED ? 0 : h == H_GREEN ? 1 : h == H_BLUE ? 2 : -1;
        if (i >= 0) { g_eax = S.attr + 8u * (uint32_t)i; g_esp += 4u + 8u; return; }
    }
    sub_0060FEF0_orig();
}

/* ---- the drawing: the finish's material ---- */
static uint32_t material(int finish) {
    static uint32_t key[2], mat[FINISHES];
    uint32_t head = rd(G_LM_LIST), tail = rd(G_LM_LIST + 4u);
    if (!head) return 0;
    if (key[0] != head || key[1] != tail) {
        key[0] = head; key[1] = tail;
        memset(mat, 0, sizeof mat);
        uint32_t want[FINISHES] = { 0 };
        for (int i = 1; i < FINISHES; i++) want[i] = bhash(k_finish[i]);
        const uint32_t end = G_LM_LIST - 4u;
        uint32_t m = head - 4u;
        for (int n = 0; m && m != end && ok(m, 0x10u) && n < 65536; n++) {
            uint32_t hh = MEM32(m + 0xcu);
            for (int i = 1; i < FINISHES; i++) if (hh == want[i] && !mat[i]) mat[i] = m;
            uint32_t nx = MEM32(m + 4u);
            m = nx ? nx - 4u : 0u;
        }
    }
    return mat[finish];
}
static struct { int on; uint32_t ri, keep; } D;
void nfs_paint_draw_begin(uint32_t ri) {
    D.on = 0;
    Entry e;
    if (!ok(ri, RI_PAINT_LM + 4u) || !ours(rd(ri + RI_RIDE), ri, &e) || !e.finish) return;
    uint32_t m = material(e.finish);
    if (!m) return;
    D.on = 1; D.ri = ri; D.keep = MEM32(ri + RI_PAINT_LM);
    MEM32(ri + RI_PAINT_LM) = m;
}
void nfs_paint_draw_end(void) {
    if (!D.on) return;
    D.on = 0;
    MEM32(D.ri + RI_PAINT_LM) = D.keep;
}
#endif
