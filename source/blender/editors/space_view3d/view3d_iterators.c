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
 */

/** \file blender/editors/space_view3d/view3d_iterators.c
 *  \ingroup spview3d
 */

#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_utildefines.h"
#include "BLI_rect.h"

#include "BKE_curve.h"
#include "BKE_DerivedMesh.h"
#include "BKE_displist.h"
#include "BKE_editmesh.h"

#include "bmesh.h"

#include "ED_screen.h"
#include "ED_view3d.h"

typedef struct foreachScreenObjectVert_userData {
	void (*func)(void *userData, MVert *mv, const float screen_co_b[2], int index);
	void *userData;
	ViewContext vc;
	eV3DProjTest clip_flag;
} foreachScreenObjectVert_userData;

typedef struct foreachScreenVert_userData {
	void (*func)(void *userData, BMVert *eve, const float screen_co_b[2], int index);
	void *userData;
	ViewContext vc;
	eV3DProjTest clip_flag;
} foreachScreenVert_userData;

/* user data structures for derived mesh callbacks */
typedef struct foreachScreenEdge_userData {
	void (*func)(void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index);
	void *userData;
	ViewContext vc;
	rctf win_rect; /* copy of: vc.ar->winx/winy, use for faster tests, minx/y will always be 0 */
	eV3DProjTest clip_flag;
} foreachScreenEdge_userData;

typedef struct foreachScreenFace_userData {
	void (*func)(void *userData, BMFace *efa, const float screen_co_b[2], int index);
	void *userData;
	ViewContext vc;
	eV3DProjTest clip_flag;
} foreachScreenFace_userData;


/* Note! - foreach funcs should be called while drawing or directly after
 * if not, ED_view3d_init_mats_rv3d() can be used for selection tools
 * but would not give correct results with dupli's for eg. which don't
 * use the object matrix in the usual way */

/* ------------------------------------------------------------------------ */


static void meshobject_foreachScreenVert__mapFunc(void *userData, int index, const float co[3],
                                            const float UNUSED(no_f[3]), const short UNUSED(no_s[3]))
{
	foreachScreenObjectVert_userData *data = userData;
	struct MVert *mv = &((Mesh *)(data->vc.obact->data))->mvert[index];

	if (!(mv->flag & ME_HIDE)) {
		float screen_co[2];

		if (ED_view3d_project_float_object(data->vc.ar, co, screen_co, data->clip_flag) != V3D_PROJ_RET_OK) {
			return;
		}

		data->func(data->userData, mv, screen_co, index);
	}
}

void meshobject_foreachScreenVert(
        ViewContext *vc,
        void (*func)(void *userData, MVert *eve, const float screen_co[2], int index),
        void *userData, eV3DProjTest clip_flag)
{
	foreachScreenObjectVert_userData data;
	DerivedMesh *dm = mesh_get_derived_deform(vc->scene, vc->obact, CD_MASK_BAREMESH);

	ED_view3d_check_mats_rv3d(vc->rv3d);

	data.vc = *vc;
	data.func = func;
	data.userData = userData;
	data.clip_flag = clip_flag;

	if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
		ED_view3d_clipping_local(vc->rv3d, vc->obact->obmat);
	}

	dm->foreachMappedVert(dm, meshobject_foreachScreenVert__mapFunc, &data, DM_FOREACH_NOP);

	dm->release(dm);
}

static void mesh_foreachScreenVert__mapFunc(void *userData, int index, const float co[3],
                                            const float UNUSED(no_f[3]), const short UNUSED(no_s[3]))
{
	foreachScreenVert_userData *data = userData;
	BMVert *eve = BM_vert_at_index(data->vc.em->bm, index);

	if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
		float screen_co[2];

		if (ED_view3d_project_float_object(data->vc.ar, co, screen_co, data->clip_flag) != V3D_PROJ_RET_OK) {
			return;
		}

		data->func(data->userData, eve, screen_co, index);
	}
}

void mesh_foreachScreenVert(
        ViewContext *vc,
        void (*func)(void *userData, BMVert *eve, const float screen_co[2], int index),
        void *userData, eV3DProjTest clip_flag)
{
	foreachScreenVert_userData data;
	DerivedMesh *dm = editbmesh_get_derived_cage(vc->scene, vc->obedit, vc->em, CD_MASK_BAREMESH);

	ED_view3d_check_mats_rv3d(vc->rv3d);

	data.vc = *vc;
	data.func = func;
	data.userData = userData;
	data.clip_flag = clip_flag;

	if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
		ED_view3d_clipping_local(vc->rv3d, vc->obedit->obmat);  /* for local clipping lookups */
	}

	BM_mesh_elem_table_ensure(vc->em->bm, BM_VERT);
	dm->foreachMappedVert(dm, mesh_foreachScreenVert__mapFunc, &data, DM_FOREACH_NOP);

	dm->release(dm);
}

/* ------------------------------------------------------------------------ */

static void mesh_foreachScreenEdge__mapFunc(void *userData, int index, const float v0co[3], const float v1co[3])
{
	foreachScreenEdge_userData *data = userData;
	BMEdge *eed = BM_edge_at_index(data->vc.em->bm, index);

	if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
		float screen_co_a[2];
		float screen_co_b[2];
		eV3DProjTest clip_flag_nowin = data->clip_flag &= ~V3D_PROJ_TEST_CLIP_WIN;

		if (ED_view3d_project_float_object(data->vc.ar, v0co, screen_co_a, clip_flag_nowin) != V3D_PROJ_RET_OK) {
			return;
		}
		if (ED_view3d_project_float_object(data->vc.ar, v1co, screen_co_b, clip_flag_nowin) != V3D_PROJ_RET_OK) {
			return;
		}

		if (data->clip_flag & V3D_PROJ_TEST_CLIP_WIN) {
			if (!BLI_rctf_isect_segment(&data->win_rect, screen_co_a, screen_co_b)) {
				return;
			}
		}

		data->func(data->userData, eed, screen_co_a, screen_co_b, index);
	}
}

void mesh_foreachScreenEdge(
        ViewContext *vc,
        void (*func)(void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index),
        void *userData, eV3DProjTest clip_flag)
{
	foreachScreenEdge_userData data;
	DerivedMesh *dm = editbmesh_get_derived_cage(vc->scene, vc->obedit, vc->em, CD_MASK_BAREMESH);

	ED_view3d_check_mats_rv3d(vc->rv3d);

	data.vc = *vc;

	data.win_rect.xmin = 0;
	data.win_rect.ymin = 0;
	data.win_rect.xmax = vc->ar->winx;
	data.win_rect.ymax = vc->ar->winy;

	data.func = func;
	data.userData = userData;
	data.clip_flag = clip_flag;

	if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
		ED_view3d_clipping_local(vc->rv3d, vc->obedit->obmat);  /* for local clipping lookups */
	}

	BM_mesh_elem_table_ensure(vc->em->bm, BM_EDGE);
	dm->foreachMappedEdge(dm, mesh_foreachScreenEdge__mapFunc, &data);

	dm->release(dm);
}

/* ------------------------------------------------------------------------ */

static void mesh_foreachScreenFace__mapFunc(void *userData, int index, const float cent[3], const float UNUSED(no[3]))
{
	foreachScreenFace_userData *data = userData;
	BMFace *efa = BM_face_at_index(data->vc.em->bm, index);

	if (!BM_elem_flag_test(efa, BM_ELEM_HIDDEN)) {
		float screen_co[2];
		if (ED_view3d_project_float_object(data->vc.ar, cent, screen_co, data->clip_flag) == V3D_PROJ_RET_OK) {
			data->func(data->userData, efa, screen_co, index);
		}
	}
}

void mesh_foreachScreenFace(
        ViewContext *vc,
        void (*func)(void *userData, BMFace *efa, const float screen_co_b[2], int index),
        void *userData, const eV3DProjTest clip_flag)
{
	foreachScreenFace_userData data;
	DerivedMesh *dm = editbmesh_get_derived_cage(vc->scene, vc->obedit, vc->em, CD_MASK_BAREMESH);

	ED_view3d_check_mats_rv3d(vc->rv3d);

	data.vc = *vc;
	data.func = func;
	data.userData = userData;
	data.clip_flag = clip_flag;

	BM_mesh_elem_table_ensure(vc->em->bm, BM_FACE);
	dm->foreachMappedFaceCenter(dm, mesh_foreachScreenFace__mapFunc, &data, DM_FOREACH_NOP);

	dm->release(dm);
}

/* ------------------------------------------------------------------------ */

void nurbs_foreachScreenVert(
        ViewContext *vc,
        void (*func)(void *userData, Nurb *nu, BPoint *bp, BezTriple *bezt, int beztindex, const float screen_co_b[2]),
        void *userData, const eV3DProjTest clip_flag)
{
	Curve *cu = vc->obedit->data;
	Nurb *nu;
	int i;
	ListBase *nurbs = BKE_curve_editNurbs_get(cu);

	ED_view3d_check_mats_rv3d(vc->rv3d);

	if (clip_flag & V3D_PROJ_TEST_CLIP_BB) {
		ED_view3d_clipping_local(vc->rv3d, vc->obedit->obmat); /* for local clipping lookups */
	}

	for (nu = nurbs->first; nu; nu = nu->next) {
		if (nu->type == CU_BEZIER) {
			for (i = 0; i < nu->pntsu; i++) {
				BezTriple *bezt = &nu->bezt[i];

				if (bezt->hide == 0) {
					float screen_co[2];

					if (cu->drawflag & CU_HIDE_HANDLES) {
						if (ED_view3d_project_float_object(vc->ar, bezt->vec[1], screen_co,
						                                   V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) == V3D_PROJ_RET_OK)
						{
							func(userData, nu, NULL, bezt, 1, screen_co);
						}
					}
					else {
						if (ED_view3d_project_float_object(vc->ar, bezt->vec[0], screen_co,
						                                   V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) == V3D_PROJ_RET_OK)
						{
							func(userData, nu, NULL, bezt, 0, screen_co);
						}
						if (ED_view3d_project_float_object(vc->ar, bezt->vec[1], screen_co,
						                                   V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) == V3D_PROJ_RET_OK)
						{
							func(userData, nu, NULL, bezt, 1, screen_co);
						}
						if (ED_view3d_project_float_object(vc->ar, bezt->vec[2], screen_co,
						                                   V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) == V3D_PROJ_RET_OK)
						{
							func(userData, nu, NULL, bezt, 2, screen_co);
						}
					}
				}
			}
		}
		else {
			for (i = 0; i < nu->pntsu * nu->pntsv; i++) {
				BPoint *bp = &nu->bp[i];

				if (bp->hide == 0) {
					float screen_co[2];
					if (ED_view3d_project_float_object(vc->ar, bp->vec, screen_co,
					                                   V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) == V3D_PROJ_RET_OK)
					{
						func(userData, nu, bp, NULL, -1, screen_co);
					}
				}
			}
		}
	}
}
