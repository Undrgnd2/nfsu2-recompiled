/* nfs_drift.h - drift mode in free roam (drift.c, Android builds with NFS_DRIFT_MODE). */
#ifndef NFS_DRIFT_H
#define NFS_DRIFT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void nfs_drift_frame(void);                 /* guest thread, after each Present */
void nfs_drift_press(void);                 /* the DRIFT touch button (any thread) */
void nfs_drift_status(int out[7]);          /* flags (1 chosen, 2 on, 4 outside free roam), multiplier, next level's
                                               points, total, best, current drift, its state */
void nfs_drift_hook(uint32_t va);           /* hook sites 0x482279, 0x471B95 */
void nfs_drift_ov_leaderboard(void);        /* wraps 0x56DDD0, 0x4C85D0, 0x4AC120 */
void nfs_drift_ov_style_points(void);
void nfs_drift_ov_message(void);
#ifdef __cplusplus
}
#endif
#endif
