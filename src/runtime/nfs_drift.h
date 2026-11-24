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
/* The headlights on a touch button (lights.c): tap off or on, hold high beam. */
void nfs_lights_key(int down);              /* the LIGHTS button (any thread) */
int nfs_lights_state(void);                 /* 0 as the game has them, 1 off, 2 high beam */
void nfs_lights_frame(void);                /* guest thread, after each Present */
void nfs_lights_flares(uint32_t ri);        /* photo.c's flares wrap: the car whose flares are drawn now */
void nfs_lights_ov_step(void);              /* wraps 0x5F6190, 0x60F510, 0x490920 */
void nfs_lights_ov_colour(void);
void nfs_lights_ov_flare(void);
/* The exhibition mode (showoff.c): the parked car's hood, trunk and doors opened step by step. */
void nfs_showoff_press(void);               /* the SHOWOFF touch button (any thread) */
int nfs_showoff_status(void);               /* the step (0 closed, 1 hood, 2 trunk, 3 doors, 4 all) | 8 the last press
                                               did nothing: the car moves | 16 not in free roam | 32 no animations,
                                               presses counted << 8 */
void nfs_showoff_frame(void);               /* guest thread, after each Present */
int nfs_showoff_put(uint32_t ri, int body, uint32_t keep[6]);   /* photo.c's car drawing wraps, before ... */
void nfs_showoff_take(const uint32_t keep[6]);                  /* ... and after the drawing */
void nfs_showoff_ov_banks(void);            /* wraps 0x438140 */
/* The distance under the GPS arrow (gpsdistance.c). */
void nfs_gps_ov_hud(void);                  /* wraps 0x4EC5A0 */
/* The Bank and the top-left box only when they say something (hudbank.c). */
void nfs_hudbank_ov_bits(void);             /* wraps 0x600090 */
/* The career garage 10 slots and Trade Car (garage.c). */
void nfs_garage_ov_find(void);
void nfs_garage_ov_find2(void);
void nfs_garage_ov_find3(void);
void nfs_garage_ov_walk(void);
void nfs_garage_ov_count(void);
void nfs_garage_ov_setup(void);
void nfs_garage_ov_new_profile(void);
void nfs_garage_ov_new_slots(void);
void nfs_garage_ov_new_career(void);
void nfs_garage_ov_career_update(void);
void nfs_garage_ov_stage(void);
void nfs_garage_ov_payload(void);
void nfs_garage_ov_save_payload(void);
void nfs_garage_ov_dirty(void);
void nfs_garage_ov_cars_loaded(void);
void nfs_garage_ov_profile_cars(void);
void nfs_garage_ov_profile(void);
void nfs_garage_ov_helpbar(void);
void nfs_garage_ov_helplabel(void);
void nfs_garage_ov_select(void);
void nfs_garage_swap_in(void);              /* cameras.c race setup wraps: the car driven from slots 6-10 ... */
void nfs_garage_swap_out(void);
void nfs_shop_ov_hud_msg(void);             /* specialties.c: wraps 0x563CF0, 0x56C420, 0x568740, 0x560550 */
void nfs_shop_ov_hud_new(void);
void nfs_shop_ov_ice_msg(void);
void nfs_shop_ov_ice_new(void);
void nfs_dyno_ov_msg(void);                 /* dyno.c: wraps 0x5647E0 */
void nfs_tune_ov_msg(void);                 /* tunegraph.c: wraps 0x5654D0 */
void nfs_carpaint_draw(uint32_t ri);        /* carpaint.c: before a car is drawn (photo.c's wraps), its own part paint */
void nfs_uniques_ov_unlocked(void);
/* The solo cheats (cheats.c). */
int nfs_cheat(int cmd, int arg);            /* any thread: 1 money, 2 nitrous refill, 3 nitrous always full, 4 parts unlocked */
int nfs_cheat_parts(void);
void nfs_cheats_frame(void);
/* Speed traps in career free roam (radars.c). */
void nfs_radars_frame(void);
int nfs_radar_count(void);
int nfs_radar_info(int i, float *start, float *dir, int *record);
/* The world map's Téléporter (teleport.c). */
int nfs_teleport_status(void);              /* 1 the world map is open in career free roam */
int nfs_teleport(int what);                 /* any thread: 1..6 a shop's kind, 100 + i radar i */
void nfs_teleport_frame(void);              /* guest thread, after each Present */                /* guest thread, after each Present */
/* A cruise control in career free roam (cruise.c). */
void nfs_cruise_ov_input(void);             /* wraps 0x407A30, the player's controls into the car */
void nfs_cruise_frame(void);                /* guest thread, after each Present */
void nfs_cruise_key(int scan);              /* any thread: 0xF6 on or off, 0xF7 slower, 0xF8 faster */
void nfs_cruise_status(int out[5]);         /* on, the speed held (km/h), last event (1 on, 2 brake, 3 crash, 4 left, 5 too slow, 6 off, 7 not here, 8 speed set), its number, the button offered */
void nfs_radars_enable(int on);             /* the touch menu's switch (any thread) */
void nfs_radars_status(int out[13]);        /* count, nearest, its distance (m), its direction (degrees), last radar, speed, record, new (1, 2 first), pass number; marker on screen, x, y (1/10000), distance */                /* guest thread, after each Present */         /* uniques.c: wraps 0x515500 */
void nfs_carshadow_frame(void);             /* carshadow.c: the cars' shadow lift (once) */
void nfs_carspot_frame(void);               /* carspot.c: a kept career spot off the map back to the start (each frame) */
void nfs_walls_frame(void);                 /* walls.c: a car past the career's walls back on its roads (each frame) */
int nfs_engage_status(void);                /* engage.c: 1 while Engage (Enter) does something in free roam */
void nfs_engage_frame(void);                /* engage.c: Engage at a marker mended (each frame) */
void nfs_sponsors_frame(void);              /* sponsors.c: Continue on the sponsor screens (once) */
void nfs_garage_hook_shop(void);            /* hook 0x547689 */
void nfs_garage_file_written(const char *path);   /* the host: a file the game wrote, closed */
void nfs_garage_file_deleted(const char *path);   /* the host: a file the game deleted */
#ifdef __cplusplus
}
#endif
#endif
