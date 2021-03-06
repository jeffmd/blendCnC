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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 by the Blender Foundation.
 * All rights reserved.
 */

/** \file blender/modifiers/intern/MOD_surface.c
 *  \ingroup modifiers
 */


#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"


#include "BKE_cdderivedmesh.h"

#include "MOD_modifiertypes.h"
#include "MOD_util.h"

#include "MEM_guardedalloc.h"


static void initData(ModifierData *md)
{
	SurfaceModifierData *surmd = (SurfaceModifierData *) md;

	surmd->bvhtree = NULL;
}

static void freeData(ModifierData *md)
{
	SurfaceModifierData *surmd = (SurfaceModifierData *) md;

	if (surmd) {
		if (surmd->bvhtree) {
			free_bvhtree_from_mesh(surmd->bvhtree);
			MEM_SAFE_FREE(surmd->bvhtree);
		}

		if (surmd->dm) {
			surmd->dm->release(surmd->dm);
			surmd->dm = NULL;
		}

		MEM_SAFE_FREE(surmd->x);

		MEM_SAFE_FREE(surmd->v);
	}
}

static bool dependsOnTime(ModifierData *UNUSED(md))
{
	return true;
}

static void deformVerts(
        ModifierData *md, Object *ob,
        DerivedMesh *derivedData,
        float (*vertexCos)[3],
        int UNUSED(numVerts),
        ModifierApplyFlag UNUSED(flag))
{
	SurfaceModifierData *surmd = (SurfaceModifierData *) md;

	if (surmd->dm)
		surmd->dm->release(surmd->dm);

	/* if possible use/create DerivedMesh */
	if (derivedData) surmd->dm = CDDM_copy(derivedData);
	else surmd->dm = get_dm(ob, NULL, NULL, NULL, false, false);

	if (surmd->dm) {
		unsigned int numverts = 0, i = 0;
		int init = 0;
		float *vec;
		MVert *x, *v;

		CDDM_apply_vert_coords(surmd->dm, vertexCos);
		CDDM_calc_normals(surmd->dm);

		numverts = surmd->dm->getNumVerts(surmd->dm);

		if (numverts != surmd->numverts ||
		    surmd->x == NULL ||
		    surmd->v == NULL )
		{
			if (surmd->x) {
				MEM_freeN(surmd->x);
				surmd->x = NULL;
			}
			if (surmd->v) {
				MEM_freeN(surmd->v);
				surmd->v = NULL;
			}

			surmd->x = MEM_calloc_arrayN(numverts, sizeof(MVert), "MVert");
			surmd->v = MEM_calloc_arrayN(numverts, sizeof(MVert), "MVert");

			surmd->numverts = numverts;

			init = 1;
		}

		/* convert to global coordinates and calculate velocity */
		for (i = 0, x = surmd->x, v = surmd->v; i < numverts; i++, x++, v++) {
			vec = CDDM_get_vert(surmd->dm, i)->co;
			mul_m4_v3(ob->obmat, vec);

			if (init)
				v->co[0] = v->co[1] = v->co[2] = 0.0f;
			else
				sub_v3_v3v3(v->co, vec, x->co);

			copy_v3_v3(x->co, vec);
		}

		if (surmd->bvhtree)
			free_bvhtree_from_mesh(surmd->bvhtree);
		else
			surmd->bvhtree = MEM_callocN(sizeof(BVHTreeFromMesh), "BVHTreeFromMesh");

		if (surmd->dm->getNumPolys(surmd->dm))
			bvhtree_from_mesh_get(surmd->bvhtree, surmd->dm, BVHTREE_FROM_LOOPTRI, 2);
		else
			bvhtree_from_mesh_get(surmd->bvhtree, surmd->dm, BVHTREE_FROM_EDGES, 2);
	}
}


ModifierTypeInfo modifierType_Surface = {
	/* name */              "Surface",
	/* structName */        "SurfaceModifierData",
	/* structSize */        sizeof(SurfaceModifierData),
	/* type */              eModifierTypeType_OnlyDeform,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_AcceptsCVs |
	                        eModifierTypeFlag_NoUserAdd,

	/* copyData */          NULL,
	/* deformVerts */       deformVerts,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        NULL,
	/* updateDepgraph */    NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     dependsOnTime,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
