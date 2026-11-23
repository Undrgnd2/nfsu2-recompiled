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
/* Ghost mode in free roam (ghost.c): the game's cars pass through the player's car, each one gives nitrous. */
void nfs_ghost_frame(void);                 /* guest thread, after each Present */
void nfs_ghost_press(void);                 /* the GHOST touch button (any thread) */
void nfs_ghost_status(int out[4]);          /* flags (1 chosen, 2 active now, 4 outside free roam, 8 nitrous just
                                               gained), cars passed through, nitrous per car (%), and the car's
                                               hydraulics (0 none, 1 not startable now, 2 startable) */
void nfs_ghost_ov_contact(void);            /* wraps 0x5B2610, 0x44AC10 */
void nfs_ghost_ov_camera_cars(void);
/* Photo mode (photo.c): the world stands, the HUD goes, the camera comes loose from the car. */
enum { PHOTO_OPEN = 1, PHOTO_MOVE, PHOTO_LIFT, PHOTO_DRAG, PHOTO_ZOOM, PHOTO_ROLL, PHOTO_MODE, PHOTO_RESET, PHOTO_PATH, PHOTO_CARS };
enum { PHOTO_PATH_CLEAR = 0, PHOTO_PATH_ADD = 1, PHOTO_PATH_PLAY = 2, PHOTO_PATH_STOP = 3 };
void nfs_photo_frame(void);                 /* guest thread, after each Present */
void nfs_photo_input(int cmd, int a, int b);/* the touch controls (any thread) */
int nfs_photo_still(void);                  /* the camera stands still (the live look's cache) */
void nfs_photo_status(int out[6]);          /* flags (1 it can open or is on, 2 on, 4 the free camera), field of view
                                               and tilt (1/10 degree), times opened, the path's points, it plays */
void nfs_photo_ov_frontend(void);           /* wraps 0x5378C0, 0x5C7EC0 */
void nfs_photo_ov_view(void);
void nfs_photo_ov_car(void);                /* wraps 0x620AE0, 0x6162E0, 0x615A10, 0x615630: the cars shown */
void nfs_photo_ov_car_parts(void);
void nfs_photo_ov_car_lights(void);
void nfs_photo_ov_car_flares(void);
/* The pause menu's Save in career free roam (savegame.c). */
void nfs_save_frame(void);                  /* guest thread, after each Present */
void nfs_save_ov_add_item(void);            /* wraps 0x50DAB0, 0x4C26B0 */
void nfs_save_ov_accept(void);
/* The drift camera in every drive (cameras.c). */
uint32_t nfs_drift_camera_view(void);       /* drift.c: the view its drift camera is in, 0 none */
void nfs_cam_frame(void);                   /* guest thread, after each Present */
void nfs_cam_ov_scroll(void);               /* replaces 0x502540; wraps 0x4FE9F0, 0x525ED0, 0x526970, 0x540780, 0x581820 */
void nfs_cam_ov_settings(void);
void nfs_cam_ov_humans(void);
void nfs_cam_ov_dyno(void);
void nfs_cam_ov_roam(void);
void nfs_cam_ov_load(void);
#ifdef __cplusplus
}
#endif
#endif
