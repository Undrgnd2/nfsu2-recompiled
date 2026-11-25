/*
 * stance.c - a car's stance (Android, the ⚙ menu's "Stance"): its wheels' camber, front and rear, how far they stand
 * out, and its tires' width, as the car is drawn; only what is seen changes, the car drives as in the original game.
 *
 * Adapted from nfsu2-web's runtime/stance.c (MIT, Copyright (c) 2026 th0mk; after Archie (AR4I)'s Stance Mod; the
 * notes on the game are there). The car's drawing 0x620AE0 (photo.c's wrap: nfs_stance_draw_begin / _end around it):
 *   - front camber: the drawing makes the angle from [0x7A6754] (7.0 degrees) times [0x7A6750] when that is not 0 (a
 *     tweak, 0 in the game; else the car's two suspension parts), and [0x7A6758] x [0x7A6750] shifts the front wheels:
 *     ours for the call (1, our camber, 0); the rear wheels, which nothing tilts, have their matrices (the drawing's
 *     arguments 5 and 7, 4 x 0x40 each, wheels 2 and 3) turned the same way before it;
 *   - the render info's +0x400 each wheel's sideways place (2 floats, the left's below 0), +0x420 each wheel's tire
 *     width: ours for the call. Each put back after.
 * Whose car: the player's (the car the career drives, else Quick Race's), in the world and on the front end's screens
 * about it (the garage, the shops: plates.c says which). The values are the menu's, per car (its handle and car type),
 * kept beside the profiles (AppData/Local/NFS Underground 2/STANCE.TXT: profile, handle, car type, the five values).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
int nfs_save_host_path(const char *rel, char *out, size_t n);
int nfs_plates_fe_ours(void);

#define G_GAMEMODE     0x00850078u
#define G_FLOW         0x008654a4u
#define G_PLAYER1      0x008900acu
#define G_CAREER_CAR   0x00863480u   /* the career car's handle */
#define G_QR_CAR       0x0083aab8u   /* the Quick Race car's handle */
#define G_PROFILE_NAME 0x0083a9e0u   /* player 1's profile: its name (32 bytes), +0x20 1 one is loaded */
#define G_FECARS       0x0083ad90u
#define G_CARTYPES     0x008a1cccu
#define G_CAMBER_F     0x007a6750u
#define G_CAMBER_DEG   0x007a6754u
#define G_CAMBER_K     0x007a6758u
#define CD_ARG_WHEELS  0x14u
#define CD_ARG_WHEELS2 0x1cu
#define WHEEL_MX       0x40u
#define RI_CAR         0u
#define RI_RIDE        4u
#define RI_WHEEL_Y     0x400u
#define RI_TIRE_W      0x420u
#define CARTYPES_N     46u
#define CARTYPE_SZ     0x890u
#define CARTYPE_HASH   0xd0u
#define REC_TYPE       0x20u
#define STANCE_FILE    "NFS Underground 2\\STANCE.TXT"
#define MAX_ENTRIES    128

typedef struct { int16_t fc, rc, fo, ro; uint16_t tw; } StanceV;   /* tenths of a degree, mm, per cent */
#define CAMBER_MAX 150
#define OFFSET_MIN (-40)
#define OFFSET_MAX 80
#define TIRE_MIN   80
#define TIRE_MAX   130

typedef struct { char profile[33]; uint32_t handle, type; StanceV v; } Entry;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static Entry s_e[MAX_ENTRIES];
static int s_n, s_loaded;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n ? n : 1u); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t u = MEM32(va); float f; memcpy(&f, &u, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t u; memcpy(&u, &f, 4); MEM32(va) = u; }
static int valid(const StanceV *v) {
    return v->fc >= 0 && v->fc <= CAMBER_MAX && v->rc >= 0 && v->rc <= CAMBER_MAX && v->fo >= OFFSET_MIN && v->fo <= OFFSET_MAX
        && v->ro >= OFFSET_MIN && v->ro <= OFFSET_MAX && v->tw >= TIRE_MIN && v->tw <= TIRE_MAX;
}

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

static void load(void) {
    s_loaded = 1; s_n = 0;
    char path[1100];
    if (!nfs_save_host_path(STANCE_FILE, path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (s_n < MAX_ENTRIES && fgets(line, sizeof line, f)) {
        Entry e; int a[5]; char name[33];
        if (sscanf(line, "%32[^\t]\t%x\t%x\t%d %d %d %d %d", name, &e.handle, &e.type, &a[0], &a[1], &a[2], &a[3], &a[4]) != 8) continue;
        e.v.fc = (int16_t)a[0]; e.v.rc = (int16_t)a[1]; e.v.fo = (int16_t)a[2]; e.v.ro = (int16_t)a[3]; e.v.tw = (uint16_t)a[4];
        if (!valid(&e.v)) continue;
        snprintf(e.profile, sizeof e.profile, "%s", name);
        s_e[s_n++] = e;
    }
    fclose(f);
    LOGI("Stance: %d cars' stance read", s_n);
}
static void save(void) {
    char path[1100];
    if (!nfs_save_host_path(STANCE_FILE, path, sizeof path)) return;
    FILE *f = fopen(path, "w");
    if (!f) { LOGI("Stance: cannot write %s", path); return; }
    for (int i = 0; i < s_n; i++)
        fprintf(f, "%s\t%08x\t%08x\t%d %d %d %d %d\n", s_e[i].profile, s_e[i].handle, s_e[i].type,
                s_e[i].v.fc, s_e[i].v.rc, s_e[i].v.fo, s_e[i].v.ro, s_e[i].v.tw);
    fclose(f);
}
static Entry *find(const char *prof, uint32_t h) {
    for (int i = 0; i < s_n; i++) if (s_e[i].handle == h && !strcasecmp(s_e[i].profile, prof)) return &s_e[i];
    return 0;
}

/* the player's car's stance into v, 1 when it has one of its own (lock held) */
static int mine(StanceV *v) {
    char prof[33]; profile(prof);
    uint32_t h = driven(), t = car_type(h);
    if (!prof[0] || !h || !t) return 0;
    if (!s_loaded) load();
    Entry *e = find(prof, h);
    if (!e || e->type != t) return 0;
    *v = e->v;
    return 1;
}

/* the menu (any thread): out = { a car (1) or none (0), has one of its own, front camber, rear (tenths of a degree),
 * front offset, rear (mm), tires' width (%) } */
void nfs_stance_get(int out[7]) {
    pthread_mutex_lock(&s_lock);
    char prof[33]; profile(prof);
    uint32_t h = driven();
    StanceV v = { 0, 0, 0, 0, 100 };
    out[0] = prof[0] && h && car_type(h);
    out[1] = out[0] && mine(&v);
    out[2] = v.fc; out[3] = v.rc; out[4] = v.fo; out[5] = v.ro; out[6] = v.tw;
    pthread_mutex_unlock(&s_lock);
}
/* the menu (any thread): the player's car's stance set (has 0: none, the game's look); 0 no car */
int nfs_stance_set(int has, int fc, int rc, int fo, int ro, int tw) {
    pthread_mutex_lock(&s_lock);
    char prof[33]; profile(prof);
    uint32_t h = driven(), t = car_type(h);
    int done = 0;
    if (prof[0] && h && t) {
        if (!s_loaded) load();
        Entry *e = find(prof, h);
        StanceV v = { (int16_t)fc, (int16_t)rc, (int16_t)fo, (int16_t)ro, (uint16_t)tw };
        if (!has || !valid(&v)) {
            if (e) { int i = (int)(e - s_e); memmove(e, e + 1, sizeof *e * (size_t)(s_n - 1 - i)); s_n--; }
        } else {
            if (!e && s_n < MAX_ENTRIES) { e = &s_e[s_n++]; memset(e, 0, sizeof *e); snprintf(e->profile, sizeof e->profile, "%s", prof); e->handle = h; }
            if (e) { e->type = t; e->v = v; }
        }
        done = 1;
    }
    pthread_mutex_unlock(&s_lock);
    return done;
}
/* the menu closed: kept */
void nfs_stance_save(void) {
    pthread_mutex_lock(&s_lock);
    if (s_loaded) { save(); LOGI("Stance: %d cars' stance kept", s_n); }
    pthread_mutex_unlock(&s_lock);
}

/* ---- the drawing ---- */
static struct {
    int on; uint32_t ri, keep_c[3], mx[4];
    float keep_mx[4][12], keep_y[8], keep_w[4];
} D;

static uint32_t ride_type(uint32_t ri) {
    uint32_t ride = rd(ri + RI_RIDE), types = rd(G_CARTYPES), t = ride ? rd(ride) : 0xffffffffu;
    return t < CARTYPES_N && ok(types + t * CARTYPE_SZ + CARTYPE_HASH, 4) ? MEM32(types + t * CARTYPE_SZ + CARTYPE_HASH) : 0;
}
/* the stance the car of render info ri is drawn with: 1 with v */
static int of_drawn(uint32_t ri, StanceV *v) {
    if (rd(G_GAMEMODE) & 0x208u) return 0;                        /* (not in a LAN or Online game) */
    uint32_t car = rd(ri + RI_CAR), flow = rd(G_FLOW), pl = rd(G_PLAYER1);
    int got = 0;
    if (flow == 3u) {                                             /* the front end: the car its screens are about */
        if (!nfs_plates_fe_ours()) return 0;
        pthread_mutex_lock(&s_lock);
        got = mine(v) && ride_type(ri) == car_type(driven());
        pthread_mutex_unlock(&s_lock);
    } else if (flow == 6u && car && pl && car == rd(pl + 4u)) {
        pthread_mutex_lock(&s_lock); got = mine(v); pthread_mutex_unlock(&s_lock);
    }
    return got;
}
static void turn(uint32_t m, float a, float keep[12]) {
    float r[12];
    for (int k = 0; k < 12; k++) keep[k] = r[k] = rdf(m + 4u * (uint32_t)k);
    float cs = cosf(a), sn = sinf(a);
    for (int row = 0; row < 3; row++) {
        float y = r[row * 4 + 1], z = r[row * 4 + 2];
        wrf(m + 4u * (uint32_t)(row * 4 + 1), y * cs - z * sn);
        wrf(m + 4u * (uint32_t)(row * 4 + 2), y * sn + z * cs);
    }
}
/* photo.c's 0x620AE0 wrap (thiscall render info; esp at its entry: the return address), before the game's drawing */
void nfs_stance_draw_begin(uint32_t ri, uint32_t esp) {
    D.on = 0;
    StanceV s;
    if (!ok(ri, RI_TIRE_W + 0x10u) || !of_drawn(ri, &s)) return;
    D.on = 1; D.ri = ri;
    D.keep_c[0] = MEM32(G_CAMBER_F); D.keep_c[1] = MEM32(G_CAMBER_DEG); D.keep_c[2] = MEM32(G_CAMBER_K);
    wrf(G_CAMBER_F, 1.0f); wrf(G_CAMBER_DEG, s.fc * 0.1f); wrf(G_CAMBER_K, 0.0f);
    for (int k = 0; k < 8; k++) {
        float y = D.keep_y[k] = rdf(ri + RI_WHEEL_Y + 4u * (uint32_t)k), o = (k < 4 ? s.fo : s.ro) * 0.001f;
        wrf(ri + RI_WHEEL_Y + 4u * (uint32_t)k, y + (y < 0 ? -o : o));
    }
    for (int k = 0; k < 4; k++) { D.keep_w[k] = rdf(ri + RI_TIRE_W + 4u * (uint32_t)k); wrf(ri + RI_TIRE_W + 4u * (uint32_t)k, D.keep_w[k] * s.tw * 0.01f); }
    float a = s.rc * 0.1f * 3.14159265f / 180.0f;
    uint32_t w[2] = { rd(esp + CD_ARG_WHEELS), rd(esp + CD_ARG_WHEELS2) };
    for (int set = 0; set < 2; set++) {
        int good = a != 0 && ok(w[set], 4u * WHEEL_MX);
        for (int i = 2; i < 4; i++) {
            uint32_t m = good ? w[set] + WHEEL_MX * (uint32_t)i : 0;
            D.mx[set * 2 + (i - 2)] = m;
            if (m) turn(m, i == 2 ? -a : a, D.keep_mx[set * 2 + (i - 2)]);
        }
    }
}
void nfs_stance_draw_end(void) {
    if (!D.on) return;
    D.on = 0;
    MEM32(G_CAMBER_F) = D.keep_c[0]; MEM32(G_CAMBER_DEG) = D.keep_c[1]; MEM32(G_CAMBER_K) = D.keep_c[2];
    for (int k = 0; k < 8; k++) wrf(D.ri + RI_WHEEL_Y + 4u * (uint32_t)k, D.keep_y[k]);
    for (int k = 0; k < 4; k++) wrf(D.ri + RI_TIRE_W + 4u * (uint32_t)k, D.keep_w[k]);
    for (int k = 0; k < 4; k++) {
        if (!D.mx[k]) continue;
        for (int j = 0; j < 12; j++) wrf(D.mx[k] + 4u * (uint32_t)j, D.keep_mx[k][j]);
    }
}
#endif
