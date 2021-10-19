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

/** \file blender/editors/transform/transform_generics.c
 *  \ingroup edtransform
 */

#include <string.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_sys_types.h" /* for intptr_t support */

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_mesh_types.h"
#include "DNA_view3d_types.h"
#include "DNA_modifier_types.h"
#include "DNA_curve_types.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "BKE_curve.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_object.h"

#include "ED_image.h"
#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_screen_types.h"
#include "ED_space_api.h"
#include "ED_view3d.h"
#include "ED_curve.h" /* for curve_editnurbs */
#include "ED_screen.h"

#include "WM_types.h"
#include "WM_api.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "transform.h"

/* ************************** Functions *************************** */

void getViewVector(const TransInfo *t, const float coord[3], float vec[3])
{
	if (t->persp != RV3D_ORTHO) {
		sub_v3_v3v3(vec, coord, t->viewinv[3]);
	}
	else {
		copy_v3_v3(vec, t->viewinv[2]);
	}
	normalize_v3(vec);
}

/* ************************** GENERICS **************************** */


static void clipMirrorModifier(TransInfo *t, Object *ob)
{
	ModifierData *md = ob->modifiers.first;
	float tolerance[3] = {0.0f, 0.0f, 0.0f};
	int axis = 0;

	for (; md; md = md->next) {
		if ((md->type == eModifierType_Mirror) && (md->mode & eModifierMode_Realtime)) {
			MirrorModifierData *mmd = (MirrorModifierData *) md;

			if (mmd->flag & MOD_MIR_CLIPPING) {
				axis = 0;
				if (mmd->flag & MOD_MIR_AXIS_X) {
					axis |= 1;
					tolerance[0] = mmd->tolerance;
				}
				if (mmd->flag & MOD_MIR_AXIS_Y) {
					axis |= 2;
					tolerance[1] = mmd->tolerance;
				}
				if (mmd->flag & MOD_MIR_AXIS_Z) {
					axis |= 4;
					tolerance[2] = mmd->tolerance;
				}
				if (axis) {
					float mtx[4][4], imtx[4][4];
					int i;
					TransData *td = t->data;

					if (mmd->mirror_ob) {
						float obinv[4][4];

						invert_m4_m4(obinv, mmd->mirror_ob->obmat);
						mul_m4_m4m4(mtx, obinv, ob->obmat);
						invert_m4_m4(imtx, mtx);
					}

					for (i = 0; i < t->total; i++, td++) {
						int clip;
						float loc[3], iloc[3];

						if (td->flag & TD_NOACTION)
							break;
						if (td->loc == NULL)
							break;

						if (td->flag & TD_SKIP)
							continue;

						copy_v3_v3(loc,  td->loc);
						copy_v3_v3(iloc, td->iloc);

						if (mmd->mirror_ob) {
							mul_m4_v3(mtx, loc);
							mul_m4_v3(mtx, iloc);
						}

						clip = 0;
						if (axis & 1) {
							if (fabsf(iloc[0]) <= tolerance[0] ||
							    loc[0] * iloc[0] < 0.0f)
							{
								loc[0] = 0.0f;
								clip = 1;
							}
						}

						if (axis & 2) {
							if (fabsf(iloc[1]) <= tolerance[1] ||
							    loc[1] * iloc[1] < 0.0f)
							{
								loc[1] = 0.0f;
								clip = 1;
							}
						}
						if (axis & 4) {
							if (fabsf(iloc[2]) <= tolerance[2] ||
							    loc[2] * iloc[2] < 0.0f)
							{
								loc[2] = 0.0f;
								clip = 1;
							}
						}
						if (clip) {
							if (mmd->mirror_ob) {
								mul_m4_v3(imtx, loc);
							}
							copy_v3_v3(td->loc, loc);
						}
					}
				}

			}
		}
	}
}

/* assumes obedit set to mesh object */
static void editbmesh_apply_to_mirror(TransInfo *t)
{
	TransData *td = t->data;
	BMVert *eve;
	int i;

	for (i = 0; i < t->total; i++, td++) {
		if (td->flag & TD_NOACTION)
			break;
		if (td->loc == NULL)
			break;
		if (td->flag & TD_SKIP)
			continue;

		eve = td->extra;
		if (eve) {
			eve->co[0] = -td->loc[0];
			eve->co[1] = td->loc[1];
			eve->co[2] = td->loc[2];
		}

		if (td->flag & TD_MIRROR_EDGE) {
			td->loc[0] = 0;
		}
	}
}

/* helper for recalcData() - for object transforms, typically in the 3D view */
static void recalcData_objects(TransInfo *t)
{
	if (t->obedit) {
		if (ELEM(t->obedit->type, OB_CURVE, OB_SURF)) {
			Curve *cu = t->obedit->data;
			ListBase *nurbs = BKE_curve_editNurbs_get(cu);
			Nurb *nu = nurbs->first;

			if (t->state != TRANS_CANCEL) {
				clipMirrorModifier(t, t->obedit);
				applyProject(t);
			}
			
			if (t->state == TRANS_CANCEL) {
				while (nu) {
					BKE_nurb_handles_calc(nu); /* Cant do testhandlesNurb here, it messes up the h1 and h2 flags */
					nu = nu->next;
				}
			}
			else {
				/* Normal updating */
				while (nu) {
					BKE_nurb_test_2d(nu);
					BKE_nurb_handles_calc(nu);
					nu = nu->next;
				}
			}

			cu->id.mod_id++;
		}
		else if (t->obedit->type == OB_MESH) {
			BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
			/* mirror modifier clipping? */
			if (t->state != TRANS_CANCEL) {
				/* apply clipping after so we never project past the clip plane [#25423] */
				applyProject(t);
				clipMirrorModifier(t, t->obedit);
			}
			if ((t->options & CTX_NO_MIRROR) == 0 && (t->flag & T_MIRROR))
				editbmesh_apply_to_mirror(t);

			if (t->mode == TFM_EDGE_SLIDE) {
				projectEdgeSlideData(t, false);
			}
			else if (t->mode == TFM_VERT_SLIDE) {
				projectVertSlideData(t, false);
			}

			((ID *)t->obedit->data)->mod_id++;
			EDBM_mesh_normals_update(em);
			BKE_editmesh_tessface_calc(em);
		}
		else {
			if (t->state != TRANS_CANCEL) {
				applyProject(t);
			}
		}
	}
	else {
		if (t->state != TRANS_CANCEL) {
			applyProject(t);
		}

		for (int i = 0; i < t->total; i++) {
			TransData *td = t->data + i;

			if (td->flag & TD_NOACTION)
				break;

			if (td->flag & TD_SKIP)
				continue;

			td->ob->id.recalc |= OB_RECALC_OB;

		}
	}
}

/* called for updating while transform acts, once per redraw */
void recalcData(TransInfo *t)
{
	/* if tests must match createTransData for correct updates */
	recalcData_objects(t);
}

void drawLine(TransInfo *t, const float center[3], const float dir[3], char axis, short options)
{
	float v1[3], v2[3], v3[3];
	unsigned char col[3], col2[3];

	if (t->spacetype == SPACE_VIEW3D) {
		View3D *v3d = t->view;

		glPushMatrix();

		//if (t->obedit) glLoadMatrixf(t->obedit->obmat);	// sets opengl viewing


		copy_v3_v3(v3, dir);
		mul_v3_fl(v3, v3d->far);

		sub_v3_v3v3(v2, center, v3);
		add_v3_v3v3(v1, center, v3);

		if (options & DRAWLIGHT) {
			col[0] = col[1] = col[2] = 220;
		}
		else {
			UI_GetThemeColor3ubv(TH_GRID, col);
		}
		UI_make_axis_color(col, col2, axis);
		glColor3ubv(col2);

		setlinestyle(0);
		glBegin(GL_LINES);
		glVertex3fv(v1);
		glVertex3fv(v2);
		glEnd();

		glPopMatrix();
	}
}

/**
 * Free data before switching to another mode.
 */
void resetTransModal(TransInfo *t)
{
	if (t->mode == TFM_EDGE_SLIDE) {
		freeEdgeSlideVerts(t, &t->custom.mode);
	}
	else if (t->mode == TFM_VERT_SLIDE) {
		freeVertSlideVerts(t, &t->custom.mode);
	}
}

void resetTransRestrictions(TransInfo *t)
{
	t->flag &= ~T_ALL_RESTRICTIONS;
}

static int initTransInfo_edit_pet_to_flag(const int proportional)
{
	switch (proportional) {
		case PROP_EDIT_ON:
			return T_PROP_EDIT;
		case PROP_EDIT_CONNECTED:
			return T_PROP_EDIT | T_PROP_CONNECTED;
		case PROP_EDIT_PROJECTED:
			return T_PROP_EDIT | T_PROP_PROJECTED;
		default:
			return 0;
	}
}

/**
 * Setup internal data, mouse, vectors
 *
 * \note \a op and \a event can be NULL
 *
 * \see #saveTransform does the reverse.
 */
void initTransInfo(bContext *C, TransInfo *t, wmOperator *op, const wmEvent *event)
{
	Scene *sce = CTX_data_scene(C);
	ToolSettings *ts = CTX_data_tool_settings(C);
	ARegion *ar = CTX_wm_region(C);
	ScrArea *sa = CTX_wm_area(C);
	Object *obedit = CTX_data_edit_object(C);
	PropertyRNA *prop;

	t->scene = sce;
	t->sa = sa;
	t->ar = ar;
	t->obedit = obedit;
	t->settings = ts;
	t->reports = op ? op->reports : NULL;

	if (obedit) {
		copy_m3_m4(t->obedit_mat, obedit->obmat);
		normalize_m3(t->obedit_mat);
	}

	t->data = NULL;
	t->ext = NULL;

	t->helpline = HLP_NONE;

	t->flag = 0;

	t->redraw = TREDRAW_HARD;  /* redraw first time */

	if (event) {
		t->mouse.imval[0] = event->mval[0];
		t->mouse.imval[1] = event->mval[1];
	}
	else {
		t->mouse.imval[0] = 0;
		t->mouse.imval[1] = 0;
	}

	t->con.imval[0] = t->mouse.imval[0];
	t->con.imval[1] = t->mouse.imval[1];

	t->mval[0] = t->mouse.imval[0];
	t->mval[1] = t->mouse.imval[1];

	t->transform        = NULL;
	t->handleEvent      = NULL;

	t->total            = 0;

	t->val = 0.0f;

	zero_v3(t->vec);
	zero_v3(t->center);
	zero_v3(t->center_global);

	unit_m3(t->mat);

	/* if there's an event, we're modal */
	if (event) {
		t->flag |= T_MODAL;
	}

	/* Crease needs edge flag */
	if (ELEM(t->mode, TFM_CREASE, TFM_BWEIGHT)) {
		t->options |= CTX_EDGE;
	}

	t->remove_on_cancel = false;

	if (op && (prop = RNA_struct_find_property(op->ptr, "remove_on_cancel")) && RNA_property_is_set(op->ptr, prop)) {
		if (RNA_property_boolean_get(op->ptr, prop)) {
			t->remove_on_cancel = true;
		}
	}

	/* Assign the space type, some exceptions for running in different mode */
	if (sa == NULL) {
		/* background mode */
		t->spacetype = SPACE_EMPTY;
	}
	else if ((ar == NULL) && (sa->spacetype == SPACE_VIEW3D)) {
		/* running in the text editor */
		t->spacetype = SPACE_EMPTY;
	}
	else {
		/* normal operation */
		t->spacetype = sa->spacetype;
	}

	/* handle T_ALT_TRANSFORM initialization, we may use for different operators */
	if (op) {
		const char *prop_id = NULL;
		if (t->mode == TFM_SHRINKFATTEN) {
			prop_id = "use_even_offset";
		}

		if (prop_id && (prop = RNA_struct_find_property(op->ptr, prop_id)) &&
		    RNA_property_is_set(op->ptr, prop))
		{
			SET_FLAG_FROM_TEST(t->flag, RNA_property_boolean_get(op->ptr, prop), T_ALT_TRANSFORM);
		}
	}

	if (t->spacetype == SPACE_VIEW3D) {
		View3D *v3d = sa->spacedata.first;

		t->view = v3d;
		/* turn manipulator off during transform */
		// FIXME: but don't do this when USING the manipulator...
		if (t->flag & T_MODAL) {
			t->twtype = v3d->twtype;
			v3d->twtype = 0;
		}

		if (v3d->flag & V3D_ALIGN) t->flag |= T_V3D_ALIGN;
		t->around = v3d->around;

		/* bend always uses the cursor */
		if (t->mode == TFM_BEND) {
			t->around = V3D_AROUND_CURSOR;
		}

		t->current_orientation = v3d->twmode;

		/* exceptional case */
		if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
			if (ELEM(t->mode, TFM_ROTATION, TFM_RESIZE, TFM_TRACKBALL)) {
				const bool use_island = transdata_check_local_islands(t, t->around);

				if (obedit && !use_island) {
					t->options |= CTX_NO_PET;
				}
			}
		}


	}
	else if (t->spacetype == SPACE_IMAGE) {
		SpaceImage *sima = sa->spacedata.first;
		// XXX for now, get View2D from the active region
		t->view = &ar->v2d;
		t->around = sima->around;

		/* image not in uv edit, nor in mask mode, can happen for some tools */
	}
	else {
		if (ar) {
			// XXX for now, get View2D  from the active region
			t->view = &ar->v2d;
			// XXX for now, the center point is the midpoint of the data
		}
		else {
			t->view = NULL;
		}
		t->around = V3D_AROUND_CENTER_BOUNDS;
	}

	if (op && ((prop = RNA_struct_find_property(op->ptr, "constraint_orientation")) &&
	           RNA_property_is_set(op->ptr, prop)))
	{
		t->current_orientation = RNA_property_enum_get(op->ptr, prop);

		if (t->current_orientation >= V3D_MANIP_CUSTOM + BIF_countTransformOrientation(C)) {
			t->current_orientation = V3D_MANIP_GLOBAL;
		}
	}

	if (op && ((prop = RNA_struct_find_property(op->ptr, "release_confirm")) &&
	           RNA_property_is_set(op->ptr, prop)))
	{
		if (RNA_property_boolean_get(op->ptr, prop)) {
			t->flag |= T_RELEASE_CONFIRM;
		}
	}
	else {
		if (U.flag & USER_RELEASECONFIRM) {
			t->flag |= T_RELEASE_CONFIRM;
		}
	}

	if (op && ((prop = RNA_struct_find_property(op->ptr, "mirror")) &&
	           RNA_property_is_set(op->ptr, prop)))
	{
		if (RNA_property_boolean_get(op->ptr, prop)) {
			t->flag |= T_MIRROR;
			t->mirror = 1;
		}
	}
	// Need stuff to take it from edit mesh or whatnot here
	else if (t->spacetype == SPACE_VIEW3D) {
		if (t->obedit && t->obedit->type == OB_MESH && (((Mesh *)t->obedit->data)->editflag & ME_EDIT_MIRROR_X)) {
			t->flag |= T_MIRROR;
			t->mirror = 1;
		}
	}

	/* setting PET flag only if property exist in operator. Otherwise, assume it's not supported */
	if (op && (prop = RNA_struct_find_property(op->ptr, "proportional"))) {
		if (RNA_property_is_set(op->ptr, prop)) {
			t->flag |= initTransInfo_edit_pet_to_flag(RNA_property_enum_get(op->ptr, prop));
		}
		else {
			/* use settings from scene only if modal */
			if (t->flag & T_MODAL) {
				if ((t->options & CTX_NO_PET) == 0) {
					if (t->obedit) {
						t->flag |= initTransInfo_edit_pet_to_flag(ts->proportional);
					}
					else if (t->obedit == NULL && ts->proportional_objects) {
						t->flag |= T_PROP_EDIT;
					}
				}
			}
		}

		if (op && ((prop = RNA_struct_find_property(op->ptr, "proportional_size")) &&
		           RNA_property_is_set(op->ptr, prop)))
		{
			t->prop_size = RNA_property_float_get(op->ptr, prop);
		}
		else {
			t->prop_size = ts->proportional_size;
		}


		/* TRANSFORM_FIX_ME rna restrictions */
		if (t->prop_size <= 0.00001f) {
			printf("Proportional size (%f) under 0.00001, resetting to 1!\n", t->prop_size);
			t->prop_size = 1.0f;
		}

		if (op && ((prop = RNA_struct_find_property(op->ptr, "proportional_edit_falloff")) &&
		           RNA_property_is_set(op->ptr, prop)))
		{
			t->prop_mode = RNA_property_enum_get(op->ptr, prop);
		}
		else {
			t->prop_mode = ts->prop_mode;
		}
	}
	else { /* add not pet option to context when not available */
		t->options |= CTX_NO_PET;
	}

	// Mirror is not supported with PET, turn it off.
#if 0
	if (t->flag & T_PROP_EDIT) {
		t->flag &= ~T_MIRROR;
	}
#endif

	setTransformViewAspect(t, t->aspect);

	if (op && (prop = RNA_struct_find_property(op->ptr, "center_override")) && RNA_property_is_set(op->ptr, prop)) {
		RNA_property_float_get_array(op->ptr, prop, t->center);
		mul_v3_v3(t->center, t->aspect);
		t->flag |= T_OVERRIDE_CENTER;
	}

	setTransformViewMatrices(t);
	initNumInput(&t->num);
}

/* Here I would suggest only TransInfo related issues, like free data & reset vars. Not redraws */
void postTrans(bContext *C, TransInfo *t)
{
	TransData *td;

	if (t->draw_handle_view)
		ED_region_draw_cb_exit(t->ar->type, t->draw_handle_view);
	if (t->draw_handle_apply)
		ED_region_draw_cb_exit(t->ar->type, t->draw_handle_apply);
	if (t->draw_handle_pixel)
		ED_region_draw_cb_exit(t->ar->type, t->draw_handle_pixel);
	if (t->draw_handle_cursor)
		WM_paint_cursor_end(CTX_wm_manager(C), t->draw_handle_cursor);

	/* Free all custom-data */
	{
		TransCustomData *custom_data = &t->custom.first_elem;
		for (int i = 0; i < TRANS_CUSTOM_DATA_ELEM_MAX; i++, custom_data++) {
			if (custom_data->free_cb) {
				/* Can take over freeing t->data and data2d etc... */
				custom_data->free_cb(t, custom_data);
				BLI_assert(custom_data->data == NULL);
			}
			else if ((custom_data->data != NULL) && custom_data->use_free) {
				MEM_freeN(custom_data->data);
				custom_data->data = NULL;
			}
		}
	}

	/* postTrans can be called when nothing is selected, so data is NULL already */
	if (t->data) {

		/* free data malloced per trans-data */
		if ((t->obedit && ELEM(t->obedit->type, OB_CURVE, OB_SURF)))
		{
			int a;
			for (a = 0, td = t->data; a < t->total; a++, td++) {
				if (td->flag & TD_BEZTRIPLE) {
					MEM_freeN(td->hdata);
				}
			}
		}
		MEM_freeN(t->data);
	}

	BLI_freelistN(&t->tsnap.points);

	if (t->ext) MEM_freeN(t->ext);
	if (t->data2d) {
		MEM_freeN(t->data2d);
		t->data2d = NULL;
	}

	if (t->spacetype == SPACE_VIEW3D) {
		View3D *v3d = t->sa->spacedata.first;
		/* restore manipulator */
		if (t->flag & T_MODAL) {
			v3d->twtype = t->twtype;
		}
	}

	if (t->mouse.data) {
		MEM_freeN(t->mouse.data);
	}

	freeSnapping(t);
}

void applyTransObjects(TransInfo *t)
{
	TransData *td;

	for (td = t->data; td < t->data + t->total; td++) {
		copy_v3_v3(td->iloc, td->loc);
		if (td->ext->rot) {
			copy_v3_v3(td->ext->irot, td->ext->rot);
		}
		if (td->ext->size) {
			copy_v3_v3(td->ext->isize, td->ext->size);
		}
	}
	recalcData(t);
}

static void restoreElement(TransData *td)
{
	/* TransData for crease has no loc */
	if (td->loc) {
		copy_v3_v3(td->loc, td->iloc);
	}
	if (td->val) {
		*td->val = td->ival;
	}

	if (td->ext && (td->flag & TD_NO_EXT) == 0) {
		if (td->ext->rot) {
			copy_v3_v3(td->ext->rot, td->ext->irot);
		}
		if (td->ext->rotAngle) {
			*td->ext->rotAngle = td->ext->irotAngle;
		}
		if (td->ext->rotAxis) {
			copy_v3_v3(td->ext->rotAxis, td->ext->irotAxis);
		}
		/* XXX, drotAngle & drotAxis not used yet */
		if (td->ext->size) {
			copy_v3_v3(td->ext->size, td->ext->isize);
		}
		if (td->ext->quat) {
			copy_qt_qt(td->ext->quat, td->ext->iquat);
		}
	}

	if (td->flag & TD_BEZTRIPLE) {
		*(td->hdata->h1) = td->hdata->ih1;
		*(td->hdata->h2) = td->hdata->ih2;
	}
}

void restoreTransObjects(TransInfo *t)
{
	TransData *td;
	TransData2D *td2d;

	for (td = t->data; td < t->data + t->total; td++) {
		restoreElement(td);
	}

	for (td2d = t->data2d; t->data2d && td2d < t->data2d + t->total; td2d++) {
		if (td2d->h1) {
			td2d->h1[0] = td2d->ih1[0];
			td2d->h1[1] = td2d->ih1[1];
		}
		if (td2d->h2) {
			td2d->h2[0] = td2d->ih2[0];
			td2d->h2[1] = td2d->ih2[1];
		}
	}

	unit_m3(t->mat);

	recalcData(t);
}

void calculateCenter2D(TransInfo *t)
{
	BLI_assert(!is_zero_v3(t->aspect));

	if (t->flag & (T_EDIT)) {
		Object *ob = t->obedit;
		float vec[3];

		copy_v3_v3(vec, t->center);
		mul_m4_v3(ob->obmat, vec);
		projectFloatView(t, vec, t->center2d);
	}
	else {
		projectFloatView(t, t->center, t->center2d);
	}
}

void calculateCenterGlobal(
        TransInfo *t, const float center_local[3],
        float r_center_global[3])
{
	/* setting constraint center */
	/* note, init functions may over-ride t->center */
	if (t->flag & (T_EDIT)) {
		Object *ob = t->obedit;
		mul_v3_m4v3(r_center_global, ob->obmat, center_local);
	}
	else {
		copy_v3_v3(r_center_global, center_local);
	}
}

void calculateCenterCursor(TransInfo *t, float r_center[3])
{
	const float *cursor;

	cursor = ED_view3d_cursor3d_get(t->scene, t->view);
	copy_v3_v3(r_center, cursor);

	/* If edit move cursor in local space */
	if (t->flag & (T_EDIT)) {
		Object *ob = t->obedit;
		float mat[3][3], imat[3][3];

		sub_v3_v3v3(r_center, r_center, ob->obmat[3]);
		copy_m3_m4(mat, ob->obmat);
		invert_m3_m3(imat, mat);
		mul_m3_v3(imat, r_center);
	}
}

void calculateCenterCursor2D(TransInfo *t, float r_center[2])
{
	const float *cursor = NULL;

	if (t->spacetype == SPACE_IMAGE) {
		SpaceImage *sima = (SpaceImage *)t->sa->spacedata.first;
		cursor = sima->cursor;
	}

	if (cursor) {
		r_center[0] = cursor[0] * t->aspect[0];
		r_center[1] = cursor[1] * t->aspect[1];
	}
}

void calculateCenterMedian(TransInfo *t, float r_center[3])
{
	float partial[3] = {0.0f, 0.0f, 0.0f};
	int total = 0;
	int i;

	for (i = 0; i < t->total; i++) {
		if (t->data[i].flag & TD_SELECTED) {
			if (!(t->data[i].flag & TD_NOCENTER)) {
				add_v3_v3(partial, t->data[i].center);
				total++;
			}
		}
	}
	if (total) {
		mul_v3_fl(partial, 1.0f / (float)total);
	}
	copy_v3_v3(r_center, partial);
}

void calculateCenterBound(TransInfo *t, float r_center[3])
{
	float max[3];
	float min[3];
	int i;
	for (i = 0; i < t->total; i++) {
		if (i) {
			if (t->data[i].flag & TD_SELECTED) {
				if (!(t->data[i].flag & TD_NOCENTER))
					minmax_v3v3_v3(min, max, t->data[i].center);
			}
		}
		else {
			copy_v3_v3(max, t->data[i].center);
			copy_v3_v3(min, t->data[i].center);
		}
	}
	mid_v3_v3v3(r_center, min, max);
}

/**
 * \param select_only: only get active center from data being transformed.
 */
bool calculateCenterActive(TransInfo *t, bool select_only, float r_center[3])
{
	bool ok = false;

	if (t->obedit) {
		if (ED_object_editmode_calc_active_center(t->obedit, select_only, r_center)) {
			ok = true;
		}
	}
	else {
		/* object mode */
		Scene *scene = t->scene;
		Object *ob = OBACT;
		if (ob && (!select_only || (ob->flag & SELECT))) {
			copy_v3_v3(r_center, ob->obmat[3]);
			ok = true;
		}
	}

	return ok;
}

static void calculateCenter_FromAround(TransInfo *t, int around, float r_center[3])
{
	switch (around) {
		case V3D_AROUND_CENTER_BOUNDS:
			calculateCenterBound(t, r_center);
			break;
		case V3D_AROUND_CENTER_MEDIAN:
			calculateCenterMedian(t, r_center);
			break;
		case V3D_AROUND_CURSOR:
			calculateCenterCursor(t, r_center);
			break;
		case V3D_AROUND_LOCAL_ORIGINS:
			/* Individual element center uses median center for helpline and such */
			calculateCenterMedian(t, r_center);
			break;
		case V3D_AROUND_ACTIVE:
		{
			if (calculateCenterActive(t, false, r_center)) {
				/* pass */
			}
			else {
				/* fallback */
				calculateCenterMedian(t, r_center);
			}
			break;
		}
	}
}

void calculateCenter(TransInfo *t)
{
	if ((t->flag & T_OVERRIDE_CENTER) == 0) {
		calculateCenter_FromAround(t, t->around, t->center);
	}
	calculateCenterGlobal(t, t->center, t->center_global);

	/* avoid calculating again */
	{
		TransCenterData *cd = &t->center_cache[t->around];
		copy_v3_v3(cd->local, t->center);
		copy_v3_v3(cd->global, t->center_global);
		cd->is_set = true;
	}

	calculateCenter2D(t);

	/* for panning from cameraview */
	if ((t->flag & T_OBJECT) && (t->flag & T_OVERRIDE_CENTER) == 0) {
		if (t->spacetype == SPACE_VIEW3D && t->ar && t->ar->regiontype == RGN_TYPE_WINDOW) {

			if (t->flag & T_CAMERA) {
				float axis[3];
				/* persinv is nasty, use viewinv instead, always right */
				copy_v3_v3(axis, t->viewinv[2]);
				normalize_v3(axis);

				/* 6.0 = 6 grid units */
				axis[0] = t->center[0] - 6.0f * axis[0];
				axis[1] = t->center[1] - 6.0f * axis[1];
				axis[2] = t->center[2] - 6.0f * axis[2];

				projectFloatView(t, axis, t->center2d);

				/* rotate only needs correct 2d center, grab needs ED_view3d_calc_zfac() value */
				if (t->mode == TFM_TRANSLATION) {
					copy_v3_v3(t->center, axis);
					copy_v3_v3(t->center_global, t->center);
				}
			}
		}
	}

	if (t->spacetype == SPACE_VIEW3D) {
		/* ED_view3d_calc_zfac() defines a factor for perspective depth correction, used in ED_view3d_win_to_delta() */

		/* zfac is only used convertViewVec only in cases operator was invoked in RGN_TYPE_WINDOW
		 * and never used in other cases.
		 *
		 * We need special case here as well, since ED_view3d_calc_zfac will crash when called
		 * for a region different from RGN_TYPE_WINDOW.
		 */
		if (t->ar->regiontype == RGN_TYPE_WINDOW) {
			t->zfac = ED_view3d_calc_zfac(t->ar->regiondata, t->center_global, NULL);
		}
		else {
			t->zfac = 0.0f;
		}
	}
}

BLI_STATIC_ASSERT(ARRAY_SIZE(((TransInfo *)NULL)->center_cache) == (V3D_AROUND_ACTIVE + 1), "test size");

/**
 * Lazy initialize transform center data, when we need to access center values from other types.
 */
const TransCenterData *transformCenter_from_type(TransInfo *t, int around)
{
	BLI_assert(around <= V3D_AROUND_ACTIVE);
	TransCenterData *cd = &t->center_cache[around];
	if (cd->is_set == false) {
		calculateCenter_FromAround(t, around, cd->local);
		calculateCenterGlobal(t, cd->local, cd->global);
		cd->is_set = true;
	}
	return cd;
}

void calculatePropRatio(TransInfo *t)
{
	TransData *td = t->data;
	int i;
	float dist;
	const bool connected = (t->flag & T_PROP_CONNECTED) != 0;

	t->proptext[0] = '\0';

	if (t->flag & T_PROP_EDIT) {
		const char *pet_id = NULL;
		for (i = 0; i < t->total; i++, td++) {
			if (td->flag & TD_SELECTED) {
				td->factor = 1.0f;
			}
			else if (t->flag & T_MIRROR && td->loc[0] * t->mirror < -0.00001f) {
				td->flag |= TD_SKIP;
				td->factor = 0.0f;
				restoreElement(td);
			}
			else if ((connected && (td->flag & TD_NOTCONNECTED || td->dist > t->prop_size)) ||
			         (connected == 0 && td->rdist > t->prop_size))
			{
				/*
				 * The elements are sorted according to their dist member in the array,
				 * that means we can stop when it finds one element outside of the propsize.
				 * do not set 'td->flag |= TD_NOACTION', the prop circle is being changed.
				 */

				td->factor = 0.0f;
				restoreElement(td);
			}
			else {
				/* Use rdist for falloff calculations, it is the real distance */
				td->flag &= ~TD_NOACTION;

				if (connected)
					dist = (t->prop_size - td->dist) / t->prop_size;
				else
					dist = (t->prop_size - td->rdist) / t->prop_size;

				/*
				 * Clamp to positive numbers.
				 * Certain corner cases with connectivity and individual centers
				 * can give values of rdist larger than propsize.
				 */
				if (dist < 0.0f)
					dist = 0.0f;

				switch (t->prop_mode) {
					case PROP_SHARP:
						td->factor = dist * dist;
						break;
					case PROP_SMOOTH:
						td->factor = 3.0f * dist * dist - 2.0f * dist * dist * dist;
						break;
					case PROP_ROOT:
						td->factor = sqrtf(dist);
						break;
					case PROP_LIN:
						td->factor = dist;
						break;
					case PROP_CONST:
						td->factor = 1.0f;
						break;
					case PROP_SPHERE:
						td->factor = sqrtf(2 * dist - dist * dist);
						break;
					case PROP_RANDOM:
						td->factor = BLI_frand() * dist;
						break;
					case PROP_INVSQUARE:
						td->factor = dist * (2.0f - dist);
						break;
					default:
						td->factor = 1;
						break;
				}
			}
		}
		switch (t->prop_mode) {
			case PROP_SHARP:
				pet_id = N_("(Sharp)");
				break;
			case PROP_SMOOTH:
				pet_id = N_("(Smooth)");
				break;
			case PROP_ROOT:
				pet_id = N_("(Root)");
				break;
			case PROP_LIN:
				pet_id = N_("(Linear)");
				break;
			case PROP_CONST:
				pet_id = N_("(Constant)");
				break;
			case PROP_SPHERE:
				pet_id = N_("(Sphere)");
				break;
			case PROP_RANDOM:
				pet_id = N_("(Random)");
				break;
			case PROP_INVSQUARE:
				pet_id = N_("(InvSquare)");
				break;
			default:
				break;
		}

		if (pet_id) {
			BLI_strncpy(t->proptext, IFACE_(pet_id), sizeof(t->proptext));
		}
	}
	else {
		for (i = 0; i < t->total; i++, td++) {
			td->factor = 1.0;
		}
	}
}

/**
 * Rotate an element, low level code, ignore protected channels.
 * (use for objects)
 * Similar to #ElementRotation.
 */
void transform_data_ext_rotate(TransData *td, float mat[3][3], bool use_drot)
{
	float totmat[3][3];
	float smat[3][3];
	float fmat[3][3];
	float obmat[3][3];

	float dmat[3][3];  /* delta rotation */
	float dmat_inv[3][3];

	mul_m3_m3m3(totmat, mat, td->mtx);
	mul_m3_m3m3(smat, td->smtx, mat);

	/* logic from BKE_object_rot_to_mat3 */
	if (use_drot) {
		if (td->ext->rotOrder > 0) {
			eulO_to_mat3(dmat, td->ext->drot, td->ext->rotOrder);
		}
		else if (td->ext->rotOrder == ROT_MODE_AXISANGLE) {
#if 0
			axis_angle_to_mat3(dmat, td->ext->drotAxis, td->ext->drotAngle);
#else
			unit_m3(dmat);
#endif
		}
		else {
			float tquat[4];
			normalize_qt_qt(tquat, td->ext->dquat);
			quat_to_mat3(dmat, tquat);
		}

		invert_m3_m3(dmat_inv, dmat);
	}


	if (td->ext->rotOrder == ROT_MODE_QUAT) {
		float quat[4];

		/* calculate the total rotatation */
		quat_to_mat3(obmat, td->ext->iquat);
		if (use_drot) {
			mul_m3_m3m3(obmat, dmat, obmat);
		}

		/* mat = transform, obmat = object rotation */
		mul_m3_m3m3(fmat, smat, obmat);

		if (use_drot) {
			mul_m3_m3m3(fmat, dmat_inv, fmat);
		}

		mat3_to_quat(quat, fmat);

		/* apply */
		copy_qt_qt(td->ext->quat, quat);
	}
	else if (td->ext->rotOrder == ROT_MODE_AXISANGLE) {
		float axis[3], angle;

		/* calculate the total rotatation */
		axis_angle_to_mat3(obmat, td->ext->irotAxis, td->ext->irotAngle);
		if (use_drot) {
			mul_m3_m3m3(obmat, dmat, obmat);
		}

		/* mat = transform, obmat = object rotation */
		mul_m3_m3m3(fmat, smat, obmat);

		if (use_drot) {
			mul_m3_m3m3(fmat, dmat_inv, fmat);
		}

		mat3_to_axis_angle(axis, &angle, fmat);

		/* apply */
		copy_v3_v3(td->ext->rotAxis, axis);
		*td->ext->rotAngle = angle;
	}
	else {
		float eul[3];

		/* calculate the total rotatation */
		eulO_to_mat3(obmat, td->ext->irot, td->ext->rotOrder);
		if (use_drot) {
			mul_m3_m3m3(obmat, dmat, obmat);
		}

		/* mat = transform, obmat = object rotation */
		mul_m3_m3m3(fmat, smat, obmat);

		if (use_drot) {
			mul_m3_m3m3(fmat, dmat_inv, fmat);
		}

		mat3_to_compatible_eulO(eul, td->ext->rot, td->ext->rotOrder, fmat);

		/* apply */
		copy_v3_v3(td->ext->rot, eul);
	}
}
