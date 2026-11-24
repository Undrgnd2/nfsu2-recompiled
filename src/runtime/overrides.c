/*
 * overrides.c - guest functions replaced by hand-written C.
 *
 * Every entry needs a reason that is a fact about the hardware or the host,
 * not "it crashed": an override hides whatever the original did.
 *
 * Calling convention inside the lifted model: the caller has pushed a return
 * slot, arguments are at g_esp+4.., the body leaves its result in g_eax and
 * pops what `ret N` would (g_esp += 4 + N).
 */
#include <string.h>
#include "recomp_types.h"
#include "nfs_runtime.h"
#ifdef NFS_DRIFT_MODE
#include "nfs_drift.h"
#endif

/* 0x006F5FC5: Cyrix detection.
 *   xor eax,eax; sahf; mov eax,5; mov ebx,2; div bl; lahf; cmp ah,2; ...
 * Cyrix parts leave the flags untouched across DIV, so AH reads back 2 (the
 * value SAHF loaded, plus the always-set bit 1); Intel/AMD do not. DIV's flags
 * are architecturally undefined and the lifter does not model them, so the
 * lifted probe always answers "Cyrix". The host is never a Cyrix: return 0. */
static void ov_is_cyrix(void) {
    g_eax = 0;
    g_esp += 4;
}

/* 0x0075CFD0: the static CRT memcpy/memmove(dst, src, n) (cdecl, returns dst).
 * It handles overlap by copying backward, and reaches its unrolled dword moves
 * through computed jumps into the middle of straight-line blocks (tables at
 * 0x75D11C, 0x75D268, 0x75D2B8...). Not every target is a lifted entry, so an
 * overlapping move with 8+ dwords stopped at "ITAIL unresolved 0x0075D27C".
 * Its contract is exactly the host memmove over guest memory. */
static void ov_crt_memmove(void) {
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (n) memmove((void *)ADDR(dst), (const void *)ADDR(src), n);
    g_eax = dst;
    g_esp += 4;
}

static const struct { uint32_t va; nfs_override_fn fn; } k_overrides[] = {
    { 0x006F5FC5u, ov_is_cyrix },
    { 0x0075CFD0u, ov_crt_memmove },
#ifdef NFS_DRIFT_MODE
    /* wraps (config/wraps.txt): drift mode in free roam, src/runtime/drift.c */
    { 0x0056DDD0u, nfs_drift_ov_leaderboard },
    { 0x004C85D0u, nfs_drift_ov_style_points },
    { 0x004AC120u, nfs_drift_ov_message },
    /* ghost mode in free roam, src/runtime/ghost.c */
    { 0x005B2610u, nfs_ghost_ov_contact },
    { 0x0044AC10u, nfs_ghost_ov_camera_cars },
    /* photo mode, src/runtime/photo.c */
    { 0x005378C0u, nfs_photo_ov_frontend },
    { 0x005C7EC0u, nfs_photo_ov_view },
    { 0x00620AE0u, nfs_photo_ov_car },
    { 0x006162E0u, nfs_photo_ov_car_parts },
    { 0x00615A10u, nfs_photo_ov_car_lights },
    { 0x00615630u, nfs_photo_ov_car_flares },
    /* the pause menu's Save, src/runtime/savegame.c */
    { 0x0050DAB0u, nfs_save_ov_add_item },
    { 0x004C26B0u, nfs_save_ov_accept },
    /* the drift camera in every drive, src/runtime/cameras.c */
    { 0x00502540u, nfs_cam_ov_scroll },
    { 0x004FE9F0u, nfs_cam_ov_settings },
    { 0x00525ED0u, nfs_cam_ov_humans },
    { 0x00526970u, nfs_cam_ov_dyno },
    { 0x00540780u, nfs_cam_ov_roam },
    { 0x00581820u, nfs_cam_ov_load },
    /* the headlights on a touch button, src/runtime/lights.c */
    { 0x005F6190u, nfs_lights_ov_step },
    { 0x0060F510u, nfs_lights_ov_colour },
    { 0x00490920u, nfs_lights_ov_flare },
    /* the exhibition mode, src/runtime/showoff.c */
    { 0x00438140u, nfs_showoff_ov_banks },
    /* the distance under the GPS arrow, src/runtime/gpsdistance.c */
    { 0x004EC5A0u, nfs_gps_ov_hud },
    /* the Bank and the top-left box only when they say something, src/runtime/hudbank.c */
    { 0x00600090u, nfs_hudbank_ov_bits },
    /* the garage 10 slots and Trade Car, src/runtime/garage.c */
    { 0x00503510u, nfs_garage_ov_find },
    { 0x00503680u, nfs_garage_ov_find2 },
    { 0x005035C0u, nfs_garage_ov_find3 },
    { 0x005162D0u, nfs_garage_ov_walk },
    { 0x00534850u, nfs_garage_ov_count },
    { 0x0053FEB0u, nfs_garage_ov_setup },
    { 0x0052A5D0u, nfs_garage_ov_new_profile },
    { 0x00516480u, nfs_garage_ov_new_slots },
    { 0x00532830u, nfs_garage_ov_new_career },
    { 0x00551320u, nfs_garage_ov_career_update },
    { 0x00532CF0u, nfs_garage_ov_stage },
    { 0x0052A280u, nfs_garage_ov_payload },
    { 0x0052A450u, nfs_garage_ov_save_payload },
    { 0x0052A4C0u, nfs_garage_ov_dirty },
    { 0x00516850u, nfs_garage_ov_cars_loaded },
    { 0x00534770u, nfs_garage_ov_profile_cars },
    { 0x005344D0u, nfs_garage_ov_profile },
    { 0x0054E6E0u, nfs_garage_ov_helpbar },
    { 0x0052D330u, nfs_garage_ov_helplabel },
    { 0x004DFC50u, nfs_garage_ov_select },
    /* the tuning graphs take a drag, src/runtime/tunegraph.c */
    { 0x005654D0u, nfs_tune_ov_msg },
    /* the dyno keeps its setups, src/runtime/dyno.c */
    { 0x005647E0u, nfs_dyno_ov_msg },
    /* the Car Specialties shop, src/runtime/specialties.c */
    { 0x00563CF0u, nfs_shop_ov_hud_msg },
    { 0x0056C420u, nfs_shop_ov_hud_new },
    { 0x00568740u, nfs_shop_ov_ice_msg },
    { 0x00560550u, nfs_shop_ov_ice_new },
    /* the unique visual parts after Caleb, src/runtime/uniques.c */
    { 0x00515500u, nfs_uniques_ov_unlocked },
    /* Ville ouverte: stage 5's walls from stage 1, src/runtime/walls.c */
    { 0x00578060u, nfs_walls_ov_groups },
    { 0x005F54A0u, nfs_walls_ov_player },
    /* the solo cheats' car lot with every car, src/runtime/cheats.c */
    { 0x00513C50u, nfs_cheats_ov_cartypes },
    /* the cruise control, src/runtime/cruise.c */
    { 0x00407A30u, nfs_cruise_ov_input },
#endif
};

nfs_override_fn nfs_override_lookup(uint32_t va) {
    for (unsigned i = 0; i < sizeof k_overrides / sizeof k_overrides[0]; i++)
        if (k_overrides[i].va == va) return k_overrides[i].fn;
    return 0;
}

int nfs_override_count(void) { return (int)(sizeof k_overrides / sizeof k_overrides[0]); }
