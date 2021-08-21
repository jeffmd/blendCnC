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

/** \file blender/editors/space_view3d/view3d_snap.c
 *  \ingroup spview3d
 */


#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "BKE_context.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_report.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "ED_object.h"
#include "ED_transverts.h"
#include "ED_screen.h"

#include "view3d_intern.h"

static bool snap_curs_to_sel_ex(bContext *C, float cursor[3]);
static bool snap_calc_active_center(bContext *C, const bool select_only, float r_center[3]);


/* *********************** operators ******************** */

static int snap_sel_to_grid_exec(bContext *C, wmOperator *UNUSED(op))
{
	Object *obedit = CTX_data_edit_object(C);
	RegionView3D *rv3d = CTX_wm_region_data(C);
	TransVertStore tvs = {NULL};
	TransVert *tv;
	float gridf, imat[3][3], bmat[3][3], vec[3];
	int a;

	gridf = rv3d->gridview;

	if (obedit) {
		if (ED_transverts_check_obedit(obedit))
			ED_transverts_create_from_obedit(&tvs, obedit, 0);
		if (tvs.transverts_tot == 0)
			return OPERATOR_CANCELLED;

		copy_m3_m4(bmat, obedit->obmat);
		invert_m3_m3(imat, bmat);

		tv = tvs.transverts;
		for (a = 0; a < tvs.transverts_tot; a++, tv++) {
			copy_v3_v3(vec, tv->loc);
			mul_m3_v3(bmat, vec);
			add_v3_v3(vec, obedit->obmat[3]);
			vec[0] = gridf * floorf(0.5f + vec[0] / gridf);
			vec[1] = gridf * floorf(0.5f + vec[1] / gridf);
			vec[2] = gridf * floorf(0.5f + vec[2] / gridf);
			sub_v3_v3(vec, obedit->obmat[3]);

			mul_m3_v3(imat, vec);
			copy_v3_v3(tv->loc, vec);
		}

		ED_transverts_update_obedit(&tvs, obedit);
		ED_transverts_free(&tvs);
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);

	return OPERATOR_FINISHED;
}

void VIEW3D_OT_snap_selected_to_grid(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Selection to Grid";
	ot->description = "Snap selected item(s) to nearest grid division";
	ot->idname = "VIEW3D_OT_snap_selected_to_grid";

	/* api callbacks */
	ot->exec = snap_sel_to_grid_exec;
	ot->poll = ED_operator_region_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* *************************************************** */

static int snap_selected_to_location(bContext *C, const float snap_target_global[3], const bool use_offset)
{
	Object *obedit = CTX_data_edit_object(C);
	View3D *v3d = CTX_wm_view3d(C);
	TransVertStore tvs = {NULL};
	TransVert *tv;
	float imat[3][3], bmat[3][3];
	float center_global[3];
	float offset_global[3];
	int a;

	if (use_offset) {
		if ((v3d && v3d->around == V3D_AROUND_ACTIVE) &&
		    snap_calc_active_center(C, true, center_global))
		{
			/* pass */
		}
		else {
			snap_curs_to_sel_ex(C, center_global);
		}
		sub_v3_v3v3(offset_global, snap_target_global, center_global);
	}

	if (obedit) {
		float snap_target_local[3];

		if (ED_transverts_check_obedit(obedit))
			ED_transverts_create_from_obedit(&tvs, obedit, 0);
		if (tvs.transverts_tot == 0)
			return OPERATOR_CANCELLED;

		copy_m3_m4(bmat, obedit->obmat);
		invert_m3_m3(imat, bmat);

		/* get the cursor in object space */
		sub_v3_v3v3(snap_target_local, snap_target_global, obedit->obmat[3]);
		mul_m3_v3(imat, snap_target_local);

		if (use_offset) {
			float offset_local[3];

			mul_v3_m3v3(offset_local, imat, offset_global);

			tv = tvs.transverts;
			for (a = 0; a < tvs.transverts_tot; a++, tv++) {
				add_v3_v3(tv->loc, offset_local);
			}
		}
		else {
			tv = tvs.transverts;
			for (a = 0; a < tvs.transverts_tot; a++, tv++) {
				copy_v3_v3(tv->loc, snap_target_local);
			}
		}

		ED_transverts_update_obedit(&tvs, obedit);
		ED_transverts_free(&tvs);
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);

	return OPERATOR_FINISHED;
}

static int snap_selected_to_cursor_exec(bContext *C, wmOperator *op)
{
	const bool use_offset = RNA_boolean_get(op->ptr, "use_offset");

	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);

	const float *snap_target_global = ED_view3d_cursor3d_get(scene, v3d);

	return snap_selected_to_location(C, snap_target_global, use_offset);
}

void VIEW3D_OT_snap_selected_to_cursor(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Selection to Cursor";
	ot->description = "Snap selected item(s) to cursor";
	ot->idname = "VIEW3D_OT_snap_selected_to_cursor";

	/* api callbacks */
	ot->exec = snap_selected_to_cursor_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* rna */
	RNA_def_boolean(ot->srna, "use_offset", 1, "Offset", "");
}

static int snap_selected_to_active_exec(bContext *C, wmOperator *op)
{
	float snap_target_global[3];

	if (snap_calc_active_center(C, false, snap_target_global) == false) {
		BKE_report(op->reports, RPT_ERROR, "No active element found!");
		return OPERATOR_CANCELLED;
	}

	return snap_selected_to_location(C, snap_target_global, false);
}

void VIEW3D_OT_snap_selected_to_active(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Selection to Active";
	ot->description = "Snap selected item(s) to the active item";
	ot->idname = "VIEW3D_OT_snap_selected_to_active";

	/* api callbacks */
	ot->exec = snap_selected_to_active_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}


/* *************************************************** */

static int snap_curs_to_grid_exec(bContext *C, wmOperator *UNUSED(op))
{
	Scene *scene = CTX_data_scene(C);
	RegionView3D *rv3d = CTX_wm_region_data(C);
	View3D *v3d = CTX_wm_view3d(C);
	float gridf, *curs;

	gridf = rv3d->gridview;
	curs = ED_view3d_cursor3d_get(scene, v3d);

	curs[0] = gridf * floorf(0.5f + curs[0] / gridf);
	curs[1] = gridf * floorf(0.5f + curs[1] / gridf);
	curs[2] = gridf * floorf(0.5f + curs[2] / gridf);

	WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);  /* hrm */

	return OPERATOR_FINISHED;
}

void VIEW3D_OT_snap_cursor_to_grid(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Cursor to Grid";
	ot->description = "Snap cursor to nearest grid division";
	ot->idname = "VIEW3D_OT_snap_cursor_to_grid";

	/* api callbacks */
	ot->exec = snap_curs_to_grid_exec;
	ot->poll = ED_operator_region_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* **************************************************** */

static bool snap_curs_to_sel_ex(bContext *C, float cursor[3])
{
	Object *obedit = CTX_data_edit_object(C);
	View3D *v3d = CTX_wm_view3d(C);
	TransVertStore tvs = {NULL};
	TransVert *tv;
	float bmat[3][3], vec[3], min[3], max[3], centroid[3];
	int count, a;

	count = 0;
	INIT_MINMAX(min, max);
	zero_v3(centroid);

	if (obedit) {

		if (ED_transverts_check_obedit(obedit))
			ED_transverts_create_from_obedit(&tvs, obedit, TM_ALL_JOINTS | TM_SKIP_HANDLES);

		if (tvs.transverts_tot == 0) {
			return false;
		}

		copy_m3_m4(bmat, obedit->obmat);

		tv = tvs.transverts;
		for (a = 0; a < tvs.transverts_tot; a++, tv++) {
			copy_v3_v3(vec, tv->loc);
			mul_m3_v3(bmat, vec);
			add_v3_v3(vec, obedit->obmat[3]);
			add_v3_v3(centroid, vec);
			minmax_v3v3_v3(min, max, vec);
		}

		if (v3d->around == V3D_AROUND_CENTER_MEDIAN) {
			mul_v3_fl(centroid, 1.0f / (float)tvs.transverts_tot);
			copy_v3_v3(cursor, centroid);
		}
		else {
			mid_v3_v3v3(cursor, min, max);
		}

		ED_transverts_free(&tvs);
	}
	else {
		if (count == 0) {
			return false;
		}

		if (v3d->around == V3D_AROUND_CENTER_MEDIAN) {
			mul_v3_fl(centroid, 1.0f / (float)count);
			copy_v3_v3(cursor, centroid);
		}
		else {
			mid_v3_v3v3(cursor, min, max);
		}
	}
	return true;
}

static int snap_curs_to_sel_exec(bContext *C, wmOperator *UNUSED(op))
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	float *curs;

	curs = ED_view3d_cursor3d_get(scene, v3d);

	if (snap_curs_to_sel_ex(C, curs)) {
		WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);

		return OPERATOR_FINISHED;
	}
	else {
		return OPERATOR_CANCELLED;
	}
}

void VIEW3D_OT_snap_cursor_to_selected(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Cursor to Selected";
	ot->description = "Snap cursor to center of selected item(s)";
	ot->idname = "VIEW3D_OT_snap_cursor_to_selected";

	/* api callbacks */
	ot->exec = snap_curs_to_sel_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ********************************************** */

/* this could be exported to be a generic function
 * see: calculateCenterActive */

static bool snap_calc_active_center(bContext *C, const bool select_only, float r_center[3])
{
	Object *obedit = CTX_data_edit_object(C);

	if (obedit) {
		if (ED_object_editmode_calc_active_center(obedit, select_only, r_center)) {
			mul_m4_v3(obedit->obmat, r_center);
			return true;
		}
	}
	else {
		Object *ob = CTX_data_active_object(C);

		if (ob) {
			{
				if (!select_only || (ob->flag & SELECT)) {
					copy_v3_v3(r_center, ob->obmat[3]);
					return true;
				}
			}
		}
	}

	return false;
}

static int snap_curs_to_active_exec(bContext *C, wmOperator *UNUSED(op))
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	float *curs;

	curs = ED_view3d_cursor3d_get(scene, v3d);

	if (snap_calc_active_center(C, false, curs)) {
		WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);
		return OPERATOR_FINISHED;
	}
	else {
		return OPERATOR_CANCELLED;
	}
}

void VIEW3D_OT_snap_cursor_to_active(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Cursor to Active";
	ot->description = "Snap cursor to active item";
	ot->idname = "VIEW3D_OT_snap_cursor_to_active";

	/* api callbacks */
	ot->exec = snap_curs_to_active_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* **************************************************** */
/*New Code - Snap Cursor to Center -*/
static int snap_curs_to_center_exec(bContext *C, wmOperator *UNUSED(op))
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	float *curs;
	curs = ED_view3d_cursor3d_get(scene, v3d);

	zero_v3(curs);

	WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);

	return OPERATOR_FINISHED;
}

void VIEW3D_OT_snap_cursor_to_center(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Snap Cursor to Center";
	ot->description = "Snap cursor to the Center";
	ot->idname = "VIEW3D_OT_snap_cursor_to_center";

	/* api callbacks */
	ot->exec = snap_curs_to_center_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* **************************************************** */


bool ED_view3d_minmax_verts(Object *obedit, float min[3], float max[3])
{
	TransVertStore tvs = {NULL};
	TransVert *tv;
	float centroid[3], vec[3], bmat[3][3];
	int a;

	if (ED_transverts_check_obedit(obedit))
		ED_transverts_create_from_obedit(&tvs, obedit, TM_ALL_JOINTS);

	if (tvs.transverts_tot == 0)
		return false;

	copy_m3_m4(bmat, obedit->obmat);

	tv = tvs.transverts;
	for (a = 0; a < tvs.transverts_tot; a++, tv++) {
		copy_v3_v3(vec, (tv->flag & TX_VERT_USE_MAPLOC) ? tv->maploc : tv->loc);
		mul_m3_v3(bmat, vec);
		add_v3_v3(vec, obedit->obmat[3]);
		add_v3_v3(centroid, vec);
		minmax_v3v3_v3(min, max, vec);
	}

	ED_transverts_free(&tvs);

	return true;
}
