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

/** \file blender/modifiers/intern/MOD_curve.c
 *  \ingroup modifiers
 */

#include <string.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"

#include "BLI_utildefines.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_lattice.h"
#include "BKE_library_query.h"
#include "BKE_modifier.h"

#include "MOD_modifiertypes.h"

static void initData(ModifierData *md)
{
	CurveModifierData *cmd = (CurveModifierData *) md;

	cmd->defaxis = MOD_CURVE_POSX;
}

static CustomDataMask requiredDataMask(Object *UNUSED(ob), ModifierData *md)
{
	CurveModifierData *cmd = (CurveModifierData *)md;
	CustomDataMask dataMask = 0;

	/* ask for vertexgroups if we need them */
	if (cmd->name[0]) dataMask |= CD_MASK_MDEFORMVERT;

	return dataMask;
}

static bool isDisabled(ModifierData *md, int UNUSED(userRenderParams))
{
	CurveModifierData *cmd = (CurveModifierData *) md;

	return !cmd->object;
}

static void foreachObjectLink(
        ModifierData *md, Object *ob,
        ObjectWalkFunc walk, void *userData)
{
	CurveModifierData *cmd = (CurveModifierData *) md;

	walk(userData, ob, &cmd->object, IDWALK_CB_NOP);
}

static void deformVerts(
        ModifierData *md, Object *ob,
        DerivedMesh *derivedData,
        float (*vertexCos)[3],
        int numVerts,
        ModifierApplyFlag UNUSED(flag))
{
	CurveModifierData *cmd = (CurveModifierData *) md;

	/* silly that defaxis and curve_deform_verts are off by 1
	 * but leave for now to save having to call do_versions */
	curve_deform_verts(md->scene, cmd->object, ob, derivedData, vertexCos, numVerts,
	                   cmd->name, cmd->defaxis - 1);
}

static void deformVertsEM(
        ModifierData *md, Object *ob, struct BMEditMesh *em,
        DerivedMesh *derivedData, float (*vertexCos)[3], int numVerts)
{
	DerivedMesh *dm = derivedData;

	if (!derivedData) dm = CDDM_from_editbmesh(em, false, false);

	deformVerts(md, ob, dm, vertexCos, numVerts, 0);

	if (!derivedData) dm->release(dm);
}


ModifierTypeInfo modifierType_Curve = {
	/* name */              "Curve",
	/* structName */        "CurveModifierData",
	/* structSize */        sizeof(CurveModifierData),
	/* type */              eModifierTypeType_OnlyDeform,
	/* flags */             eModifierTypeFlag_AcceptsCVs |
	                        eModifierTypeFlag_AcceptsLattice |
	                        eModifierTypeFlag_SupportsEditmode,

	/* copyData */          modifier_copyData_generic,
	/* deformVerts */       deformVerts,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     deformVertsEM,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  requiredDataMask,
	/* freeData */          NULL,
	/* isDisabled */        isDisabled,
	/* updateDepgraph */    NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */  NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
