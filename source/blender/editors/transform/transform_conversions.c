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

/** \file blender/editors/transform/transform_conversions.c
 *  \ingroup edtransform
 */

#include <string.h>
#include <math.h>
#include <limits.h>

#include "DNA_mesh_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_scene_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_curve_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_linklist_stack.h"
#include "BLI_string.h"
#include "BLI_bitmap.h"
#include "BLI_rect.h"

#include "BKE_DerivedMesh.h"
#include "BKE_context.h"
#include "BKE_crazyspace.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_editmesh.h"

#include "ED_image.h"
#include "ED_object.h"
#include "ED_mesh.h"

#include "WM_api.h"  /* for WM_event_add_notifier to deal with stabilization nodes */
#include "WM_types.h"

#include "UI_view2d.h"
#include "UI_interface.h"

#include "RNA_access.h"

#include "transform.h"
#include "bmesh.h"

/**
 * Transforming around ourselves is no use, fallback to individual origins,
 * useful for curve/armatures.
 */
static void transform_around_single_fallback(TransInfo *t)
{
	if ((t->total == 1) &&
	    (ELEM(t->around, V3D_AROUND_CENTER_BOUNDS, V3D_AROUND_CENTER_MEDIAN, V3D_AROUND_ACTIVE)) &&
	    (ELEM(t->mode, TFM_RESIZE, TFM_ROTATION, TFM_TRACKBALL)))
	{
		t->around = V3D_AROUND_LOCAL_ORIGINS;
	}
}

/* when transforming islands */
struct TransIslandData {
	float co[3];
	float axismtx[3][3];
};

/* ************************** Functions *************************** */

static int trans_data_compare_dist(const void *a, const void *b)
{
	const TransData *td_a = (const TransData *)a;
	const TransData *td_b = (const TransData *)b;

	if      (td_a->dist < td_b->dist) return -1;
	else if (td_a->dist > td_b->dist) return  1;
	else                              return  0;
}

static int trans_data_compare_rdist(const void *a, const void *b)
{
	const TransData *td_a = (const TransData *)a;
	const TransData *td_b = (const TransData *)b;

	if      (td_a->rdist < td_b->rdist) return -1;
	else if (td_a->rdist > td_b->rdist) return  1;
	else                                return  0;
}

void sort_trans_data_dist(TransInfo *t)
{
	TransData *start = t->data;
	int i;

	for (i = 0; i < t->total && start->flag & TD_SELECTED; i++)
		start++;

	if (i < t->total) {
		if (t->flag & T_PROP_CONNECTED)
			qsort(start, t->total - i, sizeof(TransData), trans_data_compare_dist);
		else
			qsort(start, t->total - i, sizeof(TransData), trans_data_compare_rdist);
	}
}

static void sort_trans_data(TransInfo *t)
{
	TransData *sel, *unsel;
	TransData temp;
	unsel = t->data;
	sel = t->data;
	sel += t->total - 1;
	while (sel > unsel) {
		while (unsel->flag & TD_SELECTED) {
			unsel++;
			if (unsel == sel) {
				return;
			}
		}
		while (!(sel->flag & TD_SELECTED)) {
			sel--;
			if (unsel == sel) {
				return;
			}
		}
		temp = *unsel;
		*unsel = *sel;
		*sel = temp;
		sel--;
		unsel++;
	}
}

/* distance calculated from not-selected vertex to nearest selected vertex
 * warning; this is loops inside loop, has minor N^2 issues, but by sorting list it is OK */
static void set_prop_dist(TransInfo *t, const bool with_dist)
{
	TransData *tob;
	int a;

	float _proj_vec[3];
	const float *proj_vec = NULL;

	/* support for face-islands */
	const bool use_island = transdata_check_local_islands(t, t->around);

	if (t->flag & T_PROP_PROJECTED) {
		if (t->spacetype == SPACE_VIEW3D && t->ar && t->ar->regiontype == RGN_TYPE_WINDOW) {
			RegionView3D *rv3d = t->ar->regiondata;
			normalize_v3_v3(_proj_vec, rv3d->viewinv[2]);
			proj_vec = _proj_vec;
		}
	}

	for (a = 0, tob = t->data; a < t->total; a++, tob++) {

		tob->rdist = 0.0f; // init, it was mallocced

		if ((tob->flag & TD_SELECTED) == 0) {
			TransData *td;
			int i;
			float dist_sq, vec[3];

			tob->rdist = -1.0f; // signal for next loop

			for (i = 0, td = t->data; i < t->total; i++, td++) {
				if (td->flag & TD_SELECTED) {
					if (use_island) {
						sub_v3_v3v3(vec, tob->iloc, td->iloc);
					}
					else {
						sub_v3_v3v3(vec, tob->center, td->center);
					}
					mul_m3_v3(tob->mtx, vec);

					if (proj_vec) {
						float vec_p[3];
						project_v3_v3v3(vec_p, vec, proj_vec);
						sub_v3_v3(vec, vec_p);
					}

					dist_sq = len_squared_v3(vec);
					if ((tob->rdist == -1.0f) || (dist_sq < SQUARE(tob->rdist))) {
						tob->rdist = sqrtf(dist_sq);
						if (use_island) {
							copy_v3_v3(tob->center, td->center);
							copy_m3_m3(tob->axismtx, td->axismtx);
						}
					}
				}
				else {
					break;  /* by definition transdata has selected items in beginning */
				}
			}
			if (with_dist) {
				tob->dist = tob->rdist;
			}
		}
	}
}

/* ************************** CONVERSIONS ************************* */

/* ********************* texture space ********* */

static void createTransTexspace(TransInfo *t)
{
	Scene *scene = t->scene;
	TransData *td;
	Object *ob;
	ID *id;
	short *texflag;

	ob = OBACT;

	if (ob == NULL) { // Shouldn't logically happen, but still...
		t->total = 0;
		return;
	}

	id = ob->data;
	if (id == NULL || !ELEM(GS(id->name), ID_ME, ID_CU)) {
		BKE_report(t->reports, RPT_ERROR, "Unsupported object type for text-space transform");
		t->total = 0;
		return;
	}

	if (BKE_object_obdata_is_libdata(ob)) {
		BKE_report(t->reports, RPT_ERROR, "Linked data can't text-space transform");
		t->total = 0;
		return;
	}

	t->total = 1;
	td = t->data = MEM_callocN(sizeof(TransData), "TransTexspace");
	td->ext = t->ext = MEM_callocN(sizeof(TransDataExtension), "TransTexspace");

	td->flag = TD_SELECTED;
	copy_v3_v3(td->center, ob->obmat[3]);
	td->ob = ob;

	copy_m3_m4(td->mtx, ob->obmat);
	copy_m3_m4(td->axismtx, ob->obmat);
	normalize_m3(td->axismtx);
	pseudoinverse_m3_m3(td->smtx, td->mtx, PSEUDOINVERSE_EPSILON);

	if (BKE_object_obdata_texspace_get(ob, &texflag, &td->loc, &td->ext->size, &td->ext->rot)) {
		ob->dtx |= OB_TEXSPACE;
		*texflag &= ~ME_AUTOSPACE;
	}

	copy_v3_v3(td->iloc, td->loc);
	copy_v3_v3(td->ext->irot, td->ext->rot);
	copy_v3_v3(td->ext->isize, td->ext->size);
}

/* ********************* edge (for crease) ***** */

static void createTransEdge(TransInfo *t)
{
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	TransData *td = NULL;
	BMEdge *eed;
	BMIter iter;
	float mtx[3][3], smtx[3][3];
	int count = 0, countsel = 0;
	const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;
	int cd_edge_float_offset;

	BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
		if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
			if (BM_elem_flag_test(eed, BM_ELEM_SELECT)) countsel++;
			if (is_prop_edit) count++;
		}
	}

	if (countsel == 0)
		return;

	if (is_prop_edit) {
		t->total = count;
	}
	else {
		t->total = countsel;
	}

	td = t->data = MEM_callocN(t->total * sizeof(TransData), "TransCrease");

	copy_m3_m4(mtx, t->obedit->obmat);
	pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);

	/* create data we need */
	if (t->mode == TFM_BWEIGHT) {
		BM_mesh_cd_flag_ensure(em->bm, BKE_mesh_from_object(t->obedit), ME_CDFLAG_EDGE_BWEIGHT);
		cd_edge_float_offset = CustomData_get_offset(&em->bm->edata, CD_BWEIGHT);
	}
	else { //if (t->mode == TFM_CREASE) {
		BLI_assert(t->mode == TFM_CREASE);
		BM_mesh_cd_flag_ensure(em->bm, BKE_mesh_from_object(t->obedit), ME_CDFLAG_EDGE_CREASE);
		cd_edge_float_offset = CustomData_get_offset(&em->bm->edata, CD_CREASE);
	}

	BLI_assert(cd_edge_float_offset != -1);

	BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
		if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN) && (BM_elem_flag_test(eed, BM_ELEM_SELECT) || is_prop_edit)) {
			float *fl_ptr;
			/* need to set center for center calculations */
			mid_v3_v3v3(td->center, eed->v1->co, eed->v2->co);

			td->loc = NULL;
			if (BM_elem_flag_test(eed, BM_ELEM_SELECT))
				td->flag = TD_SELECTED;
			else
				td->flag = 0;

			copy_m3_m3(td->smtx, smtx);
			copy_m3_m3(td->mtx, mtx);

			td->ext = NULL;

			fl_ptr = BM_ELEM_CD_GET_VOID_P(eed, cd_edge_float_offset);
			td->val  =  fl_ptr;
			td->ival = *fl_ptr;

			td++;
		}
	}
}

/* ********************* curve/surface ********* */

static void calc_distanceCurveVerts(TransData *head, TransData *tail)
{
	TransData *td, *td_near = NULL;
	for (td = head; td <= tail; td++) {
		if (td->flag & TD_SELECTED) {
			td_near = td;
			td->dist = 0.0f;
		}
		else if (td_near) {
			float dist;
			dist = len_v3v3(td_near->center, td->center);
			if (dist < (td - 1)->dist) {
				td->dist = (td - 1)->dist;
			}
			else {
				td->dist = dist;
			}
		}
		else {
			td->dist = FLT_MAX;
			td->flag |= TD_NOTCONNECTED;
		}
	}
	td_near = NULL;
	for (td = tail; td >= head; td--) {
		if (td->flag & TD_SELECTED) {
			td_near = td;
			td->dist = 0.0f;
		}
		else if (td_near) {
			float dist;
			dist = len_v3v3(td_near->center, td->center);
			if (td->flag & TD_NOTCONNECTED || dist < td->dist || (td + 1)->dist < td->dist) {
				td->flag &= ~TD_NOTCONNECTED;
				if (dist < (td + 1)->dist) {
					td->dist = (td + 1)->dist;
				}
				else {
					td->dist = dist;
				}
			}
		}
	}
}

/* Utility function for getting the handle data from bezier's */
static TransDataCurveHandleFlags *initTransDataCurveHandles(TransData *td, struct BezTriple *bezt)
{
	TransDataCurveHandleFlags *hdata;
	td->flag |= TD_BEZTRIPLE;
	hdata = td->hdata = MEM_mallocN(sizeof(TransDataCurveHandleFlags), "CuHandle Data");
	hdata->ih1 = bezt->h1;
	hdata->h1 = &bezt->h1;
	hdata->ih2 = bezt->h2; /* in case the second is not selected */
	hdata->h2 = &bezt->h2;
	return hdata;
}

/**
 * For the purpose of transform code we need to behave as if handles are selected,
 * even when they aren't (see special case below).
 */
static int bezt_select_to_transform_triple_flag(
        const BezTriple *bezt, const bool hide_handles)
{
	int flag = 0;

	if (hide_handles) {
		if (bezt->f2 & SELECT) {
			flag = (1 << 0) | (1 << 1) | (1 << 2);
		}
	}
	else {
		flag = (
			((bezt->f1 & SELECT) ? (1 << 0) : 0) |
			((bezt->f2 & SELECT) ? (1 << 1) : 0) |
			((bezt->f3 & SELECT) ? (1 << 2) : 0)
		);
	}

	/* Special case for auto & aligned handles:
	 * When a center point is being moved without the handles,
	 * leaving the handles stationary makes no sense and only causes strange behavior,
	 * where one handle is arbitrarily anchored, the other one is aligned and lengthened
	 * based on where the center point is moved. Also a bug when cancelling, see: T52007.
	 *
	 * A more 'correct' solution could be to store handle locations in 'TransDataCurveHandleFlags'.
	 * However that doesn't resolve odd behavior, so best transform the handles in this case.
	 */
	if ((flag != ((1 << 0) | (1 << 1) | (1 << 2))) && (flag & (1 << 1))) {
		if (ELEM(bezt->h1, HD_AUTO, HD_ALIGN) &&
		    ELEM(bezt->h2, HD_AUTO, HD_ALIGN))
		{
			flag = (1 << 0) | (1 << 1) | (1 << 2);
		}
	}

	return flag;
}

static void createTransCurveVerts(TransInfo *t)
{
	Curve *cu = t->obedit->data;
	TransData *td = NULL;
	Nurb *nu;
	BezTriple *bezt;
	BPoint *bp;
	float mtx[3][3], smtx[3][3];
	int a;
	int count = 0, countsel = 0;
	const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;
	short hide_handles = (cu->drawflag & CU_HIDE_HANDLES);
	ListBase *nurbs;

	/* to be sure */
	if (cu->editnurb == NULL) return;

#define SEL_F1 (1 << 0)
#define SEL_F2 (1 << 1)
#define SEL_F3 (1 << 2)

	/* count total of vertices, check identical as in 2nd loop for making transdata! */
	nurbs = BKE_curve_editNurbs_get(cu);
	for (nu = nurbs->first; nu; nu = nu->next) {
		if (nu->type == CU_BEZIER) {
			for (a = 0, bezt = nu->bezt; a < nu->pntsu; a++, bezt++) {
				if (bezt->hide == 0) {
					const int bezt_tx = bezt_select_to_transform_triple_flag(bezt, hide_handles);
					if (bezt_tx & SEL_F1) { countsel++; }
					if (bezt_tx & SEL_F2) { countsel++; }
					if (bezt_tx & SEL_F3) { countsel++; }
					if (is_prop_edit) count += 3;

				}
			}
		}
		else {
			for (a = nu->pntsu * nu->pntsv, bp = nu->bp; a > 0; a--, bp++) {
				if (bp->hide == 0) {
					if (is_prop_edit) count++;
					if (bp->f1 & SELECT) countsel++;
				}
			}
		}
	}
	/* note: in prop mode we need at least 1 selected */
	if (countsel == 0) return;

	if (is_prop_edit) t->total = count;
	else t->total = countsel;
	t->data = MEM_callocN(t->total * sizeof(TransData), "TransObData(Curve EditMode)");

	transform_around_single_fallback(t);

	copy_m3_m4(mtx, t->obedit->obmat);
	pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);

	td = t->data;
	for (nu = nurbs->first; nu; nu = nu->next) {
		if (nu->type == CU_BEZIER) {
			TransData *head, *tail;
			head = tail = td;
			for (a = 0, bezt = nu->bezt; a < nu->pntsu; a++, bezt++) {
				if (bezt->hide == 0) {
					TransDataCurveHandleFlags *hdata = NULL;
					float axismtx[3][3];

					if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
						float normal[3], plane[3];

						BKE_nurb_bezt_calc_normal(nu, bezt, normal);
						BKE_nurb_bezt_calc_plane(nu, bezt, plane);

						if (createSpaceNormalTangent(axismtx, normal, plane)) {
							/* pass */
						}
						else {
							normalize_v3(normal);
							axis_dominant_v3_to_m3(axismtx, normal);
							invert_m3(axismtx);
						}
					}

					/* Elements that will be transform (not always a match to selection). */
					const int bezt_tx = bezt_select_to_transform_triple_flag(bezt, hide_handles);

					if (is_prop_edit || bezt_tx & SEL_F1) {
						copy_v3_v3(td->iloc, bezt->vec[0]);
						td->loc = bezt->vec[0];
						copy_v3_v3(td->center, bezt->vec[(hide_handles ||
						                                  (t->around == V3D_AROUND_LOCAL_ORIGINS) ||
						                                  (bezt->f2 & SELECT)) ? 1 : 0]);
						if (hide_handles) {
							if (bezt->f2 & SELECT) td->flag = TD_SELECTED;
							else td->flag = 0;
						}
						else {
							if (bezt->f1 & SELECT) td->flag = TD_SELECTED;
							else td->flag = 0;
						}
						td->ext = NULL;
						td->val = NULL;

						hdata = initTransDataCurveHandles(td, bezt);

						copy_m3_m3(td->smtx, smtx);
						copy_m3_m3(td->mtx, mtx);
						if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
							copy_m3_m3(td->axismtx, axismtx);
						}

						td++;
						tail++;
					}

					/* This is the Curve Point, the other two are handles */
					if (is_prop_edit || bezt_tx & SEL_F2) {
						copy_v3_v3(td->iloc, bezt->vec[1]);
						td->loc = bezt->vec[1];
						copy_v3_v3(td->center, td->loc);
						if (bezt->f2 & SELECT) td->flag = TD_SELECTED;
						else td->flag = 0;
						td->ext = NULL;

						if (t->mode == TFM_CURVE_SHRINKFATTEN) { /* || t->mode==TFM_RESIZE) {*/ /* TODO - make points scale */
							td->val = &(bezt->radius);
							td->ival = bezt->radius;
						}
						else if (t->mode == TFM_TILT) {
							td->val = &(bezt->alfa);
							td->ival = bezt->alfa;
						}
						else {
							td->val = NULL;
						}

						copy_m3_m3(td->smtx, smtx);
						copy_m3_m3(td->mtx, mtx);
						if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
							copy_m3_m3(td->axismtx, axismtx);
						}

						if ((bezt_tx & SEL_F1) == 0 && (bezt_tx & SEL_F3) == 0)
							/* If the middle is selected but the sides arnt, this is needed */
							if (hdata == NULL) { /* if the handle was not saved by the previous handle */
								hdata = initTransDataCurveHandles(td, bezt);
							}

						td++;
						tail++;
					}
					if (is_prop_edit || bezt_tx & SEL_F3) {
						copy_v3_v3(td->iloc, bezt->vec[2]);
						td->loc = bezt->vec[2];
						copy_v3_v3(td->center, bezt->vec[(hide_handles ||
						                                  (t->around == V3D_AROUND_LOCAL_ORIGINS) ||
						                                  (bezt->f2 & SELECT)) ? 1 : 2]);
						if (hide_handles) {
							if (bezt->f2 & SELECT) td->flag = TD_SELECTED;
							else td->flag = 0;
						}
						else {
							if (bezt->f3 & SELECT) td->flag = TD_SELECTED;
							else td->flag = 0;
						}
						td->ext = NULL;
						td->val = NULL;

						if (hdata == NULL) { /* if the handle was not saved by the previous handle */
							hdata = initTransDataCurveHandles(td, bezt);
						}

						copy_m3_m3(td->smtx, smtx);
						copy_m3_m3(td->mtx, mtx);
						if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
							copy_m3_m3(td->axismtx, axismtx);
						}

						td++;
						tail++;
					}

					(void)hdata;  /* quiet warning */
				}
				else if (is_prop_edit && head != tail) {
					calc_distanceCurveVerts(head, tail - 1);
					head = tail;
				}
			}
			if (is_prop_edit && head != tail)
				calc_distanceCurveVerts(head, tail - 1);

			/* TODO - in the case of tilt and radius we can also avoid allocating the initTransDataCurveHandles
			 * but for now just don't change handle types */
			if (ELEM(t->mode, TFM_CURVE_SHRINKFATTEN, TFM_TILT, TFM_DUMMY) == 0) {
				/* sets the handles based on their selection, do this after the data is copied to the TransData */
				BKE_nurb_handles_test(nu, !hide_handles);
			}
		}
		else {
			TransData *head, *tail;
			head = tail = td;
			for (a = nu->pntsu * nu->pntsv, bp = nu->bp; a > 0; a--, bp++) {
				if (bp->hide == 0) {
					if (is_prop_edit || (bp->f1 & SELECT)) {
						float axismtx[3][3];

						if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
							if (nu->pntsv == 1) {
								float normal[3], plane[3];

								BKE_nurb_bpoint_calc_normal(nu, bp, normal);
								BKE_nurb_bpoint_calc_plane(nu, bp, plane);

								if (createSpaceNormalTangent(axismtx, normal, plane)) {
									/* pass */
								}
								else {
									normalize_v3(normal);
									axis_dominant_v3_to_m3(axismtx, normal);
									invert_m3(axismtx);
								}
							}
						}

						copy_v3_v3(td->iloc, bp->vec);
						td->loc = bp->vec;
						copy_v3_v3(td->center, td->loc);
						if (bp->f1 & SELECT) td->flag = TD_SELECTED;
						else td->flag = 0;
						td->ext = NULL;

						if (t->mode == TFM_CURVE_SHRINKFATTEN || t->mode == TFM_RESIZE) {
							td->val = &(bp->radius);
							td->ival = bp->radius;
						}
						else {
							td->val = &(bp->alfa);
							td->ival = bp->alfa;
						}

						copy_m3_m3(td->smtx, smtx);
						copy_m3_m3(td->mtx, mtx);
						if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
							if (nu->pntsv == 1) {
								copy_m3_m3(td->axismtx, axismtx);
							}
						}

						td++;
						tail++;
					}
				}
				else if (is_prop_edit && head != tail) {
					calc_distanceCurveVerts(head, tail - 1);
					head = tail;
				}
			}
			if (is_prop_edit && head != tail)
				calc_distanceCurveVerts(head, tail - 1);
		}
	}

#undef SEL_F1
#undef SEL_F2
#undef SEL_F3
}

/* ********************* mesh ****************** */

static bool bmesh_test_dist_add(
        BMVert *v, BMVert *v_other,
        float *dists, const float *dists_prev,
        /* optionally track original index */
        int *index, const int *index_prev,
        float mtx[3][3])
{
	if ((BM_elem_flag_test(v_other, BM_ELEM_SELECT) == 0) &&
	    (BM_elem_flag_test(v_other, BM_ELEM_HIDDEN) == 0))
	{
		const int i = BM_elem_index_get(v);
		const int i_other = BM_elem_index_get(v_other);
		float vec[3];
		float dist_other;
		sub_v3_v3v3(vec, v->co, v_other->co);
		mul_m3_v3(mtx, vec);

		dist_other = dists_prev[i] + len_v3(vec);
		if (dist_other < dists[i_other]) {
			dists[i_other] = dist_other;
			if (index != NULL) {
				index[i_other] = index_prev[i];
			}
			return true;
		}
	}

	return false;
}

/**
 * \param mtx: Measure disatnce in this space.
 * \param dists: Store the closest connected distance to selected vertices.
 * \param index: Optionally store the original index we're measuring the distance to (can be NULL).
 */
static void editmesh_set_connectivity_distance(BMesh *bm, float mtx[3][3], float *dists, int *index)
{
	BLI_LINKSTACK_DECLARE(queue, BMVert *);

	/* any BM_ELEM_TAG'd vertex is in 'queue_next', so we don't add in twice */
	BLI_LINKSTACK_DECLARE(queue_next, BMVert *);

	BLI_LINKSTACK_INIT(queue);
	BLI_LINKSTACK_INIT(queue_next);

	{
		BMIter viter;
		BMVert *v;
		int i;

		BM_ITER_MESH_INDEX (v, &viter, bm, BM_VERTS_OF_MESH, i) {
			float dist;
			BM_elem_index_set(v, i); /* set_inline */
			BM_elem_flag_disable(v, BM_ELEM_TAG);

			if (BM_elem_flag_test(v, BM_ELEM_SELECT) == 0 || BM_elem_flag_test(v, BM_ELEM_HIDDEN)) {
				dist = FLT_MAX;
				if (index != NULL) {
					index[i] = i;
				}
			}
			else {
				BLI_LINKSTACK_PUSH(queue, v);
				dist = 0.0f;
				if (index != NULL) {
					index[i] = i;
				}
			}

			dists[i] = dist;
		}
		bm->elem_index_dirty &= ~BM_VERT;
	}

	/* need to be very careful of feedback loops here, store previous dist's to avoid feedback */
	float *dists_prev = MEM_dupallocN(dists);
	int *index_prev = MEM_dupallocN(index);  /* may be NULL */

	do {
		BMVert *v;
		LinkNode *lnk;

		/* this is correct but slow to do each iteration,
		 * instead sync the dist's while clearing BM_ELEM_TAG (below) */
#if 0
		memcpy(dists_prev, dists, sizeof(float) * bm->totvert);
#endif

		while ((v = BLI_LINKSTACK_POP(queue))) {
			BLI_assert(dists[BM_elem_index_get(v)] != FLT_MAX);

			/* connected edge-verts */
			if (v->e != NULL) {
				BMEdge *e_iter, *e_first;

				e_iter = e_first = v->e;

				/* would normally use BM_EDGES_OF_VERT, but this runs so often,
				 * its faster to iterate on the data directly */
				do {

					if (BM_elem_flag_test(e_iter, BM_ELEM_HIDDEN) == 0) {

						/* edge distance */
						{
							BMVert *v_other = BM_edge_other_vert(e_iter, v);
							if (bmesh_test_dist_add(v, v_other, dists, dists_prev, index, index_prev, mtx)) {
								if (BM_elem_flag_test(v_other, BM_ELEM_TAG) == 0) {
									BM_elem_flag_enable(v_other, BM_ELEM_TAG);
									BLI_LINKSTACK_PUSH(queue_next, v_other);
								}
							}
						}

						/* face distance */
						if (e_iter->l) {
							BMLoop *l_iter_radial, *l_first_radial;
							/**
							 * imaginary edge diagonally across quad,
							 * \note, this takes advantage of the rules of winding that we
							 * know 2 or more of a verts edges wont reference the same face twice.
							 * Also, if the edge is hidden, the face will be hidden too.
							 */
							l_iter_radial = l_first_radial = e_iter->l;

							do {
								if ((l_iter_radial->v == v) &&
								    (l_iter_radial->f->len == 4) &&
								    (BM_elem_flag_test(l_iter_radial->f, BM_ELEM_HIDDEN) == 0))
								{
									BMVert *v_other = l_iter_radial->next->next->v;
									if (bmesh_test_dist_add(v, v_other, dists, dists_prev, index, index_prev, mtx)) {
										if (BM_elem_flag_test(v_other, BM_ELEM_TAG) == 0) {
											BM_elem_flag_enable(v_other, BM_ELEM_TAG);
											BLI_LINKSTACK_PUSH(queue_next, v_other);
										}
									}
								}
							} while ((l_iter_radial = l_iter_radial->radial_next) != l_first_radial);
						}
					}
				} while ((e_iter = BM_DISK_EDGE_NEXT(e_iter, v)) != e_first);
			}
		}


		/* clear for the next loop */
		for (lnk = queue_next; lnk; lnk = lnk->next) {
			BMVert *v_link = lnk->link;
			const int i = BM_elem_index_get(v_link);

			BM_elem_flag_disable(v_link, BM_ELEM_TAG);

			/* keep in sync, avoid having to do full memcpy each iteration */
			dists_prev[i] = dists[i];
			if (index != NULL) {
				index_prev[i] = index[i];
			}
		}

		BLI_LINKSTACK_SWAP(queue, queue_next);

		/* none should be tagged now since 'queue_next' is empty */
		BLI_assert(BM_iter_mesh_count_flag(BM_VERTS_OF_MESH, bm, BM_ELEM_TAG, true) == 0);

	} while (BLI_LINKSTACK_SIZE(queue));

	BLI_LINKSTACK_FREE(queue);
	BLI_LINKSTACK_FREE(queue_next);

	MEM_freeN(dists_prev);
	if (index_prev != NULL) {
		MEM_freeN(index_prev);
	}
}

static struct TransIslandData *editmesh_islands_info_calc(
        BMEditMesh *em, int *r_island_tot, int **r_island_vert_map,
        bool calc_single_islands)
{
	BMesh *bm = em->bm;
	struct TransIslandData *trans_islands;
	char htype;
	char itype;
	int i;

	/* group vars */
	int *groups_array;
	int (*group_index)[2];
	int group_tot;
	void **ele_array;

	int *vert_map;

	if (em->selectmode & (SCE_SELECT_VERTEX | SCE_SELECT_EDGE)) {
		groups_array = MEM_mallocN(sizeof(*groups_array) * bm->totedgesel, __func__);
		group_tot = BM_mesh_calc_edge_groups(bm, groups_array, &group_index,
		                                     NULL, NULL,
		                                     BM_ELEM_SELECT);

		htype = BM_EDGE;
		itype = BM_VERTS_OF_EDGE;

	}
	else {  /* (bm->selectmode & SCE_SELECT_FACE) */
		groups_array = MEM_mallocN(sizeof(*groups_array) * bm->totfacesel, __func__);
		group_tot = BM_mesh_calc_face_groups(bm, groups_array, &group_index,
		                                     NULL, NULL,
		                                     BM_ELEM_SELECT, BM_VERT);

		htype = BM_FACE;
		itype = BM_VERTS_OF_FACE;
	}


	trans_islands = MEM_mallocN(sizeof(*trans_islands) * group_tot, __func__);

	vert_map = MEM_mallocN(sizeof(*vert_map) * bm->totvert, __func__);
	/* we shouldn't need this, but with incorrect selection flushing
	 * its possible we have a selected vertex that's not in a face,
	 * for now best not crash in that case. */
	copy_vn_i(vert_map, bm->totvert, -1);

	BM_mesh_elem_table_ensure(bm, htype);
	ele_array = (htype == BM_FACE) ? (void **)bm->ftable : (void **)bm->etable;

	BM_mesh_elem_index_ensure(bm, BM_VERT);

	/* may be an edge OR a face array */
	for (i = 0; i < group_tot; i++) {
		BMEditSelection ese = {NULL};

		const int fg_sta = group_index[i][0];
		const int fg_len = group_index[i][1];
		float co[3], no[3], tangent[3];
		int j;

		zero_v3(co);
		zero_v3(no);
		zero_v3(tangent);

		ese.htype = htype;

		/* loop on each face in this group:
		 * - assign r_vert_map
		 * - calculate (co, no)
		 */
		for (j = 0; j < fg_len; j++) {
			float tmp_co[3], tmp_no[3], tmp_tangent[3];

			ese.ele = ele_array[groups_array[fg_sta + j]];

			BM_editselection_center(&ese, tmp_co);
			BM_editselection_normal(&ese, tmp_no);
			BM_editselection_plane(&ese, tmp_tangent);

			add_v3_v3(co, tmp_co);
			add_v3_v3(no, tmp_no);
			add_v3_v3(tangent, tmp_tangent);

			{
				/* setup vertex map */
				BMIter iter;
				BMVert *v;

				/* connected edge-verts */
				BM_ITER_ELEM (v, &iter, ese.ele, itype) {
					vert_map[BM_elem_index_get(v)] = i;
				}
			}
		}

		mul_v3_v3fl(trans_islands[i].co, co, 1.0f / (float)fg_len);

		if (createSpaceNormalTangent(trans_islands[i].axismtx, no, tangent)) {
			/* pass */
		}
		else {
			if (normalize_v3(no) != 0.0f) {
				axis_dominant_v3_to_m3(trans_islands[i].axismtx, no);
				invert_m3(trans_islands[i].axismtx);
			}
			else {
				unit_m3(trans_islands[i].axismtx);
			}
		}
	}

	MEM_freeN(groups_array);
	MEM_freeN(group_index);

	/* for PET we need islands of 1 so connected vertices can use it with V3D_AROUND_LOCAL_ORIGINS */
	if (calc_single_islands) {
		BMIter viter;
		BMVert *v;
		int group_tot_single = 0;

		BM_ITER_MESH_INDEX (v, &viter, bm, BM_VERTS_OF_MESH, i) {
			if (BM_elem_flag_test(v, BM_ELEM_SELECT) && (vert_map[i] == -1)) {
				group_tot_single += 1;
			}
		}

		if (group_tot_single != 0) {
			trans_islands = MEM_reallocN(trans_islands, sizeof(*trans_islands) * (group_tot + group_tot_single));

			BM_ITER_MESH_INDEX (v, &viter, bm, BM_VERTS_OF_MESH, i) {
				if (BM_elem_flag_test(v, BM_ELEM_SELECT) && (vert_map[i] == -1)) {
					struct TransIslandData *v_island = &trans_islands[group_tot];
					vert_map[i] = group_tot;

					copy_v3_v3(v_island->co, v->co);

					if (is_zero_v3(v->no) != 0.0f) {
						axis_dominant_v3_to_m3(v_island->axismtx, v->no);
						invert_m3(v_island->axismtx);
					}
					else {
						unit_m3(v_island->axismtx);
					}

					group_tot += 1;
				}
			}
		}
	}

	*r_island_tot = group_tot;
	*r_island_vert_map = vert_map;

	return trans_islands;
}

/* way to overwrite what data is edited with transform */
static void VertsToTransData(TransInfo *t, TransData *td, TransDataExtension *tx,
                             BMEditMesh *em, BMVert *eve, float *bweight,
                             struct TransIslandData *v_island, const bool no_island_center)
{
	float *no, _no[3];
	BLI_assert(BM_elem_flag_test(eve, BM_ELEM_HIDDEN) == 0);

	td->flag = 0;
	//if (key)
	//	td->loc = key->co;
	//else
	td->loc = eve->co;
	copy_v3_v3(td->iloc, td->loc);

	if ((t->mode == TFM_SHRINKFATTEN) &&
	    (em->selectmode & SCE_SELECT_FACE) &&
	    BM_elem_flag_test(eve, BM_ELEM_SELECT) &&
	    (BM_vert_calc_normal_ex(eve, BM_ELEM_SELECT, _no)))
	{
		no = _no;
	}
	else {
		no = eve->no;
	}

	if (v_island) {
		if (no_island_center) {
			copy_v3_v3(td->center, td->loc);
		}
		else {
			copy_v3_v3(td->center, v_island->co);
		}
		copy_m3_m3(td->axismtx, v_island->axismtx);
	}
	else if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
		copy_v3_v3(td->center, td->loc);
		createSpaceNormal(td->axismtx, no);
	}
	else {
		copy_v3_v3(td->center, td->loc);

		/* Setting normals */
		copy_v3_v3(td->axismtx[2], no);
		td->axismtx[0][0]        =
		    td->axismtx[0][1]    =
		    td->axismtx[0][2]    =
		    td->axismtx[1][0]    =
		    td->axismtx[1][1]    =
		    td->axismtx[1][2]    = 0.0f;
	}


	td->ext = NULL;
	td->val = NULL;
	td->extra = NULL;
	if (t->mode == TFM_BWEIGHT) {
		td->val  =  bweight;
		td->ival = *bweight;
	}
	if (t->mode == TFM_SHRINKFATTEN) {
		td->ext = tx;
		tx->isize[0] = BM_vert_calc_shell_factor_ex(eve, no, BM_ELEM_SELECT);
	}
}

static void createTransEditVerts(TransInfo *t)
{
	TransData *tob = NULL;
	TransDataExtension *tx = NULL;
	BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
	Mesh *me = t->obedit->data;
	BMesh *bm = em->bm;
	BMVert *eve;
	BMIter iter;
	float (*mappedcos)[3] = NULL, (*quats)[4] = NULL;
	float mtx[3][3], smtx[3][3], (*defmats)[3][3] = NULL, (*defcos)[3] = NULL;
	float *dists = NULL;
	int a;
	const int prop_mode = (t->flag & T_PROP_EDIT) ? (t->flag & T_PROP_EDIT_ALL) : 0;
	int mirror = 0;
	int cd_vert_bweight_offset = -1;
	bool use_topology = (me->editflag & ME_EDIT_MIRROR_TOPO) != 0;

	struct TransIslandData *island_info = NULL;
	int island_info_tot;
	int *island_vert_map = NULL;

	/* Snap rotation along normal needs a common axis for whole islands, otherwise one get random crazy results,
	 * see T59104. However, we do not want to use the island center for the pivot/translation reference... */
	const bool is_snap_rotate = ((t->mode == TFM_TRANSLATION) &&
	                             /* There is not guarantee that snapping is initialized yet at this point... */
	                             (usingSnappingNormal(t) || (t->settings->snap_flag & SCE_SNAP_ROTATE) != 0) &&
	                             (t->around != V3D_AROUND_LOCAL_ORIGINS));
	/* Even for translation this is needed because of island-orientation, see: T51651. */
	const bool is_island_center = (t->around == V3D_AROUND_LOCAL_ORIGINS) || is_snap_rotate;
	/* Original index of our connected vertex when connected distances are calculated.
	 * Optional, allocate if needed. */
	int *dists_index = NULL;

	if (t->flag & T_MIRROR) {
		EDBM_verts_mirror_cache_begin(em, 0, false, (t->flag & T_PROP_EDIT) == 0, use_topology);
		mirror = 1;
	}

	/**
	 * Quick check if we can transform.
	 *
	 * \note ignore modes here, even in edge/face modes, transform data is created by selected vertices.
	 * \note in prop mode we need at least 1 selected.
	 */
	if (bm->totvertsel == 0) {
		goto cleanup;
	}

	if (t->mode == TFM_BWEIGHT) {
		BM_mesh_cd_flag_ensure(bm, BKE_mesh_from_object(t->obedit), ME_CDFLAG_VERT_BWEIGHT);
		cd_vert_bweight_offset = CustomData_get_offset(&bm->vdata, CD_BWEIGHT);
	}

	if (prop_mode) {
		unsigned int count = 0;
		BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
			if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
				count++;
			}
		}

		t->total = count;

		/* allocating scratch arrays */
		if (prop_mode & T_PROP_CONNECTED) {
			dists = MEM_mallocN(em->bm->totvert * sizeof(float), __func__);
			if (is_island_center) {
				dists_index =  MEM_mallocN(em->bm->totvert * sizeof(int), __func__);
			}
		}
	}
	else {
		t->total = bm->totvertsel;
	}

	tob = t->data = MEM_callocN(t->total * sizeof(TransData), "TransObData(Mesh EditMode)");
	if (ELEM(t->mode, TFM_SHRINKFATTEN)) {
		/* warning, this is overkill, we only need 2 extra floats,
		 * but this stores loads of extra stuff, for TFM_SHRINKFATTEN its even more overkill
		 * since we may not use the 'alt' transform mode to maintain shell thickness,
		 * but with generic transform code its hard to lazy init vars */
		tx = t->ext = MEM_callocN(t->total * sizeof(TransDataExtension),
		                          "TransObData ext");
	}

	copy_m3_m4(mtx, t->obedit->obmat);
	/* we use a pseudo-inverse so that when one of the axes is scaled to 0,
	 * matrix inversion still works and we can still moving along the other */
	pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);

	if (prop_mode & T_PROP_CONNECTED) {
		editmesh_set_connectivity_distance(em->bm, mtx, dists, dists_index);
	}

	if (is_island_center) {
		/* In this specific case, near-by vertices will need to know the island of the nearest connected vertex. */
		const bool calc_single_islands = (
		        (prop_mode & T_PROP_CONNECTED) &&
		        (t->around == V3D_AROUND_LOCAL_ORIGINS) &&
		        (em->selectmode & SCE_SELECT_VERTEX));

		island_info = editmesh_islands_info_calc(em, &island_info_tot, &island_vert_map, calc_single_islands);
	}

	/* detect CrazySpace [tm] */
	if (modifiers_getCageIndex(t->scene, t->obedit, NULL, 1) != -1) {
		int totleft = -1;
		if (modifiers_isCorrectableDeformed(t->scene, t->obedit)) {
			/* check if we can use deform matrices for modifier from the
			 * start up to stack, they are more accurate than quats */
			totleft = BKE_crazyspace_get_first_deform_matrices_editbmesh(t->scene, t->obedit, em, &defmats, &defcos);
		}

		/* if we still have more modifiers, also do crazyspace
		 * correction with quats, relative to the coordinates after
		 * the modifiers that support deform matrices (defcos) */

#if 0	/* TODO, fix crazyspace+extrude so it can be enabled for general use - campbell */
		if ((totleft > 0) || (totleft == -1))
#else
		if (totleft > 0)
#endif
		{
			mappedcos = BKE_crazyspace_get_mapped_editverts(t->scene, t->obedit);
			quats = MEM_mallocN(em->bm->totvert * sizeof(*quats), "crazy quats");
			BKE_crazyspace_set_quats_editmesh(em, defcos, mappedcos, quats, !prop_mode);
			if (mappedcos)
				MEM_freeN(mappedcos);
		}

		if (defcos) {
			MEM_freeN(defcos);
		}
	}

	/* find out which half we do */
	if (mirror) {
		BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
			if (BM_elem_flag_test(eve, BM_ELEM_SELECT) && eve->co[0] != 0.0f) {
				if (eve->co[0] < 0.0f) {
					t->mirror = -1;
					mirror = -1;
				}
				break;
			}
		}
	}

	BM_ITER_MESH_INDEX (eve, &iter, bm, BM_VERTS_OF_MESH, a) {
		if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
			if (prop_mode || BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
				struct TransIslandData *v_island = NULL;
				float *bweight = (cd_vert_bweight_offset != -1) ? BM_ELEM_CD_GET_VOID_P(eve, cd_vert_bweight_offset) : NULL;

				if (island_info) {
					const int connected_index = (dists_index && dists_index[a] != -1) ? dists_index[a] : a;
					v_island = (island_vert_map[connected_index] != -1) ?
					           &island_info[island_vert_map[connected_index]] : NULL;
				}


				/* Do not use the island center in case we are using islands
				 * only to get axis for snap/rotate to normal... */
				VertsToTransData(t, tob, tx, em, eve, bweight, v_island, is_snap_rotate);
				if (tx)
					tx++;

				/* selected */
				if (BM_elem_flag_test(eve, BM_ELEM_SELECT))
					tob->flag |= TD_SELECTED;

				if (prop_mode) {
					if (prop_mode & T_PROP_CONNECTED) {
						tob->dist = dists[a];
					}
					else {
						tob->flag |= TD_NOTCONNECTED;
						tob->dist = FLT_MAX;
					}
				}

				/* CrazySpace */
				if (defmats || (quats && BM_elem_flag_test(eve, BM_ELEM_TAG))) {
					float mat[3][3], qmat[3][3], imat[3][3];

					/* use both or either quat and defmat correction */
					if (quats && BM_elem_flag_test(eve, BM_ELEM_TAG)) {
						quat_to_mat3(qmat, quats[BM_elem_index_get(eve)]);

						if (defmats)
							mul_m3_series(mat, defmats[a], qmat, mtx);
						else
							mul_m3_m3m3(mat, mtx, qmat);
					}
					else
						mul_m3_m3m3(mat, mtx, defmats[a]);

					invert_m3_m3(imat, mat);

					copy_m3_m3(tob->smtx, imat);
					copy_m3_m3(tob->mtx, mat);
				}
				else {
					copy_m3_m3(tob->smtx, smtx);
					copy_m3_m3(tob->mtx, mtx);
				}

				/* Mirror? */
				if ((mirror > 0 && tob->iloc[0] > 0.0f) || (mirror < 0 && tob->iloc[0] < 0.0f)) {
					BMVert *vmir = EDBM_verts_mirror_get(em, eve); //t->obedit, em, eve, tob->iloc, a);
					if (vmir && vmir != eve) {
						tob->extra = vmir;
					}
				}
				tob++;
			}
		}
	}

	if (island_info) {
		MEM_freeN(island_info);
		MEM_freeN(island_vert_map);
	}

	if (mirror != 0) {
		tob = t->data;
		for (a = 0; a < t->total; a++, tob++) {
			if (ABS(tob->loc[0]) <= 0.00001f) {
				tob->flag |= TD_MIRROR_EDGE;
			}
		}
	}

cleanup:
	/* crazy space free */
	if (quats)
		MEM_freeN(quats);
	if (defmats)
		MEM_freeN(defmats);
	if (dists)
		MEM_freeN(dists);
	if (dists_index)
		MEM_freeN(dists_index);

	if (t->flag & T_MIRROR) {
		EDBM_verts_mirror_cache_end(em);
	}
}


/* *********************** Object Transform data ******************* */

/* transcribe given object into TransData for Transforming */
static void ObjectToTransData(TransInfo *t, TransData *td, Object *ob)
{
	Scene *scene = t->scene;
	bool skip_invert = false;

	if (t->mode != TFM_DUMMY && ob->rigidbody_object) {
		float rot[3][3], scale[3];
		float ctime = 0.0f;

		/* only use rigid body transform if simulation is running,
		 * avoids problems with initial setup of rigid bodies */
		if (BKE_rigidbody_check_sim_running(scene->rigidbody_world, ctime)) {

			/* save original object transform */
			copy_v3_v3(td->ext->oloc, ob->loc);

			if (ob->rotmode > 0) {
				copy_v3_v3(td->ext->orot, ob->rot);
			}
			else if (ob->rotmode == ROT_MODE_AXISANGLE) {
				td->ext->orotAngle = ob->rotAngle;
				copy_v3_v3(td->ext->orotAxis, ob->rotAxis);
			}
			else {
				copy_qt_qt(td->ext->oquat, ob->quat);
			}
			/* update object's loc/rot to get current rigid body transform */
			mat4_to_loc_rot_size(ob->loc, rot, scale, ob->obmat);
			sub_v3_v3(ob->loc, ob->dloc);
			BKE_object_mat3_to_rot(ob, rot, false); /* drot is already corrected here */
		}
	}

	/* axismtx has the real orientation */
	copy_m3_m4(td->axismtx, ob->obmat);
	normalize_m3(td->axismtx);

	if (t->mode == TFM_DUMMY)
		skip_invert = true;

	if (skip_invert == false) {
		ob->transflag |= OB_NO_CONSTRAINTS;  /* BKE_object_where_is_calc_time checks this */
		BKE_object_where_is_calc(t->scene, ob);
		ob->transflag &= ~OB_NO_CONSTRAINTS;
	}
	else
		BKE_object_where_is_calc(t->scene, ob);

	td->ob = ob;

	td->loc = ob->loc;
	copy_v3_v3(td->iloc, td->loc);

	if (ob->rotmode > 0) {
		td->ext->rot = ob->rot;
		td->ext->rotAxis = NULL;
		td->ext->rotAngle = NULL;
		td->ext->quat = NULL;

		copy_v3_v3(td->ext->irot, ob->rot);
		copy_v3_v3(td->ext->drot, ob->drot);
	}
	else if (ob->rotmode == ROT_MODE_AXISANGLE) {
		td->ext->rot = NULL;
		td->ext->rotAxis = ob->rotAxis;
		td->ext->rotAngle = &ob->rotAngle;
		td->ext->quat = NULL;

		td->ext->irotAngle = ob->rotAngle;
		copy_v3_v3(td->ext->irotAxis, ob->rotAxis);
		// td->ext->drotAngle = ob->drotAngle;			// XXX, not implemented
		// copy_v3_v3(td->ext->drotAxis, ob->drotAxis);	// XXX, not implemented
	}
	else {
		td->ext->rot = NULL;
		td->ext->rotAxis = NULL;
		td->ext->rotAngle = NULL;
		td->ext->quat = ob->quat;

		copy_qt_qt(td->ext->iquat, ob->quat);
		copy_qt_qt(td->ext->dquat, ob->dquat);
	}
	td->ext->rotOrder = ob->rotmode;

	td->ext->size = ob->size;
	copy_v3_v3(td->ext->isize, ob->size);
	copy_v3_v3(td->ext->dscale, ob->dscale);

	copy_v3_v3(td->center, ob->obmat[3]);

	copy_m4_m4(td->ext->obmat, ob->obmat);

	/* is there a need to set the global<->data space conversion matrices? */
	if (ob->parent) {
		float obmtx[3][3], totmat[3][3], obinv[3][3];

		/* Get the effect of parenting, and/or certain constraints.
		 * NOTE: some Constraints, and also Tracking should never get this
		 *       done, as it doesn't work well.
		 */
		BKE_object_to_mat3(ob, obmtx);
		copy_m3_m4(totmat, ob->obmat);
		invert_m3_m3(obinv, totmat);
		mul_m3_m3m3(td->smtx, obmtx, obinv);
		invert_m3_m3(td->mtx, td->smtx);
	}
	else {
		/* no conversion to/from dataspace */
		unit_m3(td->smtx);
		unit_m3(td->mtx);
	}
}


/* sets flags in Bases to define whether they take part in transform */
/* it deselects Bases, so we have to call the clear function always after */
static void set_trans_object_base_flags(TransInfo *t)
{
	Main *bmain = CTX_data_main(t->context);
	Scene *scene = t->scene;
	View3D *v3d = t->view;

	/*
	 * if Base selected and has parent selected:
	 * base->flag = BA_WAS_SEL
	 */
	Base *base;

	/* don't do it if we're not actually going to recalculate anything */
	if (t->mode == TFM_DUMMY)
		return;

	/* makes sure base flags and object flags are identical */
	BKE_scene_base_flag_to_objects(t->scene);

	/* handle pending update events, otherwise they got copied below */
	for (base = scene->base.first; base; base = base->next) {
		if (base->object->id.recalc & OB_RECALC_ALL) {
			/* TODO(sergey): Ideally, it's not needed. */
			BKE_object_handle_update(bmain, t->scene, base->object);
		}
	}

	for (base = scene->base.first; base; base = base->next) {
		base->flag &= ~BA_WAS_SEL;

		if (TESTBASELIB_BGMODE(v3d, scene, base)) {
			Object *ob = base->object;
			Object *parsel = ob->parent;

			/* if parent selected, deselect */
			while (parsel) {
				if (parsel->flag & SELECT) {
					Base *parbase = BKE_scene_base_find(scene, parsel);
					if (parbase) { /* in rare cases this can fail */
						if (TESTBASELIB_BGMODE(v3d, scene, parbase)) {
							break;
						}
					}
				}
				parsel = parsel->parent;
			}

			if (parsel) {
				/* rotation around local centers are allowed to propagate */
				if ((t->around == V3D_AROUND_LOCAL_ORIGINS) &&
				    (t->mode == TFM_ROTATION || t->mode == TFM_TRACKBALL))
				{
					base->flag |= BA_TRANSFORM_CHILD;
				}
				else {
					base->flag &= ~SELECT;
					base->flag |= BA_WAS_SEL;
				}
			}
		}
	}


}

static bool mark_children(Object *ob)
{
	if (ob->flag & (SELECT | BA_TRANSFORM_CHILD))
		return true;

	if (ob->parent) {
		if (mark_children(ob->parent)) {
			ob->flag |= BA_TRANSFORM_CHILD;
			return true;
		}
	}

	return false;
}

static int count_proportional_objects(TransInfo *t)
{
	int total = 0;
	Scene *scene = t->scene;
	View3D *v3d = t->view;
	Base *base;

	/* rotations around local centers are allowed to propagate, so we take all objects */
	if (!((t->around == V3D_AROUND_LOCAL_ORIGINS) &&
	      (t->mode == TFM_ROTATION || t->mode == TFM_TRACKBALL)))
	{
		/* mark all parents */
		for (base = scene->base.first; base; base = base->next) {
			if (TESTBASELIB_BGMODE(v3d, scene, base)) {
				Object *parent = base->object->parent;

				/* flag all parents */
				while (parent) {
					parent->flag |= BA_TRANSFORM_PARENT;
					parent = parent->parent;
				}
			}
		}

		/* mark all children */
		for (base = scene->base.first; base; base = base->next) {
			/* all base not already selected or marked that is editable */
			if ((base->object->flag & (SELECT | BA_TRANSFORM_CHILD | BA_TRANSFORM_PARENT)) == 0 &&
			    (BASE_EDITABLE_BGMODE(v3d, scene, base)))
			{
				mark_children(base->object);
			}
		}
	}

	return total;
}

static void clear_trans_object_base_flags(TransInfo *t)
{
	Scene *sce = t->scene;
	Base *base;

	for (base = sce->base.first; base; base = base->next) {
		if (base->flag & BA_WAS_SEL)
			base->flag |= SELECT;

		base->flag &= ~(BA_WAS_SEL | BA_SNAP_FIX_DEPS_FIASCO | BA_TEMP_TAG | BA_TRANSFORM_CHILD | BA_TRANSFORM_PARENT);
	}
}

static void special_aftertrans_update__mesh(bContext *UNUSED(C), TransInfo *t)
{
	/* so automerge supports mirror */
	if ((t->scene->toolsettings->automerge) &&
	    (t->obedit && t->obedit->type == OB_MESH))
	{
		BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
		BMesh *bm = em->bm;
		char hflag;
		bool has_face_sel = (bm->totfacesel != 0);

		if (t->flag & T_MIRROR) {
			TransData *td;
			int i;

			/* rather then adjusting the selection (which the user would notice)
			 * tag all mirrored verts, then automerge those */
			BM_mesh_elem_hflag_disable_all(bm, BM_VERT, BM_ELEM_TAG, false);

			for (i = 0, td = t->data; i < t->total; i++, td++) {
				if (td->extra) {
					BM_elem_flag_enable((BMVert *)td->extra, BM_ELEM_TAG);
				}
			}

			hflag = BM_ELEM_SELECT | BM_ELEM_TAG;
		}
		else {
			hflag = BM_ELEM_SELECT;
		}

		EDBM_automerge(t->scene, t->obedit, true, hflag);

		/* Special case, this is needed or faces won't re-select.
		 * Flush selected edges to faces. */
		if (has_face_sel && (em->selectmode == SCE_SELECT_FACE)) {
			EDBM_selectmode_flush_ex(em, SCE_SELECT_EDGE);
		}
	}
}

/* inserting keys, pointcache, redraw events... */
/*
 * note: sequencer freeing has its own function now because of a conflict with transform's order of freeing (campbell)
 *       Order changed, the sequencer stuff should go back in here
 * */
void special_aftertrans_update(bContext *C, TransInfo *t)
{
	BLI_assert(bmain == CTX_data_main(C));

	Object *ob;
	const bool canceled = (t->state == TRANS_CANCEL);

	/* early out when nothing happened */
	if (t->total == 0 || t->mode == TFM_DUMMY)
		return;

	if (t->spacetype == SPACE_VIEW3D) {
		if (t->obedit) {
			/* Special Exception:
			 * We don't normally access 't->custom.mode' here, but its needed in this case. */

			if (canceled == 0) {
				/* we need to delete the temporary faces before automerging */
				if (t->mode == TFM_EDGE_SLIDE) {
					EdgeSlideData *sld = t->custom.mode.data;

					/* handle multires re-projection, done
					 * on transform completion since it's
					 * really slow -joeedh */
					projectEdgeSlideData(t, true);

					/* free temporary faces to avoid automerging and deleting
					 * during cleanup - psy-fi */
					freeEdgeSlideTempFaces(sld);
				}
				else if (t->mode == TFM_VERT_SLIDE) {
					/* as above */
					VertSlideData *sld = t->custom.mode.data;
					projectVertSlideData(t, true);
					freeVertSlideTempFaces(sld);
				}

				if (t->obedit->type == OB_MESH) {
					special_aftertrans_update__mesh(C, t);
				}
			}
			else {
				if (t->mode == TFM_EDGE_SLIDE) {
					EdgeSlideData *sld = t->custom.mode.data;

					sld->perc = 0.0;
					projectEdgeSlideData(t, false);
				}
				else if (t->mode == TFM_VERT_SLIDE) {
					VertSlideData *sld = t->custom.mode.data;

					sld->perc = 0.0;
					projectVertSlideData(t, false);
				}
			}
		}
	}

	if (t->obedit) {
		if (t->obedit->type == OB_MESH) {
			BMEditMesh *em = BKE_editmesh_from_object(t->obedit);
			/* table needs to be created for each edit command, since vertices can move etc */
			ED_mesh_mirror_spatial_table(t->obedit, em, NULL, NULL, 'e');
		}
	}
	else { /* Objects */
		int i;

		BLI_assert(t->flag & (T_OBJECT | T_TEXTURE));

		for (i = 0; i < t->total; i++) {
			TransData *td = t->data + i;
			ob = td->ob;

			if (td->flag & TD_NOACTION)
				break;

			if (td->flag & TD_SKIP)
				continue;


			/* restore rigid body transform */
			if (ob->rigidbody_object && canceled) {
				float ctime = 0.0f;
				if (BKE_rigidbody_check_sim_running(t->scene->rigidbody_world, ctime))
					BKE_rigidbody_aftertrans_update(ob, td->ext->oloc, td->ext->orot, td->ext->oquat, td->ext->orotAxis, td->ext->orotAngle);
			}
		}
	}

	clear_trans_object_base_flags(t);
}

int special_transform_moving(TransInfo *t)
{
	if (t->flag & (T_OBJECT | T_TEXTURE)) {
		return G_TRANSFORM_OBJ;
	}

	return 0;
}

static void createTransObject(bContext *C, TransInfo *t)
{
	Scene *scene = t->scene;

	TransData *td = NULL;
	TransDataExtension *tx;
	const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;

	set_trans_object_base_flags(t);

	/* count */
	t->total = CTX_DATA_COUNT(C, selected_objects);

	if (!t->total) {
		/* clear here, main transform function escapes too */
		clear_trans_object_base_flags(t);
		return;
	}

	if (is_prop_edit) {
		t->total += count_proportional_objects(t);
	}

	td = t->data = MEM_callocN(t->total * sizeof(TransData), "TransOb");
	tx = t->ext = MEM_callocN(t->total * sizeof(TransDataExtension), "TransObExtension");

	CTX_DATA_BEGIN(C, Base *, base, selected_bases)
	{
		Object *ob = base->object;

		td->flag = TD_SELECTED;
		td->protectflag = ob->protectflag;
		td->ext = tx;
		td->ext->rotOrder = ob->rotmode;

		if (base->flag & BA_TRANSFORM_CHILD) {
			td->flag |= TD_NOCENTER;
			td->flag |= TD_NO_LOC;
		}

		/* select linked objects, but skip them later */
		if (ID_IS_LINKED(ob)) {
			td->flag |= TD_SKIP;
		}

		ObjectToTransData(t, td, ob);
		td->val = NULL;
		td++;
		tx++;
	}
	CTX_DATA_END;

	if (is_prop_edit) {
		View3D *v3d = t->view;
		Base *base;

		for (base = scene->base.first; base; base = base->next) {
			Object *ob = base->object;

			/* if base is not selected, not a parent of selection or not a child of selection and it is editable */
			if ((ob->flag & (SELECT | BA_TRANSFORM_CHILD | BA_TRANSFORM_PARENT)) == 0 &&
			    BASE_EDITABLE_BGMODE(v3d, scene, base))
			{
				td->protectflag = ob->protectflag;
				td->ext = tx;
				td->ext->rotOrder = ob->rotmode;

				ObjectToTransData(t, td, ob);
				td->val = NULL;
				td++;
				tx++;
			}
		}
	}
}

#define PC_IS_ANY_SEL(pc) (((pc)->bez.f1 | (pc)->bez.f2 | (pc)->bez.f3) & SELECT)

void createTransData(bContext *C, TransInfo *t)
{
	/* if tests must match recalcData for correct updates */
	if (t->options & CTX_TEXTURE) {
		t->flag |= T_TEXTURE;
		createTransTexspace(t);
	}
	else if (t->options & CTX_EDGE) {
		t->ext = NULL;
		t->flag |= T_EDIT;
		createTransEdge(t);
		if (t->data && t->flag & T_PROP_EDIT) {
			sort_trans_data(t); // makes selected become first in array
			set_prop_dist(t, 1);
			sort_trans_data_dist(t);
		}
	}
	else if (t->spacetype == SPACE_IMAGE) {
		t->flag |= T_POINTS | T_2D_EDIT;
		if (t->obedit) {
			if (t->data && (t->flag & T_PROP_EDIT)) {
				sort_trans_data(t); // makes selected become first in array
				set_prop_dist(t, 1);
				sort_trans_data_dist(t);
			}
		}
	}
	else if (t->obedit) {
		t->ext = NULL;
		if (t->obedit->type == OB_MESH) {
			createTransEditVerts(t);
		}
		else if (ELEM(t->obedit->type, OB_CURVE, OB_SURF)) {
			createTransCurveVerts(t);
		}
		else {
			printf("edit type not implemented!\n");
		}

		t->flag |= T_EDIT | T_POINTS;

		if (t->data && t->flag & T_PROP_EDIT) {
			if (ELEM(t->obedit->type, OB_CURVE, OB_MESH)) {
				sort_trans_data(t); // makes selected become first in array
				if ((t->obedit->type == OB_MESH) && (t->flag & T_PROP_CONNECTED)) {
					/* already calculated by editmesh_set_connectivity_distance */
				}
				else {
					set_prop_dist(t, 0);
				}
				sort_trans_data_dist(t);
			}
			else {
				sort_trans_data(t); // makes selected become first in array
				set_prop_dist(t, 1);
				sort_trans_data_dist(t);
			}
		}

	}
	else {
		createTransObject(C, t);
		t->flag |= T_OBJECT;

		if (t->data && t->flag & T_PROP_EDIT) {
			// selected objects are already first, no need to presort
			set_prop_dist(t, 1);
			sort_trans_data_dist(t);
		}

		/* Check if we're transforming the camera from the camera */
		if ((t->spacetype == SPACE_VIEW3D) && (t->ar->regiontype == RGN_TYPE_WINDOW)) {
			View3D *v3d = t->view;
			RegionView3D *rv3d = t->ar->regiondata;
			if ((rv3d->persp == RV3D_CAMOB) && v3d->camera) {
				/* we could have a flag to easily check an object is being transformed */
				if (v3d->camera->id.recalc) {
					t->flag |= T_CAMERA;
				}
			}
		}
	}
}
