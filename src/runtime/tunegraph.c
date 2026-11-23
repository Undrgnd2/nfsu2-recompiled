/*
 * tunegraph.c - the ECU and turbo tuning graphs (Performance Tuning / Dyno) take a drag (Android).
 *
 * Adapted from nfsu2-web's runtime/tunegraph.c (MIT, Copyright (c) 2026 th0mk; the notes on the game are there):
 * the tuning screen (UI_PerformanceTuning_Graph.fng, its messages 0x5654D0) has the torque graph (vtable 0x79BEB8) at
 * +0x50, a column per 1000 rpm; the pointer only picks a column and its wheel moves the dot, so a tap picked one and
 * nothing moved it. Here a press on the graph keeps the column it picked until the finger goes up, and every 8 units
 * (640x480) the finger goes up or down is an Up or a Down for the screen, as a key would be. The game's mouse:
 * [0x8763D0] +4 x, +8 y, +0x21 the left button down, +0x22 it went down (+3 each with the buttons swapped).
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);
uint32_t nfs_guest_call(uint32_t va, uint32_t ecx, const uint32_t *args, unsigned count, unsigned cleanup);
void sub_005654D0_orig(void);

#define FN_TUNE_MSG  0x005654d0u   /* thiscall (msg, obj, a3, a4), ret 0x10 */
#define VT_TORQUE    0x0079beb8u
#define MSG_MOUSE    0x9803f6e2u
#define MSG_UP       0x72619778u
#define MSG_DOWN     0x911c0a4bu
#define SC_GRAPH     0x50u
#define G_RING       0x20u
#define G_SEL        0x28u
#define P_VALUE      0x0cu
#define P_X          0x38u
#define P_Y          0x3cu
#define P_W          0x40u
#define P_H          0x44u
#define P_NUM        0x48u
#define MOUSE        0x008763d0u
#define STEP_UNITS   8

static uint32_t g_drag;
static int g_inner;
static int g_ref, g_steps;

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static float rdf(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static int pointer_y(void) { return (int32_t)MEM32(MOUSE + 8u); }

static int on_graph(uint32_t g) {
    float x = (float)(int32_t)MEM32(MOUSE + 4u) - 320.f, y = (float)pointer_y() - 240.f;
    uint32_t head = g + G_RING;
    int n = 0;
    for (uint32_t p = MEM32(head); p && p != head && ok(p, P_NUM + 4u) && n < 64; p = MEM32(p), n++)
        if (x >= rdf(p + P_X) && x <= rdf(p + P_X) + rdf(p + P_W) && y <= rdf(p + P_Y) && y >= rdf(p + P_Y) - rdf(p + P_H)) return 1;
    return 0;
}
static void follow(uint32_t self, uint32_t p) {
    int y = pointer_y();
    while (g_ref - y >= STEP_UNITS || y - g_ref >= STEP_UNITS) {
        int up = g_ref > y;
        uint32_t a[4] = { up ? MSG_UP : MSG_DOWN, 0, 0, 0 };
        float v = rdf(p + P_VALUE);
        nfs_guest_call(FN_TUNE_MSG, self, a, 4, 0);
        if (rdf(p + P_VALUE) == v) { g_ref = y; break; }
        g_ref += up ? -STEP_UNITS : STEP_UNITS;
        g_steps += up ? 1 : -1;
    }
}

/* 0x5654D0 (thiscall msg, obj, a3, a4; ret 0x10): the tuning screen's messages */
void nfs_tune_ov_msg(void) {
    uint32_t self = g_ecx, g = ok(self + SC_GRAPH, 4) ? MEM32(self + SC_GRAPH) : 0;
    if (g_inner || MEM32(g_esp + 4u) != MSG_MOUSE || !ok(g, 0x30) || MEM32(g) != VT_TORQUE) { sub_005654D0_orig(); return; }
    uint32_t b = MOUSE + (MEM8(MOUSE + 0x20u) ? 0x24u : 0x21u);
    uint32_t p = MEM32(g + G_SEL);
    if (p && !ok(p, P_NUM + 4u)) p = 0;
    if (g_drag && (g_drag != self || !MEM8(b) || !p)) {
        if (g_steps && p) LOGI("Tune: column %u: %+d steps by a drag", MEM32(p + P_NUM), g_steps);
        g_drag = 0;
    }
    if (g_drag) follow(self, p);
    else {
        uint32_t a[4] = { MEM32(g_esp + 4u), MEM32(g_esp + 8u), MEM32(g_esp + 12u), MEM32(g_esp + 16u) };
        g_inner = 1; nfs_guest_call(FN_TUNE_MSG, self, a, 4, 0); g_inner = 0;
        if (MEM8(b + 1u) && MEM32(g + G_SEL) && on_graph(g)) { g_drag = self; g_ref = pointer_y(); g_steps = 0; }
    }
    g_esp += 4u + 0x10u;
}
#endif
