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
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/makesrna/intern/rna_scene_api.c
 *  \ingroup RNA
 */


#include <stdlib.h>
#include <stdio.h>

#include "BLI_utildefines.h"
#include "BLI_kdopbvh.h"
#include "BLI_path_util.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "rna_internal.h"  /* own include */

const EnumPropertyItem rna_enum_abc_compression_items[] = {
#ifdef WITH_ALEMBIC
	{ ABC_ARCHIVE_OGAWA, "OGAWA", 0, "Ogawa", "" },
	{ ABC_ARCHIVE_HDF5, "HDF5", 0, "HDF5", "" },
#endif
	{ 0, NULL, 0, NULL, NULL }
};

#ifdef RNA_RUNTIME

#include "BKE_editmesh.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_scene.h"

#include "ED_transform.h"
#include "ED_transform_snap_object_context.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

static void rna_Scene_update_tagged(Scene *scene, Main *bmain)
{
#ifdef WITH_PYTHON
	BPy_BEGIN_ALLOW_THREADS;
#endif

	BKE_scene_update_tagged(bmain, scene);

#ifdef WITH_PYTHON
	BPy_END_ALLOW_THREADS;
#endif
}

static void rna_Scene_ray_cast(
        Scene *scene, Main *bmain,
        float origin[3], float direction[3], float ray_dist,
        bool *r_success, float r_location[3], float r_normal[3], int *r_index,
        Object **r_ob, float r_obmat[16])
{
	normalize_v3(direction);

	SnapObjectContext *sctx = ED_transform_snap_object_context_create(bmain, scene, 0);

	bool ret = ED_transform_snap_object_project_ray_ex(
	        sctx,
	        &(const struct SnapObjectParams){
	            .snap_select = SNAP_ALL,
	        },
	        origin, direction, &ray_dist,
	        r_location, r_normal, r_index,
	        r_ob, (float(*)[4])r_obmat);

	ED_transform_snap_object_context_destroy(sctx);

	if (ret) {
		*r_success = true;
	}
	else {
		*r_success = false;

		unit_m4((float(*)[4])r_obmat);
		zero_v3(r_location);
		zero_v3(r_normal);
	}
}


#else

void RNA_api_scene(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

	func = RNA_def_function(srna, "update", "rna_Scene_update_tagged");
	RNA_def_function_ui_description(func,
	                                "Update data tagged to be updated from previous access to data or operators");
	RNA_def_function_flag(func, FUNC_USE_MAIN);

	/* Ray Cast */
	func = RNA_def_function(srna, "ray_cast", "rna_Scene_ray_cast");
	RNA_def_function_flag(func, FUNC_USE_MAIN);
	RNA_def_function_ui_description(func, "Cast a ray onto in object space");
	/* ray start and end */
	parm = RNA_def_float_vector(func, "origin", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_float_vector(func, "direction", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	RNA_def_float(func, "distance", BVH_RAYCAST_DIST_MAX, 0.0, BVH_RAYCAST_DIST_MAX,
	              "", "Maximum distance", 0.0, BVH_RAYCAST_DIST_MAX);
	/* return location and normal */
	parm = RNA_def_boolean(func, "result", 0, "", "");
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "location", 3, NULL, -FLT_MAX, FLT_MAX, "Location",
	                            "The hit location of this ray cast", -1e4, 1e4);
	RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "normal", 3, NULL, -FLT_MAX, FLT_MAX, "Normal",
	                            "The face normal at the ray cast hit location", -1e4, 1e4);
	RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
	RNA_def_function_output(func, parm);
	parm = RNA_def_int(func, "index", 0, 0, 0, "", "The face index, -1 when original data isn't available", 0, 0);
	RNA_def_function_output(func, parm);
	parm = RNA_def_pointer(func, "object", "Object", "", "Ray cast object");
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_matrix(func, "matrix", 4, 4, NULL, 0.0f, 0.0f, "", "Matrix", 0.0f, 0.0f);
	RNA_def_function_output(func, parm);

}

#endif
