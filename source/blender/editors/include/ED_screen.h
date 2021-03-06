/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file ED_screen.h
 *  \ingroup editors
 */

#ifndef __ED_SCREEN_H__
#define __ED_SCREEN_H__

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view2d_types.h"
#include "DNA_view3d_types.h"

struct ARegion;
struct Main;
struct Scene;
struct bContext;
struct bScreen;
struct rcti;
struct uiBlock;
struct wmEvent;
struct wmKeyConfig;
struct wmNotifier;
struct wmWindow;
struct wmWindowManager;

/* regions */
void    ED_region_do_listen(struct bScreen *sc, struct ScrArea *sa, struct ARegion *ar, struct wmNotifier *note);
void    ED_region_do_draw(struct bContext *C, struct ARegion *ar);
void    ED_region_exit(struct bContext *C, struct ARegion *ar);
void    ED_region_pixelspace(struct ARegion *ar);
void    ED_region_set(const struct bContext *C, struct ARegion *ar);
void    ED_region_update_rect(struct bContext *C, struct ARegion *ar);
void    ED_region_init(struct bContext *C, struct ARegion *ar);
void    ED_region_tag_redraw(struct ARegion *ar);
void    ED_region_tag_redraw_partial(struct ARegion *ar, const struct rcti *rct);
void    ED_region_tag_redraw_overlay(struct ARegion *ar);
void    ED_region_tag_refresh_ui(struct ARegion *ar);
void    ED_region_panels_init(struct wmWindowManager *wm, struct ARegion *ar);
void    ED_region_panels(
            const struct bContext *C, struct ARegion *ar,
            const char *context, int contextnr,
            const bool vertical);
void    ED_region_header_init(struct ARegion *ar);
void    ED_region_header(const struct bContext *C, struct ARegion *ar);
void    ED_region_cursor_set(struct wmWindow *win, struct ScrArea *sa, struct ARegion *ar);
void    ED_region_toggle_hidden(struct bContext *C, struct ARegion *ar);
void    ED_region_info_draw(struct ARegion *ar, const char *text, float fill_color[4], const bool full_redraw);
void    ED_region_image_metadata_draw(int x, int y, struct ImBuf *ibuf, const rctf *frame, float zoomx, float zoomy);
void    ED_region_grid_draw(struct ARegion *ar, float zoomx, float zoomy);
float	ED_region_blend_factor(struct ARegion *ar);
void	ED_region_visible_rect(struct ARegion *ar, struct rcti *rect);


/* spaces */
void    ED_spacetypes_keymap(struct wmKeyConfig *keyconf);
int     ED_area_header_switchbutton(const struct bContext *C, struct uiBlock *block, int yco);

/* areas */
void    ED_area_initialize(struct wmWindowManager *wm, struct wmWindow *win, struct ScrArea *sa);
void    ED_area_exit(struct bContext *C, struct ScrArea *sa);
int     ED_screen_area_active(const struct bContext *C);
void    ED_area_do_listen(struct bScreen *sc, ScrArea *sa, struct wmNotifier *note);
void    ED_area_tag_redraw(ScrArea *sa);
void    ED_area_tag_redraw_regiontype(ScrArea *sa, int type);
void    ED_area_tag_refresh(ScrArea *sa);
void    ED_area_do_refresh(struct bContext *C, ScrArea *sa);
void    ED_area_azones_update(ScrArea *sa, const int mouse_xy[]);
void    ED_area_headerprint(ScrArea *sa, const char *str);
void    ED_area_newspace(struct bContext *C, ScrArea *sa, int type, const bool skip_ar_exit);
void    ED_area_prevspace(struct bContext *C, ScrArea *sa);
void    ED_area_swapspace(struct bContext *C, ScrArea *sa1, ScrArea *sa2);
int     ED_area_headersize(void);

/* screens */
void    ED_screens_initialize(struct Main *bmain, struct wmWindowManager *wm);
void    ED_screen_draw_edges(struct wmWindow *win);
void    ED_screen_draw_join_shape(struct ScrArea *sa1, struct ScrArea *sa2);
void    ED_screen_draw_split_preview(struct ScrArea *sa, const int dir, const float fac);
void    ED_screen_refresh(struct wmWindowManager *wm, struct wmWindow *win);
void    ED_screen_do_listen(struct bContext *C, struct wmNotifier *note);
bScreen *ED_screen_duplicate(struct Main *bmain, struct wmWindow *win, struct bScreen *sc);
bScreen *ED_screen_add(struct Main *bmain, struct wmWindow *win, struct Scene *scene, const char *name);
bool    ED_screen_set(struct bContext *C, struct bScreen *sc);
bool    ED_screen_delete(struct bContext *C, struct bScreen *sc);
void    ED_screen_set_scene(struct bContext *C, struct bScreen *screen, struct Scene *scene);
bool    ED_screen_delete_scene(struct bContext *C, struct Scene *scene);
void    ED_screen_set_subwinactive(struct bContext *C, const struct wmEvent *event);
void    ED_screen_exit(struct bContext *C, struct wmWindow *window, struct bScreen *screen);
void    ED_screen_restore_temp_type(struct bContext *C, ScrArea *sa);
ScrArea *ED_screen_full_newspace(struct bContext *C, ScrArea *sa, int type);
void    ED_screen_full_prevspace(struct bContext *C, ScrArea *sa);
void    ED_screen_full_restore(struct bContext *C, ScrArea *sa);
struct ScrArea *ED_screen_state_toggle(struct bContext *C, struct wmWindow *win, struct ScrArea *sa, const short state);
void    ED_screens_header_tools_menu_create(struct bContext *C, struct uiLayout *layout, void *arg);

void    ED_refresh_viewport_fps(struct bContext *C);

/* screen keymaps */
void    ED_operatortypes_screen(void);
void    ED_keymap_screen(struct wmKeyConfig *keyconf);

/* operators; context poll callbacks */
bool ED_operator_screenactive(struct bContext *C);
bool ED_operator_screen_mainwinactive(struct bContext *C);
bool ED_operator_areaactive(struct bContext *C);
bool ED_operator_regionactive(struct bContext *C);

bool ED_operator_scene(struct bContext *C);
bool ED_operator_scene_editable(struct bContext *C);
bool ED_operator_objectmode(struct bContext *C);

bool ED_operator_view3d_active(struct bContext *C);
bool ED_operator_region_view3d_active(struct bContext *C);
bool ED_operator_outliner_active(struct bContext *C);
bool ED_operator_outliner_active_no_editobject(struct bContext *C);
bool ED_operator_file_active(struct bContext *C);
bool ED_operator_buttons_active(struct bContext *C);
bool ED_operator_image_active(struct bContext *C);
bool ED_operator_info_active(struct bContext *C);
bool ED_operator_console_active(struct bContext *C);


bool ED_operator_object_active(struct bContext *C);
bool ED_operator_object_active_editable(struct bContext *C);
bool ED_operator_object_active_editable_mesh(struct bContext *C);
bool ED_operator_object_active_editable_font(struct bContext *C);
bool ED_operator_editmesh(struct bContext *C);
bool ED_operator_editmesh_view3d(struct bContext *C);
bool ED_operator_editmesh_region_view3d(struct bContext *C);
bool ED_operator_editcurve(struct bContext *C);
bool ED_operator_editcurve_3d(struct bContext *C);
bool ED_operator_editsurf(struct bContext *C);
bool ED_operator_editsurfcurve(struct bContext *C);
bool ED_operator_editsurfcurve_region_view3d(struct bContext *C);
bool ED_operator_editfont(struct bContext *C);

/* Cache display helpers */
void ED_region_cache_draw_background(const struct ARegion *ar);
void ED_region_cache_draw_curfra_label(const int framenr, const float x, const float y);
void ED_region_cache_draw_cached_segments(const struct ARegion *ar, const int num_segments, const int *points, const int sfra, const int efra);

/* default keymaps, bitflags */
#define ED_KEYMAP_UI        1
#define ED_KEYMAP_VIEW2D    2
#define ED_KEYMAP_HEADER    64

#endif /* __ED_SCREEN_H__ */
