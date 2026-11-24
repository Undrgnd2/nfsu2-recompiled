/*
 * plates.c - the player's car's license plate with the player's own text (Android, the ⚙ menu's "Plaque").
 *
 * Adapted from nfsu2-web's runtime/plates.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there), for the
 * player's own car alone (no friends' cars here): up to 8 capitals and digits, none the game's own plate.
 * The game's plate is one texture, LICENSE_PLATE (128x64 DXT1), named in each car model's texture table (the solid's
 * +0x19 count, +0x2C table of {name hash, TextureInfo}), every car of a model sharing it. So while the player's car is
 * drawn (photo.c's wraps of 0x620AE0 and 0x6162E0: nfs_plates_begin / nfs_plates_end), each model it puts into the
 * render lists (0x5C5930, wrapped here) has its LICENSE_PLATE entry pointed at our plate's TextureInfo, and back right
 * after: the render lists keep the TextureInfo they were handed, so the next car of the model draws with its own.
 * Our plate: a TextureInfo and platform record copied from the game's LICENSE_PLATE, pixels of our own (plate_image.c,
 * A8R8G8B8, from the text and the game's plate) and the game's D3D texture made from them (0x5CE170). A new text: the
 * old texture let go (off the game's list of textures, Released, freed) and a new one made. Where: free roam and races
 * (not a LAN or Online game: every car there wears the game's plate), and the front end's screens about our car (the
 * garage, the shops), never the car lot's or a car select's.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include "plate_image.h"
#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_005C5930_orig(void);

#define G_GAMEMODE      0x00850078u
#define G_FLOW          0x008654a4u
#define G_PLAYER1       0x008900acu
#define G_FENG          0x008384c4u
#define FN_NEW          0x00575620u
#define FN_FREE         0x00575630u
#define FN_TEXINFO      0x004901d0u   /* cdecl (hash, default, 0) -> the TextureInfo loaded of that name */
#define FN_TEX_CREATE   0x005ce170u   /* thiscall TextureInfo: its D3D texture from +0x70's pixels */
#define PLATE_HASH      0x029a22f7u   /* bStringHash("LICENSE_PLATE") */
#define TI_SZ           0x7cu
#define TI_NAME         0x08u
#define TI_HASH         0x24u
#define TI_BYTES        0x38u
#define TI_PAL          0x3cu
#define TI_LEVEL0       0x40u
#define TI_W            0x44u
#define TI_H            0x46u
#define TI_LOG_W        0x48u
#define TI_LOG_H        0x49u
#define TI_KIND         0x4au
#define TI_LEVELS       0x4eu
#define TI_SELF         0x6cu
#define TI_PIXELS       0x70u
#define TI_PALETTE      0x74u
#define KIND_ARGB       0x20u
#define PLAT_SZ         0x20u
#define PLAT_FORMAT     0x14u
#define PLAT_TEX        0x18u
#define PLAT_LIST       0x1cu
#define FMT_DXT1        0x31545844u
#define FMT_ARGB        21u
#define SOLID_COUNT     0x19u
#define SOLID_TABLE     0x2cu
#define RETRY_FRAMES    60

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static char s_want[PLATE_TEXT_MAX + 1];      /* the menu's text ("" the game's plate) */
static char s_text[PLATE_TEXT_MAX + 1];      /* what our texture shows */
static uint32_t s_ti;                        /* our TextureInfo (0: none), its platform record and pixels after it */
static int s_ready, s_fe_ours;
static long s_frame, s_retry;
static uint32_t s_drawing;                   /* the TextureInfo of the car being drawn (0: the game's plate) */
static uint8_t s_stock[PLATE_W * PLATE_H * 4], s_px[PLATE_BYTES];
static uint32_t s_stock_px;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n ? n : 1u); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }

/* the menu's text (any thread): capitals and digits, 8 at most; anything else, or "", the game's plate */
void nfs_plates_set(const char *text) {
    char t[PLATE_TEXT_MAX + 1] = { 0 };
    size_t n = text ? strlen(text) : 0;
    int good = n <= PLATE_TEXT_MAX;
    for (size_t i = 0; good && i < n; i++) good = (text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= '0' && text[i] <= '9');
    if (good && n) memcpy(t, text, n);
    pthread_mutex_lock(&s_lock);
    memcpy(s_want, t, sizeof t);
    pthread_mutex_unlock(&s_lock);
}

/* ---- the front end: our car on the garage's and the shops' screens ---- */
static void read_str(uint32_t va, char *out, size_t n) {
    size_t i = 0;
    for (; va && i + 1 < n && ok(va + (uint32_t)i, 1) && MEM8(va + (uint32_t)i); i++) out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}
static int in(const char *s, const char *const *list) {
    for (int i = 0; list[i]; i++) if (!strncasecmp(s, list[i], strlen(list[i]))) return 1;
    return 0;
}
static void fe_follow(void) {
    static const char *const ours[] = { "UI_CareerCrib", "UI_CareerCarSelect", "UI_Choose", "UI_CHoose",
        "UI_PC_Customize", "UI_Paint", "UI_Decal", "UI_Custom", "UI_Rims", "UI_GenericParts", "UI_ICE",
        "UI_Performance", "UI_BuyPerformance", "UI_ShowCase", "UI_Showcase", "UI_Magazine", 0 };
    static const char *const others[] = { "UI_CareerCarLot", "CarLot", "UI_OLCarLot", "UI_QR", "UI_OL",
        "UI_StartCareer", "UI_Main", "MC_", "UI_Options", "UI_PC_LAN", "PC_OL", 0 };
    static uint32_t flow;
    if (rd(G_FLOW) != flow) { flow = rd(G_FLOW); s_fe_ours = 0; }
    if (flow != 3u) return;
    uint32_t fe = rd(G_FENG), mg = fe ? rd(fe + 8u) : 0, top = mg ? rd(mg + 0xe4u) : 0;
    char b[48]; read_str(top ? rd(top + 0xcu) : 0, b, sizeof b);
    if (b[0] && in(b, others)) s_fe_ours = 0;
    else if (b[0] && in(b, ours)) s_fe_ours = 1;
}

/* ---- the texture ---- */
static const uint8_t *stock(uint32_t src) {
    uint32_t px = rd(src + TI_PIXELS), plat = rd(src);
    if (!ok(px, PLATE_W * PLATE_H / 2u) || MEM16(src + TI_W) != PLATE_W || MEM16(src + TI_H) != PLATE_H
        || !ok(plat, PLAT_SZ) || MEM32(plat + PLAT_FORMAT) != FMT_DXT1) return 0;
    if (px != s_stock_px) { plate_dxt1((const uint8_t *)ADDR(px), PLATE_W, PLATE_H, s_stock); s_stock_px = px; }
    return s_stock;
}
/* our texture let go: off the game's list of textures (0x5CE170 put it there: +0 next, +4 before), Released, freed */
static void drop(void) {
    uint32_t pl = s_ti ? rd(s_ti) : 0, tex = pl ? rd(pl + PLAT_TEX) : 0;
    if (pl && ok(rd(pl), 8) && ok(rd(pl + 4u), 8)) {
        uint32_t next = MEM32(pl), before = MEM32(pl + 4u);
        MEM32(before) = next; MEM32(next + 4u) = before;
        MEM32(pl) = MEM32(pl + 4u) = 0;
    }
    for (int n = 0; tex && n < 8; n++) {
        uint32_t release = rd(rd(tex) + 8u);
        if (!release || !nfs_guest_call(release, 0, &tex, 1, 0)) break;
    }
    if (pl) MEM32(pl + PLAT_TEX) = 0;
    if (s_ti) nfs_guest_call(FN_FREE, 0, &s_ti, 1, 4);
    s_ti = 0; s_ready = 0; s_text[0] = 0;
}
static int make(const char *text) {
    uint32_t a[3] = { PLATE_HASH, 0, 0 }, src = nfs_guest_call(FN_TEXINFO, 0, a, 3, 12);
    if (!ok(src, TI_SZ) || !ok(rd(src), PLAT_SZ)) return 0;                        /* not loaded (yet) */
    if (!plate_image(text, strlen(text), stock(src), s_px)) return 0;
    uint32_t size = TI_SZ + PLAT_SZ + 64u + PLATE_BYTES, ti = nfs_guest_call(FN_NEW, 0, &size, 1, 4);
    if (!ok(ti, size)) return 0;
    uint32_t pl = ti + TI_SZ, pix = (pl + PLAT_SZ + 63u) & ~63u;
    memcpy((void *)ADDR(ti), (const void *)ADDR(src), TI_SZ); memcpy((void *)ADDR(pl), (const void *)ADDR(MEM32(src)), PLAT_SZ);
    MEM32(pl) = MEM32(pl + 4u) = 0;
    MEM32(pl + PLAT_TEX) = MEM32(pl + PLAT_LIST) = 0;
    MEM32(pl + PLAT_FORMAT) = FMT_ARGB;
    static const char nm[] = "PLATE_00";
    for (uint32_t q = 0; q < 24u; q++) MEM8(ti + TI_NAME + q) = q < sizeof nm - 1 ? (uint8_t)nm[q] : 0;
    MEM32(ti) = pl; MEM32(ti + TI_SELF) = ti; MEM32(ti + TI_PIXELS) = pix; MEM32(ti + TI_PALETTE) = 0;
    MEM32(ti + TI_HASH) = 0x504c0000u;                                            /* "PL": no name the game has */
    MEM32(ti + TI_BYTES) = PLATE_BYTES; MEM32(ti + TI_PAL) = 0; MEM32(ti + TI_LEVEL0) = PLATE_W * PLATE_H * 4;
    MEM16(ti + TI_W) = PLATE_W; MEM16(ti + TI_H) = PLATE_H;
    MEM8(ti + TI_LOG_W) = 7; MEM8(ti + TI_LOG_H) = 6;
    MEM8(ti + TI_KIND) = KIND_ARGB; MEM8(ti + TI_LEVELS) = PLATE_MIPS;
    memcpy((void *)ADDR(pix), s_px, PLATE_BYTES);
    s_ti = ti;
    nfs_guest_call(FN_TEX_CREATE, ti, 0, 0, 0);
    if (!rd(pl + PLAT_TEX)) { drop(); return 0; }
    memcpy(s_text, text, strlen(text) + 1);
    s_ready = 1;
    return 1;
}

/* guest thread, after each Present: our texture as the menu's text wants it */
void nfs_plates_frame(void) {
    s_frame++;
    fe_follow();
    char want[PLATE_TEXT_MAX + 1];
    pthread_mutex_lock(&s_lock); memcpy(want, s_want, sizeof want); pthread_mutex_unlock(&s_lock);
    /* (the game lets every texture of its list go when its device is lost: ours made again) */
    if (s_ti && (strcmp(want, s_text) || !rd(rd(s_ti) + PLAT_TEX))) {
        LOGI("Plates: %s let go", s_text[0] ? s_text : "a plate");
        drop();
    }
    if (!want[0] || s_ti || s_frame < s_retry) return;
    if (make(want)) LOGI("Plates: ours: %s", want);
    else s_retry = s_frame + RETRY_FRAMES;
}

/* ---- the drawing ---- */
static int ours(uint32_t car) {
    if (rd(G_GAMEMODE) & 0x208u) return 0;                     /* a LAN or Online game: the game's plates */
    uint32_t flow = rd(G_FLOW), pl = rd(G_PLAYER1);
    if (flow == 3u) return s_fe_ours;                          /* the front end's one car */
    return flow == 6u && pl && car == rd(pl + 4u);
}
/* photo.c's wraps of 0x620AE0 and 0x6162E0: the car of render info ri is drawn now; what was before, back after */
uint32_t nfs_plates_begin(uint32_t ri) {
    uint32_t before = s_drawing, car = ok(ri, 4) ? MEM32(ri) : 0;   /* (a front-end car has none) */
    s_drawing = s_ready && (car || rd(G_FLOW) == 3u) && ours(car) ? s_ti : 0;
    return before;
}
void nfs_plates_end(uint32_t before) { s_drawing = before; }
/* 0x5C5930 (thiscall view: the model's render item, ...): a model into the render lists, which keep its textures */
void nfs_plates_ov_model(void) {
    uint32_t keep = 0, was = 0, item = MEM32(g_esp + 4u);
    if (s_drawing && ok(item, 0x10u)) {
        uint32_t solid = MEM32(item + 0xcu), n = ok(solid, SOLID_TABLE + 4u) ? MEM8(solid + SOLID_COUNT) : 0;
        uint32_t table = n ? MEM32(solid + SOLID_TABLE) : 0;
        if (n && ok(table, n * 8u))
            for (uint32_t k = 0; k < n; k++)
                if (MEM32(table + 8u * k) == PLATE_HASH) { keep = table + 8u * k + 4u; was = MEM32(keep); MEM32(keep) = s_drawing; break; }
    }
    sub_005C5930_orig();
    if (keep) MEM32(keep) = was;
}
#endif
