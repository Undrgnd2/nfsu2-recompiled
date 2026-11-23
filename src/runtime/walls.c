/*
 * walls.c - a car past the career's walls goes back to the roads it can reach (Android).
 *
 * Adapted from nfsu2-web's runtime/walls.c (MIT, Copyright (c) 2026 th0mk; the full notes on the game are there):
 * career free roam closes the later stages' areas with red walls, lines of the track's barriers (track 0x88F298
 * +0x498 count, +0x49C 0x24 bytes each) which the GPS's road search does not cross. A car thrown past one (a hard hit)
 * could not come back: R puts it on the road it is on, and leaving free roam the game keeps that spot. The traffic's
 * roads (0x88A334, joints 0x883DB0) make one network over the city: cut where an enabled barrier line crosses it, the
 * part the new career's start reaches is the career's roads. A car whose nearest road is past the walls goes to the
 * nearest place on the career's roads at least WALL_GAP from a barrier line, facing away from where it was, through
 * the hot-position jump: when career free roam starts with the car there, and after a reset (R, or the game's own).
 * Career free roam in the city only, not in a new career's prologue. Log lines start "Walls:".
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
int nfs_garage_has_career_car(void);   /* garage.c: the career has a car of its own (not a new one's prologue) */

#define G_GAMEMODE      0x00850078u
#define G_RACETYPE      0x0089e7b0u
#define G_FLOW          0x008654a4u
#define G_RACEPARAMS    0x0089e7a0u
#define G_HOTPOS        0x00890140u
#define G_HOTPOS_ANGLE  0x008900d0u
#define G_HOTPOS_JUMP   0x008900cdu
#define G_HOTPOS_VALID  0x008900ceu
#define G_WORLDDT       0x00865154u
#define G_PLAYER1       0x008900acu
#define G_TRACK         0x0088f298u
#define TK_NBAR         0x498u
#define TK_BARS         0x49cu
#define BAR_SZ          0x24u
#define BAR_ON          0x10u
#define G_ROADS         0x0088a334u
#define NROADS          2048u
#define RD_ID           0x0au
#define RD_NODES        0x34u
#define RD_NODE0        0x8cu
#define NODE_SZ         0x38u
#define ND_Z            0x12u
#define G_JOINTS        0x00883db0u
#define G_NJOINTS       0x00883db4u
#define JOINT_SZ        0x20u
#define CAR_PLACE       0x60u
#define CAR_RESET_T     0x974u
#define START_X         1695.25f
#define START_Y         (-883.64f)
#define MAX_BARS        256
#define WALL_GAP        10.f
#define HEIGHT_SLACK    4.f
#define NEAR_MORE       20.f
#define ENTRY_FRAMES    30
#define JUMP_PAUSE      120
#define PLACE_MAX       100000.f

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf_(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }

enum { W_UNKNOWN, W_OPEN, W_PAST };   /* reach[]: not on the start's network, the career's roads, past its walls */

static struct {
    int built, ok, n, nseg, nbar, nreach, npast;
    float *x, *y, *z;
    uint8_t *reach;
    int *sa, *sb; uint8_t *scut;
    float bar[MAX_BARS][4];
    uint32_t sig;
} W;

static struct {
    int in, entry_done;   /* in: 1 this stay in free roam is looked at, 2 not (a new career's prologue) */
    long since, jumped_at;
    uint32_t stamp;
    unsigned jumps;
} S;

static uint32_t player_car(void){ uint32_t p = rd(G_PLAYER1), car = p ? rd(p + 4u) : 0; return ok(car, CAR_RESET_T + 4u) ? car : 0; }
static uint32_t road(uint32_t i){
    uint32_t r = rd(G_ROADS + 4u * i);
    if (!ok(r, RD_NODE0) || (int16_t)MEM16(r + RD_ID) != (int)i) return 0;   /* the table's other slots name roads again */
    int n = (int16_t)MEM16(r + RD_NODES);
    return n > 0 && n < 4096 && ok(r + RD_NODE0, (uint32_t)n * NODE_SZ) ? r : 0;
}
static uint32_t mix(uint32_t h, uint32_t v){ return (h ^ v) * 16777619u; }

/* the roads and the enabled barriers as they are now, in a word */
static uint32_t signature(void){
    uint32_t h = 2166136261u, nb = rd(G_TRACK + TK_NBAR), bars = rd(G_TRACK + TK_BARS);
    for (uint32_t i = 0; i < NROADS; i++) h = mix(h, rd(G_ROADS + 4u * i));
    h = mix(h, rd(G_JOINTS)); h = mix(h, rd(G_NJOINTS)); h = mix(h, nb);
    for (uint32_t i = 0; i < nb && i < 8192u && ok(bars + i * BAR_SZ, BAR_SZ); i++) if (MEM8(bars + i * BAR_SZ + BAR_ON)) h = mix(h, i + 1u);
    return h;
}

/* 2D: the segments a-b and c-d cross */
static int crosses(float ax, float ay, float bx, float by, const float *c){
    float d1 = (c[2] - c[0]) * (ay - c[1]) - (c[3] - c[1]) * (ax - c[0]);
    float d2 = (c[2] - c[0]) * (by - c[1]) - (c[3] - c[1]) * (bx - c[0]);
    float d3 = (bx - ax) * (c[1] - ay) - (by - ay) * (c[0] - ax);
    float d4 = (bx - ax) * (c[3] - ay) - (by - ay) * (c[2] - ax);
    return (d1 > 0.f) != (d2 > 0.f) && (d3 > 0.f) != (d4 > 0.f);
}
static int crosses_any(float ax, float ay, float bx, float by){
    for (int i = 0; i < W.nbar; i++) if (crosses(ax, ay, bx, by, W.bar[i])) return i + 1;
    return 0;
}
/* 2D distance from (px, py) to the segment a-b, and where along it (0..1) */
static float seg_dist(float px, float py, float ax, float ay, float bx, float by, float *t){
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, u = l2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.f;
    if (u < 0.f) u = 0.f; else if (u > 1.f) u = 1.f;
    if (t) *t = u;
    float ex = ax + u * dx - px, ey = ay + u * dy - py;
    return sqrtf(ex * ex + ey * ey);
}
static float bar_dist(float x, float y){
    float m = 1e30f;
    for (int i = 0; i < W.nbar; i++){ float d = seg_dist(x, y, W.bar[i][0], W.bar[i][1], W.bar[i][2], W.bar[i][3], 0); if (d < m) m = d; }
    return m;
}
/* how far a place is from a point of the roads: the height difference past HEIGHT_SLACK twice */
static float metric(float d2d, float dz){
    float h = fabsf(dz) - HEIGHT_SLACK;
    if (h < 0.f) h = 0.f;
    return sqrtf(d2d * d2d + 4.f * h * h);
}

static void drop(void){
    free(W.x); free(W.y); free(W.z); free(W.reach); free(W.sa); free(W.sb); free(W.scut);
    W.x = W.y = W.z = 0; W.reach = 0; W.sa = W.sb = 0; W.scut = 0;
    W.built = W.ok = W.n = W.nseg = W.nbar = W.nreach = W.npast = 0;
}

/* the network, its cuts and what the start reaches; W.ok when it is usable */
static void build(long f){
    (void)f;
    drop();
    W.built = 1; W.sig = signature();
    uint32_t nb = rd(G_TRACK + TK_NBAR), bars = rd(G_TRACK + TK_BARS);
    for (uint32_t i = 0; i < nb && i < 8192u && ok(bars + i * BAR_SZ, BAR_SZ); i++){
        uint32_t b = bars + i * BAR_SZ;
        if (!MEM8(b + BAR_ON)) continue;
        if (W.nbar == MAX_BARS){ LOGI("Walls: more than %d barrier lines on: left as the game has it",  MAX_BARS); return; }
        for (int k = 0; k < 4; k++) W.bar[W.nbar][k] = rdf_(b + 4u * (uint32_t)k);
        if (!isfinite(W.bar[W.nbar][0] + W.bar[W.nbar][1] + W.bar[W.nbar][2] + W.bar[W.nbar][3])) continue;
        W.nbar++;
    }
    static int base[NROADS];
    int n = 0;
    for (uint32_t i = 0; i < NROADS; i++){ uint32_t r = road(i); base[i] = r ? n : -1; if (r) n += (int16_t)MEM16(r + RD_NODES); }
    uint32_t nj = rd(G_NJOINTS), jt = rd(G_JOINTS);
    if (nj > 65536u || !ok(jt, nj * JOINT_SZ)) nj = 0;
    if (n < 100){ LOGI("Walls: no roads (%d nodes): left as the game has it",  n); return; }
    int maxseg = n + (int)nj;
    W.x = malloc(sizeof(float) * (size_t)n); W.y = malloc(sizeof(float) * (size_t)n); W.z = malloc(sizeof(float) * (size_t)n);
    W.reach = calloc((size_t)n, 1); W.sa = malloc(sizeof(int) * (size_t)maxseg); W.sb = malloc(sizeof(int) * (size_t)maxseg);
    W.scut = calloc((size_t)maxseg, 1);
    int *deg = calloc((size_t)n + 1, sizeof(int)), *adj = malloc(sizeof(int) * 2u * (size_t)maxseg), *queue = malloc(sizeof(int) * (size_t)n);
    uint8_t *seen = calloc((size_t)n, 1);
    if (!W.x || !W.y || !W.z || !W.reach || !W.sa || !W.sb || !W.scut || !deg || !adj || !queue || !seen){
        LOGI("Walls: out of memory "); free(deg); free(adj); free(queue); free(seen); drop(); W.built = 1; return;
    }
    W.n = n;
    for (uint32_t i = 0; i < NROADS; i++){
        if (base[i] < 0) continue;
        uint32_t r = road(i); int m = (int16_t)MEM16(r + RD_NODES);
        for (int k = 0; k < m; k++){
            uint32_t nd = r + RD_NODE0 + NODE_SZ * (uint32_t)k; int g = base[i] + k;
            W.x[g] = rdf_(nd); W.y[g] = rdf_(nd + 4u); W.z[g] = (float)(int16_t)MEM16(nd + ND_Z) * 0.125f;
            if (k + 1 < m){ W.sa[W.nseg] = g; W.sb[W.nseg] = g + 1; W.nseg++; }
        }
    }
    int njoint = 0;
    for (uint32_t j = 0; j < nj; j++){
        uint32_t e = jt + JOINT_SZ * j;
        int n4 = (int16_t)MEM16(e + 4u), n6 = (int16_t)MEM16(e + 6u), r8 = (int16_t)MEM16(e + 8u), ra = (int16_t)MEM16(e + 0xau);
        if (r8 < 0 || ra < 0 || r8 >= (int)NROADS || ra >= (int)NROADS || base[r8] < 0 || base[ra] < 0 || n4 < 0 || n6 < 0) continue;
        if (n4 >= (int16_t)MEM16(road((uint32_t)r8) + RD_NODES) || n6 >= (int16_t)MEM16(road((uint32_t)ra) + RD_NODES)) continue;
        W.sa[W.nseg] = base[r8] + n4; W.sb[W.nseg] = base[ra] + n6; W.nseg++; njoint++;
    }
    int ncut = 0;
    for (int s = 0; s < W.nseg; s++){
        int a = W.sa[s], b = W.sb[s];
        if (isfinite(W.x[a] + W.y[a] + W.x[b] + W.y[b]) && crosses_any(W.x[a], W.y[a], W.x[b], W.y[b])){ W.scut[s] = 1; ncut++; }
    }
    /* the start's node, then everything joined to it (seen), and what of that joins it without a cut (reach) */
    int seed = -1; float best = 1e30f;
    for (int g = 0; g < n; g++){ float d = hypotf(W.x[g] - START_X, W.y[g] - START_Y); if (d < best){ best = d; seed = g; } }
    for (int pass = 0; pass < 2; pass++){
        memset(deg, 0, sizeof(int) * ((size_t)n + 1));
        for (int s = 0; s < W.nseg; s++) if (pass == 0 || !W.scut[s]){ deg[W.sa[s] + 1]++; deg[W.sb[s] + 1]++; }
        for (int g = 0; g < n; g++) deg[g + 1] += deg[g];
        int *fill = malloc(sizeof(int) * (size_t)n);
        if (!fill){ LOGI("Walls: out of memory "); break; }
        memcpy(fill, deg, sizeof(int) * (size_t)n);
        for (int s = 0; s < W.nseg; s++) if (pass == 0 || !W.scut[s]){ adj[fill[W.sa[s]]++] = W.sb[s]; adj[fill[W.sb[s]]++] = W.sa[s]; }
        free(fill);
        uint8_t mark = pass == 0 ? 1 : 2;
        int qh = 0, qt = 0, cnt = 0;
        if (seed >= 0 && (pass == 0 || seen[seed] == 1)){ seen[seed] = mark; queue[qt++] = seed; }
        while (qh < qt){
            int u = queue[qh++]; cnt++;
            for (int i = deg[u]; i < deg[u + 1]; i++){
                int v = adj[i];
                if (pass == 0 ? !seen[v] : seen[v] == 1){ seen[v] = mark; queue[qt++] = v; }
            }
        }
        if (pass == 0) W.npast = cnt; else W.nreach = cnt;
    }
    for (int g = 0; g < n; g++) W.reach[g] = seen[g] == 2 ? W_OPEN : seen[g] == 1 ? W_PAST : W_UNKNOWN;
    int joined = W.npast; W.npast = joined - W.nreach;
    free(deg); free(adj); free(queue); free(seen);
    /* the start's roads reach most of what joins them, or something is not as this expects: left alone */
    W.ok = seed >= 0 && best < 200.f && joined > n / 2 && W.nreach * 5 > joined;
    LOGI("Walls: roads: %d nodes, %d segments (%d joints), %d barrier lines on, %d segments cut; the start reaches %d "
            "nodes, %d past the career's walls, %d off its network%s",  n, W.nseg, njoint, W.nbar, ncut, W.nreach, W.npast,
            n - joined, W.ok ? "" : ": not as expected, left as the game has it");
}

/* the network as the roads and the barriers are now (a new world, a new stage: built again) */
static void ensure(long f){
    if (!W.built || signature() != W.sig) build(f);
}

typedef struct {
    int state;            /* W_*: the place's */
    int seg; float d;     /* its nearest road segment, how far (metric) */
    int found; float qx, qy, qz, qd, hx, hy;   /* the nearest place on the career's roads WALL_GAP from a barrier, how far, the road's way */
} Where;

/* the nearest place on the career's roads to (x, y, z), at least WALL_GAP from a barrier line */
static void career_place(float x, float y, float z, Where *w){
    w->found = 0; w->qd = 1e30f;
    static const float TS[5] = { 0.f, .25f, .5f, .75f, 1.f };
    for (int s = 0; s < W.nseg; s++){
        int a = W.sa[s], b = W.sb[s];
        if (W.scut[s] || W.reach[a] != W_OPEN || W.reach[b] != W_OPEN) continue;
        float t0; float d = seg_dist(x, y, W.x[a], W.y[a], W.x[b], W.y[b], &t0);
        if (d >= w->qd) continue;                        /* (no point of it is nearer) */
        for (int k = -1; k < 5; k++){
            float t = k < 0 ? t0 : TS[k];
            float px = W.x[a] + t * (W.x[b] - W.x[a]), py = W.y[a] + t * (W.y[b] - W.y[a]), pz = W.z[a] + t * (W.z[b] - W.z[a]);
            float m = metric(hypotf(px - x, py - y), pz - z);
            if (m >= w->qd || bar_dist(px, py) < WALL_GAP) continue;
            float hx = W.x[b] - W.x[a], hy = W.y[b] - W.y[a], hl = hypotf(hx, hy);
            if (hl < 1e-3f) continue;
            w->found = 1; w->qd = m; w->qx = px; w->qy = py; w->qz = pz; w->hx = hx / hl; w->hy = hy / hl;
        }
    }
}

static void where(float x, float y, float z, Where *w){
    memset(w, 0, sizeof *w); w->seg = -1; w->d = 1e30f; w->state = W_UNKNOWN;
    for (int s = 0; s < W.nseg; s++){
        int a = W.sa[s], b = W.sb[s]; float t;
        float d2 = seg_dist(x, y, W.x[a], W.y[a], W.x[b], W.y[b], &t);
        if (d2 > w->d) continue;
        float m = metric(d2, W.z[a] + t * (W.z[b] - W.z[a]) - z);
        if (m < w->d){ w->d = m; w->seg = s; }
    }
    if (w->seg < 0) return;
    int s = w->seg, a = W.sa[s], b = W.sb[s];
    if (W.reach[a] == W_UNKNOWN && W.reach[b] == W_UNKNOWN) return;
    if (!W.scut[s] && W.reach[a] == W_OPEN && W.reach[b] == W_OPEN){ w->state = W_OPEN; return; }
    if (W.scut[s] && (W.reach[a] == W_OPEN || W.reach[b] == W_OPEN)){
        /* the segment a barrier line cuts: on its career end's side of that line, the place is on the career's roads */
        int e = W.reach[a] == W_OPEN ? a : b, i = crosses_any(x, y, W.x[e], W.y[e]);
        if (!i){ w->state = W_OPEN; return; }
    }
    /* past the walls, unless one of the career's roads is about as near, at its height, and on its side of them
     * (a road of the career's beside it, or over it with the car on that) */
    career_place(x, y, z, w);
    if (!w->found){ w->state = W_OPEN; return; }       /* (nowhere to go: left as it is) */
    w->state = w->qd > w->d + NEAR_MORE || fabsf(w->qz - z) > HEIGHT_SLACK + 2.f || crosses_any(x, y, w->qx, w->qy) ? W_PAST : W_OPEN;
}

static uint16_t heading_of(float hx, float hy){
    float a = atan2f(hy, hx);
    if (a < 0.f) a += 6.28318531f;
    return (uint16_t)(int)(a * (65536.f / 6.28318531f));
}

static const char *const WHY[] = { "", "free roam began there", "after a reset" };
enum { WHY_ENTRY = 1, WHY_RESET };

/* the car past the walls: to the nearest place on the career's roads (1: it goes) */
static int check(long f, uint32_t car, int why){
    ensure(f);
    if (!W.ok) return 0;
    float x = rdf_(car + CAR_PLACE), y = rdf_(car + CAR_PLACE + 4u), z = rdf_(car + CAR_PLACE + 8u);
    if (!isfinite(x + y + z) || fabsf(x) > PLACE_MAX || fabsf(y) > PLACE_MAX) return 0;   /* (car_rescue's) */
    Where w; where(x, y, z, &w);
    if (w.state != W_PAST) return 0;
    /* facing along the road, away from where the car was (the wall is that way) */
    float hx = w.hx, hy = w.hy;
    if ((w.qx - x) * hx + (w.qy - y) * hy < 0.f){ hx = -hx; hy = -hy; }
    uint16_t h = heading_of(hx, hy);
    wrf(G_HOTPOS, w.qx); wrf(G_HOTPOS + 4u, w.qy); wrf(G_HOTPOS + 8u, w.qz + 1.f);
    MEM16(G_HOTPOS_ANGLE) = h; MEM8(G_HOTPOS_VALID) = 1; MEM8(G_HOTPOS_JUMP) = 1;
        S.jumped_at = f;
    if (++S.jumps <= 20)
        LOGI("Walls: my car is past the career's walls (at %.1f %.1f %.1f, %s): back to %.1f %.1f %.1f, heading %04x, "
                "%.0f m away",  (double)x, (double)y, (double)z, WHY[why], (double)w.qx, (double)w.qy, (double)w.qz, h, (double)w.qd);
    return 1;
}


/* each frame (after Present) */
void nfs_walls_frame(void) {
    static long f;
    f++;
    uint32_t car = 0;
    if (rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u && rd(G_RACEPARAMS) == 4000u) car = player_car();
    if (!car) { S.in = 0; return; }
    if (!S.in) {
        memset(&S, 0, sizeof S); S.in = nfs_garage_has_career_car() ? 1 : 2; S.since = f; S.jumped_at = -100000; S.stamp = MEM32(car + CAR_RESET_T);
    }
    if (S.in != 1) return;
    if (MEM8(G_HOTPOS_JUMP)) { S.stamp = MEM32(car + CAR_RESET_T); return; }
    float wdt = rdf_(G_WORLDDT);
    int running = isfinite(wdt) && wdt > 0.f, why = 0;
    if (!S.entry_done && running && f - S.since >= ENTRY_FRAMES) { S.entry_done = 1; why = WHY_ENTRY; }
    uint32_t st = MEM32(car + CAR_RESET_T);
    if (st && st != S.stamp) why = WHY_RESET;
    S.stamp = st;
    if (why && f - S.jumped_at >= JUMP_PAUSE) check(f, car, why);
}
#endif
