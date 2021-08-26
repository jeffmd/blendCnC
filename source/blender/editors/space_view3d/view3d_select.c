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

/** \file blender/editors/space_view3d/view3d_select.c
 *  \ingroup spview3d
 */


#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <assert.h>

#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_lasso_2d.h"
#include "BLI_rect.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

/* vertex box select */
#include "IMB_imbuf_types.h"
#include "IMB_imbuf.h"
#include "BKE_global.h"

#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_editmesh.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_curve.h"
#include "ED_lattice.h"
#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_screen.h"

#include "UI_interface.h"

#include "GPU_draw.h"

#include "view3d_intern.h"  /* own include */

// #include "PIL_time_utildefines.h"

float ED_view3d_select_dist_px(void)
{
	return 75.0f * U.pixelsize;
}

/* TODO: should return whether there is valid context to continue */
void ED_view3d_viewcontext_init(bContext *C, ViewContext *vc)
{
	memset(vc, 0, sizeof(ViewContext));
	vc->ar = CTX_wm_region(C);
	vc->bmain = CTX_data_main(C);
	vc->scene = CTX_data_scene(C);
	vc->v3d = CTX_wm_view3d(C);
	vc->win = CTX_wm_window(C);
	vc->rv3d = CTX_wm_region_view3d(C);
	vc->obact = CTX_data_active_object(C);
	vc->obedit = CTX_data_edit_object(C);
}

/*
 * ob == NULL if you want global matrices
 * */
void view3d_get_transformation(const ARegion *ar, RegionView3D *rv3d, Object *ob, bglMats *mats)
{
	float cpy[4][4];
	int i, j;

	if (ob) {
		mul_m4_m4m4(cpy, rv3d->viewmat, ob->obmat);
	}
	else {
		copy_m4_m4(cpy, rv3d->viewmat);
	}

	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 4; ++j) {
			mats->projection[i * 4 + j] = rv3d->winmat[i][j];
			mats->modelview[i * 4 + j] = cpy[i][j];
		}
	}

	mats->viewport[0] = ar->winrct.xmin;
	mats->viewport[1] = ar->winrct.ymin;
	mats->viewport[2] = ar->winx;
	mats->viewport[3] = ar->winy;
}

/* ********************** view3d_select: selection manipulations ********************* */

/* local prototypes */

static void edbm_backbuf_check_and_select_verts(BMEditMesh *em, const bool select)
{
	BMVert *eve;
	BMIter iter;
	unsigned int index = bm_wireoffs;

	BM_ITER_MESH (eve, &iter, em->bm, BM_VERTS_OF_MESH) {
		if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
			if (EDBM_backbuf_check(index)) {
				BM_vert_select_set(em->bm, eve, select);
			}
		}
		index++;
	}
}

static void edbm_backbuf_check_and_select_edges(BMEditMesh *em, const bool select)
{
	BMEdge *eed;
	BMIter iter;
	unsigned int index = bm_solidoffs;

	BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
		if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
			if (EDBM_backbuf_check(index)) {
				BM_edge_select_set(em->bm, eed, select);
			}
		}
		index++;
	}
}

static void edbm_backbuf_check_and_select_faces(BMEditMesh *em, const bool select)
{
	BMFace *efa;
	BMIter iter;
	unsigned int index = 1;

	BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
		if (!BM_elem_flag_test(efa, BM_ELEM_HIDDEN)) {
			if (EDBM_backbuf_check(index)) {
				BM_face_select_set(em->bm, efa, select);
			}
		}
		index++;
	}
}

/* *********************** GESTURE AND LASSO ******************* */

typedef struct LassoSelectUserData {
	ViewContext *vc;
	const rcti *rect;
	const rctf *rect_fl;
	rctf       _rect_fl;
	const int (*mcords)[2];
	int moves;
	bool select;

	/* runtime */
	int pass;
	bool is_done;
	bool is_changed;
} LassoSelectUserData;

static void view3d_userdata_lassoselect_init(
        LassoSelectUserData *r_data,
        ViewContext *vc, const rcti *rect, const int (*mcords)[2],
        const int moves, const bool select)
{
	r_data->vc = vc;

	r_data->rect = rect;
	r_data->rect_fl = &r_data->_rect_fl;
	BLI_rctf_rcti_copy(&r_data->_rect_fl, rect);

	r_data->mcords = mcords;
	r_data->moves = moves;
	r_data->select = select;

	/* runtime */
	r_data->pass = 0;
	r_data->is_done = false;
	r_data->is_changed = false;
}

static bool view3d_selectable_data(bContext *C)
{
	Object *ob = CTX_data_active_object(C);

	if (!ED_operator_region_view3d_active(C))
		return 0;

	if (ob) {
		if (ob->mode & OB_MODE_EDIT) {
			if (ob->type == OB_FONT) {
				return 0;
			}
		}
	}

	return 1;
}


/* helper also for borderselect */
static bool edge_fully_inside_rect(const rctf *rect, const float v1[2], const float v2[2])
{
	return BLI_rctf_isect_pt_v(rect, v1) && BLI_rctf_isect_pt_v(rect, v2);
}

static bool edge_inside_rect(const rctf *rect, const float v1[2], const float v2[2])
{
	int d1, d2, d3, d4;

	/* check points in rect */
	if (edge_fully_inside_rect(rect, v1, v2)) return 1;

	/* check points completely out rect */
	if (v1[0] < rect->xmin && v2[0] < rect->xmin) return 0;
	if (v1[0] > rect->xmax && v2[0] > rect->xmax) return 0;
	if (v1[1] < rect->ymin && v2[1] < rect->ymin) return 0;
	if (v1[1] > rect->ymax && v2[1] > rect->ymax) return 0;

	/* simple check lines intersecting. */
	d1 = (v1[1] - v2[1]) * (v1[0] - rect->xmin) + (v2[0] - v1[0]) * (v1[1] - rect->ymin);
	d2 = (v1[1] - v2[1]) * (v1[0] - rect->xmin) + (v2[0] - v1[0]) * (v1[1] - rect->ymax);
	d3 = (v1[1] - v2[1]) * (v1[0] - rect->xmax) + (v2[0] - v1[0]) * (v1[1] - rect->ymax);
	d4 = (v1[1] - v2[1]) * (v1[0] - rect->xmax) + (v2[0] - v1[0]) * (v1[1] - rect->ymin);

	if (d1 < 0 && d2 < 0 && d3 < 0 && d4 < 0) return 0;
	if (d1 > 0 && d2 > 0 && d3 > 0 && d4 > 0) return 0;

	return 1;
}

static void object_deselect_all_visible(Scene *scene, View3D *v3d)
{
	Base *base;

	for (base = scene->base.first; base; base = base->next) {
		if (BASE_SELECTABLE(v3d, base)) {
			ED_base_object_select(base, BA_DESELECT);
		}
	}
}

static void do_lasso_select_objects(
        ViewContext *vc, const int mcords[][2], const short moves,
        const bool extend, const bool select)
{
	Base *base;

	if (extend == false && select)
		object_deselect_all_visible(vc->scene, vc->v3d);

	for (base = vc->scene->base.first; base; base = base->next) {
		if (BASE_SELECTABLE(vc->v3d, base)) { /* use this to avoid un-needed lasso lookups */
			if (ED_view3d_project_base(vc->ar, base) == V3D_PROJ_RET_OK) {
				if (BLI_lasso_is_point_inside(mcords, moves, base->sx, base->sy, IS_CLIPPED)) {

					ED_base_object_select(base, select ? BA_SELECT : BA_DESELECT);
					base->object->flag = base->flag;
				}
			}
		}
	}
}

static void do_lasso_select_mesh__doSelectVert(void *userData, BMVert *eve, const float screen_co[2], int UNUSED(index))
{
	LassoSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
	    BLI_lasso_is_point_inside(data->mcords, data->moves, screen_co[0], screen_co[1], IS_CLIPPED))
	{
		BM_vert_select_set(data->vc->em->bm, eve, data->select);
	}
}
static void do_lasso_select_mesh__doSelectEdge(
        void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index)
{
	LassoSelectUserData *data = userData;

	if (EDBM_backbuf_check(bm_solidoffs + index)) {
		const int x0 = screen_co_a[0];
		const int y0 = screen_co_a[1];
		const int x1 = screen_co_b[0];
		const int y1 = screen_co_b[1];

		if (data->pass == 0) {
			if (edge_fully_inside_rect(data->rect_fl, screen_co_a, screen_co_b)  &&
			    BLI_lasso_is_point_inside(data->mcords, data->moves, x0, y0, IS_CLIPPED) &&
			    BLI_lasso_is_point_inside(data->mcords, data->moves, x1, y1, IS_CLIPPED))
			{
				BM_edge_select_set(data->vc->em->bm, eed, data->select);
				data->is_done = true;
			}
		}
		else {
			if (BLI_lasso_is_edge_inside(data->mcords, data->moves, x0, y0, x1, y1, IS_CLIPPED)) {
				BM_edge_select_set(data->vc->em->bm, eed, data->select);
			}
		}
	}
}
static void do_lasso_select_mesh__doSelectFace(void *userData, BMFace *efa, const float screen_co[2], int UNUSED(index))
{
	LassoSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
	    BLI_lasso_is_point_inside(data->mcords, data->moves, screen_co[0], screen_co[1], IS_CLIPPED))
	{
		BM_face_select_set(data->vc->em->bm, efa, data->select);
	}
}

static void do_lasso_select_mesh(ViewContext *vc, const int mcords[][2], short moves, bool extend, bool select)
{
	LassoSelectUserData data;
	ToolSettings *ts = vc->scene->toolsettings;
	rcti rect;
	int bbsel;

	/* set editmesh */
	vc->em = BKE_editmesh_from_object(vc->obedit);

	BLI_lasso_boundbox(&rect, mcords, moves);

	view3d_userdata_lassoselect_init(&data, vc, &rect, mcords, moves, select);

	if (extend == false && select)
		EDBM_flag_disable_all(vc->em, BM_ELEM_SELECT);

	/* for non zbuf projections, don't change the GL state */
	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

	glLoadMatrixf(vc->rv3d->viewmat);
	bbsel = EDBM_backbuf_border_mask_init(vc, mcords, moves, rect.xmin, rect.ymin, rect.xmax, rect.ymax);

	if (ts->selectmode & SCE_SELECT_VERTEX) {
		if (bbsel) {
			edbm_backbuf_check_and_select_verts(vc->em, select);
		}
		else {
			mesh_foreachScreenVert(vc, do_lasso_select_mesh__doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}
	if (ts->selectmode & SCE_SELECT_EDGE) {
		/* Does both bbsel and non-bbsel versions (need screen cos for both) */
		data.pass = 0;
		mesh_foreachScreenEdge(vc, do_lasso_select_mesh__doSelectEdge, &data, V3D_PROJ_TEST_CLIP_NEAR);

		if (data.is_done == false) {
			data.pass = 1;
			mesh_foreachScreenEdge(vc, do_lasso_select_mesh__doSelectEdge, &data, V3D_PROJ_TEST_CLIP_NEAR);
		}
	}

	if (ts->selectmode & SCE_SELECT_FACE) {
		if (bbsel) {
			edbm_backbuf_check_and_select_faces(vc->em, select);
		}
		else {
			mesh_foreachScreenFace(vc, do_lasso_select_mesh__doSelectFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}

	EDBM_backbuf_free();
	EDBM_selectmode_flush(vc->em);
}

static void do_lasso_select_curve__doSelect(
        void *userData, Nurb *UNUSED(nu), BPoint *bp, BezTriple *bezt, int beztindex, const float screen_co[2])
{
	LassoSelectUserData *data = userData;
	Object *obedit = data->vc->obedit;
	Curve *cu = (Curve *)obedit->data;

	if (BLI_lasso_is_point_inside(data->mcords, data->moves, screen_co[0], screen_co[1], IS_CLIPPED)) {
		if (bp) {
			bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
		}
		else {
			if (cu->drawflag & CU_HIDE_HANDLES) {
				/* can only be (beztindex == 0) here since handles are hidden */
				bezt->f1 = bezt->f2 = bezt->f3 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
			}
			else {
				if (beztindex == 0) {
					bezt->f1 = data->select ? (bezt->f1 | SELECT) : (bezt->f1 & ~SELECT);
				}
				else if (beztindex == 1) {
					bezt->f2 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
				}
				else {
					bezt->f3 = data->select ? (bezt->f3 | SELECT) : (bezt->f3 & ~SELECT);
				}
			}
		}
	}
}

static void do_lasso_select_curve(ViewContext *vc, const int mcords[][2], short moves, bool extend, bool select)
{
	LassoSelectUserData data;
	rcti rect;

	BLI_lasso_boundbox(&rect, mcords, moves);

	view3d_userdata_lassoselect_init(&data, vc, &rect, mcords, moves, select);

	if (extend == false && select) {
		Curve *curve = (Curve *) vc->obedit->data;
		ED_curve_deselect_all(curve->editnurb);
	}

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	nurbs_foreachScreenVert(vc, do_lasso_select_curve__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
	BKE_curve_nurb_vert_active_validate(vc->obedit->data);
}

static void do_lasso_select_lattice__doSelect(void *userData, BPoint *bp, const float screen_co[2])
{
	LassoSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
	    BLI_lasso_is_point_inside(data->mcords, data->moves, screen_co[0], screen_co[1], IS_CLIPPED))
	{
		bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
	}
}
static void do_lasso_select_lattice(ViewContext *vc, const int mcords[][2], short moves, bool extend, bool select)
{
	LassoSelectUserData data;
	rcti rect;

	BLI_lasso_boundbox(&rect, mcords, moves);

	view3d_userdata_lassoselect_init(&data, vc, &rect, mcords, moves, select);

	if (extend == false && select)
		ED_lattice_flags_set(vc->obedit, 0);

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	lattice_foreachScreenVert(vc, do_lasso_select_lattice__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
}

static void view3d_lasso_select(
        bContext *C, ViewContext *vc,
        const int mcords[][2], short moves,
        bool extend, bool select)
{
	if (vc->obedit == NULL) { /* Object Mode */
		do_lasso_select_objects(vc, mcords, moves, extend, select);
		WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, vc->scene);
	}
	else { /* Edit Mode */
		switch (vc->obedit->type) {
			case OB_MESH:
				do_lasso_select_mesh(vc, mcords, moves, extend, select);
				break;
			case OB_CURVE:
			case OB_SURF:
				do_lasso_select_curve(vc, mcords, moves, extend, select);
				break;
			case OB_LATTICE:
				do_lasso_select_lattice(vc, mcords, moves, extend, select);
				break;
			default:
				assert(!"lasso select on incorrect object type");
				break;
		}

		WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc->obedit->data);
	}
}


/* lasso operator gives properties, but since old code works
 * with short array we convert */
static int view3d_lasso_select_exec(bContext *C, wmOperator *op)
{
	ViewContext vc;
	int mcords_tot;
	const int (*mcords)[2] = WM_gesture_lasso_path_to_array(C, op, &mcords_tot);

	if (mcords) {
		bool extend, select;
		view3d_operator_needs_opengl(C);

		/* setup view context for argument to callbacks */
		ED_view3d_viewcontext_init(C, &vc);

		extend = RNA_boolean_get(op->ptr, "extend");
		select = !RNA_boolean_get(op->ptr, "deselect");
		view3d_lasso_select(C, &vc, mcords, mcords_tot, extend, select);

		MEM_freeN((void *)mcords);

		return OPERATOR_FINISHED;
	}
	return OPERATOR_PASS_THROUGH;
}

void VIEW3D_OT_select_lasso(wmOperatorType *ot)
{
	ot->name = "Lasso Select";
	ot->description = "Select items using lasso selection";
	ot->idname = "VIEW3D_OT_select_lasso";

	ot->invoke = WM_gesture_lasso_invoke;
	ot->modal = WM_gesture_lasso_modal;
	ot->exec = view3d_lasso_select_exec;
	ot->poll = view3d_selectable_data;
	ot->cancel = WM_gesture_lasso_cancel;

	/* flags */
	ot->flag = OPTYPE_UNDO;

	/* properties */
	WM_operator_properties_gesture_lasso_select(ot);
}

/* ************************** mouse select ************************* */

/* The max number of menu items in an object select menu */
typedef struct SelMenuItemF {
	char idname[MAX_ID_NAME - 2];
	int icon;
} SelMenuItemF;

#define SEL_MENU_SIZE   22
static SelMenuItemF object_mouse_select_menu_data[SEL_MENU_SIZE];

/* special (crappy) operator only for menu select */
static const EnumPropertyItem *object_select_menu_enum_itemf(
        bContext *C, PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop), bool *r_free)
{
	EnumPropertyItem *item = NULL, item_tmp = {0};
	int totitem = 0;
	int i = 0;

	/* don't need context but avoid docgen using this */
	if (C == NULL || object_mouse_select_menu_data[i].idname[0] == '\0') {
		return DummyRNA_NULL_items;
	}

	for (; i < SEL_MENU_SIZE && object_mouse_select_menu_data[i].idname[0] != '\0'; i++) {
		item_tmp.name = object_mouse_select_menu_data[i].idname;
		item_tmp.identifier = object_mouse_select_menu_data[i].idname;
		item_tmp.value = i;
		item_tmp.icon = object_mouse_select_menu_data[i].icon;
		RNA_enum_item_add(&item, &totitem, &item_tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static int object_select_menu_exec(bContext *C, wmOperator *op)
{
	const int name_index = RNA_enum_get(op->ptr, "name");
	const bool toggle = RNA_boolean_get(op->ptr, "toggle");
	bool changed = false;
	const char *name = object_mouse_select_menu_data[name_index].idname;

	if (!toggle) {
		CTX_DATA_BEGIN (C, Base *, base, selectable_bases)
		{
			if (base->flag & SELECT) {
				ED_base_object_select(base, BA_DESELECT);
				changed = true;
			}
		}
		CTX_DATA_END;
	}

	CTX_DATA_BEGIN (C, Base *, base, selectable_bases)
	{
		/* this is a bit dodjy, there should only be ONE object with this name,
		 * but library objects can mess this up */
		if (STREQ(name, base->object->id.name + 2)) {
			ED_base_object_activate(C, base);
			ED_base_object_select(base, BA_SELECT);
			changed = true;
		}
	}
	CTX_DATA_END;

	/* weak but ensures we activate menu again before using the enum */
	memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

	/* undo? */
	if (changed) {
		WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, CTX_data_scene(C));
		return OPERATOR_FINISHED;
	}
	else {
		return OPERATOR_CANCELLED;
	}
}

void VIEW3D_OT_select_menu(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Select Menu";
	ot->description = "Menu object selection";
	ot->idname = "VIEW3D_OT_select_menu";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = object_select_menu_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* keyingset to use (dynamic enum) */
	prop = RNA_def_enum(ot->srna, "name", DummyRNA_NULL_items, 0, "Object Name", "");
	RNA_def_enum_funcs(prop, object_select_menu_enum_itemf);
	RNA_def_property_flag(prop, PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);
	ot->prop = prop;

	RNA_def_boolean(ot->srna, "toggle", 0, "Toggle", "Toggle selection instead of deselecting everything first");
}

static void deselectall_except(Scene *scene, Base *b)   /* deselect all except b */
{
	Base *base;

	for (base = FIRSTBASE; base; base = base->next) {
		if (base->flag & SELECT) {
			if (b != base) {
				ED_base_object_select(base, BA_DESELECT);
			}
		}
	}
}

static Base *object_mouse_select_menu(
        bContext *C, ViewContext *vc, unsigned int *buffer, int hits,
        const int mval[2], bool toggle)
{
	short baseCount = 0;
	bool ok;
	LinkNode *linklist = NULL;

	CTX_DATA_BEGIN (C, Base *, base, selectable_bases)
	{
		ok = false;

		/* two selection methods, the CTRL select uses max dist of 15 */
		if (buffer) {
			for (int a = 0; a < hits; a++) {
				/* index was converted */
				if (base->selcol == (buffer[(4 * a) + 3] & ~0xFFFF0000)) {
					ok = true;
					break;
				}
			}
		}
		else {
			const int dist = 15 * U.pixelsize;
			if (ED_view3d_project_base(vc->ar, base) == V3D_PROJ_RET_OK) {
				const int delta_px[2] = {base->sx - mval[0], base->sy - mval[1]};
				if (len_manhattan_v2_int(delta_px) < dist) {
					ok = true;
				}
			}
		}

		if (ok) {
			baseCount++;
			BLI_linklist_prepend(&linklist, base);

			if (baseCount == SEL_MENU_SIZE)
				break;
		}
	}
	CTX_DATA_END;

	if (baseCount == 0) {
		return NULL;
	}
	if (baseCount == 1) {
		Base *base = (Base *)linklist->link;
		BLI_linklist_free(linklist, NULL);
		return base;
	}
	else {
		/* UI, full in static array values that we later use in an enum function */
		LinkNode *node;
		int i;

		memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

		for (node = linklist, i = 0; node; node = node->next, i++) {
			Base *base = node->link;
			Object *ob = base->object;
			const char *name = ob->id.name + 2;

			BLI_strncpy(object_mouse_select_menu_data[i].idname, name, MAX_ID_NAME - 2);
			object_mouse_select_menu_data[i].icon = UI_icon_from_id(&ob->id);
		}

		{
			wmOperatorType *ot = WM_operatortype_find("VIEW3D_OT_select_menu", false);
			PointerRNA ptr;

			WM_operator_properties_create_ptr(&ptr, ot);
			RNA_boolean_set(&ptr, "toggle", toggle);
			WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &ptr);
			WM_operator_properties_free(&ptr);
		}

		BLI_linklist_free(linklist, NULL);
		return NULL;
	}
}

static int selectbuffer_ret_hits_9(unsigned int *buffer, const int hits15, const int hits9)
{
	const int offs = 4 * hits15;
	memcpy(buffer, buffer + offs, 4 * hits9 * sizeof(unsigned int));
	return hits9;
}

static int selectbuffer_ret_hits_5(unsigned int *buffer, const int hits15, const int hits9, const int hits5)
{
	const int offs = 4 * hits15 + 4 * hits9;
	memcpy(buffer, buffer + offs, 4 * hits5  * sizeof(unsigned int));
	return hits5;
}

static int mixed_object_selectbuffer(
        ViewContext *vc, unsigned int *buffer, const int mval[2],
        bool use_cycle, bool enumerate,
        bool *r_do_nearest)
{
	rcti rect;
	int hits15, hits9 = 0, hits5 = 0;
	static int last_mval[2] = {-100, -100};
	bool do_nearest = false;
	View3D *v3d = vc->v3d;

	/* define if we use solid nearest select or not */
	if (use_cycle) {
		if (v3d->drawtype > OB_WIRE) {
			do_nearest = true;
			if (len_manhattan_v2v2_int(mval, last_mval) < 3) {
				do_nearest = false;
			}
		}
		copy_v2_v2_int(last_mval, mval);
	}
	else {
		if (v3d->drawtype > OB_WIRE) {
			do_nearest = true;
		}
	}

	if (r_do_nearest) {
		*r_do_nearest = do_nearest;
	}

	do_nearest = do_nearest && !enumerate;

	const int select_mode = (do_nearest ? VIEW3D_SELECT_PICK_NEAREST : VIEW3D_SELECT_PICK_ALL);
	int hits = 0;

	/* we _must_ end cache before return, use 'goto finally' */
	view3d_opengl_select_cache_begin();

	BLI_rcti_init_pt_radius(&rect, mval, 14);
	hits15 = view3d_opengl_select(vc, buffer, MAXPICKBUF, &rect, select_mode);
	if (hits15 == 1) {
		hits = hits15;
		goto finally;
	}
	else if (hits15 > 0) {
		int offs;

		offs = 4 * hits15;
		BLI_rcti_init_pt_radius(&rect, mval, 9);
		hits9 = view3d_opengl_select(vc, buffer + offs, MAXPICKBUF - offs, &rect, select_mode);
		if (hits9 == 1) {
			hits = selectbuffer_ret_hits_9(buffer, hits15, hits9);
			goto finally;
		}
		else if (hits9 > 0) {
			offs += 4 * hits9;
			BLI_rcti_init_pt_radius(&rect, mval, 5);
			hits5 = view3d_opengl_select(vc, buffer + offs, MAXPICKBUF - offs, &rect, select_mode);
			if (hits5 == 1) {
				hits = selectbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
				goto finally;
			}
		}

		if      (hits5 > 0) { hits = selectbuffer_ret_hits_5(buffer,  hits15, hits9, hits5); goto finally; }
		else if (hits9 > 0) { hits = selectbuffer_ret_hits_9(buffer,  hits15, hits9); goto finally; }
		else                { hits = hits15; goto finally; }
	}

finally:
	view3d_opengl_select_cache_end();

	return hits;
}

/* returns basact */
static Base *mouse_select_eval_buffer(
        ViewContext *vc, const uint *buffer, int hits,
        Base *startbase, bool do_nearest)
{
	Scene *scene = vc->scene;
	View3D *v3d = vc->v3d;
	Base *base, *basact = NULL;
	int a;

	if (do_nearest) {
		unsigned int min = 0xFFFFFFFF;
		int selcol = 0, notcol = 0;


		{
			/* only exclude active object when it is selected... */
			if (BASACT && (BASACT->flag & SELECT) && hits > 1) notcol = BASACT->selcol;

			for (a = 0; a < hits; a++) {
				if (min > buffer[4 * a + 1] && notcol != (buffer[4 * a + 3] & 0xFFFF)) {
					min = buffer[4 * a + 1];
					selcol = buffer[4 * a + 3] & 0xFFFF;
				}
			}
		}

		base = FIRSTBASE;
		while (base) {
			if (BASE_SELECTABLE(v3d, base)) {
				if (base->selcol == selcol) break;
			}
			base = base->next;
		}
		if (base) basact = base;
	}
	else {

		base = startbase;
		while (base) {
			/* skip objects with select restriction, to prevent prematurely ending this loop
			 * with an un-selectable choice */
			if (base->object->restrictflag & OB_RESTRICT_SELECT) {
				base = base->next;
				if (base == NULL) base = FIRSTBASE;
				if (base == startbase) break;
			}

			if (BASE_SELECTABLE(v3d, base)) {
				for (a = 0; a < hits; a++) {
					{
						if (base->selcol == (buffer[(4 * a) + 3] & 0xFFFF))
							basact = base;
					}
				}
			}

			if (basact) break;

			base = base->next;
			if (base == NULL) base = FIRSTBASE;
			if (base == startbase) break;
		}
	}

	return basact;
}

/* mval comes from event->mval, only use within region handlers */
Base *ED_view3d_give_base_under_cursor(bContext *C, const int mval[2])
{
	ViewContext vc;
	Base *basact = NULL;
	unsigned int buffer[MAXPICKBUF];
	int hits;
	bool do_nearest;

	/* setup view context for argument to callbacks */
	view3d_operator_needs_opengl(C);
	ED_view3d_viewcontext_init(C, &vc);

	hits = mixed_object_selectbuffer(&vc, buffer, mval, false, false, &do_nearest);

	if (hits > 0) {
		basact = mouse_select_eval_buffer(&vc, buffer, hits, vc.scene->base.first, do_nearest);
	}

	return basact;
}


/* mval is region coords */
static bool ed_object_select_pick(
        bContext *C, const int mval[2],
        bool extend, bool deselect, bool toggle, bool obcenter, bool enumerate, bool object)
{
	ViewContext vc;
	ARegion *ar = CTX_wm_region(C);
	View3D *v3d = CTX_wm_view3d(C);
	Scene *scene = CTX_data_scene(C);
	Base *base, *startbase = NULL, *basact = NULL, *oldbasact = NULL;
	bool is_obedit;
	float dist = ED_view3d_select_dist_px() * 1.3333f;
	bool retval = false;
	int hits;
	const float mval_fl[2] = {(float)mval[0], (float)mval[1]};


	/* setup view context for argument to callbacks */
	ED_view3d_viewcontext_init(C, &vc);

	is_obedit = (vc.obedit != NULL);
	if (object) {
		/* signal for view3d_opengl_select to skip editmode objects */
		vc.obedit = NULL;
	}

	/* always start list from basact in wire mode */
	startbase =  FIRSTBASE;
	if (BASACT && BASACT->next) startbase = BASACT->next;

	/* This block uses the control key to make the object selected
	 * by its center point rather than its contents */

	/* in editmode do not activate */
	if (obcenter) {

		/* note; shift+alt goes to group-flush-selecting */
		if (enumerate) {
			basact = object_mouse_select_menu(C, &vc, NULL, 0, mval, toggle);
		}
		else {
			base = startbase;
			while (base) {
				if (BASE_SELECTABLE(v3d, base)) {
					float screen_co[2];
					if (ED_view3d_project_float_global(
					            ar, base->object->obmat[3], screen_co,
					            V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_WIN | V3D_PROJ_TEST_CLIP_NEAR) == V3D_PROJ_RET_OK)
					{
						float dist_temp = len_manhattan_v2v2(mval_fl, screen_co);
						if (base == BASACT) dist_temp += 10.0f;
						if (dist_temp < dist) {
							dist = dist_temp;
							basact = base;
						}
					}
				}
				base = base->next;

				if (base == NULL) base = FIRSTBASE;
				if (base == startbase) break;
			}
		}
	}
	else {
		unsigned int buffer[MAXPICKBUF];
		bool do_nearest;

		hits = mixed_object_selectbuffer(&vc, buffer, mval, true, enumerate, &do_nearest);

		if (hits > 0) {
			/* note; shift+alt goes to group-flush-selecting */
			if (enumerate) {
				basact = object_mouse_select_menu(C, &vc, buffer, hits, mval, toggle);
			}
			else {
				basact = mouse_select_eval_buffer(&vc, buffer, hits, startbase, do_nearest);
			}

		}
	}

	/* so, do we have something selected? */
	if (basact) {
		retval = true;

		if (vc.obedit) {
			/* only do select */
			deselectall_except(scene, basact);
			ED_base_object_select(basact, BA_SELECT);
		}
		/* also prevent making it active on mouse selection */
		else if (BASE_SELECTABLE(v3d, basact)) {

			oldbasact = BASACT;

			if (extend) {
				ED_base_object_select(basact, BA_SELECT);
			}
			else if (deselect) {
				ED_base_object_select(basact, BA_DESELECT);
			}
			else if (toggle) {
				if (basact->flag & SELECT) {
					if (basact == oldbasact) {
						ED_base_object_select(basact, BA_DESELECT);
					}
				}
				else {
					ED_base_object_select(basact, BA_SELECT);
				}
			}
			else {
				deselectall_except(scene, basact);
				ED_base_object_select(basact, BA_SELECT);
			}

			if ((oldbasact != basact) && (is_obedit == false)) {
				ED_base_object_activate(C, basact); /* adds notifier */
			}
		}

		WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
	}

	return retval;
}

/* ********************  border and circle ************************************** */

typedef struct BoxSelectUserData {
	ViewContext *vc;
	const rcti *rect;
	const rctf *rect_fl;
	rctf       _rect_fl;
	bool select;

	/* runtime */
	int pass;
	bool is_done;
	bool is_changed;
} BoxSelectUserData;

static void view3d_userdata_boxselect_init(
        BoxSelectUserData *r_data,
        ViewContext *vc, const rcti *rect, const bool select)
{
	r_data->vc = vc;

	r_data->rect = rect;
	r_data->rect_fl = &r_data->_rect_fl;
	BLI_rctf_rcti_copy(&r_data->_rect_fl, rect);

	r_data->select = select;

	/* runtime */
	r_data->pass = 0;
	r_data->is_done = false;
	r_data->is_changed = false;
}

bool edge_inside_circle(const float cent[2], float radius, const float screen_co_a[2], const float screen_co_b[2])
{
	const float radius_squared = radius * radius;
	return (dist_squared_to_line_segment_v2(cent, screen_co_a, screen_co_b) < radius_squared);
}

static void do_nurbs_box_select__doSelect(
        void *userData, Nurb *UNUSED(nu), BPoint *bp, BezTriple *bezt, int beztindex, const float screen_co[2])
{
	BoxSelectUserData *data = userData;
	Object *obedit = data->vc->obedit;
	Curve *cu = (Curve *)obedit->data;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co)) {
		if (bp) {
			bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
		}
		else {
			if (cu->drawflag & CU_HIDE_HANDLES) {
				/* can only be (beztindex == 0) here since handles are hidden */
				bezt->f1 = bezt->f2 = bezt->f3 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
			}
			else {
				if (beztindex == 0) {
					bezt->f1 = data->select ? (bezt->f1 | SELECT) : (bezt->f1 & ~SELECT);
				}
				else if (beztindex == 1) {
					bezt->f2 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
				}
				else {
					bezt->f3 = data->select ? (bezt->f3 | SELECT) : (bezt->f3 & ~SELECT);
				}
			}
		}
	}
}
static int do_nurbs_box_select(ViewContext *vc, rcti *rect, bool select, bool extend)
{
	BoxSelectUserData data;

	view3d_userdata_boxselect_init(&data, vc, rect, select);

	if (extend == false && select) {
		Curve *curve = (Curve *) vc->obedit->data;
		ED_curve_deselect_all(curve->editnurb);
	}

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	nurbs_foreachScreenVert(vc, do_nurbs_box_select__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
	BKE_curve_nurb_vert_active_validate(vc->obedit->data);

	return OPERATOR_FINISHED;
}

static void do_lattice_box_select__doSelect(void *userData, BPoint *bp, const float screen_co[2])
{
	BoxSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co)) {
		bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
	}
}
static int do_lattice_box_select(ViewContext *vc, rcti *rect, bool select, bool extend)
{
	BoxSelectUserData data;

	view3d_userdata_boxselect_init(&data, vc, rect, select);

	if (extend == false && select)
		ED_lattice_flags_set(vc->obedit, 0);

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	lattice_foreachScreenVert(vc, do_lattice_box_select__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

	return OPERATOR_FINISHED;
}

static void do_mesh_box_select__doSelectVert(void *userData, BMVert *eve, const float screen_co[2], int UNUSED(index))
{
	BoxSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co)) {
		BM_vert_select_set(data->vc->em->bm, eve, data->select);
	}
}
static void do_mesh_box_select__doSelectEdge(
        void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index)
{
	BoxSelectUserData *data = userData;

	if (EDBM_backbuf_check(bm_solidoffs + index)) {
		if (data->pass == 0) {
			if (edge_fully_inside_rect(data->rect_fl, screen_co_a, screen_co_b)) {
				BM_edge_select_set(data->vc->em->bm, eed, data->select);
				data->is_done = true;
			}
		}
		else {
			if (edge_inside_rect(data->rect_fl, screen_co_a, screen_co_b)) {
				BM_edge_select_set(data->vc->em->bm, eed, data->select);
			}
		}
	}
}
static void do_mesh_box_select__doSelectFace(void *userData, BMFace *efa, const float screen_co[2], int UNUSED(index))
{
	BoxSelectUserData *data = userData;

	if (BLI_rctf_isect_pt_v(data->rect_fl, screen_co)) {
		BM_face_select_set(data->vc->em->bm, efa, data->select);
	}
}
static int do_mesh_box_select(ViewContext *vc, rcti *rect, bool select, bool extend)
{
	BoxSelectUserData data;
	ToolSettings *ts = vc->scene->toolsettings;
	int bbsel;

	view3d_userdata_boxselect_init(&data, vc, rect, select);

	if (extend == false && select)
		EDBM_flag_disable_all(vc->em, BM_ELEM_SELECT);

	/* for non zbuf projections, don't change the GL state */
	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

	glLoadMatrixf(vc->rv3d->viewmat);
	bbsel = EDBM_backbuf_border_init(vc, rect->xmin, rect->ymin, rect->xmax, rect->ymax);

	if (ts->selectmode & SCE_SELECT_VERTEX) {
		if (bbsel) {
			edbm_backbuf_check_and_select_verts(vc->em, select);
		}
		else {
			mesh_foreachScreenVert(vc, do_mesh_box_select__doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}
	if (ts->selectmode & SCE_SELECT_EDGE) {
		/* Does both bbsel and non-bbsel versions (need screen cos for both) */

		data.pass = 0;
		mesh_foreachScreenEdge(vc, do_mesh_box_select__doSelectEdge, &data, V3D_PROJ_TEST_CLIP_NEAR);

		if (data.is_done == 0) {
			data.pass = 1;
			mesh_foreachScreenEdge(vc, do_mesh_box_select__doSelectEdge, &data, V3D_PROJ_TEST_CLIP_NEAR);
		}
	}

	if (ts->selectmode & SCE_SELECT_FACE) {
		if (bbsel) {
			edbm_backbuf_check_and_select_faces(vc->em, select);
		}
		else {
			mesh_foreachScreenFace(vc, do_mesh_box_select__doSelectFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}

	EDBM_backbuf_free();

	EDBM_selectmode_flush(vc->em);

	return OPERATOR_FINISHED;
}

static int view3d_borderselect_exec(bContext *C, wmOperator *op)
{
	ViewContext vc;
	rcti rect;
	bool extend;
	bool select;

	int ret = OPERATOR_CANCELLED;

	view3d_operator_needs_opengl(C);

	/* setup view context for argument to callbacks */
	ED_view3d_viewcontext_init(C, &vc);

	select = !RNA_boolean_get(op->ptr, "deselect");
	extend = RNA_boolean_get(op->ptr, "extend");
	WM_operator_properties_border_to_rcti(op, &rect);

	if (vc.obedit) {
		switch (vc.obedit->type) {
			case OB_MESH:
				vc.em = BKE_editmesh_from_object(vc.obedit);
				ret = do_mesh_box_select(&vc, &rect, select, extend);
//			if (EM_texFaceCheck())
				if (ret & OPERATOR_FINISHED) {
					WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
				}
				break;
			case OB_CURVE:
			case OB_SURF:
				ret = do_nurbs_box_select(&vc, &rect, select, extend);
				if (ret & OPERATOR_FINISHED) {
					WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
				}
				break;
			case OB_LATTICE:
				ret = do_lattice_box_select(&vc, &rect, select, extend);
				if (ret & OPERATOR_FINISHED) {
					WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
				}
				break;
			default:
				assert(!"border select on incorrect object type");
				break;
		}
	}

	return ret;
}


/* *****************Selection Operators******************* */

/* ****** Border Select ****** */
void VIEW3D_OT_select_border(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Border Select";
	ot->description = "Select items using border selection";
	ot->idname = "VIEW3D_OT_select_border";

	/* api callbacks */
	ot->invoke = WM_gesture_border_invoke;
	ot->exec = view3d_borderselect_exec;
	ot->modal = WM_gesture_border_modal;
	ot->poll = view3d_selectable_data;
	ot->cancel = WM_gesture_border_cancel;

	/* flags */
	ot->flag = OPTYPE_UNDO;

	/* rna */
	WM_operator_properties_gesture_border_select(ot);
}


/* ****** Mouse Select ****** */


static int view3d_select_exec(bContext *C, wmOperator *op)
{
	Object *obedit = CTX_data_edit_object(C);
	bool extend = RNA_boolean_get(op->ptr, "extend");
	bool deselect = RNA_boolean_get(op->ptr, "deselect");
	bool toggle = RNA_boolean_get(op->ptr, "toggle");
	bool center = RNA_boolean_get(op->ptr, "center");
	bool enumerate = RNA_boolean_get(op->ptr, "enumerate");
	/* only force object select for editmode to support vertex parenting */
	bool object = (RNA_boolean_get(op->ptr, "object") && obedit);

	bool retval = false;
	int location[2];

	RNA_int_get_array(op->ptr, "location", location);

	view3d_operator_needs_opengl(C);

	if (object) {
		obedit = NULL;

		/* ack, this is incorrect but to do this correctly we would need an
		 * alternative editmode/objectmode keymap, this copies the functionality
		 * from 2.4x where Ctrl+Select in editmode does object select only */
		center = false;
	}

	if (obedit) {
		if (obedit->type == OB_MESH)
			retval = EDBM_select_pick(C, location, extend, deselect, toggle);
		else if (obedit->type == OB_LATTICE)
			retval = ED_lattice_select_pick(C, location, extend, deselect, toggle);
		else if (ELEM(obedit->type, OB_CURVE, OB_SURF))
			retval = ED_curve_editnurb_select_pick(C, location, extend, deselect, toggle);
		else if (obedit->type == OB_FONT)
			retval = ED_curve_editfont_select_pick(C, location, extend, deselect, toggle);

	}
	retval = ed_object_select_pick(C, location, extend, deselect, toggle, center, enumerate, object);

	/* passthrough allows tweaks
	 * FINISHED to signal one operator worked
	 * */
	if (retval)
		return OPERATOR_PASS_THROUGH | OPERATOR_FINISHED;
	else
		return OPERATOR_PASS_THROUGH;  /* nothing selected, just passthrough */
}

static int view3d_select_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	RNA_int_set_array(op->ptr, "location", event->mval);

	return view3d_select_exec(C, op);
}

void VIEW3D_OT_select(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Activate/Select";
	ot->description = "Activate/select item(s)";
	ot->idname = "VIEW3D_OT_select";

	/* api callbacks */
	ot->invoke = view3d_select_invoke;
	ot->exec = view3d_select_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_UNDO;

	/* properties */
	WM_operator_properties_mouse_select(ot);

	RNA_def_boolean(ot->srna, "center", 0, "Center", "Use the object center when selecting, in editmode used to extend object selection");
	RNA_def_boolean(ot->srna, "enumerate", 0, "Enumerate", "List objects under the mouse (object mode only)");
	RNA_def_boolean(ot->srna, "object", 0, "Object", "Use object selection (editmode only)");

	prop = RNA_def_int_vector(ot->srna, "location", 2, NULL, INT_MIN, INT_MAX, "Location", "Mouse location", INT_MIN, INT_MAX);
	RNA_def_property_flag(prop, PROP_HIDDEN);
}


/* -------------------- circle select --------------------------------------------- */

typedef struct CircleSelectUserData {
	ViewContext *vc;
	bool select;
	int   mval[2];
	float mval_fl[2];
	float radius;
	float radius_squared;

	/* runtime */
	bool is_changed;
} CircleSelectUserData;

static void view3d_userdata_circleselect_init(
        CircleSelectUserData *r_data,
        ViewContext *vc, const bool select, const int mval[2], const float rad)
{
	r_data->vc = vc;
	r_data->select = select;
	copy_v2_v2_int(r_data->mval, mval);
	r_data->mval_fl[0] = mval[0];
	r_data->mval_fl[1] = mval[1];

	r_data->radius = rad;
	r_data->radius_squared = rad * rad;

	/* runtime */
	r_data->is_changed = false;
}

static void mesh_circle_doSelectVert(void *userData, BMVert *eve, const float screen_co[2], int UNUSED(index))
{
	CircleSelectUserData *data = userData;

	if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
		BM_vert_select_set(data->vc->em->bm, eve, data->select);
	}
}
static void mesh_circle_doSelectEdge(
        void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int UNUSED(index))
{
	CircleSelectUserData *data = userData;

	if (edge_inside_circle(data->mval_fl, data->radius, screen_co_a, screen_co_b)) {
		BM_edge_select_set(data->vc->em->bm, eed, data->select);
	}
}
static void mesh_circle_doSelectFace(void *userData, BMFace *efa, const float screen_co[2], int UNUSED(index))
{
	CircleSelectUserData *data = userData;

	if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
		BM_face_select_set(data->vc->em->bm, efa, data->select);
	}
}

static void mesh_circle_select(ViewContext *vc, const bool select, const int mval[2], float rad)
{
	ToolSettings *ts = vc->scene->toolsettings;
	int bbsel;
	CircleSelectUserData data;

	bbsel = EDBM_backbuf_circle_init(vc, mval[0], mval[1], (short)(rad + 1.0f));
	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */

	vc->em = BKE_editmesh_from_object(vc->obedit);

	view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

	if (ts->selectmode & SCE_SELECT_VERTEX) {
		if (bbsel) {
			edbm_backbuf_check_and_select_verts(vc->em, select);
		}
		else {
			mesh_foreachScreenVert(vc, mesh_circle_doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}

	if (ts->selectmode & SCE_SELECT_EDGE) {
		if (bbsel) {
			edbm_backbuf_check_and_select_edges(vc->em, select);
		}
		else {
			mesh_foreachScreenEdge(vc, mesh_circle_doSelectEdge, &data, V3D_PROJ_TEST_CLIP_NEAR);
		}
	}

	if (ts->selectmode & SCE_SELECT_FACE) {
		if (bbsel) {
			edbm_backbuf_check_and_select_faces(vc->em, select);
		}
		else {
			mesh_foreachScreenFace(vc, mesh_circle_doSelectFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
		}
	}

	EDBM_backbuf_free();
	EDBM_selectmode_flush(vc->em);
}

static void nurbscurve_circle_doSelect(
        void *userData, Nurb *UNUSED(nu), BPoint *bp, BezTriple *bezt, int beztindex, const float screen_co[2])
{
	CircleSelectUserData *data = userData;
	Object *obedit = data->vc->obedit;
	Curve *cu = (Curve *)obedit->data;

	if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
		if (bp) {
			bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
		}
		else {
			if (cu->drawflag & CU_HIDE_HANDLES) {
				/* can only be (beztindex == 0) here since handles are hidden */
				bezt->f1 = bezt->f2 = bezt->f3 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
			}
			else {
				if (beztindex == 0) {
					bezt->f1 = data->select ? (bezt->f1 | SELECT) : (bezt->f1 & ~SELECT);
				}
				else if (beztindex == 1) {
					bezt->f2 = data->select ? (bezt->f2 | SELECT) : (bezt->f2 & ~SELECT);
				}
				else {
					bezt->f3 = data->select ? (bezt->f3 | SELECT) : (bezt->f3 & ~SELECT);
				}
			}
		}
	}
}
static void nurbscurve_circle_select(ViewContext *vc, const bool select, const int mval[2], float rad)
{
	CircleSelectUserData data;

	view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	nurbs_foreachScreenVert(vc, nurbscurve_circle_doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
	BKE_curve_nurb_vert_active_validate(vc->obedit->data);
}


static void latticecurve_circle_doSelect(void *userData, BPoint *bp, const float screen_co[2])
{
	CircleSelectUserData *data = userData;

	if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
		bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
	}
}
static void lattice_circle_select(ViewContext *vc, const bool select, const int mval[2], float rad)
{
	CircleSelectUserData data;

	view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

	ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
	lattice_foreachScreenVert(vc, latticecurve_circle_doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
}


/** Callbacks for circle selection in Editmode */

static void obedit_circle_select(ViewContext *vc, const bool select, const int mval[2], float rad)
{
	switch (vc->obedit->type) {
		case OB_MESH:
			mesh_circle_select(vc, select, mval, rad);
			break;
		case OB_CURVE:
		case OB_SURF:
			nurbscurve_circle_select(vc, select, mval, rad);
			break;
		case OB_LATTICE:
			lattice_circle_select(vc, select, mval, rad);
			break;
		default:
			return;
	}
}

static bool object_circle_select(ViewContext *vc, const bool select, const int mval[2], float rad)
{
	Scene *scene = vc->scene;
	const float radius_squared = rad * rad;
	const float mval_fl[2] = {mval[0], mval[1]};
	bool changed = false;
	const int select_flag = select ? SELECT : 0;


	Base *base;
	for (base = FIRSTBASE; base; base = base->next) {
		if (BASE_SELECTABLE(vc->v3d, base) && ((base->flag & SELECT) != select_flag)) {
			float screen_co[2];
			if (ED_view3d_project_float_global(
			            vc->ar, base->object->obmat[3], screen_co,
			            V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_WIN | V3D_PROJ_TEST_CLIP_NEAR) == V3D_PROJ_RET_OK)
			{
				if (len_squared_v2v2(mval_fl, screen_co) <= radius_squared) {
					ED_base_object_select(base, select ? BA_SELECT : BA_DESELECT);
					changed = true;
				}
			}
		}
	}

	return changed;
}

/* not a real operator, only for circle test */
static int view3d_circle_select_exec(bContext *C, wmOperator *op)
{
	ViewContext vc;
	ED_view3d_viewcontext_init(C, &vc);
	Object *obact = vc.obact;
	Object *obedit = vc.obedit;
	const int radius = RNA_int_get(op->ptr, "radius");
	const bool select = !RNA_boolean_get(op->ptr, "deselect");
	const int mval[2] = {RNA_int_get(op->ptr, "x"),
	                     RNA_int_get(op->ptr, "y")};

	if (obedit)
	{
		view3d_operator_needs_opengl(C);

		if (CTX_data_edit_object(C)) {
			obedit_circle_select(&vc, select, mval, (float)radius);
			WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obact->data);
		}
	}
	else {
		if (object_circle_select(&vc, select, mval, (float)radius)) {
			WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, vc.scene);
		}
	}

	return OPERATOR_FINISHED;
}

void VIEW3D_OT_select_circle(wmOperatorType *ot)
{
	ot->name = "Circle Select";
	ot->description = "Select items using circle selection";
	ot->idname = "VIEW3D_OT_select_circle";

	ot->invoke = WM_gesture_circle_invoke;
	ot->modal = WM_gesture_circle_modal;
	ot->exec = view3d_circle_select_exec;
	ot->poll = view3d_selectable_data;
	ot->cancel = WM_gesture_circle_cancel;

	/* flags */
	ot->flag = OPTYPE_UNDO;

	/* properties */
	WM_operator_properties_gesture_circle_select(ot);
}
