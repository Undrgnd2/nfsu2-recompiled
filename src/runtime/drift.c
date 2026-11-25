/*
 * drift.c - drift mode in free roam (Android): the DRIFT touch button turns
 * the game's own drift-event handling, drift camera and drift scoring on
 * while the player drives around the city, and off again.
 *
 * Adapted from nfsu2-web's runtime/drift.c (MIT, Copyright (c) 2026 th0mk;
 * see that file for the full reverse-engineering notes).  The same SPEED2.EXE
 * addresses; what differs is the host: guest calls go through callGuest
 * (nfs_guest_call), the overrides are wraps (config/wraps.txt) and the score
 * is drawn by the touch controls (nfs_drift_status) instead of the web page.
 *
 *  - the switch: [0x890104], the developers' "is this a drift event?" dword
 *    that nothing in the game sets; with it on the car takes its drift-event
 *    physics branches and the race coordinator runs the drift tick;
 *  - free roam lacks the drift event's styles in its DriftScore and a route:
 *    both added while the switch is on (add_styles, no_route);
 *  - the car's physics are made again from the drift parameter set and the
 *    career car's Drift dyno setup (physics_in), and back after (physics_out);
 *    the nitrous is kept as it was (a drift event has none);
 *  - the drift camera (view 4) while on, the player's view back after;
 *  - three drift-event routines that free roam's HUD cannot take are skipped
 *    while the switch is ours (nfs_drift_ov_*), and the music keeps playing
 *    (nfs_drift_hook).
 * Only in free roam itself: not in a race, a career race, Online or LAN.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

/* guest_runtime.cpp */
int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
/* the lifted originals of the wrapped functions (config/wraps.txt) */
void sub_0056DDD0_orig(void);
void sub_004C85D0_orig(void);
void sub_004AC120_orig(void);

#define G_FLOW          0x008654a4u   /* 6 in the world */
#define G_GAMEMODE      0x00850078u   /* 1 career, 8 Online, 0x200 LAN */
#define G_RACETYPE      0x0089e7b0u   /* 0 free roam */
#define G_DRIFTRACE     0x0089e7d9u   /* byte: a drift event */
#define G_RACE          0x00890118u   /* +0 state (4 racing, 6 paused), +0x24 racers, +0xBD0 racer entries */
#define G_CAREER_RACE   0x00863470u
#define G_CAREER_CAR    0x00863480u
#define G_FENG          0x008384c4u
#define G_PSETTINGS     0x0083aa64u
#define PS_SIZE         0x28u
#define PS_VIEW         0x10u
#define G_FECARS        0x0083ad90u
#define FN_FE_HIDE      0x0050ca00u   /* cdecl (FE object, 0) */
#define G_PLAYER1       0x008900acu   /* Player*: +4 its car, +0x10 its number, +0x9A4 its HUD */
#define G_FORCEDRIFT    0x00890104u
#define G_STYLES        0x00864624u
#define G_STYLEPOOL     0x00864628u
#define FN_POOL_ALLOC   0x00440e50u   /* cdecl (pool) */
#define FN_CAR_TUNED    0x005f6c20u   /* thiscall car (1) */
#define FN_STYLE_INIT   0x0056d650u   /* thiscall style */
#define FN_CAMERA_KEY   0x005f2fa0u   /* thiscall Player (step) */
#define HUD_STYLES      0x4cu
#define HUD_DRIFT       0x2cu
#define N_STYLES        39u
#define MODE_DRIFT      4u
#define RC_DRIFTSCORE   0x1d4u
#define RC_DRIFTCAR     0x1d8u
#define DC_CAR          0x08u
#define DC_LEVEL        0x1cu
#define DC_GAIN         0x20u
#define DC_BEST         0x30u
#define ST_STATE        0x14u
#define ST_PEND         0x24u
#define ST_TOTAL        0x2cu
#define PL_HUD          0x9a4u
#define VIEW_DRIFT      4u
#define VIEW_LAST       4u
#define CAR_RIDE        0x10u
#define CAR_PHYSOBJ     0x34u
#define WH_GRIP         0x188u
#define CAR_PHYS        0x20u
#define PH_NOS_LEN      0x2a0u
#define CAR_NOS_STATE   0x454u
#define RIDE_TUNING     0x398u
#define REC_SETUPS      0x360u
#define SETUP_SZ        0x29u
#define SETUP_DRIFT     2u
#define C_TABLE         0x9bd8u       /* the front-end cars' 91 records */
#define TABLE_N         91u
#define R_HANDLE        0x08u
#define NOTE_FRAMES     180

static struct {
    int want, on, cam, note;
    uint32_t saved, style;
    float last;
    double total, best;
    uint32_t tuned; int swapped;
    uint8_t kept[SETUP_SZ];
} D;
static atomic_int s_presses;
static atomic_int s_status[7];

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static float rdf(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}
static uint32_t call0(uint32_t fn, uint32_t self) { return nfs_guest_call(fn, self, 0, 0, 0); }
static uint32_t call1(uint32_t fn, uint32_t self, uint32_t a, unsigned cleanup) { return nfs_guest_call(fn, self, &a, 1, cleanup); }

static uint32_t race(void) { uint32_t r = rd(G_RACE); return ok(r, 0xbd4) ? r : 0; }
static uint32_t racer(void) { uint32_t r = race(), p = r ? rd(r + 0xbd0u) : 0; return ok(p, RC_DRIFTCAR + 4u) ? p : 0; }
static int roam_world(void) {
    uint32_t r = race(), pl = rd(G_PLAYER1);
    return MEM32(G_FLOW) == 6u && r && MEM32(r + 0x24u) == 1u && (MEM32(r) == 4u || MEM32(r) == 6u) && !MEM32(G_CAREER_RACE) &&
           MEM32(G_RACETYPE) == 0u && !(MEM32(G_GAMEMODE) & 0x208u) && !MEM8(G_DRIFTRACE) && pl && rd(pl + 4u);
}
/* the HUD is the top package: no menu, map, message or race card over it */
static int hud_on_top(void) {
    uint32_t pl = rd(G_PLAYER1), hud = pl ? rd(pl + PL_HUD) : 0, fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0;
    char a[40], b[40];
    read_str(top ? rd(top + 0xcu) : 0, a, sizeof a); read_str(hud ? rd(hud + 0x10u) : 0, b, sizeof b);
    return a[0] && !strcasecmp(a, b);
}
static uint32_t view_addr(void) {
    uint32_t pl = rd(G_PLAYER1), n = pl ? rd(pl + 0x10u) : 9u;
    return n < 2u ? G_PSETTINGS + PS_SIZE * n + PS_VIEW : 0;
}
static uint32_t garage_find(uint32_t db, uint32_t h) {
    if (!h) return 0;
    for (uint32_t i = 0; i < TABLE_N; i++) { uint32_t r = rd(db + C_TABLE + 4u * i); if (ok(r, R_HANDLE + 4u) && MEM32(r + R_HANDLE) == h) return r; }
    return 0;
}

static void add_styles(uint32_t rc) {
    uint32_t ds = rd(rc + RC_DRIFTSCORE), tab = MEM32(G_STYLES), pool = MEM32(G_STYLEPOOL);
    if (!ok(ds, 4u * N_STYLES) || !ok(tab, 0x30u * N_STYLES) || !pool) return;
    int added = 0;
    for (uint32_t i = 0; i < N_STYLES; i++) {
        if (!(MEM8(tab + 0x30u * i + 7u) & MODE_DRIFT) || MEM32(ds + 4u * i)) continue;
        uint32_t s = call1(FN_POOL_ALLOC, 0, pool, 4);
        if (!ok(s, 0x3c)) { LOGI("Drift: no style from the pool (entry %u)", i); return; }
        MEM32(s + 0x34u) = 0; MEM32(s + 0x38u) = 0; MEM8(s + 0x10u) = 0; MEM8(s + 0x18u) = 1;
        MEM32(s + 0xcu) = i; MEM32(s + 8u) = tab + 0x30u * i;
        call0(FN_STYLE_INIT, s);
        MEM32(ds + 4u * i) = s;
        MEM8(s + 0x10u) = 1;
        added++;
    }
    if (added) LOGI("Drift: the drift event's styles: %d added to free roam's DriftScore %08x", added, ds);
}
static void no_route(uint32_t rc) {
    uint32_t dc = rd(rc + RC_DRIFTCAR), car = ok(dc, DC_BEST + 4u) ? MEM32(dc + DC_CAR) : 0, rt = car ? rd(car + 0x1cu) : 0;
    if (!ok(rt, 0x18)) return;
    float d = rdf(rt + 0x14u);
    if (rdf(dc + DC_BEST) > d) wrf(dc + DC_BEST, d);
}
static void count(uint32_t rc) {
    uint32_t ds = rd(rc + RC_DRIFTSCORE), st = ok(ds, 4) ? MEM32(ds) : 0;
    if (!ok(st, ST_TOTAL + 4u)) return;
    float tot = rdf(st + ST_TOTAL);
    if (st != D.style) { D.style = st; D.last = tot; return; }
    if (tot >= D.last + 1.f) {
        double got = (double)(int)(tot - D.last);
        D.total += got;
        if (got > D.best) D.best = got;
        LOGI("Drift: a drift of %.0f (total %.0f, best %.0f)", got, D.total, D.best);
    }
    D.last = tot;
}
/* free roam's style popup stays up while the switch is on: kept hidden */
static void hide_popup(void) {
    uint32_t pl = rd(G_PLAYER1), hud = pl ? rd(pl + PL_HUD) : 0, el = hud ? rd(hud + HUD_STYLES) : 0;
    if (!ok(el, 0x3c)) return;
    for (uint32_t o = 0x34u; o <= 0x38u; o += 4u) {
        uint32_t obj = MEM32(el + o);
        if (ok(obj, 0x20) && !(MEM32(obj + 0x1cu) & 1u)) call1(FN_FE_HIDE, 0, obj, 4);
    }
}
static void camera_on(void) {
    uint32_t va = view_addr(), pl = rd(G_PLAYER1);
    if (!va || !pl) return;
    uint32_t v = MEM32(va);
    if (v != VIEW_DRIFT) {
        D.saved = v;
        MEM32(va) = VIEW_DRIFT - 1u;
        call1(FN_CAMERA_KEY, pl, 1, 0);
    } else D.saved = VIEW_DRIFT;
    D.cam = MEM32(va) == VIEW_DRIFT;
    if (!D.cam) { MEM32(va) = v; LOGI("Drift: no drift camera (view %u)", v); }
}
static void camera_off(int live) {
    uint32_t va = view_addr(), pl = rd(G_PLAYER1), back = D.saved <= VIEW_LAST ? D.saved : 3u;
    D.cam = 0;
    if (!va || MEM32(va) != VIEW_DRIFT || MEM8(G_DRIFTRACE) || back == VIEW_DRIFT) return;
    if (live && pl) { MEM32(va) = back - 1u; call1(FN_CAMERA_KEY, pl, 1, 0); }
    else MEM32(va) = back;
}
/* the car made again (0x5F6C20) with its nitrous as it was */
static void car_tuned(uint32_t car) {
    uint32_t ph = rd(car + CAR_PHYS), nos[4] = {0, 0, 0, 0};
    int keep = ok(ph, PH_NOS_LEN + 4u) && ok(car + CAR_NOS_STATE, 16u);
    float len = keep ? rdf(ph + PH_NOS_LEN) : 0.f;
    for (uint32_t i = 0; keep && i < 4u; i++) nos[i] = MEM32(car + CAR_NOS_STATE + 4u * i);
    call1(FN_CAR_TUNED, car, 1, 0);
    if (!keep || rd(car + CAR_PHYS) != ph) return;
    wrf(ph + PH_NOS_LEN, len);
    for (uint32_t i = 0; i < 4u; i++) MEM32(car + CAR_NOS_STATE + 4u * i) = nos[i];
}
static void physics_in(void) {
    uint32_t car = rd(rd(G_PLAYER1) + 4u), ride = car ? rd(car + CAR_RIDE) : 0,
             rec = MEM32(G_GAMEMODE) == 1u ? garage_find(G_FECARS, MEM32(G_CAREER_CAR)) : 0;
    if (!ok(ride, RIDE_TUNING + SETUP_SZ)) return;
    uint32_t setup = ok(rec, REC_SETUPS + 5u * SETUP_SZ) ? rec + REC_SETUPS + SETUP_DRIFT * SETUP_SZ : 0;
    int differ = 0;
    for (uint32_t i = 0; setup && i < SETUP_SZ; i++) differ += MEM8(setup + i) != MEM8(ride + RIDE_TUNING + i);
    if (differ) {
        for (uint32_t i = 0; i < SETUP_SZ; i++) { D.kept[i] = MEM8(ride + RIDE_TUNING + i); MEM8(ride + RIDE_TUNING + i) = MEM8(setup + i); }
        D.swapped = 1;
    }
    D.tuned = car;
    car_tuned(car);
    LOGI("Drift: the car's physics for a drift event%s (%d of %u values from the Drift setup), nitrous kept",
         setup ? "" : ", no Drift setup of its own", differ, SETUP_SZ);
}
static void physics_out(void) {
    uint32_t car = D.tuned, ride = car ? rd(car + CAR_RIDE) : 0;
    int same = car && rd(rd(G_PLAYER1) + 4u) == car && ok(ride, RIDE_TUNING + SETUP_SZ);
    if (same) {   /* the drift assist leaves the rear wheels' grip factor changed */
        uint32_t po = rd(car + CAR_PHYSOBJ);
        for (uint32_t o = 0x40u; ok(po, 0x48) && o <= 0x44u; o += 4u) {
            uint32_t w = MEM32(po + o);
            if (ok(w, WH_GRIP + 4u)) wrf(w + WH_GRIP, 1.f);
        }
    }
    if (same && D.swapped) for (uint32_t i = 0; i < SETUP_SZ; i++) MEM8(ride + RIDE_TUNING + i) = D.kept[i];
    D.tuned = 0; D.swapped = 0;
    if (!car) return;
    if (!same || MEM32(G_FLOW) != 6u) { LOGI("Drift: the car is gone, no physics to give back"); return; }
    car_tuned(car);
    LOGI("Drift: the car's own physics back");
}
static void publish(void) {
    uint32_t dc = 0, st = 0, rc = D.on ? racer() : 0, ds = rc ? rd(rc + RC_DRIFTSCORE) : 0;
    if (rc) dc = rd(rc + RC_DRIFTCAR);
    if (ok(ds, 4)) st = MEM32(ds);
    if (!ok(st, ST_TOTAL + 4u)) st = 0;
    int lvl = ok(dc, DC_GAIN + 4u) ? (int)MEM32(dc + DC_LEVEL) : 1;
    if (lvl < 1 || lvl > 5) lvl = 1;
    float pend = st ? rdf(st + ST_PEND) : 0.f;
    int sst = st ? (int)MEM32(st + ST_STATE) : 2;
    atomic_store(&s_status[0], D.want | (D.on ? 2 : 0) | (D.note > 0 ? 4 : 0));
    atomic_store(&s_status[1], lvl);
    atomic_store(&s_status[2], lvl < 5 && ok(dc, DC_GAIN + 4u) ? (int)(int32_t)MEM32(dc + DC_GAIN) : 0);
    atomic_store(&s_status[3], D.total < 2e9 ? (int)D.total : 2000000000);
    atomic_store(&s_status[4], D.best < 2e9 ? (int)D.best : 2000000000);
    atomic_store(&s_status[5], pend > 0.f && pend < 1e9f && (sst == 0 || sst == 1) ? (int)pend : 0);
    atomic_store(&s_status[6], sst);
}

void nfs_drift_press(void) { atomic_fetch_add(&s_presses, 1); }
/* the view drift mode's drift camera is in (cameras.c: not the player's own), 0 none */
uint32_t nfs_drift_camera_view(void) { return D.cam ? view_addr() : 0; }
void nfs_drift_status(int out[7]) { for (int i = 0; i < 7; i++) out[i] = atomic_load(&s_status[i]); }

/* each frame, on the guest thread (after Present) */
void nfs_drift_frame(void) {
    int world = roam_world(), top = world && hud_on_top(), drive = top && MEM32(race()) == 4u;
    int n = atomic_exchange(&s_presses, 0);
    if (D.note > 0) D.note--;
    for (int i = 0; i < n; i++) {
        if (world) { D.want = !D.want; D.note = 0; LOGI("Drift: drift mode %s", D.want ? "on" : "off"); }
        else { D.note = NOTE_FRAMES; LOGI("Drift: button outside free roam: nothing"); }
    }
    int on = D.want && drive;
    if (on != D.on) {
        D.on = on;
        MEM32(G_FORCEDRIFT) = (uint32_t)on;
        LOGI("Drift: drift handling %s%s", on ? "on" : "off",
             on ? "" : !D.want ? "" : !world ? " (not in free roam)" : !top ? " (a screen over the HUD)" : " (paused)");
        if (on) physics_in();
        else physics_out();
    }
    if (on) {
        uint32_t rc = racer();
        if (rc) { add_styles(rc); no_route(rc); count(rc); }
        hide_popup();
        if (!D.cam) camera_on();
    } else if (D.cam && (!D.want || !world)) camera_off(MEM32(G_FLOW) == 6u && hud_on_top());
    publish();
}

/* The music's tests of a drift event: none while the switch is ours (free roam's songs keep playing). */
/* Music in the drag and drift events (the ⚙ menu; nfsu2-web's race_music, off by default as the original, whose
 * silence leaves the engine to be heard for the shifts): the music manager's tick (0x482180) plays the EA Trax songs
 * in the world only when the race is no drift (0x401910) and no drag event (0x4018F0), and the in-game mix (0x471970)
 * turns the music's channel all the way down for both ([0x89E7D8] drag, [0x89E7D9] drift). With it on neither sees
 * the race's drag or drift byte, so those races play their songs from the start, as a circuit race does, at the
 * game's music volume. */
static atomic_int s_race_music;
void nfs_drift_race_music(int on) {
    if (atomic_exchange(&s_race_music, on != 0) != (on != 0)) LOGI("Drift: music in drag and drift races %s", on ? "on" : "off");
}
void nfs_drift_hook(uint32_t va) {
    int music = atomic_load(&s_race_music), mode = D.on && !MEM8(G_DRIFTRACE);
    switch (va) {
    case 0x00482279u: if (mode || music) g_eax &= ~0xffu; break;   /* music manager: test al, al (0x401910, a drift event?) */
    case 0x00482287u: if (music) g_eax &= ~0xffu; break;           /* test al, al (0x4018F0, a drag event?) */
    case 0x00471B7Au: case 0x00471B8Cu: if (music) g_eax &= ~0xffu; break;   /* in-game mix: a drag, a drift event */
    case 0x00471B95u: if (mode) g_eax = 0; break;                   /* in-game mix: test eax, eax (eax = [0x890104]) */
    }
}

/* Drift-event routines free roam cannot take, skipped while the switch is ours. */
void nfs_drift_ov_leaderboard(void) {            /* DriftManager 0x56DDD0: free roam's AI cars are no racers */
    if (!D.on || MEM8(G_DRIFTRACE)) { sub_0056DDD0_orig(); return; }
    g_esp += 4;
}
void nfs_drift_ov_style_points(void) {           /* 0x4C85D0, thiscall (1): the drift part's popup and panel */
    if (!D.on || MEM8(G_DRIFTRACE)) { sub_004C85D0_orig(); return; }
    g_esp += 8;
}
void nfs_drift_ov_message(void) {                /* FEngHud::ShowNiftyDriftyMessage 0x4AC120, thiscall (1) */
    if ((!D.on || MEM8(G_DRIFTRACE)) && rd(g_ecx + HUD_DRIFT)) { sub_004AC120_orig(); return; }
    g_esp += 8;
}
#endif
