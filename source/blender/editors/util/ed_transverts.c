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

/** \file blender/editors/util/ed_transverts.c
 *  \ingroup edutil
 */

#include "MEM_guardedalloc.h"

#include "DNA_curve_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "BKE_curve.h"
#include "BKE_editmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_context.h"

#include "ED_transverts.h"  /* own include */


/* copied from editobject.c, now uses (almost) proper depgraph */
void ED_transverts_update_obedit(TransVertStore *tvs, Object *obedit)
{
	const int mode = tvs->mode;
	BLI_assert(ED_transverts_check_obedit(obedit) == true);

	if (obedit->type == OB_MESH) {
		BMEditMesh *em = BKE_editmesh_from_object(obedit);
		BM_mesh_normals_update(em->bm);
	}
	else if (ELEM(obedit->type, OB_CURVE, OB_SURF)) {
		Curve *cu = obedit->data;
		ListBase *nurbs = BKE_curve_editNurbs_get(cu);
		Nurb *nu = nurbs->first;

		while (nu) {
			/* keep handles' vectors unchanged */
			if (nu->bezt && (mode & TM_SKIP_HANDLES)) {
				int a = nu->pntsu;
				TransVert *tv = tvs->transverts;
				BezTriple *bezt = nu->bezt;

				while (a--) {
					if (bezt->hide == 0) {
						bool skip_handle = false;
						if (bezt->f2 & SELECT)
							skip_handle = (mode & TM_SKIP_HANDLES) != 0;

						if ((bezt->f1 & SELECT) && !skip_handle) {
							BLI_assert(tv->loc == bezt->vec[0]);
							tv++;
						}

						if (bezt->f2 & SELECT) {
							float v[3];

							if (((bezt->f1 & SELECT) && !skip_handle) == 0) {
								sub_v3_v3v3(v, tv->loc, tv->oldloc);
								add_v3_v3(bezt->vec[0], v);
							}

							if (((bezt->f3 & SELECT) && !skip_handle) == 0) {
								sub_v3_v3v3(v, tv->loc, tv->oldloc);
								add_v3_v3(bezt->vec[2], v);
							}

							BLI_assert(tv->loc == bezt->vec[1]);
							tv++;
						}

						if ((bezt->f3 & SELECT) && !skip_handle) {
							BLI_assert(tv->loc == bezt->vec[2]);
							tv++;
						}
					}

					bezt++;
				}
			}

			BKE_nurb_test_2d(nu);
			BKE_nurb_handles_test(nu, true); /* test for bezier too */
			nu = nu->next;
		}
	}
}

static void set_mapped_co(void *vuserdata, int index, const float co[3],
                          const float UNUSED(no[3]), const short UNUSED(no_s[3]))
{
	void **userdata = vuserdata;
	BMEditMesh *em = userdata[0];
	TransVert *tv = userdata[1];
	BMVert *eve = BM_vert_at_index(em->bm, index);

	if (BM_elem_index_get(eve) != TM_INDEX_SKIP) {
		tv = &tv[BM_elem_index_get(eve)];

		/* be clever, get the closest vertex to the original,
		 * behaves most logically when the mirror modifier is used for eg [#33051]*/
		if ((tv->flag & TX_VERT_USE_MAPLOC) == 0) {
			/* first time */
			copy_v3_v3(tv->maploc, co);
			tv->flag |= TX_VERT_USE_MAPLOC;
		}
		else {
			/* find best location to use */
			if (len_squared_v3v3(eve->co, co) < len_squared_v3v3(eve->co, tv->maploc)) {
				copy_v3_v3(tv->maploc, co);
			}
		}
	}
}

bool ED_transverts_check_obedit(Object *obedit)
{
	return (ELEM(obedit->type, OB_MESH, OB_SURF, OB_CURVE));
}

void ED_transverts_create_from_obedit(TransVertStore *tvs, Object *obedit, const int mode)
{
	Nurb *nu;
	BezTriple *bezt;
	BPoint *bp;
	TransVert *tv = NULL;
	BMVert *eve;
	int a;

	tvs->transverts_tot = 0;

	if (obedit->type == OB_MESH) {
		BMEditMesh *em = BKE_editmesh_from_object(obedit);
		BMesh *bm = em->bm;
		BMIter iter;
		void *userdata[2] = {em, NULL};
		/*int proptrans = 0; */ /*UNUSED*/

		/* abuses vertex index all over, set, just set dirty here,
		 * perhaps this could use its own array instead? - campbell */

		/* transform now requires awareness for select mode, so we tag the f1 flags in verts */
		tvs->transverts_tot = 0;
		if (em->selectmode & SCE_SELECT_VERTEX) {
			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN) && BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
					BM_elem_index_set(eve, TM_INDEX_ON); /* set_dirty! */
					tvs->transverts_tot++;
				}
				else {
					BM_elem_index_set(eve, TM_INDEX_OFF);  /* set_dirty! */
				}
			}
		}
		else if (em->selectmode & SCE_SELECT_EDGE) {
			BMEdge *eed;

			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				BM_elem_index_set(eve, TM_INDEX_OFF);  /* set_dirty! */
			}

			BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
				if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN) && BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
					BM_elem_index_set(eed->v1, TM_INDEX_ON);  /* set_dirty! */
					BM_elem_index_set(eed->v2, TM_INDEX_ON);  /* set_dirty! */
				}
			}

			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				if (BM_elem_index_get(eve) == TM_INDEX_ON) tvs->transverts_tot++;
			}
		}
		else {
			BMFace *efa;

			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				BM_elem_index_set(eve, TM_INDEX_OFF);  /* set_dirty! */
			}

			BM_ITER_MESH (efa, &iter, bm, BM_FACES_OF_MESH) {
				if (!BM_elem_flag_test(efa, BM_ELEM_HIDDEN) && BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
					BMIter liter;
					BMLoop *l;

					BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
						BM_elem_index_set(l->v, TM_INDEX_ON); /* set_dirty! */
					}
				}
			}

			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				if (BM_elem_index_get(eve) == TM_INDEX_ON) tvs->transverts_tot++;
			}
		}
		/* for any of the 3 loops above which all dirty the indices */
		bm->elem_index_dirty |= BM_VERT;

		/* and now make transverts */
		if (tvs->transverts_tot) {
			tv = tvs->transverts = MEM_callocN(tvs->transverts_tot * sizeof(TransVert), __func__);

			a = 0;
			BM_ITER_MESH (eve, &iter, bm, BM_VERTS_OF_MESH) {
				if (BM_elem_index_get(eve)) {
					BM_elem_index_set(eve, a);  /* set_dirty! */
					copy_v3_v3(tv->oldloc, eve->co);
					tv->loc = eve->co;
					tv->flag = (BM_elem_index_get(eve) == TM_INDEX_ON) ? SELECT : 0;

					if (mode & TM_CALC_NORMALS) {
						tv->flag |= TX_VERT_USE_NORMAL;
						copy_v3_v3(tv->normal, eve->no);
					}

					tv++;
					a++;
				}
				else {
					BM_elem_index_set(eve, TM_INDEX_SKIP);  /* set_dirty! */
				}
			}
			/* set dirty already, above */

			userdata[1] = tvs->transverts;
		}

		if (tvs->transverts && em->derivedCage) {
			BM_mesh_elem_table_ensure(bm, BM_VERT);
			em->derivedCage->foreachMappedVert(em->derivedCage, set_mapped_co, userdata, DM_FOREACH_NOP);
		}
	}
	else if (ELEM(obedit->type, OB_CURVE, OB_SURF)) {
		Curve *cu = obedit->data;
		int totmalloc = 0;
		ListBase *nurbs = BKE_curve_editNurbs_get(cu);

		for (nu = nurbs->first; nu; nu = nu->next) {
			if (nu->type == CU_BEZIER)
				totmalloc += 3 * nu->pntsu;
			else
				totmalloc += nu->pntsu * nu->pntsv;
		}
		tv = tvs->transverts = MEM_callocN(totmalloc * sizeof(TransVert), __func__);

		nu = nurbs->first;
		while (nu) {
			if (nu->type == CU_BEZIER) {
				a = nu->pntsu;
				bezt = nu->bezt;
				while (a--) {
					if (bezt->hide == 0) {
						bool skip_handle = false;
						if (bezt->f2 & SELECT)
							skip_handle = (mode & TM_SKIP_HANDLES) != 0;

						if ((bezt->f1 & SELECT) && !skip_handle) {
							copy_v3_v3(tv->oldloc, bezt->vec[0]);
							tv->loc = bezt->vec[0];
							tv->flag = bezt->f1 & SELECT;

							if (mode & TM_CALC_NORMALS) {
								tv->flag |= TX_VERT_USE_NORMAL;
								BKE_nurb_bezt_calc_plane(nu, bezt, tv->normal);
							}

							tv++;
							tvs->transverts_tot++;
						}
						if (bezt->f2 & SELECT) {
							copy_v3_v3(tv->oldloc, bezt->vec[1]);
							tv->loc = bezt->vec[1];
							tv->flag = bezt->f2 & SELECT;

							if (mode & TM_CALC_NORMALS) {
								tv->flag |= TX_VERT_USE_NORMAL;
								BKE_nurb_bezt_calc_plane(nu, bezt, tv->normal);
							}

							tv++;
							tvs->transverts_tot++;
						}
						if ((bezt->f3 & SELECT) && !skip_handle) {
							copy_v3_v3(tv->oldloc, bezt->vec[2]);
							tv->loc = bezt->vec[2];
							tv->flag = bezt->f3 & SELECT;

							if (mode & TM_CALC_NORMALS) {
								tv->flag |= TX_VERT_USE_NORMAL;
								BKE_nurb_bezt_calc_plane(nu, bezt, tv->normal);
							}

							tv++;
							tvs->transverts_tot++;
						}
					}
					bezt++;
				}
			}
			else {
				a = nu->pntsu * nu->pntsv;
				bp = nu->bp;
				while (a--) {
					if (bp->hide == 0) {
						if (bp->f1 & SELECT) {
							copy_v3_v3(tv->oldloc, bp->vec);
							tv->loc = bp->vec;
							tv->flag = bp->f1 & SELECT;
							tv++;
							tvs->transverts_tot++;
						}
					}
					bp++;
				}
			}
			nu = nu->next;
		}
	}

	if (!tvs->transverts_tot && tvs->transverts) {
		/* prevent memory leak. happens for curves due to */
		/* difficult condition of adding points to trans data */
		MEM_freeN(tvs->transverts);
		tvs->transverts = NULL;
	}

	tvs->mode = mode;
}

void ED_transverts_free(TransVertStore *tvs)
{
	MEM_SAFE_FREE(tvs->transverts);
	tvs->transverts_tot = 0;
}

bool ED_transverts_poll(bContext *C)
{
	Object *obedit = CTX_data_edit_object(C);
	if (obedit) {
		if (ED_transverts_check_obedit(obedit)) {
			return true;
		}
	}
	return false;
}
