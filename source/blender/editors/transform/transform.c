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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file blender/editors/transform/transform.c
 *  \ingroup edtransform
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"  /* PET modes */
#include "DNA_object_types.h"

#include "BLI_alloca.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_rect.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ghash.h"
#include "BLI_utildefines_stack.h"
#include "BLI_memarena.h"

#include "BKE_editmesh.h"
#include "BKE_editmesh_bvh.h"
#include "BKE_context.h"
#include "BKE_unit.h"
#include "BKE_report.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "ED_image.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_view3d.h"
#include "ED_mesh.h"

#include "WM_types.h"
#include "WM_api.h"

#include "UI_view2d.h"
#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"

#include "RNA_access.h"

#include "BLF_api.h"
#include "BLT_translation.h"

#include "transform.h"

/* Disabling, since when you type you know what you are doing, and being able to set it to zero is handy. */
// #define USE_NUM_NO_ZERO

static void drawTransformApply(const struct bContext *C, ARegion *ar, void *arg);
static void doEdgeSlide(TransInfo *t, float perc);
static void doVertSlide(TransInfo *t, float perc);

static void drawEdgeSlide(TransInfo *t);
static void drawVertSlide(TransInfo *t);
static void postInputRotation(TransInfo *t, float values[3]);

static void ElementRotation(TransInfo *t, TransData *td, float mat[3][3], const short around);
static void initSnapSpatial(TransInfo *t, float r_snap[3]);


/* Transform Callbacks */
static void initBend(TransInfo *t);
static eRedrawFlag handleEventBend(TransInfo *t, const struct wmEvent *event);
static void Bend(TransInfo *t, const int mval[2]);

static void initShear(TransInfo *t);
static eRedrawFlag handleEventShear(TransInfo *t, const struct wmEvent *event);
static void applyShear(TransInfo *t, const int mval[2]);

static void initResize(TransInfo *t);
static void applyResize(TransInfo *t, const int mval[2]);

static void initTranslation(TransInfo *t);
static void applyTranslation(TransInfo *t, const int mval[2]);

static void initToSphere(TransInfo *t);
static void applyToSphere(TransInfo *t, const int mval[2]);

static void initRotation(TransInfo *t);
static void applyRotation(TransInfo *t, const int mval[2]);

static void initShrinkFatten(TransInfo *t);
static void applyShrinkFatten(TransInfo *t, const int mval[2]);

static void initTilt(TransInfo *t);
static void applyTilt(TransInfo *t, const int mval[2]);

static void initCurveShrinkFatten(TransInfo *t);
static void applyCurveShrinkFatten(TransInfo *t, const int mval[2]);

static void initTrackball(TransInfo *t);
static void applyTrackball(TransInfo *t, const int mval[2]);

static void initPushPull(TransInfo *t);
static void applyPushPull(TransInfo *t, const int mval[2]);

static void initCrease(TransInfo *t);
static void applyCrease(TransInfo *t, const int mval[2]);

static void initEdgeSlide_ex(TransInfo *t, bool use_double_side, bool use_even, bool flipped, bool use_clamp);
static void initEdgeSlide(TransInfo *t);
static eRedrawFlag handleEventEdgeSlide(TransInfo *t, const struct wmEvent *event);
static void applyEdgeSlide(TransInfo *t, const int mval[2]);

static void initVertSlide_ex(TransInfo *t, bool use_even, bool flipped, bool use_clamp);
static void initVertSlide(TransInfo *t);
static eRedrawFlag handleEventVertSlide(TransInfo *t, const struct wmEvent *event);
static void applyVertSlide(TransInfo *t, const int mval[2]);

static void initMirror(TransInfo *t);
static void applyMirror(TransInfo *t, const int mval[2]);

static void initAlign(TransInfo *t);
static void applyAlign(TransInfo *t, const int mval[2]);

/* end transform callbacks */


static bool transdata_check_local_center(TransInfo *t, short around)
{
	return ((around == V3D_AROUND_LOCAL_ORIGINS) && (
	            (t->flag & (T_OBJECT)) ||
	            (t->obedit && ELEM(t->obedit->type, OB_MESH, OB_CURVE)) )
	        );
}

bool transdata_check_local_islands(TransInfo *t, short around)
{
	return ((around == V3D_AROUND_LOCAL_ORIGINS) && (
	        (t->obedit && ELEM(t->obedit->type, OB_MESH))));
}

/* ************************** SPACE DEPENDANT CODE **************************** */

void setTransformViewMatrices(TransInfo *t)
{
	if (t->spacetype == SPACE_VIEW3D && t->ar && t->ar->regiontype == RGN_TYPE_WINDOW) {
		RegionView3D *rv3d = t->ar->regiondata;

		copy_m4_m4(t->viewmat, rv3d->viewmat);
		copy_m4_m4(t->viewinv, rv3d->viewinv);
		copy_m4_m4(t->persmat, rv3d->persmat);
		copy_m4_m4(t->persinv, rv3d->persinv);
		t->persp = rv3d->persp;
	}
	else {
		unit_m4(t->viewmat);
		unit_m4(t->viewinv);
		unit_m4(t->persmat);
		unit_m4(t->persinv);
		t->persp = RV3D_ORTHO;
	}

	calculateCenter2D(t);
}

void setTransformViewAspect(TransInfo *t, float r_aspect[3])
{
	copy_v3_fl(r_aspect, 1.0f);

}

static void convertViewVec2D(View2D *v2d, float r_vec[3], int dx, int dy)
{
	float divx = BLI_rcti_size_x(&v2d->mask);
	float divy = BLI_rcti_size_y(&v2d->mask);

	r_vec[0] = BLI_rctf_size_x(&v2d->cur) * dx / divx;
	r_vec[1] = BLI_rctf_size_y(&v2d->cur) * dy / divy;
	r_vec[2] = 0.0f;
}

void convertViewVec(TransInfo *t, float r_vec[3], double dx, double dy)
{
	if ((t->spacetype == SPACE_VIEW3D) && (t->ar->regiontype == RGN_TYPE_WINDOW)) {
		const float mval_f[2] = {(float)dx, (float)dy};
		ED_view3d_win_to_delta(t->ar, mval_f, r_vec, t->zfac);
	}
	else if (t->spacetype == SPACE_IMAGE) {
		convertViewVec2D(t->view, r_vec, dx, dy);

		r_vec[0] *= t->aspect[0];
		r_vec[1] *= t->aspect[1];
	}
	else {
		printf("%s: called in an invalid context\n", __func__);
		zero_v3(r_vec);
	}
}

void projectIntViewEx(TransInfo *t, const float vec[3], int adr[2], const eV3DProjTest flag)
{
	if (t->spacetype == SPACE_VIEW3D) {
		if (t->ar->regiontype == RGN_TYPE_WINDOW) {
			if (ED_view3d_project_int_global(t->ar, vec, adr, flag) != V3D_PROJ_RET_OK) {
				/* this is what was done in 2.64, perhaps we can be smarter? */
				adr[0] = (int)2140000000.0f;
				adr[1] = (int)2140000000.0f;
			}
		}
	}
	else if (t->spacetype == SPACE_IMAGE) {
		{
			float v[2];

			v[0] = vec[0] / t->aspect[0];
			v[1] = vec[1] / t->aspect[1];

			UI_view2d_view_to_region(t->view, v[0], v[1], &adr[0], &adr[1]);
		}
	}
}
void projectIntView(TransInfo *t, const float vec[3], int adr[2])
{
	projectIntViewEx(t, vec, adr, V3D_PROJ_TEST_NOP);
}

void projectFloatViewEx(TransInfo *t, const float vec[3], float adr[2], const eV3DProjTest flag)
{
	switch (t->spacetype) {
		case SPACE_VIEW3D:
		{
			if (t->ar->regiontype == RGN_TYPE_WINDOW) {
				/* allow points behind the view [#33643] */
				if (ED_view3d_project_float_global(t->ar, vec, adr, flag) != V3D_PROJ_RET_OK) {
					/* XXX, 2.64 and prior did this, weak! */
					adr[0] = t->ar->winx / 2.0f;
					adr[1] = t->ar->winy / 2.0f;
				}
				return;
			}
			break;
		}
		default:
		{
			int a[2] = {0, 0};
			projectIntView(t, vec, a);
			adr[0] = a[0];
			adr[1] = a[1];
			break;
		}
	}
}
void projectFloatView(TransInfo *t, const float vec[3], float adr[2])
{
	projectFloatViewEx(t, vec, adr, V3D_PROJ_TEST_NOP);
}

void applyAspectRatio(TransInfo *t, float vec[2])
{
	if ((t->spacetype == SPACE_IMAGE) && (t->mode == TFM_TRANSLATION)) {
		SpaceImage *sima = t->sa->spacedata.first;

		if ((sima->flag & SI_COORDFLOATS) == 0) {
			int width, height;
			ED_space_image_get_size(sima, &width, &height);

			vec[0] *= width;
			vec[1] *= height;
		}

		vec[0] /= t->aspect[0];
		vec[1] /= t->aspect[1];
	}
}

void removeAspectRatio(TransInfo *t, float vec[2])
{
	if ((t->spacetype == SPACE_IMAGE) && (t->mode == TFM_TRANSLATION)) {
		SpaceImage *sima = t->sa->spacedata.first;

		if ((sima->flag & SI_COORDFLOATS) == 0) {
			int width, height;
			ED_space_image_get_size(sima, &width, &height);

			vec[0] /= width;
			vec[1] /= height;
		}

		vec[0] *= t->aspect[0];
		vec[1] *= t->aspect[1];
	}
}

static void viewRedrawForce(const bContext *C, TransInfo *t)
{
	if (t->spacetype == SPACE_VIEW3D) {
		/* Do we need more refined tags? */
		WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);
	}
	else if (t->spacetype == SPACE_IMAGE) {
		 {
			// XXX how to deal with lock?
			SpaceImage *sima = (SpaceImage *)t->sa->spacedata.first;
			if (sima->lock) WM_event_add_notifier(C, NC_GEOM | ND_DATA, t->obedit->data);
			else ED_area_tag_redraw(t->sa);
		}
	}
}

static void viewRedrawPost(bContext *C, TransInfo *t)
{
	ED_area_headerprint(t->sa, NULL);

	if (t->spacetype == SPACE_VIEW3D) {
		/* redraw UV editor */
		if (ELEM(t->mode, TFM_VERT_SLIDE, TFM_EDGE_SLIDE))
		{
			WM_event_add_notifier(C, NC_GEOM | ND_DATA, NULL);
		}

		/* XXX temp, first hack to get auto-render in compositor work (ton) */
		WM_event_add_notifier(C, NC_SCENE | ND_TRANSFORM_DONE, CTX_data_scene(C));

	}

}

/* ************************** TRANSFORMATIONS **************************** */

/* ************************************************* */

/* NOTE: these defines are saved in keymap files, do not change values but just add new ones */
enum {
	TFM_MODAL_CANCEL         = 1,
	TFM_MODAL_CONFIRM        = 2,
	TFM_MODAL_TRANSLATE      = 3,
	TFM_MODAL_ROTATE         = 4,
	TFM_MODAL_RESIZE         = 5,
	TFM_MODAL_SNAP_INV_ON    = 6,
	TFM_MODAL_SNAP_INV_OFF   = 7,
	TFM_MODAL_SNAP_TOGGLE    = 8,
	TFM_MODAL_AXIS_X         = 9,
	TFM_MODAL_AXIS_Y         = 10,
	TFM_MODAL_AXIS_Z         = 11,
	TFM_MODAL_PLANE_X        = 12,
	TFM_MODAL_PLANE_Y        = 13,
	TFM_MODAL_PLANE_Z        = 14,
	TFM_MODAL_CONS_OFF       = 15,
	TFM_MODAL_ADD_SNAP       = 16,
	TFM_MODAL_REMOVE_SNAP    = 17,

/* 18 and 19 used by numinput, defined in transform.h */

	TFM_MODAL_PROPSIZE_UP    = 20,
	TFM_MODAL_PROPSIZE_DOWN  = 21,

	TFM_MODAL_EDGESLIDE_UP   = 24,
	TFM_MODAL_EDGESLIDE_DOWN = 25,

/* for analog input, like trackpad */
	TFM_MODAL_PROPSIZE       = 26,
/* node editor insert offset (aka auto-offset) direction toggle */
	TFM_MODAL_INSERTOFS_TOGGLE_DIR         = 27,
};

/* called in transform_ops.c, on each regeneration of keymaps */
wmKeyMap *transform_modal_keymap(wmKeyConfig *keyconf)
{
	static const EnumPropertyItem modal_items[] = {
		{TFM_MODAL_CONFIRM, "CONFIRM", 0, "Confirm", ""},
		{TFM_MODAL_CANCEL, "CANCEL", 0, "Cancel", ""},
		{TFM_MODAL_AXIS_X, "AXIS_X", 0, "X axis", ""},
		{TFM_MODAL_AXIS_Y, "AXIS_Y", 0, "Y axis", ""},
		{TFM_MODAL_AXIS_Z, "AXIS_Z", 0, "Z axis", ""},
		{TFM_MODAL_PLANE_X, "PLANE_X", 0, "X plane", ""},
		{TFM_MODAL_PLANE_Y, "PLANE_Y", 0, "Y plane", ""},
		{TFM_MODAL_PLANE_Z, "PLANE_Z", 0, "Z plane", ""},
		{TFM_MODAL_CONS_OFF, "CONS_OFF", 0, "Clear Constraints", ""},
		{TFM_MODAL_SNAP_INV_ON, "SNAP_INV_ON", 0, "Snap Invert", ""},
		{TFM_MODAL_SNAP_INV_OFF, "SNAP_INV_OFF", 0, "Snap Invert (Off)", ""},
		{TFM_MODAL_SNAP_TOGGLE, "SNAP_TOGGLE", 0, "Snap Toggle", ""},
		{TFM_MODAL_ADD_SNAP, "ADD_SNAP", 0, "Add Snap Point", ""},
		{TFM_MODAL_REMOVE_SNAP, "REMOVE_SNAP", 0, "Remove Last Snap Point", ""},
		{NUM_MODAL_INCREMENT_UP, "INCREMENT_UP", 0, "Numinput Increment Up", ""},
		{NUM_MODAL_INCREMENT_DOWN, "INCREMENT_DOWN", 0, "Numinput Increment Down", ""},
		{TFM_MODAL_PROPSIZE_UP, "PROPORTIONAL_SIZE_UP", 0, "Increase Proportional Influence", ""},
		{TFM_MODAL_PROPSIZE_DOWN, "PROPORTIONAL_SIZE_DOWN", 0, "Decrease Proportional Influence", ""},
		{TFM_MODAL_EDGESLIDE_UP, "EDGESLIDE_EDGE_NEXT", 0, "Select next Edge Slide Edge", ""},
		{TFM_MODAL_EDGESLIDE_DOWN, "EDGESLIDE_PREV_NEXT", 0, "Select previous Edge Slide Edge", ""},
		{TFM_MODAL_PROPSIZE, "PROPORTIONAL_SIZE", 0, "Adjust Proportional Influence", ""},
		{TFM_MODAL_INSERTOFS_TOGGLE_DIR, "INSERTOFS_TOGGLE_DIR", 0, "Toggle Direction for Node Auto-offset", ""},
		{TFM_MODAL_TRANSLATE, "TRANSLATE", 0, "Translate", ""},
		{TFM_MODAL_ROTATE, "ROTATE", 0, "Rotate", ""},
		{TFM_MODAL_RESIZE, "RESIZE", 0, "Resize", ""},
		{0, NULL, 0, NULL, NULL}
	};

	wmKeyMap *keymap = WM_modalkeymap_get(keyconf, "Transform Modal Map");

	/* this function is called for each spacetype, only needs to add map once */
	if (keymap && keymap->modal_items) return NULL;

	keymap = WM_modalkeymap_add(keyconf, "Transform Modal Map", modal_items);

	/* items for modal map */
	WM_modalkeymap_add_item(keymap, LEFTMOUSE,  KM_PRESS, KM_ANY, 0, TFM_MODAL_CONFIRM);
	WM_modalkeymap_add_item(keymap, RETKEY,     KM_PRESS, KM_ANY, 0, TFM_MODAL_CONFIRM);
	WM_modalkeymap_add_item(keymap, PADENTER,   KM_PRESS, KM_ANY, 0, TFM_MODAL_CONFIRM);
	WM_modalkeymap_add_item(keymap, RIGHTMOUSE, KM_PRESS, KM_ANY, 0, TFM_MODAL_CANCEL);
	WM_modalkeymap_add_item(keymap, ESCKEY,     KM_PRESS, KM_ANY, 0, TFM_MODAL_CANCEL);

	WM_modalkeymap_add_item(keymap, XKEY, KM_PRESS, 0, 0, TFM_MODAL_AXIS_X);
	WM_modalkeymap_add_item(keymap, YKEY, KM_PRESS, 0, 0, TFM_MODAL_AXIS_Y);
	WM_modalkeymap_add_item(keymap, ZKEY, KM_PRESS, 0, 0, TFM_MODAL_AXIS_Z);

	WM_modalkeymap_add_item(keymap, XKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PLANE_X);
	WM_modalkeymap_add_item(keymap, YKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PLANE_Y);
	WM_modalkeymap_add_item(keymap, ZKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PLANE_Z);

	WM_modalkeymap_add_item(keymap, CKEY, KM_PRESS, 0, 0, TFM_MODAL_CONS_OFF);

	WM_modalkeymap_add_item(keymap, GKEY, KM_PRESS, 0, 0, TFM_MODAL_TRANSLATE);
	WM_modalkeymap_add_item(keymap, RKEY, KM_PRESS, 0, 0, TFM_MODAL_ROTATE);
	WM_modalkeymap_add_item(keymap, SKEY, KM_PRESS, 0, 0, TFM_MODAL_RESIZE);

	WM_modalkeymap_add_item(keymap, TABKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_SNAP_TOGGLE);

	WM_modalkeymap_add_item(keymap, LEFTCTRLKEY, KM_PRESS, KM_ANY, 0, TFM_MODAL_SNAP_INV_ON);
	WM_modalkeymap_add_item(keymap, LEFTCTRLKEY, KM_RELEASE, KM_ANY, 0, TFM_MODAL_SNAP_INV_OFF);

	WM_modalkeymap_add_item(keymap, RIGHTCTRLKEY, KM_PRESS, KM_ANY, 0, TFM_MODAL_SNAP_INV_ON);
	WM_modalkeymap_add_item(keymap, RIGHTCTRLKEY, KM_RELEASE, KM_ANY, 0, TFM_MODAL_SNAP_INV_OFF);

	WM_modalkeymap_add_item(keymap, AKEY, KM_PRESS, 0, 0, TFM_MODAL_ADD_SNAP);
	WM_modalkeymap_add_item(keymap, AKEY, KM_PRESS, KM_ALT, 0, TFM_MODAL_REMOVE_SNAP);

	WM_modalkeymap_add_item(keymap, PAGEUPKEY, KM_PRESS, 0, 0, TFM_MODAL_PROPSIZE_UP);
	WM_modalkeymap_add_item(keymap, PAGEDOWNKEY, KM_PRESS, 0, 0, TFM_MODAL_PROPSIZE_DOWN);
	WM_modalkeymap_add_item(keymap, PAGEUPKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PROPSIZE_UP);
	WM_modalkeymap_add_item(keymap, PAGEDOWNKEY, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PROPSIZE_DOWN);
	WM_modalkeymap_add_item(keymap, WHEELDOWNMOUSE, KM_PRESS, 0, 0, TFM_MODAL_PROPSIZE_UP);
	WM_modalkeymap_add_item(keymap, WHEELUPMOUSE, KM_PRESS, 0, 0, TFM_MODAL_PROPSIZE_DOWN);
	WM_modalkeymap_add_item(keymap, WHEELDOWNMOUSE, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PROPSIZE_UP);
	WM_modalkeymap_add_item(keymap, WHEELUPMOUSE, KM_PRESS, KM_SHIFT, 0, TFM_MODAL_PROPSIZE_DOWN);
	WM_modalkeymap_add_item(keymap, MOUSEPAN, 0, 0, 0, TFM_MODAL_PROPSIZE);

	WM_modalkeymap_add_item(keymap, WHEELDOWNMOUSE, KM_PRESS, KM_ALT, 0, TFM_MODAL_EDGESLIDE_UP);
	WM_modalkeymap_add_item(keymap, WHEELUPMOUSE, KM_PRESS, KM_ALT, 0, TFM_MODAL_EDGESLIDE_DOWN);

	/* node editor only */
	WM_modalkeymap_add_item(keymap, TKEY, KM_PRESS, 0, 0, TFM_MODAL_INSERTOFS_TOGGLE_DIR);

	return keymap;
}

static void transform_event_xyz_constraint(TransInfo *t, short key_type, char cmode, bool is_plane)
{
	if (!(t->flag & T_NO_CONSTRAINT)) {
		int constraint_axis, constraint_plane;
		const bool edit_2d = (t->flag & T_2D_EDIT) != 0;
		const char *msg1 = "", *msg2 = "", *msg3 = "";
		char axis;

		/* Initialize */
		switch (key_type) {
			case XKEY:
				msg1 = IFACE_("along X");
				msg2 = IFACE_("along %s X");
				msg3 = IFACE_("locking %s X");
				axis = 'X';
				constraint_axis = CON_AXIS0;
				break;
			case YKEY:
				msg1 = IFACE_("along Y");
				msg2 = IFACE_("along %s Y");
				msg3 = IFACE_("locking %s Y");
				axis = 'Y';
				constraint_axis = CON_AXIS1;
				break;
			case ZKEY:
				msg1 = IFACE_("along Z");
				msg2 = IFACE_("along %s Z");
				msg3 = IFACE_("locking %s Z");
				axis = 'Z';
				constraint_axis = CON_AXIS2;
				break;
			default:
				/* Invalid key */
				return;
		}
		constraint_plane = ((CON_AXIS0 | CON_AXIS1 | CON_AXIS2) & (~constraint_axis));

		if (edit_2d && (key_type != ZKEY)) {
			if (cmode == axis) {
				stopConstraint(t);
			}
			else {
				setUserConstraint(t, V3D_MANIP_GLOBAL, constraint_axis, msg1);
			}
		}
		else if (!edit_2d) {
			if (cmode == axis) {
				if (t->con.orientation != V3D_MANIP_GLOBAL) {
					stopConstraint(t);
				}
				else {
					short orientation = (t->current_orientation != V3D_MANIP_GLOBAL ?
					                     t->current_orientation : V3D_MANIP_LOCAL);
					if (is_plane == false) {
						setUserConstraint(t, orientation, constraint_axis, msg2);
					}
					else {
						setUserConstraint(t, orientation, constraint_plane, msg3);
					}
				}
			}
			else {
				if (is_plane == false) {
					setUserConstraint(t, V3D_MANIP_GLOBAL, constraint_axis, msg2);
				}
				else {
					setUserConstraint(t, V3D_MANIP_GLOBAL, constraint_plane, msg3);
				}
			}
		}
		t->redraw |= TREDRAW_HARD;
	}
}

int transformEvent(TransInfo *t, const wmEvent *event)
{
	char cmode = constraintModeToChar(t);
	bool handled = false;
	const int modifiers_prev = t->modifiers;

	t->redraw |= handleMouseInput(t, &t->mouse, event);

	/* Handle modal numinput events first, if already activated. */
	if (((event->val == KM_PRESS) || (event->type == EVT_MODAL_MAP)) &&
	    hasNumInput(&t->num) && handleNumInput(t->context, &(t->num), event))
	{
		t->redraw |= TREDRAW_HARD;
		handled = true;
	}
	else if (event->type == MOUSEMOVE) {
		if (t->modifiers & MOD_CONSTRAINT_SELECT)
			t->con.mode |= CON_SELECT;

		copy_v2_v2_int(t->mval, event->mval);

		/* Use this for soft redraw. Might cause flicker in object mode */
		// t->redraw |= TREDRAW_SOFT;
		t->redraw |= TREDRAW_HARD;

		if (t->state == TRANS_STARTING) {
			t->state = TRANS_RUNNING;
		}

		applyMouseInput(t, &t->mouse, t->mval, t->values);

		// Snapping mouse move events
		t->redraw |= handleSnapping(t, event);
		handled = true;
	}
	/* handle modal keymap first */
	else if (event->type == EVT_MODAL_MAP) {
		switch (event->val) {
			case TFM_MODAL_CANCEL:
				t->state = TRANS_CANCEL;
				handled = true;
				break;
			case TFM_MODAL_CONFIRM:
				t->state = TRANS_CONFIRM;
				handled = true;
				break;
			case TFM_MODAL_TRANSLATE:
				/* only switch when... */
				if (ELEM(t->mode, TFM_ROTATION, TFM_RESIZE, TFM_TRACKBALL, TFM_EDGE_SLIDE, TFM_VERT_SLIDE)) {
					restoreTransObjects(t);
					resetTransModal(t);
					resetTransRestrictions(t);
					initTranslation(t);
					initSnapping(t, NULL); // need to reinit after mode change
					t->redraw |= TREDRAW_HARD;
					WM_event_add_mousemove(t->context);
					handled = true;
				}
				else {
					if (t->obedit && t->obedit->type == OB_MESH) {
						if ((t->mode == TFM_TRANSLATION) && (t->spacetype == SPACE_VIEW3D)) {
							restoreTransObjects(t);
							resetTransModal(t);
							resetTransRestrictions(t);

							/* first try edge slide */
							initEdgeSlide(t);
							/* if that fails, do vertex slide */
							if (t->state == TRANS_CANCEL) {
								t->state = TRANS_STARTING;
								initVertSlide(t);
							}
							/* vert slide can fail on unconnected vertices (rare but possible) */
							if (t->state == TRANS_CANCEL) {
								t->mode = TFM_TRANSLATION;
								t->state = TRANS_STARTING;
								restoreTransObjects(t);
								resetTransRestrictions(t);
								initTranslation(t);
							}
							initSnapping(t, NULL); // need to reinit after mode change
							t->redraw |= TREDRAW_HARD;
							handled = true;
							WM_event_add_mousemove(t->context);
						}
					}
				}
				break;
			case TFM_MODAL_ROTATE:
				/* only switch when... */
				if (!(t->options & CTX_TEXTURE) ) {
					if (ELEM(t->mode, TFM_ROTATION, TFM_RESIZE, TFM_TRACKBALL, TFM_TRANSLATION, TFM_EDGE_SLIDE, TFM_VERT_SLIDE)) {
						restoreTransObjects(t);
						resetTransModal(t);
						resetTransRestrictions(t);

						if (t->mode == TFM_ROTATION) {
							initTrackball(t);
						}
						else {
							initRotation(t);
						}
						initSnapping(t, NULL); // need to reinit after mode change
						t->redraw |= TREDRAW_HARD;
						handled = true;
					}
				}
				break;
			case TFM_MODAL_RESIZE:
				/* only switch when... */
				if (ELEM(t->mode, TFM_ROTATION, TFM_TRANSLATION, TFM_TRACKBALL, TFM_EDGE_SLIDE, TFM_VERT_SLIDE)) {

					/* Scale isn't normally very useful after extrude along normals, see T39756 */
					if ((t->con.mode & CON_APPLY) && (t->con.orientation == V3D_MANIP_NORMAL)) {
						stopConstraint(t);
					}

					restoreTransObjects(t);
					resetTransModal(t);
					resetTransRestrictions(t);
					initResize(t);
					initSnapping(t, NULL); // need to reinit after mode change
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				else if (t->mode == TFM_SHRINKFATTEN) {
					t->flag ^= T_ALT_TRANSFORM;
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				else if (t->mode == TFM_RESIZE) {
				}
				break;

			case TFM_MODAL_SNAP_INV_ON:
				t->modifiers |= MOD_SNAP_INVERT;
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;
			case TFM_MODAL_SNAP_INV_OFF:
				t->modifiers &= ~MOD_SNAP_INVERT;
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;
			case TFM_MODAL_SNAP_TOGGLE:
				t->modifiers ^= MOD_SNAP;
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;
			case TFM_MODAL_AXIS_X:
				if (!(t->flag & T_NO_CONSTRAINT)) {
					transform_event_xyz_constraint(t, XKEY, cmode, false);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_AXIS_Y:
				if ((t->flag & T_NO_CONSTRAINT) == 0) {
					transform_event_xyz_constraint(t, YKEY, cmode, false);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_AXIS_Z:
				if ((t->flag & (T_NO_CONSTRAINT)) == 0) {
					transform_event_xyz_constraint(t, ZKEY, cmode, false);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_PLANE_X:
				if ((t->flag & (T_NO_CONSTRAINT | T_2D_EDIT)) == 0) {
					transform_event_xyz_constraint(t, XKEY, cmode, true);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_PLANE_Y:
				if ((t->flag & (T_NO_CONSTRAINT | T_2D_EDIT)) == 0) {
					transform_event_xyz_constraint(t, YKEY, cmode, true);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_PLANE_Z:
				if ((t->flag & (T_NO_CONSTRAINT | T_2D_EDIT)) == 0) {
					transform_event_xyz_constraint(t, ZKEY, cmode, true);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_CONS_OFF:
				if ((t->flag & T_NO_CONSTRAINT) == 0) {
					stopConstraint(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_ADD_SNAP:
				addSnapPoint(t);
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;
			case TFM_MODAL_REMOVE_SNAP:
				removeSnapPoint(t);
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;
			case TFM_MODAL_PROPSIZE:
				/* MOUSEPAN usage... */
				if (t->flag & T_PROP_EDIT) {
					float fac = 1.0f + 0.005f *(event->y - event->prevy);
					t->prop_size *= fac;
					if (t->spacetype == SPACE_VIEW3D && t->persp != RV3D_ORTHO) {
						t->prop_size = max_ff(min_ff(t->prop_size, ((View3D *)t->view)->far), T_PROP_SIZE_MIN);
					}
					else {
						t->prop_size = max_ff(min_ff(t->prop_size, T_PROP_SIZE_MAX), T_PROP_SIZE_MIN);
					}
					calculatePropRatio(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_PROPSIZE_UP:
				if (t->flag & T_PROP_EDIT) {
					t->prop_size *= (t->modifiers & MOD_PRECISION) ? 1.01f : 1.1f;
					if (t->spacetype == SPACE_VIEW3D && t->persp != RV3D_ORTHO) {
						t->prop_size = min_ff(t->prop_size, ((View3D *)t->view)->far);
					}
					else {
						t->prop_size = min_ff(t->prop_size, T_PROP_SIZE_MAX);
					}
					calculatePropRatio(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case TFM_MODAL_PROPSIZE_DOWN:
				if (t->flag & T_PROP_EDIT) {
					t->prop_size /= (t->modifiers & MOD_PRECISION) ? 1.01f : 1.1f;
					t->prop_size = max_ff(t->prop_size, T_PROP_SIZE_MIN);
					calculatePropRatio(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			default:
				break;
		}
	}
	/* else do non-mapped events */
	else if (event->val == KM_PRESS) {
		switch (event->type) {
			case RIGHTMOUSE:
				t->state = TRANS_CANCEL;
				handled = true;
				break;
			/* enforce redraw of transform when modifiers are used */
			case LEFTSHIFTKEY:
			case RIGHTSHIFTKEY:
				t->modifiers |= MOD_CONSTRAINT_PLANE;
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;

			case SPACEKEY:
				t->state = TRANS_CONFIRM;
				handled = true;
				break;

			case MIDDLEMOUSE:
				if ((t->flag & T_NO_CONSTRAINT) == 0) {
					/* exception for switching to dolly, or trackball, in camera view */
					if (t->flag & T_CAMERA) {
						if (t->mode == TFM_TRANSLATION)
							setLocalConstraint(t, (CON_AXIS2), IFACE_("along local Z"));
						else if (t->mode == TFM_ROTATION) {
							restoreTransObjects(t);
							initTrackball(t);
						}
					}
					else {
						t->modifiers |= MOD_CONSTRAINT_SELECT;
						if (t->con.mode & CON_APPLY) {
							stopConstraint(t);
						}
						else {
							if (event->shift) {
								initSelectConstraint(t, t->spacemtx);
							}
							else {
								/* bit hackish... but it prevents mmb select to print the orientation from menu */
								float mati[3][3];
								strcpy(t->spacename, "global");
								unit_m3(mati);
								initSelectConstraint(t, mati);
							}
							postSelectConstraint(t);
						}
					}
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case ESCKEY:
				t->state = TRANS_CANCEL;
				handled = true;
				break;
			case PADENTER:
			case RETKEY:
				t->state = TRANS_CONFIRM;
				handled = true;
				break;
			case GKEY:
				/* only switch when... */
				if (ELEM(t->mode, TFM_ROTATION, TFM_RESIZE, TFM_TRACKBALL)) {
					restoreTransObjects(t);
					resetTransModal(t);
					resetTransRestrictions(t);
					initTranslation(t);
					initSnapping(t, NULL); // need to reinit after mode change
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case SKEY:
				/* only switch when... */
				if (ELEM(t->mode, TFM_ROTATION, TFM_TRANSLATION, TFM_TRACKBALL)) {
					restoreTransObjects(t);
					resetTransModal(t);
					resetTransRestrictions(t);
					initResize(t);
					initSnapping(t, NULL); // need to reinit after mode change
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case RKEY:
				/* only switch when... */
				if (!(t->options & CTX_TEXTURE)) {
					if (ELEM(t->mode, TFM_ROTATION, TFM_RESIZE, TFM_TRACKBALL, TFM_TRANSLATION)) {
						restoreTransObjects(t);
						resetTransModal(t);
						resetTransRestrictions(t);

						if (t->mode == TFM_ROTATION) {
							initTrackball(t);
						}
						else {
							initRotation(t);
						}
						initSnapping(t, NULL); // need to reinit after mode change
						t->redraw |= TREDRAW_HARD;
						handled = true;
					}
				}
				break;
			case CKEY:
				if (event->alt) {
					if (!(t->options & CTX_NO_PET)) {
						t->flag ^= T_PROP_CONNECTED;
						sort_trans_data_dist(t);
						calculatePropRatio(t);
						t->redraw = TREDRAW_HARD;
						handled = true;
					}
				}
				break;
			case OKEY:
				if (t->flag & T_PROP_EDIT && event->shift) {
					t->prop_mode = (t->prop_mode + 1) % PROP_MODE_MAX;
					calculatePropRatio(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case PADPLUSKEY:
				if (event->alt && t->flag & T_PROP_EDIT) {
					t->prop_size *= (t->modifiers & MOD_PRECISION) ? 1.01f : 1.1f;
					if (t->spacetype == SPACE_VIEW3D && t->persp != RV3D_ORTHO)
						t->prop_size = min_ff(t->prop_size, ((View3D *)t->view)->far);
					calculatePropRatio(t);
					t->redraw = TREDRAW_HARD;
					handled = true;
				}
				break;
			case PADMINUS:
				if (event->alt && t->flag & T_PROP_EDIT) {
					t->prop_size /= (t->modifiers & MOD_PRECISION) ? 1.01f : 1.1f;
					calculatePropRatio(t);
					t->redraw = TREDRAW_HARD;
					handled = true;
				}
				break;
			case LEFTALTKEY:
			case RIGHTALTKEY:
				if (ELEM(t->spacetype, SPACE_VIEW3D)) {
					t->flag |= T_ALT_TRANSFORM;
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			default:
				break;
		}

		/* Snapping key events */
		t->redraw |= handleSnapping(t, event);
	}
	else if (event->val == KM_RELEASE) {
		switch (event->type) {
			case LEFTSHIFTKEY:
			case RIGHTSHIFTKEY:
				t->modifiers &= ~MOD_CONSTRAINT_PLANE;
				t->redraw |= TREDRAW_HARD;
				handled = true;
				break;

			case MIDDLEMOUSE:
				if ((t->flag & T_NO_CONSTRAINT) == 0) {
					t->modifiers &= ~MOD_CONSTRAINT_SELECT;
					postSelectConstraint(t);
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			case LEFTALTKEY:
			case RIGHTALTKEY:
				if (ELEM(t->spacetype, SPACE_VIEW3D)) {
					t->flag &= ~T_ALT_TRANSFORM;
					t->redraw |= TREDRAW_HARD;
					handled = true;
				}
				break;
			default:
				break;
		}

		/* confirm transform if launch key is released after mouse move */
		if (t->flag & T_RELEASE_CONFIRM) {
			/* XXX Keyrepeat bug in Xorg messes this up, will test when fixed */
			if ((event->type == t->launch_event) && ISMOUSE(t->launch_event)) {
				t->state = TRANS_CONFIRM;
			}
		}
	}

	/* if we change snap options, get the unsnapped values back */
	if ((t->modifiers   & (MOD_SNAP | MOD_SNAP_INVERT)) !=
	    (modifiers_prev & (MOD_SNAP | MOD_SNAP_INVERT)))
	{
		applyMouseInput(t, &t->mouse, t->mval, t->values);
	}

	/* Per transform event, if present */
	if (t->handleEvent &&
	    (!handled ||
	     /* Needed for vertex slide, see [#38756] */
	     (event->type == MOUSEMOVE)))
	{
		t->redraw |= t->handleEvent(t, event);
	}

	/* Try to init modal numinput now, if possible. */
	if (!(handled || t->redraw) && ((event->val == KM_PRESS) || (event->type == EVT_MODAL_MAP)) &&
	    handleNumInput(t->context, &(t->num), event))
	{
		t->redraw |= TREDRAW_HARD;
		handled = true;
	}

	if (handled || t->redraw) {
		return 0;
	}
	else {
		return OPERATOR_PASS_THROUGH;
	}
}

bool calculateTransformCenter(bContext *C, int centerMode, float cent3d[3], float cent2d[2])
{
	TransInfo *t = MEM_callocN(sizeof(TransInfo), "TransInfo data");
	bool success;

	t->state = TRANS_RUNNING;

	/* avoid calculating PET */
	t->options = CTX_NO_PET;

	t->mode = TFM_DUMMY;

	initTransInfo(C, t, NULL, NULL);

	/* avoid doing connectivity lookups (when V3D_AROUND_LOCAL_ORIGINS is set) */
	t->around = V3D_AROUND_CENTER_BOUNDS;

	createTransData(C, t);              // make TransData structs from selection

	t->around = centerMode;             // override userdefined mode

	if (t->total == 0) {
		success = false;
	}
	else {
		success = true;

		calculateCenter(t);

		if (cent2d) {
			copy_v2_v2(cent2d, t->center2d);
		}

		if (cent3d) {
			// Copy center from constraint center. Transform center can be local
			copy_v3_v3(cent3d, t->center_global);
		}
	}


	/* aftertrans does insert keyframes, and clears base flags; doesn't read transdata */
	special_aftertrans_update(C, t);

	postTrans(C, t);

	MEM_freeN(t);

	return success;
}

typedef enum {
	UP,
	DOWN,
	LEFT,
	RIGHT
} ArrowDirection;
static void drawArrow(ArrowDirection d, short offset, short length, short size)
{
	switch (d) {
		case LEFT:
			offset = -offset;
			length = -length;
			size = -size;
			ATTR_FALLTHROUGH;
		case RIGHT:
			glBegin(GL_LINES);
			glVertex2s(offset, 0);
			glVertex2s(offset + length, 0);
			glVertex2s(offset + length, 0);
			glVertex2s(offset + length - size, -size);
			glVertex2s(offset + length, 0);
			glVertex2s(offset + length - size,  size);
			glEnd();
			break;

		case DOWN:
			offset = -offset;
			length = -length;
			size = -size;
			ATTR_FALLTHROUGH;
		case UP:
			glBegin(GL_LINES);
			glVertex2s(0, offset);
			glVertex2s(0, offset + length);
			glVertex2s(0, offset + length);
			glVertex2s(-size, offset + length - size);
			glVertex2s(0, offset + length);
			glVertex2s(size, offset + length - size);
			glEnd();
			break;
	}
}

static void drawArrowHead(ArrowDirection d, short size)
{
	switch (d) {
		case LEFT:
			size = -size;
			ATTR_FALLTHROUGH;
		case RIGHT:
			glBegin(GL_LINES);
			glVertex2s(0, 0);
			glVertex2s(-size, -size);
			glVertex2s(0, 0);
			glVertex2s(-size,  size);
			glEnd();
			break;

		case DOWN:
			size = -size;
			ATTR_FALLTHROUGH;
		case UP:
			glBegin(GL_LINES);
			glVertex2s(0, 0);
			glVertex2s(-size, -size);
			glVertex2s(0, 0);
			glVertex2s(size, -size);
			glEnd();
			break;
	}
}

static void drawArc(float size, float angle_start, float angle_end, int segments)
{
	float delta = (angle_end - angle_start) / segments;
	float angle;
	int a;

	glBegin(GL_LINE_STRIP);

	for (angle = angle_start, a = 0; a < segments; angle += delta, a++) {
		glVertex2f(cosf(angle) * size, sinf(angle) * size);
	}
	glVertex2f(cosf(angle_end) * size, sinf(angle_end) * size);

	glEnd();
}

static bool helpline_poll(bContext *C)
{
	ARegion *ar = CTX_wm_region(C);

	if (ar && ar->regiontype == RGN_TYPE_WINDOW)
		return 1;
	return 0;
}

static void drawHelpline(bContext *UNUSED(C), int x, int y, void *customdata)
{
	TransInfo *t = (TransInfo *)customdata;

	if (t->helpline != HLP_NONE && !(t->flag & T_USES_MANIPULATOR)) {
		float cent[2];
		int mval[2];

		mval[0] = x;
		mval[1] = y;

		projectFloatViewEx(t, t->center_global, cent, V3D_PROJ_TEST_CLIP_ZERO);

		glPushMatrix();

		switch (t->helpline) {
			case HLP_SPRING:
				UI_ThemeColor(TH_VIEW_OVERLAY);

				setlinestyle(3);
				glLineWidth(1);
				glBegin(GL_LINES);
				glVertex2iv(t->mval);
				glVertex2fv(cent);
				glEnd();

				glTranslate2iv(mval);
				glRotatef(-RAD2DEGF(atan2f(cent[0] - t->mval[0], cent[1] - t->mval[1])), 0, 0, 1);

				setlinestyle(0);
				glLineWidth(3.0);
				drawArrow(UP, 5, 10, 5);
				drawArrow(DOWN, 5, 10, 5);
				break;
			case HLP_HARROW:
				UI_ThemeColor(TH_VIEW_OVERLAY);

				glTranslate2iv(mval);

				glLineWidth(3.0);
				drawArrow(RIGHT, 5, 10, 5);
				drawArrow(LEFT, 5, 10, 5);
				break;
			case HLP_VARROW:
				UI_ThemeColor(TH_VIEW_OVERLAY);

				glTranslate2iv(mval);

				glLineWidth(3.0);
				drawArrow(UP, 5, 10, 5);
				drawArrow(DOWN, 5, 10, 5);
				break;
			case HLP_ANGLE:
			{
				float dx = t->mval[0] - cent[0], dy = t->mval[1] - cent[1];
				float angle = atan2f(dy, dx);
				float dist = hypotf(dx, dy);
				float delta_angle = min_ff(15.0f / dist, (float)M_PI / 4.0f);
				float spacing_angle = min_ff(5.0f / dist, (float)M_PI / 12.0f);
				UI_ThemeColor(TH_VIEW_OVERLAY);

				setlinestyle(3);
				glLineWidth(1);
				glBegin(GL_LINES);
				glVertex2iv(t->mval);
				glVertex2fv(cent);
				glEnd();

				glTranslatef(cent[0] - t->mval[0] + mval[0], cent[1] - t->mval[1] + mval[1], 0);

				setlinestyle(0);
				glLineWidth(3.0);
				drawArc(dist, angle - delta_angle, angle - spacing_angle, 10);
				drawArc(dist, angle + spacing_angle, angle + delta_angle, 10);

				glPushMatrix();

				glTranslatef(cosf(angle - delta_angle) * dist, sinf(angle - delta_angle) * dist, 0);
				glRotatef(RAD2DEGF(angle - delta_angle), 0, 0, 1);

				drawArrowHead(DOWN, 5);

				glPopMatrix();

				glTranslatef(cosf(angle + delta_angle) * dist, sinf(angle + delta_angle) * dist, 0);
				glRotatef(RAD2DEGF(angle + delta_angle), 0, 0, 1);

				drawArrowHead(UP, 5);
				break;
			}
			case HLP_TRACKBALL:
			{
				unsigned char col[3], col2[3];
				UI_GetThemeColor3ubv(TH_GRID, col);

				glTranslate2iv(mval);

				glLineWidth(3.0);

				UI_make_axis_color(col, col2, 'X');
				glColor3ubv((GLubyte *)col2);

				drawArrow(RIGHT, 5, 10, 5);
				drawArrow(LEFT, 5, 10, 5);

				UI_make_axis_color(col, col2, 'Y');
				glColor3ubv((GLubyte *)col2);

				drawArrow(UP, 5, 10, 5);
				drawArrow(DOWN, 5, 10, 5);
				break;
			}
		}

		glPopMatrix();
	}
}

static void drawTransformView(const struct bContext *C, ARegion *UNUSED(ar), void *arg)
{
	TransInfo *t = arg;

	glLineWidth(1.0);

	drawConstraint(t);
	drawPropCircle(C, t);
	drawSnapping(C, t);

	/* edge slide, vert slide */
	drawEdgeSlide(t);
	drawVertSlide(t);
}

/**
 * \see #initTransform which reads values from the operator.
 */
void saveTransform(bContext *C, TransInfo *t, wmOperator *op)
{
	ToolSettings *ts = CTX_data_tool_settings(C);
	bool constraint_axis[3] = {false, false, false};
	int proportional = 0;
	PropertyRNA *prop;

	// Save back mode in case we're in the generic operator
	if ((prop = RNA_struct_find_property(op->ptr, "mode"))) {
		RNA_property_enum_set(op->ptr, prop, t->mode);
	}

	if ((prop = RNA_struct_find_property(op->ptr, "value"))) {
		float values[4];

		copy_v4_v4(values, (t->flag & T_AUTOVALUES) ? t->auto_values : t->values);

		if (RNA_property_array_check(prop)) {
			RNA_property_float_set_array(op->ptr, prop, values);
		}
		else {
			RNA_property_float_set(op->ptr, prop, values[0]);
		}
	}

	/* convert flag to enum */
	switch (t->flag & T_PROP_EDIT_ALL) {
		case T_PROP_EDIT:
			proportional = PROP_EDIT_ON;
			break;
		case (T_PROP_EDIT | T_PROP_CONNECTED):
			proportional = PROP_EDIT_CONNECTED;
			break;
		case (T_PROP_EDIT | T_PROP_PROJECTED):
			proportional = PROP_EDIT_PROJECTED;
			break;
		default:
			proportional = PROP_EDIT_OFF;
			break;
	}

	// If modal, save settings back in scene if not set as operator argument
	if ((t->flag & T_MODAL) || (op->flag & OP_IS_REPEAT)) {
		/* save settings if not set in operator */

		/* skip saving proportional edit if it was not actually used */
		if (!(t->options & CTX_NO_PET)) {
			if ((prop = RNA_struct_find_property(op->ptr, "proportional")) &&
			    !RNA_property_is_set(op->ptr, prop))
			{
				if (t->obedit)
					ts->proportional = proportional;
				else
					ts->proportional_objects = (proportional != PROP_EDIT_OFF);
			}

			if ((prop = RNA_struct_find_property(op->ptr, "proportional_size"))) {
				ts->proportional_size =
				        RNA_property_is_set(op->ptr, prop) ? RNA_property_float_get(op->ptr, prop) : t->prop_size;
			}

			if ((prop = RNA_struct_find_property(op->ptr, "proportional_edit_falloff")) &&
			    !RNA_property_is_set(op->ptr, prop))
			{
				ts->prop_mode = t->prop_mode;
			}
		}

		/* do we check for parameter? */
		if (t->modifiers & MOD_SNAP) {
			ts->snap_flag |= SCE_SNAP;
		}
		else {
			ts->snap_flag &= ~SCE_SNAP;
		}

		if (t->spacetype == SPACE_VIEW3D) {
			if ((prop = RNA_struct_find_property(op->ptr, "constraint_orientation")) &&
			    !RNA_property_is_set(op->ptr, prop))
			{
				View3D *v3d = t->view;

				v3d->twmode = t->current_orientation;
			}
		}
	}

	if ((prop = RNA_struct_find_property(op->ptr, "proportional"))) {
		RNA_property_enum_set(op->ptr, prop, proportional);
		RNA_enum_set(op->ptr, "proportional_edit_falloff", t->prop_mode);
		RNA_float_set(op->ptr, "proportional_size", t->prop_size);
	}

	if ((prop = RNA_struct_find_property(op->ptr, "axis"))) {
		RNA_property_float_set_array(op->ptr, prop, t->axis);
	}

	if ((prop = RNA_struct_find_property(op->ptr, "mirror"))) {
		RNA_property_boolean_set(op->ptr, prop, (t->flag & T_MIRROR) != 0);
	}

	if ((prop = RNA_struct_find_property(op->ptr, "constraint_axis"))) {
		/* constraint orientation can be global, event if user selects something else
		 * so use the orientation in the constraint if set
		 * */
		if (t->con.mode & CON_APPLY) {
			RNA_enum_set(op->ptr, "constraint_orientation", t->con.orientation);
		}
		else {
			RNA_enum_set(op->ptr, "constraint_orientation", t->current_orientation);
		}

		if (t->con.mode & CON_APPLY) {
			if (t->con.mode & CON_AXIS0) {
				constraint_axis[0] = true;
			}
			if (t->con.mode & CON_AXIS1) {
				constraint_axis[1] = true;
			}
			if (t->con.mode & CON_AXIS2) {
				constraint_axis[2] = true;
			}
		}

		/* Only set if needed, so we can hide in the UI when nothing is set.
		 * See 'transform_poll_property'. */
		if (ELEM(true, UNPACK3(constraint_axis))) {
			RNA_property_boolean_set_array(op->ptr, prop, constraint_axis);
		}
	}

	{
		const char *prop_id = NULL;
		if (t->mode == TFM_SHRINKFATTEN) {
			prop_id = "use_even_offset";
		}

		if (prop_id && (prop = RNA_struct_find_property(op->ptr, prop_id))) {

			RNA_property_boolean_set(op->ptr, prop, (t->flag & T_ALT_TRANSFORM) != 0);
		}
	}

}

/**
 * \note  caller needs to free 't' on a 0 return
 * \warning  \a event might be NULL (when tweaking from redo panel)
 * \see #saveTransform which writes these values back.
 */
bool initTransform(bContext *C, TransInfo *t, wmOperator *op, const wmEvent *event, int mode)
{
	int options = 0;
	PropertyRNA *prop;

	t->context = C;

	/* added initialize, for external calls to set stuff in TransInfo, like undo string */

	t->state = TRANS_STARTING;

	if ((prop = RNA_struct_find_property(op->ptr, "texture_space")) && RNA_property_is_set(op->ptr, prop)) {
		if (RNA_property_boolean_get(op->ptr, prop)) {
			options |= CTX_TEXTURE;
		}
	}

	t->options = options;

	t->mode = mode;

	/* Needed to translate tweak events to mouse buttons. */
	t->launch_event = event ? WM_userdef_event_type_from_keymap_type(event->type) : -1;

	// XXX Remove this when wm_operator_call_internal doesn't use window->eventstate (which can have type = 0)
	// For manipulator only, so assume LEFTMOUSE
	if (t->launch_event == 0) {
		t->launch_event = LEFTMOUSE;
	}

	unit_m3(t->spacemtx);

	initTransInfo(C, t, op, event);
	initTransformOrientation(C, t);

	if (t->spacetype == SPACE_VIEW3D) {
		t->draw_handle_apply = ED_region_draw_cb_activate(t->ar->type, drawTransformApply, t, REGION_DRAW_PRE_VIEW);
		t->draw_handle_view = ED_region_draw_cb_activate(t->ar->type, drawTransformView, t, REGION_DRAW_POST_VIEW);
		t->draw_handle_cursor = WM_paint_cursor_activate(CTX_wm_manager(C), helpline_poll, drawHelpline, t);
	}
	else if (t->spacetype == SPACE_IMAGE) {
		t->draw_handle_view = ED_region_draw_cb_activate(t->ar->type, drawTransformView, t, REGION_DRAW_POST_VIEW);
		t->draw_handle_cursor = WM_paint_cursor_activate(CTX_wm_manager(C), helpline_poll, drawHelpline, t);
	}

	createTransData(C, t);          // make TransData structs from selection

	if (t->total == 0) {
		postTrans(C, t);
		return 0;
	}

	if (event) {
		/* keymap for shortcut header prints */
		t->keymap = WM_keymap_active(CTX_wm_manager(C), op->type->modalkeymap);

		/* Stupid code to have Ctrl-Click on manipulator work ok
		 *
		 * do this only for translation/rotation/resize due to only this
		 * moded are available from manipulator and doing such check could
		 * lead to keymap conflicts for other modes (see #31584)
		 */
		if (ELEM(mode, TFM_TRANSLATION, TFM_ROTATION, TFM_RESIZE)) {
			wmKeyMapItem *kmi;

			for (kmi = t->keymap->items.first; kmi; kmi = kmi->next) {
				if (kmi->propvalue == TFM_MODAL_SNAP_INV_ON && kmi->val == KM_PRESS) {
					if ((ELEM(kmi->type, LEFTCTRLKEY, RIGHTCTRLKEY) &&   event->ctrl)  ||
					    (ELEM(kmi->type, LEFTSHIFTKEY, RIGHTSHIFTKEY) && event->shift) ||
					    (ELEM(kmi->type, LEFTALTKEY, RIGHTALTKEY) &&     event->alt)   ||
					    ((kmi->type == OSKEY) &&                         event->oskey) )
					{
						t->modifiers |= MOD_SNAP_INVERT;
					}
					break;
				}
			}
		}
	}

	initSnapping(t, op); // Initialize snapping data AFTER mode flags

	initSnapSpatial(t, t->snap_spatial);

	mode = t->mode;

	calculatePropRatio(t);
	calculateCenter(t);

	if (event) {
		/* Initialize accurate transform to settings requested by keymap. */
		bool use_accurate = false;
		if ((prop = RNA_struct_find_property(op->ptr, "use_accurate")) && RNA_property_is_set(op->ptr, prop)) {
			if (RNA_property_boolean_get(op->ptr, prop)) {
				use_accurate = true;
			}
		}
		initMouseInput(t, &t->mouse, t->center2d, event->mval, use_accurate);
	}

	switch (mode) {
		case TFM_TRANSLATION:
			initTranslation(t);
			break;
		case TFM_ROTATION:
			initRotation(t);
			break;
		case TFM_RESIZE:
			initResize(t);
			break;
		case TFM_TOSPHERE:
			initToSphere(t);
			break;
		case TFM_SHEAR:
			initShear(t);
			break;
		case TFM_BEND:
			initBend(t);
			break;
		case TFM_SHRINKFATTEN:
			initShrinkFatten(t);
			break;
		case TFM_TILT:
			initTilt(t);
			break;
		case TFM_CURVE_SHRINKFATTEN:
			initCurveShrinkFatten(t);
			break;
		case TFM_TRACKBALL:
			initTrackball(t);
			break;
		case TFM_PUSHPULL:
			initPushPull(t);
			break;
		case TFM_CREASE:
			initCrease(t);
			break;
		case TFM_EDGE_SLIDE:
		case TFM_VERT_SLIDE:
		{
			const bool use_even = (op ? RNA_boolean_get(op->ptr, "use_even") : false);
			const bool flipped = (op ? RNA_boolean_get(op->ptr, "flipped") : false);
			const bool use_clamp = (op ? RNA_boolean_get(op->ptr, "use_clamp") : true);
			if (mode == TFM_EDGE_SLIDE) {
				const bool use_double_side = (op ? !RNA_boolean_get(op->ptr, "single_side") : true);
				initEdgeSlide_ex(t, use_double_side, use_even, flipped, use_clamp);
			}
			else {
				initVertSlide_ex(t, use_even, flipped, use_clamp);
			}
			break;
		}
		case TFM_MIRROR:
			initMirror(t);
			break;
		case TFM_ALIGN:
			initAlign(t);
			break;
	}

	if (t->state == TRANS_CANCEL) {
		postTrans(C, t);
		return 0;
	}

	/* Transformation axis from operator */
	if ((prop = RNA_struct_find_property(op->ptr, "axis")) && RNA_property_is_set(op->ptr, prop)) {
		RNA_property_float_get_array(op->ptr, prop, t->axis);
		normalize_v3(t->axis);
		copy_v3_v3(t->axis_orig, t->axis);
	}

	/* Constraint init from operator */
	if ((prop = RNA_struct_find_property(op->ptr, "constraint_axis")) && RNA_property_is_set(op->ptr, prop)) {
		bool constraint_axis[3];

		RNA_property_boolean_get_array(op->ptr, prop, constraint_axis);

		if (constraint_axis[0] || constraint_axis[1] || constraint_axis[2]) {
			t->con.mode |= CON_APPLY;

			if (constraint_axis[0]) {
				t->con.mode |= CON_AXIS0;
			}
			if (constraint_axis[1]) {
				t->con.mode |= CON_AXIS1;
			}
			if (constraint_axis[2]) {
				t->con.mode |= CON_AXIS2;
			}

			setUserConstraint(t, t->current_orientation, t->con.mode, "%s");
		}
	}

	/* overwrite initial values if operator supplied a non-null vector
	 *
	 * keep last so we can apply the constraints space.
	 */
	if ((prop = RNA_struct_find_property(op->ptr, "value")) && RNA_property_is_set(op->ptr, prop)) {
		float values[4] = {0}; /* in case value isn't length 4, avoid uninitialized memory  */

		if (RNA_property_array_check(prop)) {
			RNA_float_get_array(op->ptr, "value", values);
		}
		else {
			values[0] = RNA_float_get(op->ptr, "value");
		}

		copy_v4_v4(t->values, values);
		copy_v4_v4(t->auto_values, values);
		t->flag |= T_AUTOVALUES;
	}

	t->context = NULL;

	return 1;
}

void transformApply(bContext *C, TransInfo *t)
{
	t->context = C;

	if ((t->redraw & TREDRAW_HARD) || (t->draw_handle_apply == NULL && (t->redraw & TREDRAW_SOFT))) {
		selectConstraint(t);
		if (t->transform) {
			t->transform(t, t->mval);  // calls recalcData()
			viewRedrawForce(C, t);
		}
		t->redraw = TREDRAW_NOTHING;
	}
	else if (t->redraw & TREDRAW_SOFT) {
		viewRedrawForce(C, t);
	}

	/* If auto confirm is on, break after one pass */
	if (t->options & CTX_AUTOCONFIRM) {
		t->state = TRANS_CONFIRM;
	}

	t->context = NULL;
}

static void drawTransformApply(const bContext *C, ARegion *UNUSED(ar), void *arg)
{
	TransInfo *t = arg;

	if (t->redraw & TREDRAW_SOFT) {
		t->redraw |= TREDRAW_HARD;
		transformApply((bContext *)C, t);
	}
}

int transformEnd(bContext *C, TransInfo *t)
{
	int exit_code = OPERATOR_RUNNING_MODAL;

	t->context = C;

	if (t->state != TRANS_STARTING && t->state != TRANS_RUNNING) {
		/* handle restoring objects */
		if (t->state == TRANS_CANCEL) {
			/* exception, edge slide transformed UVs too */
			if (t->mode == TFM_EDGE_SLIDE) {
				doEdgeSlide(t, 0.0f);
			}
			else if (t->mode == TFM_VERT_SLIDE) {
				doVertSlide(t, 0.0f);
			}

			exit_code = OPERATOR_CANCELLED;
			restoreTransObjects(t); // calls recalcData()
		}
		else {
			exit_code = OPERATOR_FINISHED;
		}

		/* aftertrans does insert keyframes, and clears base flags; doesn't read transdata */
		special_aftertrans_update(C, t);

		/* free data */
		postTrans(C, t);

		/* send events out for redraws */
		viewRedrawPost(C, t);

		viewRedrawForce(C, t);
	}

	t->context = NULL;

	return exit_code;
}

/* ************************** TRANSFORM LOCKS **************************** */

static void protectedTransBits(short protectflag, float vec[3])
{
	if (protectflag & OB_LOCK_LOCX)
		vec[0] = 0.0f;
	if (protectflag & OB_LOCK_LOCY)
		vec[1] = 0.0f;
	if (protectflag & OB_LOCK_LOCZ)
		vec[2] = 0.0f;
}

static void protectedSizeBits(short protectflag, float size[3])
{
	if (protectflag & OB_LOCK_SCALEX)
		size[0] = 1.0f;
	if (protectflag & OB_LOCK_SCALEY)
		size[1] = 1.0f;
	if (protectflag & OB_LOCK_SCALEZ)
		size[2] = 1.0f;
}

static void protectedRotateBits(short protectflag, float eul[3], const float oldeul[3])
{
	if (protectflag & OB_LOCK_ROTX)
		eul[0] = oldeul[0];
	if (protectflag & OB_LOCK_ROTY)
		eul[1] = oldeul[1];
	if (protectflag & OB_LOCK_ROTZ)
		eul[2] = oldeul[2];
}


/* this function only does the delta rotation */
/* axis-angle is usually internally stored as quats... */
static void protectedAxisAngleBits(short protectflag, float axis[3], float *angle, float oldAxis[3], float oldAngle)
{
	/* check that protection flags are set */
	if ((protectflag & (OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ | OB_LOCK_ROTW)) == 0)
		return;

	if (protectflag & OB_LOCK_ROT4D) {
		/* axis-angle getting limited as 4D entities that they are... */
		if (protectflag & OB_LOCK_ROTW)
			*angle = oldAngle;
		if (protectflag & OB_LOCK_ROTX)
			axis[0] = oldAxis[0];
		if (protectflag & OB_LOCK_ROTY)
			axis[1] = oldAxis[1];
		if (protectflag & OB_LOCK_ROTZ)
			axis[2] = oldAxis[2];
	}
	else {
		/* axis-angle get limited with euler... */
		float eul[3], oldeul[3];

		axis_angle_to_eulO(eul, EULER_ORDER_DEFAULT, axis, *angle);
		axis_angle_to_eulO(oldeul, EULER_ORDER_DEFAULT, oldAxis, oldAngle);

		if (protectflag & OB_LOCK_ROTX)
			eul[0] = oldeul[0];
		if (protectflag & OB_LOCK_ROTY)
			eul[1] = oldeul[1];
		if (protectflag & OB_LOCK_ROTZ)
			eul[2] = oldeul[2];

		eulO_to_axis_angle(axis, angle, eul, EULER_ORDER_DEFAULT);

		/* when converting to axis-angle, we need a special exception for the case when there is no axis */
		if (IS_EQF(axis[0], axis[1]) && IS_EQF(axis[1], axis[2])) {
			/* for now, rotate around y-axis then (so that it simply becomes the roll) */
			axis[1] = 1.0f;
		}
	}
}

/* this function only does the delta rotation */
static void protectedQuaternionBits(short protectflag, float quat[4], const float oldquat[4])
{
	/* check that protection flags are set */
	if ((protectflag & (OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ | OB_LOCK_ROTW)) == 0)
		return;

	if (protectflag & OB_LOCK_ROT4D) {
		/* quaternions getting limited as 4D entities that they are... */
		if (protectflag & OB_LOCK_ROTW)
			quat[0] = oldquat[0];
		if (protectflag & OB_LOCK_ROTX)
			quat[1] = oldquat[1];
		if (protectflag & OB_LOCK_ROTY)
			quat[2] = oldquat[2];
		if (protectflag & OB_LOCK_ROTZ)
			quat[3] = oldquat[3];
	}
	else {
		/* quaternions get limited with euler... (compatibility mode) */
		float eul[3], oldeul[3], nquat[4], noldquat[4];
		float qlen;

		qlen = normalize_qt_qt(nquat, quat);
		normalize_qt_qt(noldquat, oldquat);

		quat_to_eul(eul, nquat);
		quat_to_eul(oldeul, noldquat);

		if (protectflag & OB_LOCK_ROTX)
			eul[0] = oldeul[0];
		if (protectflag & OB_LOCK_ROTY)
			eul[1] = oldeul[1];
		if (protectflag & OB_LOCK_ROTZ)
			eul[2] = oldeul[2];

		eul_to_quat(quat, eul);

		/* restore original quat size */
		mul_qt_fl(quat, qlen);

		/* quaternions flip w sign to accumulate rotations correctly */
		if ((nquat[0] < 0.0f && quat[0] > 0.0f) ||
		    (nquat[0] > 0.0f && quat[0] < 0.0f))
		{
			mul_qt_fl(quat, -1.0f);
		}
	}
}

/* Transform (Bend) */

/** \name Transform Bend
 * \{ */

struct BendCustomData {
	float warp_sta[3];
	float warp_end[3];

	float warp_nor[3];
	float warp_tan[3];

	/* for applying the mouse distance */
	float warp_init_dist;
};

static void initBend(TransInfo *t)
{
	const float mval_fl[2] = {UNPACK2(t->mval)};
	const float *curs;
	float tvec[3];
	struct BendCustomData *data;

	t->mode = TFM_BEND;
	t->transform = Bend;
	t->handleEvent = handleEventBend;

	setInputPostFct(&t->mouse, postInputRotation);
	initMouseInputMode(t, &t->mouse, INPUT_ANGLE_SPRING);

	t->idx_max = 1;
	t->num.idx_max = 1;
	t->snap[0] = 0.0f;
	t->snap[1] = DEG2RAD(5.0);
	t->snap[2] = DEG2RAD(1.0);

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_use_radians = (t->scene->unit.system_rotation == USER_UNIT_ROT_RADIANS);
	t->num.unit_type[0] = B_UNIT_ROTATION;
	t->num.unit_type[1] = B_UNIT_LENGTH;

	t->flag |= T_NO_CONSTRAINT;

	//copy_v3_v3(t->center, ED_view3d_cursor3d_get(t->scene, t->view));
	if ((t->flag & T_OVERRIDE_CENTER) == 0) {
		calculateCenterCursor(t, t->center);
	}
	calculateCenterGlobal(t, t->center, t->center_global);

	t->val = 0.0f;

	data = MEM_callocN(sizeof(*data), __func__);

	curs = ED_view3d_cursor3d_get(t->scene, t->view);
	copy_v3_v3(data->warp_sta, curs);
	ED_view3d_win_to_3d(t->sa->spacedata.first, t->ar, curs, mval_fl, data->warp_end);

	copy_v3_v3(data->warp_nor, t->viewinv[2]);
	if (t->flag & T_EDIT) {
		sub_v3_v3(data->warp_sta, t->obedit->obmat[3]);
		sub_v3_v3(data->warp_end, t->obedit->obmat[3]);
	}
	normalize_v3(data->warp_nor);

	/* tangent */
	sub_v3_v3v3(tvec, data->warp_end, data->warp_sta);
	cross_v3_v3v3(data->warp_tan, tvec, data->warp_nor);
	normalize_v3(data->warp_tan);

	data->warp_init_dist = len_v3v3(data->warp_end, data->warp_sta);

	t->custom.mode.data = data;
	t->custom.mode.use_free = true;
}

static eRedrawFlag handleEventBend(TransInfo *UNUSED(t), const wmEvent *event)
{
	eRedrawFlag status = TREDRAW_NOTHING;

	if (event->type == MIDDLEMOUSE && event->val == KM_PRESS) {
		status = TREDRAW_HARD;
	}

	return status;
}

static void Bend(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	float vec[3];
	float pivot[3];
	float warp_end_radius[3];
	int i;
	char str[UI_MAX_DRAW_STR];
	const struct BendCustomData *data = t->custom.mode.data;
	const bool is_clamp = (t->flag & T_ALT_TRANSFORM) == 0;

	union {
		struct { float angle, scale; };
		float vector[2];
	} values;

	/* amount of radians for bend */
	copy_v2_v2(values.vector, t->values);

#if 0
	snapGrid(t, angle_rad);
#else
	/* hrmf, snapping radius is using 'angle' steps, need to convert to something else
	 * this isnt essential but nicer to give reasonable snapping values for radius */
	if (t->tsnap.mode == SCE_SNAP_MODE_INCREMENT) {
		const float radius_snap = 0.1f;
		const float snap_hack = (t->snap[1] * data->warp_init_dist) / radius_snap;
		values.scale *= snap_hack;
		snapGridIncrement(t, values.vector);
		values.scale /= snap_hack;
	}
#endif

	if (applyNumInput(&t->num, values.vector)) {
		values.scale = values.scale / data->warp_init_dist;
	}

	copy_v2_v2(t->values, values.vector);

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN * 2];

		outputNumInput(&(t->num), c, &t->scene->unit);

		BLI_snprintf(str, sizeof(str), IFACE_("Bend Angle: %s Radius: %s Alt, Clamp %s"),
		             &c[0], &c[NUM_STR_REP_LEN],
		             WM_bool_as_string(is_clamp));
	}
	else {
		/* default header print */
		BLI_snprintf(str, sizeof(str), IFACE_("Bend Angle: %.3f Radius: %.4f, Alt, Clamp %s"),
		             RAD2DEGF(values.angle), values.scale * data->warp_init_dist,
		             WM_bool_as_string(is_clamp));
	}

	values.angle *= -1.0f;
	values.scale *= data->warp_init_dist;

	/* calc 'data->warp_end' from 'data->warp_end_init' */
	copy_v3_v3(warp_end_radius, data->warp_end);
	dist_ensure_v3_v3fl(warp_end_radius, data->warp_sta, values.scale);
	/* done */

	/* calculate pivot */
	copy_v3_v3(pivot, data->warp_sta);
	if (values.angle > 0.0f) {
		madd_v3_v3fl(pivot, data->warp_tan, -values.scale * shell_angle_to_dist((float)M_PI_2 - values.angle));
	}
	else {
		madd_v3_v3fl(pivot, data->warp_tan, +values.scale * shell_angle_to_dist((float)M_PI_2 + values.angle));
	}

	for (i = 0; i < t->total; i++, td++) {
		float mat[3][3];
		float delta[3];
		float fac, fac_scaled;

		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (UNLIKELY(values.angle == 0.0f)) {
			copy_v3_v3(td->loc, td->iloc);
			continue;
		}

		copy_v3_v3(vec, td->iloc);
		mul_m3_v3(td->mtx, vec);

		fac = line_point_factor_v3(vec, data->warp_sta, warp_end_radius);
		if (is_clamp) {
			CLAMP(fac, 0.0f, 1.0f);
		}

		fac_scaled = fac * td->factor;
		axis_angle_normalized_to_mat3(mat, data->warp_nor, values.angle * fac_scaled);
		interp_v3_v3v3(delta, data->warp_sta, warp_end_radius, fac_scaled);
		sub_v3_v3(delta, data->warp_sta);

		/* delta is subtracted, rotation adds back this offset */
		sub_v3_v3(vec, delta);

		sub_v3_v3(vec, pivot);
		mul_m3_v3(mat, vec);
		add_v3_v3(vec, pivot);

		mul_m3_v3(td->smtx, vec);

		/* rotation */
		if ((t->flag & T_POINTS) == 0) {
			ElementRotation(t, td, mat, V3D_AROUND_LOCAL_ORIGINS);
		}

		/* location */
		copy_v3_v3(td->loc, vec);
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Shear) */

/** \name Transform Shear
 * \{ */

static void initShear(TransInfo *t)
{
	t->mode = TFM_SHEAR;
	t->transform = applyShear;
	t->handleEvent = handleEventShear;

	initMouseInputMode(t, &t->mouse, INPUT_HORIZONTAL_RATIO);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;  /* Don't think we have any unit here? */

	t->flag |= T_NO_CONSTRAINT;
}

static eRedrawFlag handleEventShear(TransInfo *t, const wmEvent *event)
{
	eRedrawFlag status = TREDRAW_NOTHING;

	if (event->type == MIDDLEMOUSE && event->val == KM_PRESS) {
		/* Use custom.mode.data pointer to signal Shear direction */
		if (t->custom.mode.data == NULL) {
			initMouseInputMode(t, &t->mouse, INPUT_VERTICAL_RATIO);
			t->custom.mode.data = (void *)1;
		}
		else {
			initMouseInputMode(t, &t->mouse, INPUT_HORIZONTAL_RATIO);
			t->custom.mode.data = NULL;
		}

		status = TREDRAW_HARD;
	}
	else if (event->type == XKEY && event->val == KM_PRESS) {
		initMouseInputMode(t, &t->mouse, INPUT_HORIZONTAL_RATIO);
		t->custom.mode.data = NULL;

		status = TREDRAW_HARD;
	}
	else if (event->type == YKEY && event->val == KM_PRESS) {
		initMouseInputMode(t, &t->mouse, INPUT_VERTICAL_RATIO);
		t->custom.mode.data = (void *)1;

		status = TREDRAW_HARD;
	}

	return status;
}


static void applyShear(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	float vec[3];
	float smat[3][3], tmat[3][3], totmat[3][3], persmat[3][3], persinv[3][3];
	float value;
	int i;
	char str[UI_MAX_DRAW_STR];
	const bool is_local_center = transdata_check_local_center(t, t->around);

	copy_m3_m4(persmat, t->viewmat);
	invert_m3_m3(persinv, persmat);

	value = t->values[0];

	snapGridIncrement(t, &value);

	applyNumInput(&t->num, &value);

	t->values[0] = value;

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		BLI_snprintf(str, sizeof(str), IFACE_("Shear: %s %s"), c, t->proptext);
	}
	else {
		/* default header print */
		BLI_snprintf(str, sizeof(str), IFACE_("Shear: %.3f %s (Press X or Y to set shear axis)"), value, t->proptext);
	}

	unit_m3(smat);

	// Custom data signals shear direction
	if (t->custom.mode.data == NULL)
		smat[1][0] = value;
	else
		smat[0][1] = value;

	mul_m3_m3m3(tmat, smat, persmat);
	mul_m3_m3m3(totmat, persinv, tmat);

	for (i = 0; i < t->total; i++, td++) {
		const float *center, *co;

		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (t->obedit) {
			float mat3[3][3];
			mul_m3_m3m3(mat3, totmat, td->mtx);
			mul_m3_m3m3(tmat, td->smtx, mat3);
		}
		else {
			copy_m3_m3(tmat, totmat);
		}

		if (is_local_center) {
			center = td->center;
			co = td->loc;
		}
		else {
			center = t->center;
			co = td->center;
		}

		sub_v3_v3v3(vec, co, center);

		mul_m3_v3(tmat, vec);

		add_v3_v3(vec, center);
		sub_v3_v3(vec, co);

		mul_v3_fl(vec, td->factor);

		add_v3_v3v3(td->loc, td->iloc, vec);
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Resize) */

/** \name Transform Resize
 * \{ */

static void initResize(TransInfo *t)
{
	t->mode = TFM_RESIZE;
	t->transform = applyResize;

	initMouseInputMode(t, &t->mouse, INPUT_SPRING_FLIP);

	t->flag |= T_NULL_ONE;
	t->num.val_flag[0] |= NUM_NULL_ONE;
	t->num.val_flag[1] |= NUM_NULL_ONE;
	t->num.val_flag[2] |= NUM_NULL_ONE;
	t->num.flag |= NUM_AFFECT_ALL;
	if (!t->obedit) {
		t->flag |= T_NO_ZERO;
#ifdef USE_NUM_NO_ZERO
		t->num.val_flag[0] |= NUM_NO_ZERO;
		t->num.val_flag[1] |= NUM_NO_ZERO;
		t->num.val_flag[2] |= NUM_NO_ZERO;
#endif
	}

	t->idx_max = 2;
	t->num.idx_max = 2;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;
	t->num.unit_type[1] = B_UNIT_NONE;
	t->num.unit_type[2] = B_UNIT_NONE;
}

static void headerResize(TransInfo *t, const float vec[3], char str[UI_MAX_DRAW_STR])
{
	char tvec[NUM_STR_REP_LEN * 3];
	size_t ofs = 0;
	if (hasNumInput(&t->num)) {
		outputNumInput(&(t->num), tvec, &t->scene->unit);
	}
	else {
		BLI_snprintf(&tvec[0], NUM_STR_REP_LEN, "%.4f", vec[0]);
		BLI_snprintf(&tvec[NUM_STR_REP_LEN], NUM_STR_REP_LEN, "%.4f", vec[1]);
		BLI_snprintf(&tvec[NUM_STR_REP_LEN * 2], NUM_STR_REP_LEN, "%.4f", vec[2]);
	}

	if (t->con.mode & CON_APPLY) {
		switch (t->num.idx_max) {
			case 0:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_("Scale: %s%s %s"),
				                    &tvec[0], t->con.text, t->proptext);
				break;
			case 1:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_("Scale: %s : %s%s %s"),
				                    &tvec[0], &tvec[NUM_STR_REP_LEN], t->con.text, t->proptext);
				break;
			case 2:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_("Scale: %s : %s : %s%s %s"), &tvec[0],
				                    &tvec[NUM_STR_REP_LEN], &tvec[NUM_STR_REP_LEN * 2], t->con.text, t->proptext);
				break;
		}
	}
	else {
		if (t->flag & T_2D_EDIT) {
			ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_("Scale X: %s   Y: %s%s %s"),
			                    &tvec[0], &tvec[NUM_STR_REP_LEN], t->con.text, t->proptext);
		}
		else {
			ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_("Scale X: %s   Y: %s  Z: %s%s %s"),
			                    &tvec[0], &tvec[NUM_STR_REP_LEN], &tvec[NUM_STR_REP_LEN * 2], t->con.text, t->proptext);
		}
	}

	if (t->flag & T_PROP_EDIT_ALL) {
		ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_(" Proportional size: %.2f"), t->prop_size);
	}
}

/**
 * \a smat is reference matrix only.
 *
 * \note this is a tricky area, before making changes see: T29633, T42444
 */
static void TransMat3ToSize(float mat[3][3], float smat[3][3], float size[3])
{
	float rmat[3][3];

	mat3_to_rot_size(rmat, size, mat);

	/* first tried with dotproduct... but the sign flip is crucial */
	if (dot_v3v3(rmat[0], smat[0]) < 0.0f) size[0] = -size[0];
	if (dot_v3v3(rmat[1], smat[1]) < 0.0f) size[1] = -size[1];
	if (dot_v3v3(rmat[2], smat[2]) < 0.0f) size[2] = -size[2];
}

static void ElementResize(TransInfo *t, TransData *td, float mat[3][3])
{
	float tmat[3][3], smat[3][3], center[3];
	float vec[3];

	if (t->flag & T_EDIT) {
		mul_m3_m3m3(smat, mat, td->mtx);
		mul_m3_m3m3(tmat, td->smtx, smat);
	}
	else {
		copy_m3_m3(tmat, mat);
	}

	if (t->con.applySize) {
		t->con.applySize(t, td, tmat);
	}

	/* local constraint shouldn't alter center */
	if (transdata_check_local_center(t, t->around)) {
		copy_v3_v3(center, td->center);
	}
	else {
		copy_v3_v3(center, t->center);
	}

	if (td->ext) {
		float fsize[3];

		if (t->flag & (T_OBJECT | T_TEXTURE)) {
			float obsizemat[3][3];
			/* Reorient the size mat to fit the oriented object. */
			mul_m3_m3m3(obsizemat, tmat, td->axismtx);
			/* print_m3("obsizemat", obsizemat); */
			TransMat3ToSize(obsizemat, td->axismtx, fsize);
			/* print_v3("fsize", fsize); */
		}
		else {
			mat3_to_size(fsize, tmat);
		}

		protectedSizeBits(td->protectflag, fsize);

		if ((t->flag & T_V3D_ALIGN) == 0) {   /* align mode doesn't resize objects itself */
			if ((td->flag & TD_SINGLESIZE) && !(t->con.mode & CON_APPLY)) {
				/* scale val and reset size */
				*td->val = td->ival * (1 + (fsize[0] - 1) * td->factor);

				td->ext->size[0] = td->ext->isize[0];
				td->ext->size[1] = td->ext->isize[1];
				td->ext->size[2] = td->ext->isize[2];
			}
			else {
				/* Reset val if SINGLESIZE but using a constraint */
				if (td->flag & TD_SINGLESIZE)
					*td->val = td->ival;

				td->ext->size[0] = td->ext->isize[0] * (1 + (fsize[0] - 1) * td->factor);
				td->ext->size[1] = td->ext->isize[1] * (1 + (fsize[1] - 1) * td->factor);
				td->ext->size[2] = td->ext->isize[2] * (1 + (fsize[2] - 1) * td->factor);
			}
		}

	}

	/* For individual element center, Editmode need to use iloc */
	if (t->flag & T_POINTS)
		sub_v3_v3v3(vec, td->iloc, center);
	else
		sub_v3_v3v3(vec, td->center, center);

	mul_m3_v3(tmat, vec);

	add_v3_v3(vec, center);
	if (t->flag & T_POINTS)
		sub_v3_v3(vec, td->iloc);
	else
		sub_v3_v3(vec, td->center);

	mul_v3_fl(vec, td->factor);

	if (t->flag & T_OBJECT) {
		mul_m3_v3(td->smtx, vec);
	}

	protectedTransBits(td->protectflag, vec);
	if (td->loc) {
		add_v3_v3v3(td->loc, td->iloc, vec);
	}

}

static void applyResize(TransInfo *t, const int mval[2])
{
	TransData *td;
	float mat[3][3];
	int i;
	char str[UI_MAX_DRAW_STR];

	if (t->flag & T_AUTOVALUES) {
		copy_v3_v3(t->values, t->auto_values);
	}
	else {
		float ratio;

		/* for manipulator, center handle, the scaling can't be done relative to center */
		if ((t->flag & T_USES_MANIPULATOR) && t->con.mode == 0) {
			ratio = 1.0f - ((t->mouse.imval[0] - mval[0]) + (t->mouse.imval[1] - mval[1])) / 100.0f;
		}
		else {
			ratio = t->values[0];
		}

		copy_v3_fl(t->values, ratio);

		snapGridIncrement(t, t->values);

		if (applyNumInput(&t->num, t->values)) {
			constraintNumInput(t, t->values);
		}

		applySnapping(t, t->values);
	}

	size_to_mat3(mat, t->values);

	if (t->con.applySize) {
		t->con.applySize(t, NULL, mat);
	}

	copy_m3_m3(t->mat, mat);    // used in manipulator

	headerResize(t, t->values, str);

	for (i = 0, td = t->data; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		ElementResize(t, td, mat);
	}

	/* evil hack - redo resize if cliping needed */
	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (ToSphere) */

/** \name Transform ToSphere
 * \{ */

static void initToSphere(TransInfo *t)
{
	TransData *td = t->data;
	int i;

	t->mode = TFM_TOSPHERE;
	t->transform = applyToSphere;

	initMouseInputMode(t, &t->mouse, INPUT_HORIZONTAL_RATIO);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;

	t->num.val_flag[0] |= NUM_NULL_ONE | NUM_NO_NEGATIVE;
	t->flag |= T_NO_CONSTRAINT;

	// Calculate average radius
	for (i = 0; i < t->total; i++, td++) {
		t->val += len_v3v3(t->center, td->iloc);
	}

	t->val /= (float)t->total;
}

static void applyToSphere(TransInfo *t, const int UNUSED(mval[2]))
{
	float vec[3];
	float ratio, radius;
	int i;
	char str[UI_MAX_DRAW_STR];
	TransData *td = t->data;

	ratio = t->values[0];

	snapGridIncrement(t, &ratio);

	applyNumInput(&t->num, &ratio);

	CLAMP(ratio, 0.0f, 1.0f);

	t->values[0] = ratio;

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		BLI_snprintf(str, sizeof(str), IFACE_("To Sphere: %s %s"), c, t->proptext);
	}
	else {
		/* default header print */
		BLI_snprintf(str, sizeof(str), IFACE_("To Sphere: %.4f %s"), ratio, t->proptext);
	}


	for (i = 0; i < t->total; i++, td++) {
		float tratio;
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		sub_v3_v3v3(vec, td->iloc, t->center);

		radius = normalize_v3(vec);

		tratio = ratio * td->factor;

		mul_v3_fl(vec, radius * (1.0f - tratio) + t->val * tratio);

		add_v3_v3v3(td->loc, t->center, vec);
	}


	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Rotation) */

/** \name Transform Rotation
 * \{ */

static void postInputRotation(TransInfo *t, float values[3])
{
	if ((t->con.mode & CON_APPLY) && t->con.applyRot) {
		t->con.applyRot(t, NULL, t->axis, values);
	}
}

static void initRotation(TransInfo *t)
{
	t->mode = TFM_ROTATION;
	t->transform = applyRotation;

	setInputPostFct(&t->mouse, postInputRotation);
	initMouseInputMode(t, &t->mouse, INPUT_ANGLE);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = DEG2RAD(5.0);
	t->snap[2] = DEG2RAD(1.0);

	copy_v3_fl(t->num.val_inc, t->snap[2]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_use_radians = (t->scene->unit.system_rotation == USER_UNIT_ROT_RADIANS);
	t->num.unit_type[0] = B_UNIT_ROTATION;

	if (t->flag & T_2D_EDIT)
		t->flag |= T_NO_CONSTRAINT;

	{
		negate_v3_v3(t->axis, t->viewinv[2]);
		normalize_v3(t->axis);
	}

	copy_v3_v3(t->axis_orig, t->axis);
}

/**
 * Applies values of rotation to `td->loc` and `td->ext->quat`
 * based on a rotation matrix (mat) and a pivot (center).
 *
 * Protected axis and other transform settings are taken into account.
 */
static void ElementRotation_ex(TransInfo *t, TransData *td, float mat[3][3], const float *center)
{
	float vec[3], totmat[3][3], smat[3][3];
	float eul[3], fmat[3][3], quat[4];

	if (t->flag & T_POINTS) {
		mul_m3_m3m3(totmat, mat, td->mtx);
		mul_m3_m3m3(smat, td->smtx, totmat);

		sub_v3_v3v3(vec, td->iloc, center);
		mul_m3_v3(smat, vec);

		add_v3_v3v3(td->loc, vec, center);

		sub_v3_v3v3(vec, td->loc, td->iloc);
		protectedTransBits(td->protectflag, vec);
		add_v3_v3v3(td->loc, td->iloc, vec);


		if (td->flag & TD_USEQUAT) {
			mul_m3_series(fmat, td->smtx, mat, td->mtx);
			mat3_to_quat(quat, fmat);   // Actual transform

			if (td->ext->quat) {
				mul_qt_qtqt(td->ext->quat, quat, td->ext->iquat);

				/* is there a reason not to have this here? -jahka */
				protectedQuaternionBits(td->protectflag, td->ext->quat, td->ext->iquat);
			}
		}
	}
	else {
		if ((td->flag & TD_NO_LOC) == 0) {
			/* translation */
			sub_v3_v3v3(vec, td->center, center);
			mul_m3_v3(mat, vec);
			add_v3_v3(vec, center);
			/* vec now is the location where the object has to be */
			sub_v3_v3(vec, td->center);
			mul_m3_v3(td->smtx, vec);

			protectedTransBits(td->protectflag, vec);

			add_v3_v3v3(td->loc, td->iloc, vec);
		}



		/* rotation */
		if ((t->flag & T_V3D_ALIGN) == 0) { // align mode doesn't rotate objects itself
			/* euler or quaternion? */
			if ((td->ext->rotOrder == ROT_MODE_QUAT) || (td->flag & TD_USEQUAT)) {
				/* can be called for texture space translate for example, then opt out */
				if (td->ext->quat) {
					mul_m3_series(fmat, td->smtx, mat, td->mtx);
					mat3_to_quat(quat, fmat);   // Actual transform

					mul_qt_qtqt(td->ext->quat, quat, td->ext->iquat);
					/* this function works on end result */
					protectedQuaternionBits(td->protectflag, td->ext->quat, td->ext->iquat);
				}
			}
			else if (td->ext->rotOrder == ROT_MODE_AXISANGLE) {
				/* calculate effect based on quats */
				float iquat[4], tquat[4];

				axis_angle_to_quat(iquat, td->ext->irotAxis, td->ext->irotAngle);

				mul_m3_series(fmat, td->smtx, mat, td->mtx);
				mat3_to_quat(quat, fmat);   // Actual transform
				mul_qt_qtqt(tquat, quat, iquat);

				quat_to_axis_angle(td->ext->rotAxis, td->ext->rotAngle, tquat);

				/* this function works on end result */
				protectedAxisAngleBits(td->protectflag, td->ext->rotAxis, td->ext->rotAngle, td->ext->irotAxis,
				                       td->ext->irotAngle);
			}
			else {
				float obmat[3][3];

				mul_m3_m3m3(totmat, mat, td->mtx);
				mul_m3_m3m3(smat, td->smtx, totmat);

				/* calculate the total rotatation in eulers */
				add_v3_v3v3(eul, td->ext->irot, td->ext->drot); /* correct for delta rot */
				eulO_to_mat3(obmat, eul, td->ext->rotOrder);
				/* mat = transform, obmat = object rotation */
				mul_m3_m3m3(fmat, smat, obmat);

				mat3_to_compatible_eulO(eul, td->ext->rot, td->ext->rotOrder, fmat);

				/* correct back for delta rot */
				sub_v3_v3v3(eul, eul, td->ext->drot);

				/* and apply */
				protectedRotateBits(td->protectflag, eul, td->ext->irot);
				copy_v3_v3(td->ext->rot, eul);
			}

		}
	}
}

static void ElementRotation(TransInfo *t, TransData *td, float mat[3][3], const short around)
{
	const float *center;

	/* local constraint shouldn't alter center */
	if (transdata_check_local_center(t, around)) {
		center = td->center;
	}
	else {
		center = t->center;
	}

	ElementRotation_ex(t, td, mat, center);
}

static void applyRotationValue(TransInfo *t, float angle, float axis[3])
{
	TransData *td = t->data;
	float mat[3][3];
	int i;

	axis_angle_normalized_to_mat3(mat, axis, angle);

	for (i = 0; i < t->total; i++, td++) {

		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (t->con.applyRot) {
			t->con.applyRot(t, td, axis, NULL);
			axis_angle_normalized_to_mat3(mat, axis, angle * td->factor);
		}
		else if (t->flag & T_PROP_EDIT) {
			axis_angle_normalized_to_mat3(mat, axis, angle * td->factor);
		}

		ElementRotation(t, td, mat, t->around);
	}
}

static void applyRotation(TransInfo *t, const int UNUSED(mval[2]))
{
	char str[UI_MAX_DRAW_STR];
	size_t ofs = 0;

	float final;

	final = t->values[0];

	snapGridIncrement(t, &final);

	if ((t->con.mode & CON_APPLY) && t->con.applyRot) {
		t->con.applyRot(t, NULL, t->axis, NULL);
	}
	else {
		/* reset axis if constraint is not set */
		copy_v3_v3(t->axis, t->axis_orig);
	}

	applySnapping(t, &final);

	/* Used to clamp final result in [-PI, PI[ range, no idea why, inheritance from 2.4x area, see T48998. */
	applyNumInput(&t->num, &final);

	t->values[0] = final;

	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Rot: %s %s %s"), &c[0], t->con.text, t->proptext);
	}
	else {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Rot: %.2f%s %s"),
		                    RAD2DEGF(final), t->con.text, t->proptext);
	}

	if (t->flag & T_PROP_EDIT_ALL) {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_(" Proportional size: %.2f"), t->prop_size);
	}

	applyRotationValue(t, final, t->axis);

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Rotation - Trackball) */

/** \name Transform Rotation - Trackball
 * \{ */

static void initTrackball(TransInfo *t)
{
	t->mode = TFM_TRACKBALL;
	t->transform = applyTrackball;

	initMouseInputMode(t, &t->mouse, INPUT_TRACKBALL);

	t->idx_max = 1;
	t->num.idx_max = 1;
	t->snap[0] = 0.0f;
	t->snap[1] = DEG2RAD(5.0);
	t->snap[2] = DEG2RAD(1.0);

	copy_v3_fl(t->num.val_inc, t->snap[2]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_use_radians = (t->scene->unit.system_rotation == USER_UNIT_ROT_RADIANS);
	t->num.unit_type[0] = B_UNIT_ROTATION;
	t->num.unit_type[1] = B_UNIT_ROTATION;

	t->flag |= T_NO_CONSTRAINT;
}

static void applyTrackballValue(TransInfo *t, const float axis1[3], const float axis2[3], float angles[2])
{
	TransData *td = t->data;
	float mat[3][3];
	float axis[3];
	float angle;
	int i;

	mul_v3_v3fl(axis, axis1, angles[0]);
	madd_v3_v3fl(axis, axis2, angles[1]);
	angle = normalize_v3(axis);
	axis_angle_normalized_to_mat3(mat, axis, angle);

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (t->flag & T_PROP_EDIT) {
			axis_angle_normalized_to_mat3(mat, axis, td->factor * angle);
		}

		ElementRotation(t, td, mat, t->around);
	}
}

static void applyTrackball(TransInfo *t, const int UNUSED(mval[2]))
{
	char str[UI_MAX_DRAW_STR];
	size_t ofs = 0;
	float axis1[3], axis2[3];
#if 0  /* UNUSED */
	float mat[3][3], totmat[3][3], smat[3][3];
#endif
	float phi[2];

	copy_v3_v3(axis1, t->persinv[0]);
	copy_v3_v3(axis2, t->persinv[1]);
	normalize_v3(axis1);
	normalize_v3(axis2);

	copy_v2_v2(phi, t->values);

	snapGridIncrement(t, phi);

	applyNumInput(&t->num, phi);

	copy_v2_v2(t->values, phi);

	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN * 2];

		outputNumInput(&(t->num), c, &t->scene->unit);

		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Trackball: %s %s %s"),
		                    &c[0], &c[NUM_STR_REP_LEN], t->proptext);
	}
	else {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Trackball: %.2f %.2f %s"),
		                    RAD2DEGF(phi[0]), RAD2DEGF(phi[1]), t->proptext);
	}

	if (t->flag & T_PROP_EDIT_ALL) {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_(" Proportional size: %.2f"), t->prop_size);
	}

#if 0  /* UNUSED */
	axis_angle_normalized_to_mat3(smat, axis1, phi[0]);
	axis_angle_normalized_to_mat3(totmat, axis2, phi[1]);

	mul_m3_m3m3(mat, smat, totmat);

	// TRANSFORM_FIX_ME
	//copy_m3_m3(t->mat, mat);	// used in manipulator
#endif

	applyTrackballValue(t, axis1, axis2, phi);

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Translation) */

static void initSnapSpatial(TransInfo *t, float r_snap[3])
{
	if (t->spacetype == SPACE_VIEW3D) {
		RegionView3D *rv3d = t->ar->regiondata;

		if (rv3d) {
			r_snap[0] = 0.0f;
			r_snap[1] = rv3d->gridview * 1.0f;
			r_snap[2] = r_snap[1] * 0.1f;
		}
	}
	else if (t->spacetype == SPACE_IMAGE) {
		r_snap[0] = 0.0f;
		r_snap[1] = 0.0625f;
		r_snap[2] = 0.03125f;
	}
	else {
		r_snap[0] = 0.0f;
		r_snap[1] = r_snap[2] = 1.0f;
	}
}

/** \name Transform Translation
 * \{ */

static void initTranslation(TransInfo *t)
{
	t->mode = TFM_TRANSLATION;
	t->transform = applyTranslation;

	initMouseInputMode(t, &t->mouse, INPUT_VECTOR);

	t->idx_max = (t->flag & T_2D_EDIT) ? 1 : 2;
	t->num.flag = 0;
	t->num.idx_max = t->idx_max;

	copy_v3_v3(t->snap, t->snap_spatial);

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	if (t->spacetype == SPACE_VIEW3D) {
		/* Handling units makes only sense in 3Dview... See T38877. */
		t->num.unit_type[0] = B_UNIT_LENGTH;
		t->num.unit_type[1] = B_UNIT_LENGTH;
		t->num.unit_type[2] = B_UNIT_LENGTH;
	}
	else {
		t->num.unit_type[0] = B_UNIT_NONE;
		t->num.unit_type[1] = B_UNIT_NONE;
		t->num.unit_type[2] = B_UNIT_NONE;
	}
}

static void headerTranslation(TransInfo *t, const float vec[3], char str[UI_MAX_DRAW_STR])
{
	size_t ofs = 0;
	char tvec[NUM_STR_REP_LEN * 3];
	char distvec[NUM_STR_REP_LEN];
	float dist;

	if (hasNumInput(&t->num)) {
		outputNumInput(&(t->num), tvec, &t->scene->unit);
		dist = len_v3(t->num.val);
	}
	else {
		float dvec[3];

		copy_v3_v3(dvec, vec);
		applyAspectRatio(t, dvec);

		dist = len_v3(vec);
		if (!(t->flag & T_2D_EDIT) && t->scene->unit.system) {
			const bool do_split = (t->scene->unit.flag & USER_UNIT_OPT_SPLIT) != 0;
			int i;

			for (i = 0; i < 3; i++) {
				bUnit_AsString(&tvec[NUM_STR_REP_LEN * i], NUM_STR_REP_LEN, dvec[i] * t->scene->unit.scale_length,
				               4, t->scene->unit.system, B_UNIT_LENGTH, do_split, true);
			}
		}
		else {
			BLI_snprintf(&tvec[0], NUM_STR_REP_LEN, "%.4f", dvec[0]);
			BLI_snprintf(&tvec[NUM_STR_REP_LEN], NUM_STR_REP_LEN, "%.4f", dvec[1]);
			BLI_snprintf(&tvec[NUM_STR_REP_LEN * 2], NUM_STR_REP_LEN, "%.4f", dvec[2]);
		}
	}

	if (!(t->flag & T_2D_EDIT) && t->scene->unit.system) {
		const bool do_split = (t->scene->unit.flag & USER_UNIT_OPT_SPLIT) != 0;
		bUnit_AsString(distvec, sizeof(distvec), dist * t->scene->unit.scale_length, 4, t->scene->unit.system,
		               B_UNIT_LENGTH, do_split, false);
	}
	else if (dist > 1e10f || dist < -1e10f) {
		/* prevent string buffer overflow */
		BLI_snprintf(distvec, NUM_STR_REP_LEN, "%.4e", dist);
	}
	else {
		BLI_snprintf(distvec, NUM_STR_REP_LEN, "%.4f", dist);
	}

	if (t->con.mode & CON_APPLY) {
		switch (t->num.idx_max) {
			case 0:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, "D: %s (%s)%s %s ",
				               &tvec[0], distvec, t->con.text, t->proptext);
				break;
			case 1:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, "D: %s   D: %s (%s)%s %s",
				                    &tvec[0], &tvec[NUM_STR_REP_LEN], distvec, t->con.text, t->proptext);
				break;
			case 2:
				ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, "D: %s   D: %s  D: %s (%s)%s %s",
				                    &tvec[0], &tvec[NUM_STR_REP_LEN], &tvec[NUM_STR_REP_LEN * 2], distvec,
				                    t->con.text, t->proptext);
				break;
		}
	}
	else {
		if (t->flag & T_2D_EDIT) {
			ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, "Dx: %s   Dy: %s (%s)%s %s",
			                    &tvec[0], &tvec[NUM_STR_REP_LEN], distvec, t->con.text, t->proptext);
		}
		else {
			ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, "Dx: %s   Dy: %s  Dz: %s (%s)%s %s",
			                    &tvec[0], &tvec[NUM_STR_REP_LEN], &tvec[NUM_STR_REP_LEN * 2], distvec, t->con.text,
			                    t->proptext);
		}
	}

	if (t->flag & T_PROP_EDIT_ALL) {
		ofs += BLI_snprintf(str + ofs, UI_MAX_DRAW_STR - ofs, IFACE_(" Proportional size: %.2f"), t->prop_size);
	}

}

static void applyTranslationValue(TransInfo *t, const float vec[3])
{
	TransData *td = t->data;
	float tvec[3];

	/* The ideal would be "apply_snap_align_rotation" only when a snap point is found
	 * so, maybe inside this function is not the best place to apply this rotation.
	 * but you need "handle snapping rotation before doing the translation" (really?) */
	const bool apply_snap_align_rotation = usingSnappingNormal(t);// && (t->tsnap.status & POINT_INIT);
	float pivot[3];
	if (apply_snap_align_rotation) {
		copy_v3_v3(pivot, t->tsnap.snapTarget);
		/* The pivot has to be in local-space (see T49494) */
		if (t->flag & T_EDIT) {
			Object *ob = t->obedit;
			mul_m4_v3(ob->imat, pivot);
		}
	}

	for (int i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		float rotate_offset[3] = {0};
		bool use_rotate_offset = false;

		/* handle snapping rotation before doing the translation */
		if (apply_snap_align_rotation) {
			float mat[3][3];

			if (validSnappingNormal(t)) {
				const float *original_normal;

				original_normal = td->axismtx[2];

				rotation_between_vecs_to_mat3(mat, original_normal, t->tsnap.snapNormal);
			}
			else {
				unit_m3(mat);
			}

			ElementRotation_ex(t, td, mat, pivot);

			if (td->loc) {
				use_rotate_offset = true;
				sub_v3_v3v3(rotate_offset, td->loc, td->iloc);
			}
		}

		if (t->con.applyVec) {
			float pvec[3];
			t->con.applyVec(t, td, vec, tvec, pvec);
		}
		else {
			copy_v3_v3(tvec, vec);
		}

		if (use_rotate_offset) {
			add_v3_v3(tvec, rotate_offset);
		}

		mul_m3_v3(td->smtx, tvec);
		mul_v3_fl(tvec, td->factor);

		protectedTransBits(td->protectflag, tvec);

		if (td->loc)
			add_v3_v3v3(td->loc, td->iloc, tvec);

	}
}

static void applyTranslation(TransInfo *t, const int UNUSED(mval[2]))
{
	char str[UI_MAX_DRAW_STR];
	float value_final[3];

	if (t->flag & T_AUTOVALUES) {
		copy_v3_v3(t->values, t->auto_values);
	}
	else {
		if ((t->con.mode & CON_APPLY) == 0) {
			snapGridIncrement(t, t->values);
		}

		if (applyNumInput(&t->num, t->values)) {
			removeAspectRatio(t, t->values);
		}

		applySnapping(t, t->values);
	}

	if (t->con.mode & CON_APPLY) {
		float pvec[3] = {0.0f, 0.0f, 0.0f};
		t->con.applyVec(t, NULL, t->values, value_final, pvec);
		headerTranslation(t, pvec, str);

		/* only so we have re-usable value with redo, see T46741. */
		mul_v3_m3v3(t->values, t->con.imtx, value_final);
	}
	else {
		headerTranslation(t, t->values, str);
		copy_v3_v3(value_final, t->values);
	}

	/* don't use 't->values' now on */

	applyTranslationValue(t, value_final);

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */




/* -------------------------------------------------------------------- */
/* Transform (Shrink-Fatten) */

/** \name Transform Shrink-Fatten
 * \{ */

static void initShrinkFatten(TransInfo *t)
{
	// If not in mesh edit mode, fallback to Resize
	if (t->obedit == NULL || t->obedit->type != OB_MESH) {
		initResize(t);
	}
	else {
		t->mode = TFM_SHRINKFATTEN;
		t->transform = applyShrinkFatten;

		initMouseInputMode(t, &t->mouse, INPUT_VERTICAL_ABSOLUTE);

		t->idx_max = 0;
		t->num.idx_max = 0;
		t->snap[0] = 0.0f;
		t->snap[1] = 1.0f;
		t->snap[2] = t->snap[1] * 0.1f;

		copy_v3_fl(t->num.val_inc, t->snap[1]);
		t->num.unit_sys = t->scene->unit.system;
		t->num.unit_type[0] = B_UNIT_LENGTH;

		t->flag |= T_NO_CONSTRAINT;
	}
}


static void applyShrinkFatten(TransInfo *t, const int UNUSED(mval[2]))
{
	float distance;
	int i;
	char str[UI_MAX_DRAW_STR];
	size_t ofs = 0;
	TransData *td = t->data;

	distance = -t->values[0];

	snapGridIncrement(t, &distance);

	applyNumInput(&t->num, &distance);

	t->values[0] = -distance;

	/* header print for NumInput */
	ofs += BLI_strncpy_rlen(str + ofs, IFACE_("Shrink/Fatten:"), sizeof(str) - ofs);
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];
		outputNumInput(&(t->num), c, &t->scene->unit);
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, " %s", c);
	}
	else {
		/* default header print */
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, " %.4f", distance);
	}

	if (t->proptext[0]) {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, " %s", t->proptext);
	}
	ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, ", (");

	if (t->keymap) {
		wmKeyMapItem *kmi = WM_modalkeymap_find_propvalue(t->keymap, TFM_MODAL_RESIZE);
		if (kmi) {
			ofs += WM_keymap_item_to_string(kmi, false, str + ofs, sizeof(str) - ofs);
		}
	}
	BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_(" or Alt) Even Thickness %s"),
	             WM_bool_as_string((t->flag & T_ALT_TRANSFORM) != 0));
	/* done with header string */

	for (i = 0; i < t->total; i++, td++) {
		float tdistance;  /* temp dist */
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		/* get the final offset */
		tdistance = distance * td->factor;
		if (td->ext && (t->flag & T_ALT_TRANSFORM)) {
			tdistance *= td->ext->isize[0];  /* shell factor */
		}

		madd_v3_v3v3fl(td->loc, td->iloc, td->axismtx[2], tdistance);
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Tilt) */

/** \name Transform Tilt
 * \{ */

static void initTilt(TransInfo *t)
{
	t->mode = TFM_TILT;
	t->transform = applyTilt;

	initMouseInputMode(t, &t->mouse, INPUT_ANGLE);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = DEG2RAD(5.0);
	t->snap[2] = DEG2RAD(1.0);

	copy_v3_fl(t->num.val_inc, t->snap[2]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_use_radians = (t->scene->unit.system_rotation == USER_UNIT_ROT_RADIANS);
	t->num.unit_type[0] = B_UNIT_ROTATION;

	t->flag |= T_NO_CONSTRAINT | T_NO_PROJECT;
}


static void applyTilt(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	int i;
	char str[UI_MAX_DRAW_STR];

	float final;

	final = t->values[0];

	snapGridIncrement(t, &final);

	applyNumInput(&t->num, &final);

	t->values[0] = final;

	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		BLI_snprintf(str, sizeof(str), IFACE_("Tilt: %s?? %s"), &c[0], t->proptext);

		/* XXX For some reason, this seems needed for this op, else RNA prop is not updated... :/ */
		t->values[0] = final;
	}
	else {
		BLI_snprintf(str, sizeof(str), IFACE_("Tilt: %.2f?? %s"), RAD2DEGF(final), t->proptext);
	}

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (td->val) {
			*td->val = td->ival + final * td->factor;
		}
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Curve Shrink/Fatten) */

/** \name Transform Curve Shrink/Fatten
 * \{ */

static void initCurveShrinkFatten(TransInfo *t)
{
	t->mode = TFM_CURVE_SHRINKFATTEN;
	t->transform = applyCurveShrinkFatten;

	initMouseInputMode(t, &t->mouse, INPUT_SPRING);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;

	t->flag |= T_NO_ZERO;
#ifdef USE_NUM_NO_ZERO
	t->num.val_flag[0] |= NUM_NO_ZERO;
#endif

	t->flag |= T_NO_CONSTRAINT;
}

static void applyCurveShrinkFatten(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	float ratio;
	int i;
	char str[UI_MAX_DRAW_STR];

	ratio = t->values[0];

	snapGridIncrement(t, &ratio);

	applyNumInput(&t->num, &ratio);

	t->values[0] = ratio;

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);
		BLI_snprintf(str, sizeof(str), IFACE_("Shrink/Fatten: %s"), c);
	}
	else {
		BLI_snprintf(str, sizeof(str), IFACE_("Shrink/Fatten: %3f"), ratio);
	}

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (td->val) {
			*td->val = td->ival * ratio;
			/* apply PET */
			*td->val = (*td->val * td->factor) + ((1.0f - td->factor) * td->ival);
			if (*td->val <= 0.0f) *td->val = 0.001f;
		}
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Push/Pull) */

/** \name Transform Push/Pull
 * \{ */

static void initPushPull(TransInfo *t)
{
	t->mode = TFM_PUSHPULL;
	t->transform = applyPushPull;

	initMouseInputMode(t, &t->mouse, INPUT_VERTICAL_ABSOLUTE);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 1.0f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_LENGTH;
}


static void applyPushPull(TransInfo *t, const int UNUSED(mval[2]))
{
	float vec[3], axis_global[3];
	float distance;
	int i;
	char str[UI_MAX_DRAW_STR];
	TransData *td = t->data;

	distance = t->values[0];

	snapGridIncrement(t, &distance);

	applyNumInput(&t->num, &distance);

	t->values[0] = distance;

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		BLI_snprintf(str, sizeof(str), IFACE_("Push/Pull: %s%s %s"), c, t->con.text, t->proptext);
	}
	else {
		/* default header print */
		BLI_snprintf(str, sizeof(str), IFACE_("Push/Pull: %.4f%s %s"), distance, t->con.text, t->proptext);
	}

	if (t->con.applyRot && t->con.mode & CON_APPLY) {
		t->con.applyRot(t, NULL, axis_global, NULL);
	}

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		sub_v3_v3v3(vec, t->center, td->center);
		if (t->con.applyRot && t->con.mode & CON_APPLY) {
			float axis[3];
			copy_v3_v3(axis, axis_global);
			t->con.applyRot(t, td, axis, NULL);

			mul_m3_v3(td->smtx, axis);
			if (isLockConstraint(t)) {
				float dvec[3];
				project_v3_v3v3(dvec, vec, axis);
				sub_v3_v3(vec, dvec);
			}
			else {
				project_v3_v3v3(vec, vec, axis);
			}
		}
		normalize_v3_length(vec, distance * td->factor);

		add_v3_v3v3(td->loc, td->iloc, vec);
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Crease) */

/** \name Transform Crease
 * \{ */

static void initCrease(TransInfo *t)
{
	t->mode = TFM_CREASE;
	t->transform = applyCrease;

	initMouseInputMode(t, &t->mouse, INPUT_SPRING_DELTA);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;

	t->flag |= T_NO_CONSTRAINT | T_NO_PROJECT;
}

static void applyCrease(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	float crease;
	int i;
	char str[UI_MAX_DRAW_STR];

	crease = t->values[0];

	CLAMP_MAX(crease, 1.0f);

	snapGridIncrement(t, &crease);

	applyNumInput(&t->num, &crease);

	t->values[0] = crease;

	/* header print for NumInput */
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];

		outputNumInput(&(t->num), c, &t->scene->unit);

		if (crease >= 0.0f)
			BLI_snprintf(str, sizeof(str), IFACE_("Crease: +%s %s"), c, t->proptext);
		else
			BLI_snprintf(str, sizeof(str), IFACE_("Crease: %s %s"), c, t->proptext);
	}
	else {
		/* default header print */
		if (crease >= 0.0f)
			BLI_snprintf(str, sizeof(str), IFACE_("Crease: +%.3f %s"), crease, t->proptext);
		else
			BLI_snprintf(str, sizeof(str), IFACE_("Crease: %.3f %s"), crease, t->proptext);
	}

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		if (td->val) {
			*td->val = td->ival + crease * td->factor;
			if (*td->val < 0.0f) *td->val = 0.0f;
			if (*td->val > 1.0f) *td->val = 1.0f;
		}
	}

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */



/* -------------------------------------------------------------------- */
/* Original Data Store */

/** \name Orig-Data Store Utility Functions
 * \{ */

static void slide_origdata_init_flag(
        TransInfo *t, SlideOrigData *sod)
{
	sod->use_origfaces = false;
	sod->cd_loop_mdisp_offset = -1;
}

static void slide_origdata_init_data(
        TransInfo *t, SlideOrigData *sod)
{
	if (sod->use_origfaces) {
		BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
		BMesh *bm = em->bm;

		sod->origfaces = BLI_ghash_ptr_new(__func__);
		sod->bm_origfaces = BM_mesh_create(
		        &bm_mesh_allocsize_default,
		        &((struct BMeshCreateParams){.use_toolflags = false,}));
		/* we need to have matching customdata */
		BM_mesh_copy_init_customdata(sod->bm_origfaces, bm, NULL);
	}
}

static void slide_origdata_create_data_vert(
        BMesh *bm, SlideOrigData *sod,
        TransDataGenericSlideVert *sv)
{
	BMIter liter;
	int j, l_num;
	float *loop_weights;

	/* copy face data */
	// BM_ITER_ELEM (l, &liter, sv->v, BM_LOOPS_OF_VERT) {
	BM_iter_init(&liter, bm, BM_LOOPS_OF_VERT, sv->v);
	l_num = liter.count;
	loop_weights = BLI_array_alloca(loop_weights, l_num);
	for (j = 0; j < l_num; j++) {
		BMLoop *l = BM_iter_step(&liter);
		BMLoop *l_prev, *l_next;
		void **val_p;
		if (!BLI_ghash_ensure_p(sod->origfaces, l->f, &val_p)) {
			BMFace *f_copy = BM_face_copy(sod->bm_origfaces, bm, l->f, true, true);
			*val_p = f_copy;
		}

		if ((l_prev = BM_loop_find_prev_nodouble(l, l->next, FLT_EPSILON)) &&
		    (l_next = BM_loop_find_next_nodouble(l, l_prev,  FLT_EPSILON)))
		{
			loop_weights[j] = angle_v3v3v3(l_prev->v->co, l->v->co, l_next->v->co);
		}
		else {
			loop_weights[j] = 0.0f;
		}

	}

	/* store cd_loop_groups */
	if (sod->layer_math_map_num && (l_num != 0)) {
		sv->cd_loop_groups = BLI_memarena_alloc(sod->arena, sod->layer_math_map_num * sizeof(void *));
		for (j = 0; j < sod->layer_math_map_num; j++) {
			const int layer_nr = sod->layer_math_map[j];
			sv->cd_loop_groups[j] = BM_vert_loop_groups_data_layer_create(bm, sv->v, layer_nr, loop_weights, sod->arena);
		}
	}
	else {
		sv->cd_loop_groups = NULL;
	}

	BLI_ghash_insert(sod->origverts, sv->v, sv);
}

static void slide_origdata_create_data(
        TransInfo *t, SlideOrigData *sod,
        TransDataGenericSlideVert *sv_array, unsigned int v_stride, unsigned int v_num)
{
	if (sod->use_origfaces) {
		BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
		BMesh *bm = em->bm;
		unsigned int i;
		TransDataGenericSlideVert *sv;

		int layer_index_dst;
		int j;

		layer_index_dst = 0;

		if (CustomData_has_math(&bm->ldata)) {
			/* over alloc, only 'math' layers are indexed */
			sod->layer_math_map = MEM_mallocN(bm->ldata.totlayer * sizeof(int), __func__);
			for (j = 0; j < bm->ldata.totlayer; j++) {
				if (CustomData_layer_has_math(&bm->ldata, j)) {
					sod->layer_math_map[layer_index_dst++] = j;
				}
			}
			BLI_assert(layer_index_dst != 0);
		}

		sod->layer_math_map_num = layer_index_dst;

		sod->arena = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);

		sod->origverts = BLI_ghash_ptr_new_ex(__func__, v_num);

		for (i = 0, sv = sv_array; i < v_num; i++, sv = POINTER_OFFSET(sv, v_stride)) {
			slide_origdata_create_data_vert(bm, sod, sv);
		}

		if (t->flag & T_MIRROR) {
			TransData *td = t->data;
			TransDataGenericSlideVert *sv_mirror;

			sod->sv_mirror = MEM_callocN(sizeof(*sv_mirror) * t->total, __func__);
			sod->totsv_mirror = t->total;

			sv_mirror = sod->sv_mirror;

			for (i = 0; i < t->total; i++, td++) {
				BMVert *eve = td->extra;
				if (eve) {
					sv_mirror->v = eve;
					copy_v3_v3(sv_mirror->co_orig_3d, eve->co);

					slide_origdata_create_data_vert(bm, sod, sv_mirror);
					sv_mirror++;
				}
				else {
					sod->totsv_mirror--;
				}
			}

			if (sod->totsv_mirror == 0) {
				MEM_freeN(sod->sv_mirror);
				sod->sv_mirror = NULL;
			}
		}
	}
}

/**
 * If we're sliding the vert, return its original location, if not, the current location is good.
 */
static const float *slide_origdata_orig_vert_co(SlideOrigData *sod, BMVert *v)
{
	TransDataGenericSlideVert *sv = BLI_ghash_lookup(sod->origverts, v);
	return sv ? sv->co_orig_3d : v->co;
}

static void slide_origdata_interp_data_vert(
        SlideOrigData *sod, BMesh *bm, bool is_final,
        TransDataGenericSlideVert *sv)
{
	BMIter liter;
	int j, l_num;
	float *loop_weights;
	const bool is_moved = (len_squared_v3v3(sv->v->co, sv->co_orig_3d) > FLT_EPSILON);
	const bool do_loop_weight = sod->layer_math_map_num && is_moved;
	const bool do_loop_mdisps = is_final && is_moved && (sod->cd_loop_mdisp_offset != -1);
	const float *v_proj_axis = sv->v->no;
	/* original (l->prev, l, l->next) projections for each loop ('l' remains unchanged) */
	float v_proj[3][3];

	if (do_loop_weight || do_loop_mdisps) {
		project_plane_normalized_v3_v3v3(v_proj[1], sv->co_orig_3d, v_proj_axis);
	}

	// BM_ITER_ELEM (l, &liter, sv->v, BM_LOOPS_OF_VERT)
	BM_iter_init(&liter, bm, BM_LOOPS_OF_VERT, sv->v);
	l_num = liter.count;
	loop_weights = do_loop_weight ? BLI_array_alloca(loop_weights, l_num) : NULL;
	for (j = 0; j < l_num; j++) {
		BMFace *f_copy;  /* the copy of 'f' */
		BMLoop *l = BM_iter_step(&liter);

		f_copy = BLI_ghash_lookup(sod->origfaces, l->f);

		/* only loop data, no vertex data since that contains shape keys,
		 * and we do not want to mess up other shape keys */
		BM_loop_interp_from_face(bm, l, f_copy, false, false);

		/* make sure face-attributes are correct (e.g. MTexPoly) */
		BM_elem_attrs_copy_ex(sod->bm_origfaces, bm, f_copy, l->f, 0x0, CD_MASK_NORMAL);

		/* weight the loop */
		if (do_loop_weight) {
			const float eps = 1.0e-8f;
			const BMLoop *l_prev = l->prev;
			const BMLoop *l_next = l->next;
			const float *co_prev = slide_origdata_orig_vert_co(sod, l_prev->v);
			const float *co_next = slide_origdata_orig_vert_co(sod, l_next->v);
			bool co_prev_ok;
			bool co_next_ok;


			/* In the unlikely case that we're next to a zero length edge -
			 * walk around the to the next.
			 *
			 * Since we only need to check if the vertex is in this corner,
			 * its not important _which_ loop - as long as its not overlapping
			 * 'sv->co_orig_3d', see: T45096. */
			project_plane_normalized_v3_v3v3(v_proj[0], co_prev, v_proj_axis);
			while (UNLIKELY(((co_prev_ok = (len_squared_v3v3(v_proj[1], v_proj[0]) > eps)) == false) &&
			                ((l_prev = l_prev->prev) != l->next)))
			{
				co_prev = slide_origdata_orig_vert_co(sod, l_prev->v);
				project_plane_normalized_v3_v3v3(v_proj[0], co_prev, v_proj_axis);
			}
			project_plane_normalized_v3_v3v3(v_proj[2], co_next, v_proj_axis);
			while (UNLIKELY(((co_next_ok = (len_squared_v3v3(v_proj[1], v_proj[2]) > eps)) == false) &&
			                ((l_next = l_next->next) != l->prev)))
			{
				co_next = slide_origdata_orig_vert_co(sod, l_next->v);
				project_plane_normalized_v3_v3v3(v_proj[2], co_next, v_proj_axis);
			}

			if (co_prev_ok && co_next_ok) {
				const float dist = dist_signed_squared_to_corner_v3v3v3(sv->v->co, UNPACK3(v_proj), v_proj_axis);

				loop_weights[j] = (dist >= 0.0f) ? 1.0f : ((dist <= -eps) ? 0.0f : (1.0f + (dist / eps)));
				if (UNLIKELY(!isfinite(loop_weights[j]))) {
					loop_weights[j] = 0.0f;
				}
			}
			else {
				loop_weights[j] = 0.0f;
			}
		}
	}

	if (sod->layer_math_map_num) {
		if (do_loop_weight) {
			for (j = 0; j < sod->layer_math_map_num; j++) {
				BM_vert_loop_groups_data_layer_merge_weights(bm, sv->cd_loop_groups[j], sod->layer_math_map[j], loop_weights);
			}
		}
		else {
			for (j = 0; j < sod->layer_math_map_num; j++) {
				BM_vert_loop_groups_data_layer_merge(bm, sv->cd_loop_groups[j], sod->layer_math_map[j]);
			}
		}
	}

	/* Special handling for multires
	 *
	 * Interpolate from every other loop (not ideal)
	 * However values will only be taken from loops which overlap other mdisps.
	 * */
	if (do_loop_mdisps) {
		float (*faces_center)[3] = BLI_array_alloca(faces_center, l_num);
		BMLoop *l;

		BM_ITER_ELEM_INDEX (l, &liter, sv->v, BM_LOOPS_OF_VERT, j) {
			BM_face_calc_center_median(l->f, faces_center[j]);
		}

		BM_ITER_ELEM_INDEX (l, &liter, sv->v, BM_LOOPS_OF_VERT, j) {
			BMFace *f_copy = BLI_ghash_lookup(sod->origfaces, l->f);
			float f_copy_center[3];
			BMIter liter_other;
			BMLoop *l_other;
			int j_other;

			BM_face_calc_center_median(f_copy, f_copy_center);

			BM_ITER_ELEM_INDEX (l_other, &liter_other, sv->v, BM_LOOPS_OF_VERT, j_other) {
				BM_face_interp_multires_ex(
				        bm, l_other->f, f_copy,
				        faces_center[j_other], f_copy_center, sod->cd_loop_mdisp_offset);
			}
		}
	}
}

static void slide_origdata_interp_data(
        TransInfo *t, SlideOrigData *sod,
        TransDataGenericSlideVert *sv, unsigned int v_stride, unsigned int v_num,
        bool is_final)
{
	if (sod->use_origfaces) {
		BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
		BMesh *bm = em->bm;
		unsigned int i;
		const bool has_mdisps = (sod->cd_loop_mdisp_offset != -1);

		for (i = 0; i < v_num; i++, sv = POINTER_OFFSET(sv, v_stride)) {

			if (sv->cd_loop_groups || has_mdisps) {
				slide_origdata_interp_data_vert(sod, bm, is_final, sv);
			}
		}

		if (sod->sv_mirror) {
			sv = sod->sv_mirror;
			for (i = 0; i < v_num; i++, sv++) {
				if (sv->cd_loop_groups || has_mdisps) {
					slide_origdata_interp_data_vert(sod, bm, is_final, sv);
				}
			}
		}
	}
}

static void slide_origdata_free_date(
        SlideOrigData *sod)
{
	if (sod->use_origfaces) {
		if (sod->bm_origfaces) {
			BM_mesh_free(sod->bm_origfaces);
			sod->bm_origfaces = NULL;
		}

		if (sod->origfaces) {
			BLI_ghash_free(sod->origfaces, NULL, NULL);
			sod->origfaces = NULL;
		}

		if (sod->origverts) {
			BLI_ghash_free(sod->origverts, NULL, NULL);
			sod->origverts = NULL;
		}

		if (sod->arena) {
			BLI_memarena_free(sod->arena);
			sod->arena = NULL;
		}

		MEM_SAFE_FREE(sod->layer_math_map);

		MEM_SAFE_FREE(sod->sv_mirror);
	}
}

/** \} */

/* -------------------------------------------------------------------- */
/* Transform (Edge Slide) */

/** \name Transform Edge Slide
 * \{ */

static void calcEdgeSlideCustomPoints(struct TransInfo *t)
{
	EdgeSlideData *sld = t->custom.mode.data;

	setCustomPoints(t, &t->mouse, sld->mval_end, sld->mval_start);

	/* setCustomPoints isn't normally changing as the mouse moves,
	 * in this case apply mouse input immediately so we don't refresh
	 * with the value from the previous points */
	applyMouseInput(t, &t->mouse, t->mval, t->values);
}


static BMEdge *get_other_edge(BMVert *v, BMEdge *e)
{
	BMIter iter;
	BMEdge *e_iter;

	BM_ITER_ELEM (e_iter, &iter, v, BM_EDGES_OF_VERT) {
		if (BM_elem_flag_test(e_iter, BM_ELEM_SELECT) && e_iter != e) {
			return e_iter;
		}
	}

	return NULL;
}

/* interpoaltes along a line made up of 2 segments (used for edge slide) */
static void interp_line_v3_v3v3v3(float p[3], const float v1[3], const float v2[3], const float v3[3], float t)
{
	float t_mid, t_delta;

	/* could be pre-calculated */
	t_mid = line_point_factor_v3(v2, v1, v3);

	t_delta = t - t_mid;
	if (t_delta < 0.0f) {
		if (UNLIKELY(fabsf(t_mid) < FLT_EPSILON)) {
			copy_v3_v3(p, v2);
		}
		else {
			interp_v3_v3v3(p, v1, v2, t / t_mid);
		}
	}
	else {
		t = t - t_mid;
		t_mid = 1.0f - t_mid;

		if (UNLIKELY(fabsf(t_mid) < FLT_EPSILON)) {
			copy_v3_v3(p, v3);
		}
		else {
			interp_v3_v3v3(p, v2, v3, t / t_mid);
		}
	}
}

/**
 * Find the closest point on the ngon on the opposite side.
 * used to set the edge slide distance for ngons.
 */
static bool bm_loop_calc_opposite_co(BMLoop *l_tmp,
                                     const float plane_no[3],
                                     float r_co[3])
{
	/* skip adjacent edges */
	BMLoop *l_first = l_tmp->next;
	BMLoop *l_last  = l_tmp->prev;
	BMLoop *l_iter;
	float dist = FLT_MAX;

	l_iter = l_first;
	do {
		float tvec[3];
		if (isect_line_plane_v3(tvec,
		                        l_iter->v->co, l_iter->next->v->co,
		                        l_tmp->v->co, plane_no))
		{
			const float fac = line_point_factor_v3(tvec, l_iter->v->co, l_iter->next->v->co);
			/* allow some overlap to avoid missing the intersection because of float precision */
			if ((fac > -FLT_EPSILON) && (fac < 1.0f + FLT_EPSILON)) {
				/* likelihood of multiple intersections per ngon is quite low,
				 * it would have to loop back on its self, but better support it
				 * so check for the closest opposite edge */
				const float tdist = len_v3v3(l_tmp->v->co, tvec);
				if (tdist < dist) {
					copy_v3_v3(r_co, tvec);
					dist = tdist;
				}
			}
		}
	} while ((l_iter = l_iter->next) != l_last);

	return (dist != FLT_MAX);
}

/**
 * Given 2 edges and a loop, step over the loops
 * and calculate a direction to slide along.
 *
 * \param r_slide_vec: the direction to slide,
 * the length of the vector defines the slide distance.
 */
static BMLoop *get_next_loop(BMVert *v, BMLoop *l,
                             BMEdge *e_prev, BMEdge *e_next, float r_slide_vec[3])
{
	BMLoop *l_first;
	float vec_accum[3] = {0.0f, 0.0f, 0.0f};
	float vec_accum_len = 0.0f;
	int i = 0;

	BLI_assert(BM_edge_share_vert(e_prev, e_next) == v);
	BLI_assert(BM_vert_in_edge(l->e, v));

	l_first = l;
	do {
		l = BM_loop_other_edge_loop(l, v);

		if (l->e == e_next) {
			if (i) {
				normalize_v3_length(vec_accum, vec_accum_len / (float)i);
			}
			else {
				/* When there is no edge to slide along,
				 * we must slide along the vector defined by the face we're attach to */
				BMLoop *l_tmp = BM_face_vert_share_loop(l_first->f, v);

				BLI_assert(ELEM(l_tmp->e, e_prev, e_next) && ELEM(l_tmp->prev->e, e_prev, e_next));

				if (l_tmp->f->len == 4) {
					/* we could use code below, but in this case
					 * sliding diagonally across the quad works well */
					sub_v3_v3v3(vec_accum, l_tmp->next->next->v->co, v->co);
				}
				else {
					float tdir[3];
					BM_loop_calc_face_direction(l_tmp, tdir);
					cross_v3_v3v3(vec_accum, l_tmp->f->no, tdir);
#if 0
					/* rough guess, we can  do better! */
					normalize_v3_length(vec_accum, (BM_edge_calc_length(e_prev) + BM_edge_calc_length(e_next)) / 2.0f);
#else
					/* be clever, check the opposite ngon edge to slide into.
					 * this gives best results */
					{
						float tvec[3];
						float dist;

						if (bm_loop_calc_opposite_co(l_tmp, tdir, tvec)) {
							dist = len_v3v3(l_tmp->v->co, tvec);
						}
						else {
							dist = (BM_edge_calc_length(e_prev) + BM_edge_calc_length(e_next)) / 2.0f;
						}

						normalize_v3_length(vec_accum, dist);
					}
#endif
				}
			}

			copy_v3_v3(r_slide_vec, vec_accum);
			return l;
		}
		else {
			/* accumulate the normalized edge vector,
			 * normalize so some edges don't skew the result */
			float tvec[3];
			sub_v3_v3v3(tvec, BM_edge_other_vert(l->e, v)->co, v->co);
			vec_accum_len += normalize_v3(tvec);
			add_v3_v3(vec_accum, tvec);
			i += 1;
		}

		if (BM_loop_other_edge_loop(l, v)->e == e_next) {
			if (i) {
				normalize_v3_length(vec_accum, vec_accum_len / (float)i);
			}

			copy_v3_v3(r_slide_vec, vec_accum);
			return BM_loop_other_edge_loop(l, v);
		}

	} while ((l != l->radial_next) &&
	         ((l = l->radial_next) != l_first));

	if (i) {
		normalize_v3_length(vec_accum, vec_accum_len / (float)i);
	}

	copy_v3_v3(r_slide_vec, vec_accum);

	return NULL;
}

/**
 * Calculate screenspace `mval_start` / `mval_end`, optionally slide direction.
 */
static void calcEdgeSlide_mval_range(
        TransInfo *t, EdgeSlideData *sld, const int *sv_table, const int loop_nr,
        const float mval[2], const bool use_occlude_geometry, const bool use_calc_direction)
{
	TransDataEdgeSlideVert *sv_array = sld->sv;
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	BMesh *bm = em->bm;
	ARegion *ar = t->ar;
	View3D *v3d = NULL;
	RegionView3D *rv3d = NULL;
	float projectMat[4][4];
	BMBVHTree *bmbvh;

	/* only for use_calc_direction */
	float (*loop_dir)[3] = NULL, *loop_maxdist = NULL;

	float mval_start[2], mval_end[2];
	float mval_dir[3], dist_best_sq;
	BMIter iter;
	BMEdge *e;

	if (t->spacetype == SPACE_VIEW3D) {
		/* background mode support */
		v3d = t->sa ? t->sa->spacedata.first : NULL;
		rv3d = t->ar ? t->ar->regiondata : NULL;
	}

	if (!rv3d) {
		/* ok, let's try to survive this */
		unit_m4(projectMat);
	}
	else {
		ED_view3d_ob_project_mat_get(rv3d, t->obedit, projectMat);
	}

	if (use_occlude_geometry) {
		bmbvh = BKE_bmbvh_new_from_editmesh(em, BMBVH_RESPECT_HIDDEN, NULL, false);
	}
	else {
		bmbvh = NULL;
	}

	/* find mouse vectors, the global one, and one per loop in case we have
	 * multiple loops selected, in case they are oriented different */
	zero_v3(mval_dir);
	dist_best_sq = -1.0f;

	if (use_calc_direction) {
		loop_dir = MEM_callocN(sizeof(float[3]) * loop_nr, "sv loop_dir");
		loop_maxdist = MEM_mallocN(sizeof(float) * loop_nr, "sv loop_maxdist");
		copy_vn_fl(loop_maxdist, loop_nr, -1.0f);
	}

	BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
		if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
			int i;

			/* search cross edges for visible edge to the mouse cursor,
			 * then use the shared vertex to calculate screen vector*/
			for (i = 0; i < 2; i++) {
				BMIter iter_other;
				BMEdge *e_other;

				BMVert *v = i ? e->v1 : e->v2;
				BM_ITER_ELEM (e_other, &iter_other, v, BM_EDGES_OF_VERT) {
					/* screen-space coords */
					float sco_a[3], sco_b[3];
					float dist_sq;
					int j, l_nr;

					if (BM_elem_flag_test(e_other, BM_ELEM_SELECT))
						continue;

					/* This test is only relevant if object is not wire-drawn! See [#32068]. */
					if (use_occlude_geometry && !BMBVH_EdgeVisible(bmbvh, e_other, ar, v3d, t->obedit)) {
						continue;
					}

					BLI_assert(sv_table[BM_elem_index_get(v)] != -1);
					j = sv_table[BM_elem_index_get(v)];

					if (sv_array[j].v_side[1]) {
						ED_view3d_project_float_v3_m4(ar, sv_array[j].v_side[1]->co, sco_b, projectMat);
					}
					else {
						add_v3_v3v3(sco_b, v->co, sv_array[j].dir_side[1]);
						ED_view3d_project_float_v3_m4(ar, sco_b, sco_b, projectMat);
					}

					if (sv_array[j].v_side[0]) {
						ED_view3d_project_float_v3_m4(ar, sv_array[j].v_side[0]->co, sco_a, projectMat);
					}
					else {
						add_v3_v3v3(sco_a, v->co, sv_array[j].dir_side[0]);
						ED_view3d_project_float_v3_m4(ar, sco_a, sco_a, projectMat);
					}

					/* global direction */
					dist_sq = dist_squared_to_line_segment_v2(mval, sco_b, sco_a);
					if ((dist_best_sq == -1.0f) ||
					    /* intentionally use 2d size on 3d vector */
					    (dist_sq < dist_best_sq && (len_squared_v2v2(sco_b, sco_a) > 0.1f)))
					{
						dist_best_sq = dist_sq;
						sub_v3_v3v3(mval_dir, sco_b, sco_a);
					}

					if (use_calc_direction) {
						/* per loop direction */
						l_nr = sv_array[j].loop_nr;
						if (loop_maxdist[l_nr] == -1.0f || dist_sq < loop_maxdist[l_nr]) {
							loop_maxdist[l_nr] = dist_sq;
							sub_v3_v3v3(loop_dir[l_nr], sco_b, sco_a);
						}
					}
				}
			}
		}
	}

	if (use_calc_direction) {
		int i;
		sv_array = sld->sv;
		for (i = 0; i < sld->totsv; i++, sv_array++) {
			/* switch a/b if loop direction is different from global direction */
			int l_nr = sv_array->loop_nr;
			if (dot_v3v3(loop_dir[l_nr], mval_dir) < 0.0f) {
				swap_v3_v3(sv_array->dir_side[0], sv_array->dir_side[1]);
				SWAP(BMVert *, sv_array->v_side[0], sv_array->v_side[1]);
			}
		}

		MEM_freeN(loop_dir);
		MEM_freeN(loop_maxdist);
	}

	/* possible all of the edge loops are pointing directly at the view */
	if (UNLIKELY(len_squared_v2(mval_dir) < 0.1f)) {
		mval_dir[0] = 0.0f;
		mval_dir[1] = 100.0f;
	}

	/* zero out start */
	zero_v2(mval_start);

	/* dir holds a vector along edge loop */
	copy_v2_v2(mval_end, mval_dir);
	mul_v2_fl(mval_end, 0.5f);

	sld->mval_start[0] = t->mval[0] + mval_start[0];
	sld->mval_start[1] = t->mval[1] + mval_start[1];

	sld->mval_end[0] = t->mval[0] + mval_end[0];
	sld->mval_end[1] = t->mval[1] + mval_end[1];

	if (bmbvh) {
		BKE_bmbvh_free(bmbvh);
	}
}

static void calcEdgeSlide_even(
        TransInfo *t, EdgeSlideData *sld, const float mval[2])
{
	TransDataEdgeSlideVert *sv = sld->sv;

	if (sld->totsv > 0) {
		ARegion *ar = t->ar;
		RegionView3D *rv3d = NULL;
		float projectMat[4][4];

		int i = 0;

		float v_proj[2];
		float dist_sq = 0;
		float dist_min_sq = FLT_MAX;

		if (t->spacetype == SPACE_VIEW3D) {
			/* background mode support */
			rv3d = t->ar ? t->ar->regiondata : NULL;
		}

		if (!rv3d) {
			/* ok, let's try to survive this */
			unit_m4(projectMat);
		}
		else {
			ED_view3d_ob_project_mat_get(rv3d, t->obedit, projectMat);
		}

		for (i = 0; i < sld->totsv; i++, sv++) {
			/* Set length */
			sv->edge_len = len_v3v3(sv->dir_side[0], sv->dir_side[1]);

			ED_view3d_project_float_v2_m4(ar, sv->v->co, v_proj, projectMat);
			dist_sq = len_squared_v2v2(mval, v_proj);
			if (dist_sq < dist_min_sq) {
				dist_min_sq = dist_sq;
				sld->curr_sv_index = i;
			}
		}
	}
	else {
		sld->curr_sv_index = 0;
	}
}

static bool createEdgeSlideVerts_double_side(TransInfo *t, bool use_even, bool flipped, bool use_clamp)
{
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	BMesh *bm = em->bm;
	BMIter iter;
	BMEdge *e;
	BMVert *v;
	TransDataEdgeSlideVert *sv_array;
	int sv_tot;
	int *sv_table;  /* BMVert -> sv_array index */
	EdgeSlideData *sld = MEM_callocN(sizeof(*sld), "sld");
	float mval[2] = {(float)t->mval[0], (float)t->mval[1]};
	int numsel, i, loop_nr;
	bool use_occlude_geometry = false;
	View3D *v3d = NULL;
	RegionView3D *rv3d = NULL;

	slide_origdata_init_flag(t, &sld->orig_data);

	sld->use_even = use_even;
	sld->curr_sv_index = 0;
	sld->flipped = flipped;
	if (!use_clamp)
		t->flag |= T_ALT_TRANSFORM;

	/*ensure valid selection*/
	BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
		if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
			BMIter iter2;
			numsel = 0;
			BM_ITER_ELEM (e, &iter2, v, BM_EDGES_OF_VERT) {
				if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
					/* BMESH_TODO: this is probably very evil,
					 * set v->e to a selected edge*/
					v->e = e;

					numsel++;
				}
			}

			if (numsel == 0 || numsel > 2) {
				MEM_freeN(sld);
				return false; /* invalid edge selection */
			}
		}
	}

	BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
		if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
			/* note, any edge with loops can work, but we won't get predictable results, so bail out */
			if (!BM_edge_is_manifold(e) && !BM_edge_is_boundary(e)) {
				/* can edges with at least once face user */
				MEM_freeN(sld);
				return false;
			}
		}
	}

	sv_table = MEM_mallocN(sizeof(*sv_table) * bm->totvert, __func__);

#define INDEX_UNSET   -1
#define INDEX_INVALID -2

	{
		int j = 0;
		BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
			if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
				BM_elem_flag_enable(v, BM_ELEM_TAG);
				sv_table[i] = INDEX_UNSET;
				j += 1;
			}
			else {
				BM_elem_flag_disable(v, BM_ELEM_TAG);
				sv_table[i] = INDEX_INVALID;
			}
			BM_elem_index_set(v, i); /* set_inline */
		}
		bm->elem_index_dirty &= ~BM_VERT;

		if (!j) {
			MEM_freeN(sld);
			MEM_freeN(sv_table);
			return false;
		}
		sv_tot = j;
	}

	sv_array = MEM_callocN(sizeof(TransDataEdgeSlideVert) * sv_tot, "sv_array");
	loop_nr = 0;

	STACK_DECLARE(sv_array);
	STACK_INIT(sv_array, sv_tot);

	while (1) {
		float vec_a[3], vec_b[3];
		BMLoop *l_a, *l_b;
		BMLoop *l_a_prev, *l_b_prev;
		BMVert *v_first;
		/* If this succeeds call get_next_loop()
		 * which calculates the direction to slide based on clever checks.
		 *
		 * otherwise we simply use 'e_dir' as an edge-rail.
		 * (which is better when the attached edge is a boundary, see: T40422)
		 */
#define EDGESLIDE_VERT_IS_INNER(v, e_dir) \
		((BM_edge_is_boundary(e_dir) == false) && \
		 (BM_vert_edge_count_nonwire(v) == 2))

		v = NULL;
		BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
			if (BM_elem_flag_test(v, BM_ELEM_TAG))
				break;

		}

		if (!v)
			break;

		if (!v->e)
			continue;

		v_first = v;

		/*walk along the edge loop*/
		e = v->e;

		/*first, rewind*/
		do {
			e = get_other_edge(v, e);
			if (!e) {
				e = v->e;
				break;
			}

			if (!BM_elem_flag_test(BM_edge_other_vert(e, v), BM_ELEM_TAG))
				break;

			v = BM_edge_other_vert(e, v);
		} while (e != v_first->e);

		BM_elem_flag_disable(v, BM_ELEM_TAG);

		l_a = e->l;
		l_b = e->l->radial_next;

		/* regarding e_next, use get_next_loop()'s improved interpolation where possible */
		{
			BMEdge *e_next = get_other_edge(v, e);
			if (e_next) {
				get_next_loop(v, l_a, e, e_next, vec_a);
			}
			else {
				BMLoop *l_tmp = BM_loop_other_edge_loop(l_a, v);
				if (EDGESLIDE_VERT_IS_INNER(v, l_tmp->e)) {
					get_next_loop(v, l_a, e, l_tmp->e, vec_a);
				}
				else {
					sub_v3_v3v3(vec_a, BM_edge_other_vert(l_tmp->e, v)->co, v->co);
				}
			}
		}

		/* !BM_edge_is_boundary(e); */
		if (l_b != l_a) {
			BMEdge *e_next = get_other_edge(v, e);
			if (e_next) {
				get_next_loop(v, l_b, e, e_next, vec_b);
			}
			else {
				BMLoop *l_tmp = BM_loop_other_edge_loop(l_b, v);
				if (EDGESLIDE_VERT_IS_INNER(v, l_tmp->e)) {
					get_next_loop(v, l_b, e, l_tmp->e, vec_b);
				}
				else {
					sub_v3_v3v3(vec_b, BM_edge_other_vert(l_tmp->e, v)->co, v->co);
				}
			}
		}
		else {
			l_b = NULL;
		}

		l_a_prev = NULL;
		l_b_prev = NULL;

#define SV_FROM_VERT(v) ( \
		(sv_table[BM_elem_index_get(v)] == INDEX_UNSET) ? \
			((void)(sv_table[BM_elem_index_get(v)] = STACK_SIZE(sv_array)), STACK_PUSH_RET_PTR(sv_array)) : \
			(&sv_array[sv_table[BM_elem_index_get(v)]]))

		/*iterate over the loop*/
		v_first = v;
		do {
			bool l_a_ok_prev;
			bool l_b_ok_prev;
			TransDataEdgeSlideVert *sv;
			BMVert *v_prev;
			BMEdge *e_prev;

			/* XXX, 'sv' will initialize multiple times, this is suspicious. see [#34024] */
			BLI_assert(v != NULL);
			BLI_assert(sv_table[BM_elem_index_get(v)] != INDEX_INVALID);
			sv = SV_FROM_VERT(v);
			sv->v = v;
			copy_v3_v3(sv->v_co_orig, v->co);
			sv->loop_nr = loop_nr;

			if (l_a || l_a_prev) {
				BMLoop *l_tmp = BM_loop_other_edge_loop(l_a ? l_a : l_a_prev, v);
				sv->v_side[0] = BM_edge_other_vert(l_tmp->e, v);
				copy_v3_v3(sv->dir_side[0], vec_a);
			}

			if (l_b || l_b_prev) {
				BMLoop *l_tmp = BM_loop_other_edge_loop(l_b ? l_b : l_b_prev, v);
				sv->v_side[1] = BM_edge_other_vert(l_tmp->e, v);
				copy_v3_v3(sv->dir_side[1], vec_b);
			}

			v_prev = v;
			v = BM_edge_other_vert(e, v);

			e_prev = e;
			e = get_other_edge(v, e);

			if (!e) {
				BLI_assert(v != NULL);

				BLI_assert(sv_table[BM_elem_index_get(v)] != INDEX_INVALID);
				sv = SV_FROM_VERT(v);

				sv->v = v;
				copy_v3_v3(sv->v_co_orig, v->co);
				sv->loop_nr = loop_nr;

				if (l_a) {
					BMLoop *l_tmp = BM_loop_other_edge_loop(l_a, v);
					sv->v_side[0] = BM_edge_other_vert(l_tmp->e, v);
					if (EDGESLIDE_VERT_IS_INNER(v, l_tmp->e)) {
						get_next_loop(v, l_a, e_prev, l_tmp->e, sv->dir_side[0]);
					}
					else {
						sub_v3_v3v3(sv->dir_side[0], sv->v_side[0]->co, v->co);
					}
				}

				if (l_b) {
					BMLoop *l_tmp = BM_loop_other_edge_loop(l_b, v);
					sv->v_side[1] = BM_edge_other_vert(l_tmp->e, v);
					if (EDGESLIDE_VERT_IS_INNER(v, l_tmp->e)) {
						get_next_loop(v, l_b, e_prev, l_tmp->e, sv->dir_side[1]);
					}
					else {
						sub_v3_v3v3(sv->dir_side[1], sv->v_side[1]->co, v->co);
					}
				}

				BM_elem_flag_disable(v, BM_ELEM_TAG);
				BM_elem_flag_disable(v_prev, BM_ELEM_TAG);

				break;
			}
			l_a_ok_prev = (l_a != NULL);
			l_b_ok_prev = (l_b != NULL);

			l_a_prev = l_a;
			l_b_prev = l_b;

			if (l_a) {
				l_a = get_next_loop(v, l_a, e_prev, e, vec_a);
			}
			else {
				zero_v3(vec_a);
			}

			if (l_b) {
				l_b = get_next_loop(v, l_b, e_prev, e, vec_b);
			}
			else {
				zero_v3(vec_b);
			}


			if (l_a && l_b) {
				/* pass */
			}
			else {
				if (l_a || l_b) {
					/* find the opposite loop if it was missing previously */
					if      (l_a == NULL && l_b && (l_b->radial_next != l_b)) l_a = l_b->radial_next;
					else if (l_b == NULL && l_a && (l_a->radial_next != l_a)) l_b = l_a->radial_next;
				}
				else if (e->l != NULL) {
					/* if there are non-contiguous faces, we can still recover
					 * the loops of the new edges faces */

					/* note!, the behavior in this case means edges may move in opposite directions,
					 * this could be made to work more usefully. */

					if (l_a_ok_prev) {
						l_a = e->l;
						l_b = (l_a->radial_next != l_a) ? l_a->radial_next : NULL;
					}
					else if (l_b_ok_prev) {
						l_b = e->l;
						l_a = (l_b->radial_next != l_b) ? l_b->radial_next : NULL;
					}
				}

				if (!l_a_ok_prev && l_a) {
					get_next_loop(v, l_a, e, e_prev, vec_a);
				}
				if (!l_b_ok_prev && l_b) {
					get_next_loop(v, l_b, e, e_prev, vec_b);
				}
			}

			BM_elem_flag_disable(v, BM_ELEM_TAG);
			BM_elem_flag_disable(v_prev, BM_ELEM_TAG);
		} while ((e != v_first->e) && (l_a || l_b));

#undef SV_FROM_VERT
#undef INDEX_UNSET
#undef INDEX_INVALID

		loop_nr++;

#undef EDGESLIDE_VERT_IS_INNER
	}

	/* EDBM_flag_disable_all(em, BM_ELEM_SELECT); */

	BLI_assert(STACK_SIZE(sv_array) == sv_tot);

	sld->sv = sv_array;
	sld->totsv = sv_tot;

	/* use for visibility checks */
	if (t->spacetype == SPACE_VIEW3D) {
		v3d = t->sa ? t->sa->spacedata.first : NULL;
		rv3d = t->ar ? t->ar->regiondata : NULL;
		use_occlude_geometry = (v3d && t->obedit->dt > OB_WIRE && v3d->drawtype > OB_WIRE);
	}

	calcEdgeSlide_mval_range(t, sld, sv_table, loop_nr, mval, use_occlude_geometry, true);

	/* create copies of faces for customdata projection */
	bmesh_edit_begin(bm, BMO_OPTYPE_FLAG_UNTAN_MULTIRES);
	slide_origdata_init_data(t, &sld->orig_data);
	slide_origdata_create_data(t, &sld->orig_data, (TransDataGenericSlideVert *)sld->sv, sizeof(*sld->sv), sld->totsv);

	if (rv3d) {
		calcEdgeSlide_even(t, sld, mval);
	}

	sld->em = em;

	sld->perc = 0.0f;

	t->custom.mode.data = sld;

	MEM_freeN(sv_table);

	return true;
}

/**
 * A simple version of #createEdgeSlideVerts_double_side
 * Which assumes the longest unselected.
 */
static bool createEdgeSlideVerts_single_side(TransInfo *t, bool use_even, bool flipped, bool use_clamp)
{
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	BMesh *bm = em->bm;
	BMIter iter;
	BMEdge *e;
	TransDataEdgeSlideVert *sv_array;
	int sv_tot;
	int *sv_table;  /* BMVert -> sv_array index */
	EdgeSlideData *sld = MEM_callocN(sizeof(*sld), "sld");
	float mval[2] = {(float)t->mval[0], (float)t->mval[1]};
	int loop_nr;
	bool use_occlude_geometry = false;
	View3D *v3d = NULL;
	RegionView3D *rv3d = NULL;

	if (t->spacetype == SPACE_VIEW3D) {
		/* background mode support */
		v3d = t->sa ? t->sa->spacedata.first : NULL;
		rv3d = t->ar ? t->ar->regiondata : NULL;
	}

	slide_origdata_init_flag(t, &sld->orig_data);

	sld->use_even = use_even;
	sld->curr_sv_index = 0;
	/* happens to be best for single-sided */
	sld->flipped = !flipped;
	if (!use_clamp)
		t->flag |= T_ALT_TRANSFORM;

	/* ensure valid selection */
	{
		int i = 0, j = 0;
		BMVert *v;

		BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
			if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
				float len_sq_max = -1.0f;
				BMIter iter2;
				BM_ITER_ELEM (e, &iter2, v, BM_EDGES_OF_VERT) {
					if (!BM_elem_flag_test(e, BM_ELEM_SELECT)) {
						float len_sq = BM_edge_calc_length_squared(e);
						if (len_sq > len_sq_max) {
							len_sq_max = len_sq;
							v->e = e;
						}
					}
				}

				if (len_sq_max != -1.0f) {
					j++;
				}
			}
			BM_elem_index_set(v, i); /* set_inline */
		}
		bm->elem_index_dirty &= ~BM_VERT;

		if (!j) {
			MEM_freeN(sld);
			return false;
		}

		sv_tot = j;
	}

	BLI_assert(sv_tot != 0);
	/* over alloc */
	sv_array = MEM_callocN(sizeof(TransDataEdgeSlideVert) * bm->totvertsel, "sv_array");

	/* same loop for all loops, weak but we dont connect loops in this case */
	loop_nr = 1;

	sv_table = MEM_mallocN(sizeof(*sv_table) * bm->totvert, __func__);

	{
		int i = 0, j = 0;
		BMVert *v;

		BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
			sv_table[i] = -1;
			if ((v->e != NULL) && (BM_elem_flag_test(v, BM_ELEM_SELECT))) {
				if (BM_elem_flag_test(v->e, BM_ELEM_SELECT) == 0) {
					TransDataEdgeSlideVert *sv;
					sv = &sv_array[j];
					sv->v = v;
					copy_v3_v3(sv->v_co_orig, v->co);
					sv->v_side[0] = BM_edge_other_vert(v->e, v);
					sub_v3_v3v3(sv->dir_side[0], sv->v_side[0]->co, v->co);
					sv->loop_nr = 0;
					sv_table[i] = j;
					j += 1;
				}
			}
		}
	}

	/* check for wire vertices,
	 * interpolate the directions of wire verts between non-wire verts */
	if (sv_tot != bm->totvert) {
		const int sv_tot_nowire = sv_tot;
		TransDataEdgeSlideVert *sv_iter = sv_array;

		for (int i = 0; i < sv_tot_nowire; i++, sv_iter++) {
			BMIter eiter;
			BM_ITER_ELEM (e, &eiter, sv_iter->v, BM_EDGES_OF_VERT) {
				/* walk over wire */
				TransDataEdgeSlideVert *sv_end = NULL;
				BMEdge *e_step = e;
				BMVert *v = sv_iter->v;
				int j;

				j = sv_tot;

				while (1) {
					BMVert *v_other = BM_edge_other_vert(e_step, v);
					int endpoint = (
					        (sv_table[BM_elem_index_get(v_other)] != -1) +
					        (BM_vert_is_edge_pair(v_other) == false));

					if ((BM_elem_flag_test(e_step, BM_ELEM_SELECT) &&
					     BM_elem_flag_test(v_other, BM_ELEM_SELECT)) &&
					     (endpoint == 0))
					{
						/* scan down the list */
						TransDataEdgeSlideVert *sv;
						BLI_assert(sv_table[BM_elem_index_get(v_other)] == -1);
						sv_table[BM_elem_index_get(v_other)] = j;
						sv = &sv_array[j];
						sv->v = v_other;
						copy_v3_v3(sv->v_co_orig, v_other->co);
						copy_v3_v3(sv->dir_side[0], sv_iter->dir_side[0]);
						j++;

						/* advance! */
						v = v_other;
						e_step = BM_DISK_EDGE_NEXT(e_step, v_other);
					}
					else {
						if ((endpoint == 2) && (sv_tot != j)) {
							BLI_assert(BM_elem_index_get(v_other) != -1);
							sv_end = &sv_array[sv_table[BM_elem_index_get(v_other)]];
						}
						break;
					}
				}

				if (sv_end) {
					int sv_tot_prev = sv_tot;
					const float *co_src = sv_iter->v->co;
					const float *co_dst = sv_end->v->co;
					const float *dir_src = sv_iter->dir_side[0];
					const float *dir_dst = sv_end->dir_side[0];
					sv_tot = j;

					while (j-- != sv_tot_prev) {
						float factor;
						factor = line_point_factor_v3(sv_array[j].v->co, co_src, co_dst);
						interp_v3_v3v3(sv_array[j].dir_side[0], dir_src, dir_dst, factor);
					}
				}
			}
		}
	}

	/* EDBM_flag_disable_all(em, BM_ELEM_SELECT); */

	sld->sv = sv_array;
	sld->totsv = sv_tot;

	/* use for visibility checks */
	if (t->spacetype == SPACE_VIEW3D) {
		v3d = t->sa ? t->sa->spacedata.first : NULL;
		rv3d = t->ar ? t->ar->regiondata : NULL;
		use_occlude_geometry = (v3d && t->obedit->dt > OB_WIRE && v3d->drawtype > OB_WIRE);
	}

	calcEdgeSlide_mval_range(t, sld, sv_table, loop_nr, mval, use_occlude_geometry, false);

	/* create copies of faces for customdata projection */
	bmesh_edit_begin(bm, BMO_OPTYPE_FLAG_UNTAN_MULTIRES);
	slide_origdata_init_data(t, &sld->orig_data);
	slide_origdata_create_data(t, &sld->orig_data, (TransDataGenericSlideVert *)sld->sv, sizeof(*sld->sv), sld->totsv);

	if (rv3d) {
		calcEdgeSlide_even(t, sld, mval);
	}

	sld->em = em;

	sld->perc = 0.0f;

	t->custom.mode.data = sld;

	MEM_freeN(sv_table);

	return true;
}

void projectEdgeSlideData(TransInfo *t, bool is_final)
{
	EdgeSlideData *sld = t->custom.mode.data;
	SlideOrigData *sod = &sld->orig_data;

	if (sod->use_origfaces == false) {
		return;
	}

	slide_origdata_interp_data(t, sod, (TransDataGenericSlideVert *)sld->sv, sizeof(*sld->sv), sld->totsv, is_final);
}

void freeEdgeSlideTempFaces(EdgeSlideData *sld)
{
	slide_origdata_free_date(&sld->orig_data);
}

void freeEdgeSlideVerts(TransInfo *UNUSED(t), TransCustomData *custom_data)
{
	EdgeSlideData *sld = custom_data->data;

	if (!sld)
		return;

	freeEdgeSlideTempFaces(sld);

	bmesh_edit_end(sld->em->bm, BMO_OPTYPE_FLAG_UNTAN_MULTIRES);

	MEM_freeN(sld->sv);
	MEM_freeN(sld);

	custom_data->data = NULL;
}

static void initEdgeSlide_ex(TransInfo *t, bool use_double_side, bool use_even, bool flipped, bool use_clamp)
{
	EdgeSlideData *sld;
	bool ok;

	t->mode = TFM_EDGE_SLIDE;
	t->transform = applyEdgeSlide;
	t->handleEvent = handleEventEdgeSlide;

	if (use_double_side) {
		ok = createEdgeSlideVerts_double_side(t, use_even, flipped, use_clamp);
	}
	else {
		ok = createEdgeSlideVerts_single_side(t, use_even, flipped, use_clamp);
	}

	if (!ok) {
		t->state = TRANS_CANCEL;
		return;
	}

	sld = t->custom.mode.data;

	if (!sld)
		return;

	t->custom.mode.free_cb = freeEdgeSlideVerts;

	/* set custom point first if you want value to be initialized by init */
	calcEdgeSlideCustomPoints(t);
	initMouseInputMode(t, &t->mouse, INPUT_CUSTOM_RATIO_FLIP);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;

	t->flag |= T_NO_CONSTRAINT | T_NO_PROJECT;
}

static void initEdgeSlide(TransInfo *t)
{
	initEdgeSlide_ex(t, true, false, false, true);
}

static eRedrawFlag handleEventEdgeSlide(struct TransInfo *t, const struct wmEvent *event)
{
	if (t->mode == TFM_EDGE_SLIDE) {
		EdgeSlideData *sld = t->custom.mode.data;

		if (sld) {
			switch (event->type) {
				case EKEY:
					if (event->val == KM_PRESS) {
						sld->use_even = !sld->use_even;
						calcEdgeSlideCustomPoints(t);
						return TREDRAW_HARD;
					}
					break;
				case FKEY:
					if (event->val == KM_PRESS) {
						sld->flipped = !sld->flipped;
						calcEdgeSlideCustomPoints(t);
						return TREDRAW_HARD;
					}
					break;
				case CKEY:
					/* use like a modifier key */
					if (event->val == KM_PRESS) {
						t->flag ^= T_ALT_TRANSFORM;
						calcEdgeSlideCustomPoints(t);
						return TREDRAW_HARD;
					}
					break;
				case EVT_MODAL_MAP:
					switch (event->val) {
						case TFM_MODAL_EDGESLIDE_DOWN:
							sld->curr_sv_index = ((sld->curr_sv_index - 1) + sld->totsv) % sld->totsv;
							return TREDRAW_HARD;
						case TFM_MODAL_EDGESLIDE_UP:
							sld->curr_sv_index = (sld->curr_sv_index + 1) % sld->totsv;
							return TREDRAW_HARD;
					}
					break;
				case MOUSEMOVE:
					calcEdgeSlideCustomPoints(t);
					break;
				default:
					break;
			}
		}
	}
	return TREDRAW_NOTHING;
}

static void drawEdgeSlide(TransInfo *t)
{
	if ((t->mode == TFM_EDGE_SLIDE) && t->custom.mode.data) {
		EdgeSlideData *sld = t->custom.mode.data;
		const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);

		/* Even mode */
		if ((sld->use_even == true) || (is_clamp == false)) {
			View3D *v3d = t->view;
			const float line_size = UI_GetThemeValuef(TH_OUTLINE_WIDTH) + 0.5f;

			if (v3d && v3d->zbuf)
				glDisable(GL_DEPTH_TEST);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			glPushAttrib(GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT);
			glPushMatrix();

			glMultMatrixf(t->obedit->obmat);

			if (sld->use_even == true) {
				float co_a[3], co_b[3], co_mark[3];
				TransDataEdgeSlideVert *curr_sv = &sld->sv[sld->curr_sv_index];
				const float fac = (sld->perc + 1.0f) / 2.0f;
				const float ctrl_size = UI_GetThemeValuef(TH_FACEDOT_SIZE) + 1.5f;
				const float guide_size = ctrl_size - 0.5f;
				const int alpha_shade = -30;

				add_v3_v3v3(co_a, curr_sv->v_co_orig, curr_sv->dir_side[0]);
				add_v3_v3v3(co_b, curr_sv->v_co_orig, curr_sv->dir_side[1]);

				glLineWidth(line_size);
				UI_ThemeColorShadeAlpha(TH_EDGE_SELECT, 80, alpha_shade);
				glBegin(GL_LINES);
				if (curr_sv->v_side[0]) {
					glVertex3fv(curr_sv->v_side[0]->co);
					glVertex3fv(curr_sv->v_co_orig);
				}
				if (curr_sv->v_side[1]) {
					glVertex3fv(curr_sv->v_side[1]->co);
					glVertex3fv(curr_sv->v_co_orig);
				}
				glEnd();

				UI_ThemeColorShadeAlpha(TH_SELECT, -30, alpha_shade);
				glPointSize(ctrl_size);
				glBegin(GL_POINTS);
				if (sld->flipped) {
					if (curr_sv->v_side[1]) glVertex3fv(curr_sv->v_side[1]->co);
				}
				else {
					if (curr_sv->v_side[0]) glVertex3fv(curr_sv->v_side[0]->co);
				}
				glEnd();

				UI_ThemeColorShadeAlpha(TH_SELECT, 255, alpha_shade);
				glPointSize(guide_size);
				glBegin(GL_POINTS);
#if 0
				interp_v3_v3v3(co_mark, co_b, co_a, fac);
				glVertex3fv(co_mark);
#endif
				interp_line_v3_v3v3v3(co_mark, co_b, curr_sv->v_co_orig, co_a, fac);
				glVertex3fv(co_mark);
				glEnd();
			}
			else {
				if (is_clamp == false) {
					const int side_index = sld->curr_side_unclamp;
					TransDataEdgeSlideVert *sv;
					int i;
					const int alpha_shade = -160;

					glLineWidth(line_size);
					UI_ThemeColorShadeAlpha(TH_EDGE_SELECT, 80, alpha_shade);
					glBegin(GL_LINES);

					sv = sld->sv;
					for (i = 0; i < sld->totsv; i++, sv++) {
						float a[3], b[3];

						if (!is_zero_v3(sv->dir_side[side_index])) {
							copy_v3_v3(a, sv->dir_side[side_index]);
						}
						else {
							copy_v3_v3(a, sv->dir_side[!side_index]);
						}

						mul_v3_fl(a, 100.0f);
						negate_v3_v3(b, a);
						add_v3_v3(a, sv->v_co_orig);
						add_v3_v3(b, sv->v_co_orig);

						glVertex3fv(a);
						glVertex3fv(b);
					}
					glEnd();
				}
				else {
					BLI_assert(0);
				}
			}

			glPopMatrix();
			glPopAttrib();

			glDisable(GL_BLEND);

			if (v3d && v3d->zbuf)
				glEnable(GL_DEPTH_TEST);
		}
	}
}

static void doEdgeSlide(TransInfo *t, float perc)
{
	EdgeSlideData *sld = t->custom.mode.data;
	TransDataEdgeSlideVert *svlist = sld->sv, *sv;
	int i;

	sld->perc = perc;
	sv = svlist;

	if (sld->use_even == false) {
		const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);
		if (is_clamp) {
			const int side_index = (perc < 0.0f);
			const float perc_final = fabsf(perc);
			for (i = 0; i < sld->totsv; i++, sv++) {
				madd_v3_v3v3fl(sv->v->co, sv->v_co_orig, sv->dir_side[side_index], perc_final);
			}

			sld->curr_side_unclamp = side_index;
		}
		else {
			const int side_index = sld->curr_side_unclamp;
			const float perc_init = fabsf(perc) * ((sld->curr_side_unclamp == (perc < 0.0f)) ? 1 : -1);
			for (i = 0; i < sld->totsv; i++, sv++) {
				float dir_flip[3];
				float perc_final = perc_init;
				if (!is_zero_v3(sv->dir_side[side_index])) {
					copy_v3_v3(dir_flip, sv->dir_side[side_index]);
				}
				else {
					copy_v3_v3(dir_flip, sv->dir_side[!side_index]);
					perc_final *= -1;
				}
				madd_v3_v3v3fl(sv->v->co, sv->v_co_orig, dir_flip, perc_final);
			}
		}
	}
	else {
		/**
		 * Implementation note, even mode ignores the starting positions and uses only the
		 * a/b verts, this could be changed/improved so the distance is still met but the verts are moved along
		 * their original path (which may not be straight), however how it works now is OK and matches 2.4x - Campbell
		 *
		 * \note len_v3v3(curr_sv->dir_side[0], curr_sv->dir_side[1])
		 * is the same as the distance between the original vert locations, same goes for the lines below.
		 */
		TransDataEdgeSlideVert *curr_sv = &sld->sv[sld->curr_sv_index];
		const float curr_length_perc = curr_sv->edge_len * (((sld->flipped ? perc : -perc) + 1.0f) / 2.0f);

		float co_a[3];
		float co_b[3];

		for (i = 0; i < sld->totsv; i++, sv++) {
			if (sv->edge_len > FLT_EPSILON) {
				const float fac = min_ff(sv->edge_len, curr_length_perc) / sv->edge_len;

				add_v3_v3v3(co_a, sv->v_co_orig, sv->dir_side[0]);
				add_v3_v3v3(co_b, sv->v_co_orig, sv->dir_side[1]);

				if (sld->flipped) {
					interp_line_v3_v3v3v3(sv->v->co, co_b, sv->v_co_orig, co_a, fac);
				}
				else {
					interp_line_v3_v3v3v3(sv->v->co, co_a, sv->v_co_orig, co_b, fac);
				}
			}
		}
	}
}

static void applyEdgeSlide(TransInfo *t, const int UNUSED(mval[2]))
{
	char str[UI_MAX_DRAW_STR];
	size_t ofs = 0;
	float final;
	EdgeSlideData *sld =  t->custom.mode.data;
	bool flipped = sld->flipped;
	bool use_even = sld->use_even;
	const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);
	const bool is_constrained = !(is_clamp == false || hasNumInput(&t->num));

	final = t->values[0];

	snapGridIncrement(t, &final);

	/* only do this so out of range values are not displayed */
	if (is_constrained) {
		CLAMP(final, -1.0f, 1.0f);
	}

	applyNumInput(&t->num, &final);

	t->values[0] = final;

	/* header string */
	ofs += BLI_strncpy_rlen(str + ofs, IFACE_("Edge Slide: "), sizeof(str) - ofs);
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];
		outputNumInput(&(t->num), c, &t->scene->unit);
		ofs += BLI_strncpy_rlen(str + ofs, &c[0], sizeof(str) - ofs);
	}
	else {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, "%.4f ", final);
	}
	ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("(E)ven: %s, "), WM_bool_as_string(use_even));
	if (use_even) {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("(F)lipped: %s, "), WM_bool_as_string(flipped));
	}
	ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Alt or (C)lamp: %s"), WM_bool_as_string(is_clamp));
	/* done with header string */

	/* do stuff here */
	doEdgeSlide(t, final);

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Vert Slide) */

/** \name Transform Vert Slide
 * \{ */

static void calcVertSlideCustomPoints(struct TransInfo *t)
{
	VertSlideData *sld = t->custom.mode.data;
	TransDataVertSlideVert *sv = &sld->sv[sld->curr_sv_index];

	const float *co_orig_3d = sv->co_orig_3d;
	const float *co_curr_3d = sv->co_link_orig_3d[sv->co_link_curr];

	float co_curr_2d[2], co_orig_2d[2];

	int mval_ofs[2], mval_start[2], mval_end[2];

	ED_view3d_project_float_v2_m4(t->ar, co_orig_3d, co_orig_2d, sld->proj_mat);
	ED_view3d_project_float_v2_m4(t->ar, co_curr_3d, co_curr_2d, sld->proj_mat);

	ARRAY_SET_ITEMS(mval_ofs, t->mouse.imval[0] - co_orig_2d[0], t->mouse.imval[1] - co_orig_2d[1]);
	ARRAY_SET_ITEMS(mval_start, co_orig_2d[0] + mval_ofs[0], co_orig_2d[1] + mval_ofs[1]);
	ARRAY_SET_ITEMS(mval_end, co_curr_2d[0] + mval_ofs[0], co_curr_2d[1] + mval_ofs[1]);

	if (sld->flipped && sld->use_even) {
		setCustomPoints(t, &t->mouse, mval_start, mval_end);
	}
	else {
		setCustomPoints(t, &t->mouse, mval_end, mval_start);
	}

	/* setCustomPoints isn't normally changing as the mouse moves,
	 * in this case apply mouse input immediately so we don't refresh
	 * with the value from the previous points */
	applyMouseInput(t, &t->mouse, t->mval, t->values);
}

/**
 * Run once when initializing vert slide to find the reference edge
 */
static void calcVertSlideMouseActiveVert(struct TransInfo *t, const int mval[2])
{
	VertSlideData *sld = t->custom.mode.data;
	float mval_fl[2] = {UNPACK2(mval)};
	TransDataVertSlideVert *sv;

	/* set the vertex to use as a reference for the mouse direction 'curr_sv_index' */
	float dist_sq = 0.0f;
	float dist_min_sq = FLT_MAX;
	int i;

	for (i = 0, sv = sld->sv; i < sld->totsv; i++, sv++) {
		float co_2d[2];

		ED_view3d_project_float_v2_m4(t->ar, sv->co_orig_3d, co_2d, sld->proj_mat);

		dist_sq = len_squared_v2v2(mval_fl, co_2d);
		if (dist_sq < dist_min_sq) {
			dist_min_sq = dist_sq;
			sld->curr_sv_index = i;
		}
	}
}

/**
 * Run while moving the mouse to slide along the edge matching the mouse direction
 */
static void calcVertSlideMouseActiveEdges(struct TransInfo *t, const int mval[2])
{
	VertSlideData *sld = t->custom.mode.data;
	float imval_fl[2] = {UNPACK2(t->mouse.imval)};
	float  mval_fl[2] = {UNPACK2(mval)};

	float dir[3];
	TransDataVertSlideVert *sv;
	int i;

	/* note: we could save a matrix-multiply for each vertex
	 * by finding the closest edge in local-space.
	 * However this skews the outcome with non-uniform-scale. */

	/* first get the direction of the original mouse position */
	sub_v2_v2v2(dir, imval_fl, mval_fl);
	ED_view3d_win_to_delta(t->ar, dir, dir, t->zfac);
	normalize_v3(dir);

	for (i = 0, sv = sld->sv; i < sld->totsv; i++, sv++) {
		if (sv->co_link_tot > 1) {
			float dir_dot_best = -FLT_MAX;
			int co_link_curr_best = -1;
			int j;

			for (j = 0; j < sv->co_link_tot; j++) {
				float tdir[3];
				float dir_dot;

				sub_v3_v3v3(tdir, sv->co_orig_3d, sv->co_link_orig_3d[j]);
				mul_mat3_m4_v3(t->obedit->obmat, tdir);
				project_plane_v3_v3v3(tdir, tdir, t->viewinv[2]);

				normalize_v3(tdir);
				dir_dot = dot_v3v3(dir, tdir);
				if (dir_dot > dir_dot_best) {
					dir_dot_best = dir_dot;
					co_link_curr_best = j;
				}
			}

			if (co_link_curr_best != -1) {
				sv->co_link_curr = co_link_curr_best;
			}
		}
	}
}

static bool createVertSlideVerts(TransInfo *t, bool use_even, bool flipped, bool use_clamp)
{
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	BMesh *bm = em->bm;
	BMIter iter;
	BMIter eiter;
	BMEdge *e;
	BMVert *v;
	TransDataVertSlideVert *sv_array;
	VertSlideData *sld = MEM_callocN(sizeof(*sld), "sld");
	int j;

	slide_origdata_init_flag(t, &sld->orig_data);

	sld->use_even = use_even;
	sld->curr_sv_index = 0;
	sld->flipped = flipped;
	if (!use_clamp)
		t->flag |= T_ALT_TRANSFORM;

	j = 0;
	BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
		bool ok = false;
		if (BM_elem_flag_test(v, BM_ELEM_SELECT) && v->e) {
			BM_ITER_ELEM (e, &eiter, v, BM_EDGES_OF_VERT) {
				if (!BM_elem_flag_test(e, BM_ELEM_HIDDEN)) {
					ok = true;
					break;
				}
			}
		}

		if (ok) {
			BM_elem_flag_enable(v, BM_ELEM_TAG);
			j += 1;
		}
		else {
			BM_elem_flag_disable(v, BM_ELEM_TAG);
		}
	}

	if (!j) {
		MEM_freeN(sld);
		return false;
	}

	sv_array = MEM_callocN(sizeof(TransDataVertSlideVert) * j, "sv_array");

	j = 0;
	BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
		if (BM_elem_flag_test(v, BM_ELEM_TAG)) {
			int k;
			sv_array[j].v = v;
			copy_v3_v3(sv_array[j].co_orig_3d, v->co);

			k = 0;
			BM_ITER_ELEM (e, &eiter, v, BM_EDGES_OF_VERT) {
				if (!BM_elem_flag_test(e, BM_ELEM_HIDDEN)) {
					k++;
				}
			}

			sv_array[j].co_link_orig_3d = MEM_mallocN(sizeof(*sv_array[j].co_link_orig_3d) * k, __func__);
			sv_array[j].co_link_tot = k;

			k = 0;
			BM_ITER_ELEM (e, &eiter, v, BM_EDGES_OF_VERT) {
				if (!BM_elem_flag_test(e, BM_ELEM_HIDDEN)) {
					BMVert *v_other = BM_edge_other_vert(e, v);
					copy_v3_v3(sv_array[j].co_link_orig_3d[k], v_other->co);
					k++;
				}
			}
			j++;
		}
	}

	sld->sv = sv_array;
	sld->totsv = j;

	bmesh_edit_begin(bm, BMO_OPTYPE_FLAG_UNTAN_MULTIRES);
	slide_origdata_init_data(t, &sld->orig_data);
	slide_origdata_create_data(t, &sld->orig_data, (TransDataGenericSlideVert *)sld->sv, sizeof(*sld->sv), sld->totsv);

	sld->em = em;

	sld->perc = 0.0f;

	t->custom.mode.data = sld;

	/* most likely will be set below */
	unit_m4(sld->proj_mat);

	if (t->spacetype == SPACE_VIEW3D) {
		/* view vars */
		RegionView3D *rv3d = NULL;
		ARegion *ar = t->ar;

		rv3d = ar ? ar->regiondata : NULL;
		if (rv3d) {
			ED_view3d_ob_project_mat_get(rv3d, t->obedit, sld->proj_mat);
		}

		calcVertSlideMouseActiveVert(t, t->mval);
		calcVertSlideMouseActiveEdges(t, t->mval);
	}

	return true;
}

void projectVertSlideData(TransInfo *t, bool is_final)
{
	VertSlideData *sld = t->custom.mode.data;
	SlideOrigData *sod = &sld->orig_data;

	if (sod->use_origfaces == false) {
		return;
	}

	slide_origdata_interp_data(t, sod, (TransDataGenericSlideVert *)sld->sv, sizeof(*sld->sv), sld->totsv, is_final);
}

void freeVertSlideTempFaces(VertSlideData *sld)
{
	slide_origdata_free_date(&sld->orig_data);
}

void freeVertSlideVerts(TransInfo *UNUSED(t), TransCustomData *custom_data)
{
	VertSlideData *sld = custom_data->data;

	if (!sld)
		return;

	freeVertSlideTempFaces(sld);

	bmesh_edit_end(sld->em->bm, BMO_OPTYPE_FLAG_UNTAN_MULTIRES);

	if (sld->totsv > 0) {
		TransDataVertSlideVert *sv = sld->sv;
		int i = 0;
		for (i = 0; i < sld->totsv; i++, sv++) {
			MEM_freeN(sv->co_link_orig_3d);
		}
	}

	MEM_freeN(sld->sv);
	MEM_freeN(sld);

	custom_data->data = NULL;
}

static void initVertSlide_ex(TransInfo *t, bool use_even, bool flipped, bool use_clamp)
{
	VertSlideData *sld;

	t->mode = TFM_VERT_SLIDE;
	t->transform = applyVertSlide;
	t->handleEvent = handleEventVertSlide;

	if (!createVertSlideVerts(t, use_even, flipped, use_clamp)) {
		t->state = TRANS_CANCEL;
		return;
	}

	sld = t->custom.mode.data;

	if (!sld)
		return;

	t->custom.mode.free_cb = freeVertSlideVerts;

	/* set custom point first if you want value to be initialized by init */
	calcVertSlideCustomPoints(t);
	initMouseInputMode(t, &t->mouse, INPUT_CUSTOM_RATIO);

	t->idx_max = 0;
	t->num.idx_max = 0;
	t->snap[0] = 0.0f;
	t->snap[1] = 0.1f;
	t->snap[2] = t->snap[1] * 0.1f;

	copy_v3_fl(t->num.val_inc, t->snap[1]);
	t->num.unit_sys = t->scene->unit.system;
	t->num.unit_type[0] = B_UNIT_NONE;

	t->flag |= T_NO_CONSTRAINT | T_NO_PROJECT;
}

static void initVertSlide(TransInfo *t)
{
	initVertSlide_ex(t, false, false, true);
}

static eRedrawFlag handleEventVertSlide(struct TransInfo *t, const struct wmEvent *event)
{
	if (t->mode == TFM_VERT_SLIDE) {
		VertSlideData *sld = t->custom.mode.data;

		if (sld) {
			switch (event->type) {
				case EKEY:
					if (event->val == KM_PRESS) {
						sld->use_even = !sld->use_even;
						if (sld->flipped) {
							calcVertSlideCustomPoints(t);
						}
						return TREDRAW_HARD;
					}
					break;
				case FKEY:
					if (event->val == KM_PRESS) {
						sld->flipped = !sld->flipped;
						calcVertSlideCustomPoints(t);
						return TREDRAW_HARD;
					}
					break;
				case CKEY:
					/* use like a modifier key */
					if (event->val == KM_PRESS) {
						t->flag ^= T_ALT_TRANSFORM;
						calcVertSlideCustomPoints(t);
						return TREDRAW_HARD;
					}
					break;
#if 0
				case EVT_MODAL_MAP:
					switch (event->val) {
						case TFM_MODAL_EDGESLIDE_DOWN:
							sld->curr_sv_index = ((sld->curr_sv_index - 1) + sld->totsv) % sld->totsv;
							break;
						case TFM_MODAL_EDGESLIDE_UP:
							sld->curr_sv_index = (sld->curr_sv_index + 1) % sld->totsv;
							break;
					}
					break;
#endif
				case MOUSEMOVE:
				{
					/* don't recalculate the best edge */
					const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);
					if (is_clamp) {
						calcVertSlideMouseActiveEdges(t, event->mval);
					}
					calcVertSlideCustomPoints(t);
					break;
				}
				default:
					break;
			}
		}
	}
	return TREDRAW_NOTHING;
}

static void drawVertSlide(TransInfo *t)
{
	if ((t->mode == TFM_VERT_SLIDE) && t->custom.mode.data) {
		VertSlideData *sld = t->custom.mode.data;
		const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);

		/* Non-Prop mode */
		{
			View3D *v3d = t->view;
			TransDataVertSlideVert *curr_sv = &sld->sv[sld->curr_sv_index];
			TransDataVertSlideVert *sv;
			const float ctrl_size = UI_GetThemeValuef(TH_FACEDOT_SIZE) + 1.5f;
			const float line_size = UI_GetThemeValuef(TH_OUTLINE_WIDTH) + 0.5f;
			const int alpha_shade = -160;
			int i;

			if (v3d && v3d->zbuf)
				glDisable(GL_DEPTH_TEST);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			glPushAttrib(GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT);
			glPushMatrix();

			glMultMatrixf(t->obedit->obmat);

			glLineWidth(line_size);
			UI_ThemeColorShadeAlpha(TH_EDGE_SELECT, 80, alpha_shade);
			glBegin(GL_LINES);
			if (is_clamp) {
				sv = sld->sv;
				for (i = 0; i < sld->totsv; i++, sv++) {
					glVertex3fv(sv->co_orig_3d);
					glVertex3fv(sv->co_link_orig_3d[sv->co_link_curr]);
				}
			}
			else {
				sv = sld->sv;
				for (i = 0; i < sld->totsv; i++, sv++) {
					float a[3], b[3];
					sub_v3_v3v3(a, sv->co_link_orig_3d[sv->co_link_curr], sv->co_orig_3d);
					mul_v3_fl(a, 100.0f);
					negate_v3_v3(b, a);
					add_v3_v3(a, sv->co_orig_3d);
					add_v3_v3(b, sv->co_orig_3d);

					glVertex3fv(a);
					glVertex3fv(b);
				}
			}
			glEnd();

			glPointSize(ctrl_size);

			glBegin(GL_POINTS);
			glVertex3fv((sld->flipped && sld->use_even) ?
			            curr_sv->co_link_orig_3d[curr_sv->co_link_curr] :
			            curr_sv->co_orig_3d);
			glEnd();

			glDisable(GL_BLEND);

			/* direction from active vertex! */
			if ((t->mval[0] != t->mouse.imval[0]) ||
			    (t->mval[1] != t->mouse.imval[1]))
			{
				float zfac;
				float mval_ofs[2];
				float co_orig_3d[3];
				float co_dest_3d[3];

				mval_ofs[0] = t->mval[0] - t->mouse.imval[0];
				mval_ofs[1] = t->mval[1] - t->mouse.imval[1];

				mul_v3_m4v3(co_orig_3d, t->obedit->obmat, curr_sv->co_orig_3d);
				zfac = ED_view3d_calc_zfac(t->ar->regiondata, co_orig_3d, NULL);

				ED_view3d_win_to_delta(t->ar, mval_ofs, co_dest_3d, zfac);

				invert_m4_m4(t->obedit->imat, t->obedit->obmat);
				mul_mat3_m4_v3(t->obedit->imat, co_dest_3d);

				add_v3_v3(co_dest_3d, curr_sv->co_orig_3d);

				glLineWidth(1);
				setlinestyle(1);

				cpack(0xffffff);
				glBegin(GL_LINES);
				glVertex3fv(curr_sv->co_orig_3d);
				glVertex3fv(co_dest_3d);

				glEnd();
			}

			glPopMatrix();
			glPopAttrib();

			if (v3d && v3d->zbuf)
				glEnable(GL_DEPTH_TEST);
		}
	}
}

static void doVertSlide(TransInfo *t, float perc)
{
	VertSlideData *sld = t->custom.mode.data;
	TransDataVertSlideVert *svlist = sld->sv, *sv;
	int i;

	sld->perc = perc;
	sv = svlist;

	if (sld->use_even == false) {
		for (i = 0; i < sld->totsv; i++, sv++) {
			interp_v3_v3v3(sv->v->co, sv->co_orig_3d, sv->co_link_orig_3d[sv->co_link_curr], perc);
		}
	}
	else {
		TransDataVertSlideVert *sv_curr = &sld->sv[sld->curr_sv_index];
		const float edge_len_curr = len_v3v3(sv_curr->co_orig_3d, sv_curr->co_link_orig_3d[sv_curr->co_link_curr]);
		const float tperc = perc * edge_len_curr;

		for (i = 0; i < sld->totsv; i++, sv++) {
			float edge_len;
			float dir[3];

			sub_v3_v3v3(dir, sv->co_link_orig_3d[sv->co_link_curr], sv->co_orig_3d);
			edge_len = normalize_v3(dir);

			if (edge_len > FLT_EPSILON) {
				if (sld->flipped) {
					madd_v3_v3v3fl(sv->v->co, sv->co_link_orig_3d[sv->co_link_curr], dir, -tperc);
				}
				else {
					madd_v3_v3v3fl(sv->v->co, sv->co_orig_3d, dir, tperc);
				}
			}
			else {
				copy_v3_v3(sv->v->co, sv->co_orig_3d);
			}
		}
	}
}

static void applyVertSlide(TransInfo *t, const int UNUSED(mval[2]))
{
	char str[UI_MAX_DRAW_STR];
	size_t ofs = 0;
	float final;
	VertSlideData *sld =  t->custom.mode.data;
	const bool flipped = sld->flipped;
	const bool use_even = sld->use_even;
	const bool is_clamp = !(t->flag & T_ALT_TRANSFORM);
	const bool is_constrained = !(is_clamp == false || hasNumInput(&t->num));

	final = t->values[0];

	snapGridIncrement(t, &final);

	/* only do this so out of range values are not displayed */
	if (is_constrained) {
		CLAMP(final, 0.0f, 1.0f);
	}

	applyNumInput(&t->num, &final);

	t->values[0] = final;

	/* header string */
	ofs += BLI_strncpy_rlen(str + ofs, IFACE_("Vert Slide: "), sizeof(str) - ofs);
	if (hasNumInput(&t->num)) {
		char c[NUM_STR_REP_LEN];
		outputNumInput(&(t->num), c, &t->scene->unit);
		ofs += BLI_strncpy_rlen(str + ofs, &c[0], sizeof(str) - ofs);
	}
	else {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, "%.4f ", final);
	}
	ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("(E)ven: %s, "), WM_bool_as_string(use_even));
	if (use_even) {
		ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("(F)lipped: %s, "), WM_bool_as_string(flipped));
	}
	ofs += BLI_snprintf(str + ofs, sizeof(str) - ofs, IFACE_("Alt or (C)lamp: %s"), WM_bool_as_string(is_clamp));
	/* done with header string */

	/* do stuff here */
	doVertSlide(t, final);

	recalcData(t);

	ED_area_headerprint(t->sa, str);
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Mirror) */

/** \name Transform Mirror
 * \{ */

static void initMirror(TransInfo *t)
{
	t->transform = applyMirror;
	initMouseInputMode(t, &t->mouse, INPUT_NONE);

	t->flag |= T_NULL_ONE;
	if (!t->obedit) {
		t->flag |= T_NO_ZERO;
	}
}

static void applyMirror(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td;
	float size[3], mat[3][3];
	int i;
	char str[UI_MAX_DRAW_STR];

	/*
	 * OPTIMIZATION:
	 * This still recalcs transformation on mouse move
	 * while it should only recalc on constraint change
	 * */

	/* if an axis has been selected */
	if (t->con.mode & CON_APPLY) {
		size[0] = size[1] = size[2] = -1;

		size_to_mat3(mat, size);

		if (t->con.applySize) {
			t->con.applySize(t, NULL, mat);
		}

		BLI_snprintf(str, sizeof(str), IFACE_("Mirror%s"), t->con.text);

		for (i = 0, td = t->data; i < t->total; i++, td++) {
			if (td->flag & TD_NOACTION)
				break;

			if (td->flag & TD_SKIP)
				continue;

			ElementResize(t, td, mat);
		}

		recalcData(t);

		ED_area_headerprint(t->sa, str);
	}
	else {
		size[0] = size[1] = size[2] = 1;

		size_to_mat3(mat, size);

		for (i = 0, td = t->data; i < t->total; i++, td++) {
			if (td->flag & TD_NOACTION)
				break;

			if (td->flag & TD_SKIP)
				continue;

			ElementResize(t, td, mat);
		}

		recalcData(t);

		if (t->flag & T_2D_EDIT)
			ED_area_headerprint(t->sa, IFACE_("Select a mirror axis (X, Y)"));
		else
			ED_area_headerprint(t->sa, IFACE_("Select a mirror axis (X, Y, Z)"));
	}
}
/** \} */


/* -------------------------------------------------------------------- */
/* Transform (Align) */

/** \name Transform Align
 * \{ */

static void initAlign(TransInfo *t)
{
	t->flag |= T_NO_CONSTRAINT;

	t->transform = applyAlign;

	initMouseInputMode(t, &t->mouse, INPUT_NONE);
}

static void applyAlign(TransInfo *t, const int UNUSED(mval[2]))
{
	TransData *td = t->data;
	float center[3];
	int i;

	/* saving original center */
	copy_v3_v3(center, t->center);

	for (i = 0; i < t->total; i++, td++) {
		float mat[3][3], invmat[3][3];

		if (td->flag & TD_NOACTION)
			break;

		if (td->flag & TD_SKIP)
			continue;

		/* around local centers */
		if (t->flag & T_OBJECT) {
			copy_v3_v3(t->center, td->center);
		}
		else {
			if (t->settings->selectmode & SCE_SELECT_FACE) {
				copy_v3_v3(t->center, td->center);
			}
		}

		invert_m3_m3(invmat, td->axismtx);

		mul_m3_m3m3(mat, t->spacemtx, invmat);

		ElementRotation(t, td, mat, t->around);
	}

	/* restoring original center */
	copy_v3_v3(t->center, center);

	recalcData(t);

	ED_area_headerprint(t->sa, IFACE_("Align"));
}
/** \} */


/* TODO, move to: transform_query.c */
bool checkUseAxisMatrix(TransInfo *t)
{
	/* currently only checks for editmode */
	if (t->flag & T_EDIT) {
		if ((t->around == V3D_AROUND_LOCAL_ORIGINS) &&
		    (ELEM(t->obedit->type, OB_MESH, OB_CURVE)))
		{
			/* not all editmode supports axis-matrix */
			return true;
		}
	}

	return false;
}
