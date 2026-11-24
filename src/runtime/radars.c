/*
 * radars.c - speed traps in career free roam (Android): radars on the city's longest straight roads, each with the
 * player's record (the top speed through it), kept beside the profiles (AppData/Local/NFS Underground 2/RADARS.TXT).
 *
 *  - the radars: the traffic's roads (0x88A334, walls.c's network) walked for straight runs (each segment within
 *    STRAIGHT_DEG of the run's first) of RUN_MIN metres or more; their middles, the longest first, at least SPACING
 *    apart, on this career's side of its walls (walls.c nfs_walls_open_at), RADARS_MAX of them; made again when the
 *    roads change (another world);
 *  - a pass: the player's car within ZONE_M of one (and its height), its fastest speed there (car +0x42C, m/s) taken
 *    when it leaves (ZONE_OUT_M), counted over PASS_MIN_KMH;
 *  - the touch controls (nativeRadarStatus): the nearest radar's distance and its direction from where the car looks,
 *    the last pass (which radar, its speed, the record, a new one or not, a number that changes with each pass).
 * Career free roam only ([0x850078] 1, not LAN or Online), the world running, the player driving.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
int nfs_save_host_path(const char *rel, char *out, size_t n);
int nfs_walls_open_at(float x, float y, float z);

#define G_GAMEMODE    0x00850078u
#define G_RACETYPE    0x0089e7b0u
#define G_FLOW        0x008654a4u
#define G_RACEPARAMS  0x0089e7a0u
#define G_PLAYER1     0x008900acu
#define G_ROADS       0x0088a334u
#define NROADS        2048u
#define RD_ID         0x0au
#define RD_NODES      0x34u
#define RD_NODE0      0x8cu
#define NODE_SZ       0x38u
#define ND_Z          0x12u
#define CAR_PLACE     0x60u
#define CAR_FWD       0x90u
#define CAR_SPEED     0x42cu
#define RADARS_MAX    16
#define RUN_MIN       180.f
#define STRAIGHT_DEG  4.f
#define SPACING       500.f
#define ZONE_M        14.f
#define ZONE_OUT_M    30.f
#define ZONE_DZ       6.f
#define PASS_MIN_KMH  40.f
#define RECORDS_FILE  "NFS Underground 2\\RADARS.TXT"

typedef struct { float x, y, z; int record; } Radar;
static struct {
    uint32_t sig; int built, n;
    Radar r[RADARS_MAX];
    int in; float best;               /* the radar the car is in (-1 none), its fastest there (km/h) */
} R = { .in = -1 };
static atomic_int s_status[9];        /* count, nearest, its distance (m), its direction (degrees, + right), last radar,
                                         its speed, the record, 1 a new record, the pass's number */
static int s_passes;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = rd(va); float f; memcpy(&f, &v, 4); return f; }
static uint32_t road(uint32_t i) {
    uint32_t r = rd(G_ROADS + 4u * i);
    if (!ok(r, RD_NODE0) || (int16_t)MEM16(r + RD_ID) != (int)i) return 0;
    int n = (int16_t)MEM16(r + RD_NODES);
    return n > 1 && n < 4096 && ok(r + RD_NODE0, (uint32_t)n * NODE_SZ) ? r : 0;
}
static uint32_t signature(void) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < NROADS; i++) h = (h ^ rd(G_ROADS + 4u * i)) * 16777619u;
    return h;
}

static void load_records(void) {
    char path[1100];
    if (!nfs_save_host_path(RECORDS_FILE, path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    float x, y; int rec;
    while (fscanf(f, "%f %f %d", &x, &y, &rec) == 3)
        for (int i = 0; i < R.n; i++) if (hypotf(R.r[i].x - x, R.r[i].y - y) < 30.f && rec > R.r[i].record) R.r[i].record = rec;
    fclose(f);
}
static void save_records(void) {
    char path[1100];
    if (!nfs_save_host_path(RECORDS_FILE, path, sizeof path)) return;
    /* the records of radars of other worlds kept: the file's lines not of these, then these */
    char keep[8192] = ""; size_t k = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        float x, y; int rec;
        while (fscanf(f, "%f %f %d", &x, &y, &rec) == 3) {
            int ours = 0;
            for (int i = 0; i < R.n; i++) if (hypotf(R.r[i].x - x, R.r[i].y - y) < 30.f) ours = 1;
            if (!ours && k + 64 < sizeof keep) k += (size_t)snprintf(keep + k, sizeof keep - k, "%.1f %.1f %d\n", (double)x, (double)y, rec);
        }
        fclose(f);
    }
    f = fopen(path, "w");
    if (!f) { LOGI("Radars: cannot write %s", path); return; }
    fputs(keep, f);
    for (int i = 0; i < R.n; i++) if (R.r[i].record > 0) fprintf(f, "%.1f %.1f %d\n", (double)R.r[i].x, (double)R.r[i].y, R.r[i].record);
    fclose(f);
}

typedef struct { float x, y, z, len; } Run;
static int run_cmp(const void *a, const void *b) { float d = ((const Run *)b)->len - ((const Run *)a)->len; return d > 0 ? 1 : d < 0 ? -1 : 0; }

static void build(void) {
    R.built = 1; R.sig = signature(); R.n = 0; R.in = -1;
    int cap = 4096, nrun = 0;
    Run *runs = malloc(sizeof(Run) * (size_t)cap);
    if (!runs) return;
    const float cosmax = cosf(STRAIGHT_DEG * 0.017453292f);
    for (uint32_t i = 0; i < NROADS && nrun < cap; i++) {
        uint32_t r = road(i);
        if (!r) continue;
        int m = (int16_t)MEM16(r + RD_NODES), a = 0;
        while (a + 1 < m && nrun < cap) {
            uint32_t na = r + RD_NODE0 + NODE_SZ * (uint32_t)a;
            float ax = rdf(na), ay = rdf(na + 4u), dx0 = 0, dy0 = 0, len = 0;
            int b = a;
            while (b + 1 < m) {
                uint32_t p = r + RD_NODE0 + NODE_SZ * (uint32_t)b, q = p + NODE_SZ;
                float sx = rdf(q) - rdf(p), sy = rdf(q + 4u) - rdf(p + 4u), sl = hypotf(sx, sy);
                if (!(sl > 0.01f) || sl > 1000.f) break;
                if (b == a) { dx0 = sx / sl; dy0 = sy / sl; }
                else if ((sx * dx0 + sy * dy0) / sl < cosmax) break;
                len += sl; b++;
            }
            if (len >= RUN_MIN) {
                uint32_t nb = r + RD_NODE0 + NODE_SZ * (uint32_t)b;
                float bx = rdf(nb), by = rdf(nb + 4u);
                float za = (float)(int16_t)MEM16(na + ND_Z) * 0.125f, zb = (float)(int16_t)MEM16(nb + ND_Z) * 0.125f;
                if (isfinite(ax + ay + bx + by)) runs[nrun++] = (Run){ (ax + bx) / 2, (ay + by) / 2, (za + zb) / 2, len };
            }
            a = b > a ? b : a + 1;
        }
    }
    qsort(runs, (size_t)nrun, sizeof(Run), run_cmp);
    for (int i = 0; i < nrun && R.n < RADARS_MAX; i++) {
        int near = 0;
        for (int k = 0; k < R.n && !near; k++) near = hypotf(R.r[k].x - runs[i].x, R.r[k].y - runs[i].y) < SPACING;
        if (near || !nfs_walls_open_at(runs[i].x, runs[i].y, runs[i].z + 0.5f)) continue;
        R.r[R.n++] = (Radar){ runs[i].x, runs[i].y, runs[i].z, 0 };
    }
    free(runs);
    load_records();
    LOGI("Radars: %d radars on the city's longest straight roads (%d straight runs)", R.n, nrun);
}

static atomic_int s_on = 1;
void nfs_radars_enable(int on) { atomic_store(&s_on, on != 0); }
void nfs_radars_status(int out[9]) { for (int i = 0; i < 9; i++) out[i] = atomic_load(&s_status[i]); }

void nfs_radars_frame(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    int roam = atomic_load(&s_on) && rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u && rd(G_RACEPARAMS) == 4000u && ok(car, CAR_SPEED + 4u);
    if (!roam) { atomic_store(&s_status[0], 0); R.in = -1; return; }
    static unsigned tick;
    if (!R.built || (++tick % 120u == 0u && signature() != R.sig)) build();   /* (another world: built again) */
    if (!R.n) { atomic_store(&s_status[0], 0); return; }
    float x = rdf(car + CAR_PLACE), y = rdf(car + CAR_PLACE + 4u), z = rdf(car + CAR_PLACE + 8u);
    float fx = rdf(car + CAR_FWD), fy = rdf(car + CAR_FWD + 4u), kmh = fabsf(rdf(car + CAR_SPEED)) * 3.6f;
    if (!isfinite(x + y + z) || !isfinite(kmh)) return;
    int near = -1; float nd = 1e30f;
    for (int i = 0; i < R.n; i++) { float d = hypotf(R.r[i].x - x, R.r[i].y - y); if (d < nd) { nd = d; near = i; } }
    /* the pass */
    if (R.in >= 0) {
        Radar *r = &R.r[R.in];
        float d = hypotf(r->x - x, r->y - y);
        if (kmh > R.best) R.best = kmh;
        if (d > ZONE_OUT_M) {
            int v = (int)lrintf(R.best), was = r->record, rec = v > was;
            if (v >= PASS_MIN_KMH) {
                if (rec) { r->record = v; save_records(); }
                s_passes++;
                atomic_store(&s_status[4], R.in + 1); atomic_store(&s_status[5], v); atomic_store(&s_status[6], r->record);
                atomic_store(&s_status[7], rec && was > 0 ? 1 : rec ? 2 : 0); atomic_store(&s_status[8], s_passes);
                LOGI("Radars: radar %d, %d km/h (record %d%s)", R.in + 1, v, r->record, rec ? ", new" : "");
            }
            R.in = -1;
        }
    } else if (near >= 0 && nd < ZONE_M && fabsf(R.r[near].z - z) < ZONE_DZ) { R.in = near; R.best = kmh; }
    /* the nearest one, as the car looks */
    float dx = R.r[near].x - x, dy = R.r[near].y - y, fl = hypotf(fx, fy), ang = 0;
    if (fl > 1e-3f && nd > 1e-3f) {
        float c = (dx * fx + dy * fy) / (nd * fl), s = (fx * dy - fy * dx) / (nd * fl);
        ang = -atan2f(s, c) * 57.29578f;            /* + to the right (the game's y up the map) */
    }
    atomic_store(&s_status[0], R.n); atomic_store(&s_status[1], near + 1);
    atomic_store(&s_status[2], (int)lrintf(nd)); atomic_store(&s_status[3], (int)lrintf(ang));
}
#endif
