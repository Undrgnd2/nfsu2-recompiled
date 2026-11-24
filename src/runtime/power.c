/*
 * power.c - "Super puissance": the player's car ×1.5, ×2 or ×3 in career free roam (Android, the ⚙ menu).
 *
 * The game has a factor of its own for a car's power: the vehicle's +0x468, 1 from its constructor (0x591DE0), what
 * the career's catch-up gives an AI car behind the player (0x4172D0, nfsu2-web catchup.c: "torque, governor and
 * grip") and two player cars' handicap (0x5F6740). The drivetrain's update (0x5A3380, from 0x5AAD90) takes it as the
 * engine's factor, the governor's step (0x5A49A0, 0x599170) its limit's: the car pulls harder, goes faster, and its
 * tyres keep up. Nothing sets it for one player's car, so it is held at the factor chosen every frame in career free
 * roam, and put back to 1 when the switch goes off, the car leaves free roam (a race, a shop) or another car comes.
 */
#ifdef NFS_DRIFT_MODE
#include "recomp_types.h"
#include "nfs_drift.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NFSU2", __VA_ARGS__)

int nfs_guest_readable(uint32_t va, uint32_t bytes);

#define G_GAMEMODE    0x00850078u
#define G_RACETYPE    0x0089e7b0u
#define G_FLOW        0x008654a4u
#define G_RACEPARAMS  0x0089e7a0u
#define G_PLAYER1     0x008900acu
#define CAR_POWER     0x468u

static const float FACTOR[4] = { 1.f, 1.5f, 2.f, 3.f };
static atomic_int s_level, s_applied;
static uint32_t s_car;            /* the car given a factor, put back to 1 when it is not ours to hold any more */

static int ok(uint32_t va, uint32_t n) { return va >= 0x10000u && nfs_guest_readable(va, n); }
static uint32_t rd(uint32_t va) { return ok(va, 4) ? MEM32(va) : 0; }
static void put(uint32_t car, float f) { uint32_t u; memcpy(&u, &f, 4); if (ok(car + CAR_POWER, 4)) MEM32(car + CAR_POWER) = u; }

/* the touch menu's level (any thread): 0 off, 1 ×1.5, 2 ×2, 3 ×3; 1 when it is held now */
int nfs_power(int level) {
    atomic_store(&s_level, level < 0 ? 0 : level > 3 ? 3 : level);
    return atomic_load(&s_applied);
}
void nfs_power_frame(void) {
    uint32_t pl = rd(G_PLAYER1), car = pl ? rd(pl + 4u) : 0;
    int level = atomic_load(&s_level);
    int roam = rd(G_GAMEMODE) == 1u && rd(G_RACETYPE) == 0u && rd(G_FLOW) == 6u && rd(G_RACEPARAMS) == 4000u && ok(car, CAR_POWER + 4u);
    int want = roam && level > 0;
    if (s_car && (!want || s_car != car)) {
        put(s_car, 1.f);
        LOGI("Power: x1 again (%s)", !level ? "off" : s_car != car && roam ? "another car" : "not in free roam");
        s_car = 0;
    }
    if (want) {
        if (!s_car) LOGI("Power: x%.1f", (double)FACTOR[level]);
        put(car, FACTOR[level]);
        s_car = car;
    }
    atomic_store(&s_applied, want);
}
#endif
