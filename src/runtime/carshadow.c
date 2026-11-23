/*
 * carshadow.c - a car's shadow on the road no longer covers its wheels (Android).
 *
 * Adapted from nfsu2-web's runtime/carshadow.c (MIT, Copyright (c) 2026 th0mk), after TerminatorVasya's NFSU2 Shadow
 * Fix (nfsmods.xyz/mod/7328, its ShadowOffset): the car drawings 0x6162E0 and 0x620AE0 lay the shadow on the ground
 * lifted by the constant at 0x7A66B4 (.rdata, 0.04 m), which puts its edge into the bottom of each tyre. Here 0.015
 * (0: the shadow clips into the road while driving). The lifted code reads it from memory each time, so one write at
 * the first frame is all, on the data of the exe this port is made for only.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

#define SHADOW_LIFT     0x007a66b4u

void nfs_carshadow_frame(void) {
    static int done;
    if (done) return;
    done = 1;
    float f; uint32_t v = MEM32(SHADOW_LIFT); memcpy(&f, &v, 4);
    if (f != 0.04f) { LOGI("Shadow: the exe's shadow lift is not the one expected: left as it is"); return; }
    f = 0.015f; memcpy(&v, &f, 4); MEM32(SHADOW_LIFT) = v;
    LOGI("Shadow: the cars' shadows 1.5 cm above the road, not 4");
}
#endif
