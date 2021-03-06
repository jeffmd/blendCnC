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

/** \file blender/makesrna/intern/rna_object_api.c
 *  \ingroup RNA
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "BLI_utildefines.h"
#include "BLI_kdopbvh.h"

#include "RNA_define.h"

#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "rna_internal.h"  /* own include */

#ifdef RNA_RUNTIME

#include "BLI_math.h"

#include "BKE_bvhutils.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_font.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_report.h"

#include "ED_object.h"

#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"

#include "MEM_guardedalloc.h"

static void rna_Object_calc_matrix_camera(
        Object *ob, float mat_ret[16], int width, int height, float scalex, float scaley)
{
	CameraParams params;

	/* setup parameters */
	BKE_camera_params_init(&params);
	BKE_camera_params_from_object(&params, ob);

	/* compute matrix, viewplane, .. */
	BKE_camera_params_compute_viewplane(&params, width, height, scalex, scaley);
	BKE_camera_params_compute_matrix(&params);

	copy_m4_m4((float (*)[4])mat_ret, params.winmat);
}

static void rna_Object_camera_fit_coords(
        Object *ob, Scene *scene, int num_cos, float *cos, float co_ret[3], float *scale_ret)
{
	BKE_camera_view_frame_fit_to_coords(scene, (const float (*)[3])cos, num_cos / 3, ob, co_ret, scale_ret);
}

/* copied from Mesh_getFromObject and adapted to RNA interface */
/* settings: 0 - preview, 1 - render */
static Mesh *rna_Object_to_mesh(
        Object *ob, Main *bmain, ReportList *reports, Scene *sce,
        bool apply_modifiers, int settings, bool calc_tessface, bool calc_undeformed)
{
	return rna_Main_meshes_new_from_object(bmain, reports, sce, ob, apply_modifiers, settings, calc_tessface, calc_undeformed);
}

static bool rna_Object_is_visible(Object *ob, Scene *sce)
{
	return !(ob->restrictflag & OB_RESTRICT_VIEW) && (ob->lay & sce->lay);
}

#if 0
static void rna_Mesh_assign_verts_to_group(Object *ob, bDeformGroup *group, int *indices, int totindex,
                                           float weight, int assignmode)
{
	if (ob->type != OB_MESH) {
		BKE_report(reports, RPT_ERROR, "Object should be of mesh type");
		return;
	}

	Mesh *me = (Mesh *)ob->data;
	int group_index = BLI_findlink(&ob->defbase, group);
	if (group_index == -1) {
		BKE_report(reports, RPT_ERROR, "No vertex groups assigned to mesh");
		return;
	}

	if (assignmode != WEIGHT_REPLACE && assignmode != WEIGHT_ADD && assignmode != WEIGHT_SUBTRACT) {
		BKE_report(reports, RPT_ERROR, "Bad assignment mode");
		return;
	}

	/* makes a set of dVerts corresponding to the mVerts */
	if (!me->dvert)
		create_dverts(&me->id);

	/* loop list adding verts to group  */
	for (i = 0; i < totindex; i++) {
		if (i < 0 || i >= me->totvert) {
			BKE_report(reports, RPT_ERROR, "Bad vertex index in list");
			return;
		}

		add_vert_defnr(ob, group_index, i, weight, assignmode);
	}
}
#endif

/* don't call inside a loop */
static int dm_looptri_to_poly_index(DerivedMesh *dm, const MLoopTri *lt)
{
	const int *index_mp_to_orig = dm->getPolyDataArray(dm, CD_ORIGINDEX);
	return index_mp_to_orig ? index_mp_to_orig[lt->poly] : lt->poly;
}

static void rna_Object_ray_cast(
        Object *ob, ReportList *reports,
        float origin[3], float direction[3], float distance,
        bool *r_success, float r_location[3], float r_normal[3], int *r_index)
{
	bool success = false;

	if (ob->derivedFinal == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' has no mesh data to be used for ray casting", ob->id.name + 2);
		return;
	}

	/* Test BoundBox first (efficiency) */
	BoundBox *bb = BKE_object_boundbox_get(ob);
	float distmin;
	normalize_v3(direction);  /* Needed for valid distance check from isect_ray_aabb_v3_simple() call. */
	if (!bb ||
	    (isect_ray_aabb_v3_simple(origin, direction, bb->vec[0], bb->vec[6], &distmin, NULL) && distmin <= distance))
	{
		BVHTreeFromMesh treeData = {NULL};

		/* no need to managing allocation or freeing of the BVH data. this is generated and freed as needed */
		bvhtree_from_mesh_get(&treeData, ob->derivedFinal, BVHTREE_FROM_LOOPTRI, 4);

		/* may fail if the mesh has no faces, in that case the ray-cast misses */
		if (treeData.tree != NULL) {
			BVHTreeRayHit hit;

			hit.index = -1;
			hit.dist = distance;

			if (BLI_bvhtree_ray_cast(treeData.tree, origin, direction, 0.0f, &hit,
			                         treeData.raycast_callback, &treeData) != -1)
			{
				if (hit.dist <= distance) {
					*r_success = success = true;

					copy_v3_v3(r_location, hit.co);
					copy_v3_v3(r_normal, hit.no);
					*r_index = dm_looptri_to_poly_index(ob->derivedFinal, &treeData.looptri[hit.index]);
				}
			}

			free_bvhtree_from_mesh(&treeData);
		}
	}
	if (success == false) {
		*r_success = false;

		zero_v3(r_location);
		zero_v3(r_normal);
		*r_index = -1;
	}
}

static void rna_Object_closest_point_on_mesh(
        Object *ob, ReportList *reports, float origin[3], float distance,
        bool *r_success, float r_location[3], float r_normal[3], int *r_index)
{
	BVHTreeFromMesh treeData = {NULL};

	if (ob->derivedFinal == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' has no mesh data to be used for finding nearest point",
		            ob->id.name + 2);
		return;
	}

	/* no need to managing allocation or freeing of the BVH data. this is generated and freed as needed */
	bvhtree_from_mesh_get(&treeData, ob->derivedFinal, BVHTREE_FROM_LOOPTRI, 4);

	if (treeData.tree == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' could not create internal data for finding nearest point",
		            ob->id.name + 2);
		return;
	}
	else {
		BVHTreeNearest nearest;

		nearest.index = -1;
		nearest.dist_sq = distance * distance;

		if (BLI_bvhtree_find_nearest(treeData.tree, origin, &nearest, treeData.nearest_callback, &treeData) != -1) {
			*r_success = true;

			copy_v3_v3(r_location, nearest.co);
			copy_v3_v3(r_normal, nearest.no);
			*r_index = dm_looptri_to_poly_index(ob->derivedFinal, &treeData.looptri[nearest.index]);

			goto finally;
		}
	}

	*r_success = false;

	zero_v3(r_location);
	zero_v3(r_normal);
	*r_index = -1;

finally:
	free_bvhtree_from_mesh(&treeData);
}

/* ObjectBase */

static void rna_ObjectBase_layers_from_view(Base *base, View3D *v3d)
{
	base->lay = base->object->lay = v3d->lay;
}

static bool rna_Object_is_modified(Object *ob, Scene *scene, int settings)
{
	return BKE_object_is_modified(scene, ob) & settings;
}

#ifndef NDEBUG
void rna_Object_dm_info(struct Object *ob, int type, char *result)
{
	DerivedMesh *dm = NULL;
	bool dm_release = false;
	char *ret = NULL;

	result[0] = '\0';

	switch (type) {
		case 0:
			if (ob->type == OB_MESH) {
				dm = CDDM_from_mesh(ob->data);
				ret = DM_debug_info(dm);
				dm_release = true;
			}
			break;
		case 1:
			dm = ob->derivedDeform;
			break;
		case 2:
			dm = ob->derivedFinal;
			break;
	}

	if (dm) {
		ret = DM_debug_info(dm);
		if (dm_release) {
			dm->release(dm);
		}
		if (ret) {
			strcpy(result, ret);
			MEM_freeN(ret);
		}
	}
}
#endif /* NDEBUG */

static bool rna_Object_update_from_editmode(Object *ob, Main *bmain)
{
	return ED_object_editmode_load(bmain, ob);
}
#else /* RNA_RUNTIME */

void RNA_api_object(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

	static const EnumPropertyItem mesh_type_items[] = {
		{eModifierMode_Realtime, "PREVIEW", 0, "Preview", "Apply modifier preview settings"},
		{eModifierMode_Render, "RENDER", 0, "Render", "Apply modifier render settings"},
		{0, NULL, 0, NULL, NULL}
	};

#ifndef NDEBUG
	static const EnumPropertyItem mesh_dm_info_items[] = {
		{0, "SOURCE", 0, "Source", "Source mesh"},
		{1, "DEFORM", 0, "Deform", "Objects deform mesh"},
		{2, "FINAL", 0, "Final", "Objects final mesh"},
		{0, NULL, 0, NULL, NULL}
	};
#endif

	/* Camera-related operations */
	func = RNA_def_function(srna, "calc_matrix_camera", "rna_Object_calc_matrix_camera");
	RNA_def_function_ui_description(func, "Generate the camera projection matrix of this object "
	                                      "(mostly useful for Camera and Lamp types)");
	parm = RNA_def_property(func, "result", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(parm, "", "The camera projection matrix");
	RNA_def_function_output(func, parm);
	parm = RNA_def_int(func, "x", 1, 0, INT_MAX, "", "Width of the render area", 0, 10000);
	parm = RNA_def_int(func, "y", 1, 0, INT_MAX, "", "Height of the render area", 0, 10000);
	parm = RNA_def_float(func, "scale_x", 1.0f, 1.0e-6f, FLT_MAX, "", "Width scaling factor", 1.0e-2f, 100.0f);
	parm = RNA_def_float(func, "scale_y", 1.0f, 1.0e-6f, FLT_MAX, "", "height scaling factor", 1.0e-2f, 100.0f);

	func = RNA_def_function(srna, "camera_fit_coords", "rna_Object_camera_fit_coords");
	RNA_def_function_ui_description(func, "Compute the coordinate (and scale for ortho cameras) "
	                                      "given object should be to 'see' all given coordinates");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "Scene to get render size information from, if available");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_float_array(func, "coordinates", 1, NULL, -FLT_MAX, FLT_MAX, "", "Coordinates to fit in",
	                           -FLT_MAX, FLT_MAX);
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL | PROP_DYNAMIC, PARM_REQUIRED);
	parm = RNA_def_property(func, "co_return", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_array(parm, 3);
	RNA_def_property_ui_text(parm, "", "The location to aim to be able to see all given points");
	RNA_def_parameter_flags(parm, 0, PARM_OUTPUT);
	parm = RNA_def_property(func, "scale_return", PROP_FLOAT, PROP_NONE);
	RNA_def_property_ui_text(parm, "", "The ortho scale to aim to be able to see all given points (if relevant)");
	RNA_def_parameter_flags(parm, 0, PARM_OUTPUT);

	/* mesh */
	func = RNA_def_function(srna, "to_mesh", "rna_Object_to_mesh");
	RNA_def_function_ui_description(func, "Create a Mesh data-block with modifiers applied");
	RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "scene", "Scene", "", "Scene within which to evaluate modifiers");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_boolean(func, "apply_modifiers", 0, "", "Apply modifiers");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Modifier settings to apply");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	RNA_def_boolean(func, "calc_tessface", true, "Calculate Tessellation", "Calculate tessellation faces");
	RNA_def_boolean(func, "calc_undeformed", false, "Calculate Undeformed", "Calculate undeformed vertex coordinates");
	parm = RNA_def_pointer(func, "mesh", "Mesh", "",
	                       "Mesh created from object, remove it if it is only used for export");
	RNA_def_function_return(func, parm);

	/* Ray Cast */
	func = RNA_def_function(srna, "ray_cast", "rna_Object_ray_cast");
	RNA_def_function_ui_description(func, "Cast a ray onto in object space");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);

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

	/* Nearest Point */
	func = RNA_def_function(srna, "closest_point_on_mesh", "rna_Object_closest_point_on_mesh");
	RNA_def_function_ui_description(func, "Find the nearest point in object space");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);

	/* location of point for test and max distance */
	parm = RNA_def_float_vector(func, "origin", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	/* default is sqrt(FLT_MAX) */
	RNA_def_float(func, "distance", 1.844674352395373e+19, 0.0, FLT_MAX, "", "Maximum distance", 0.0, FLT_MAX);

	/* return location and normal */
	parm = RNA_def_boolean(func, "result", 0, "", "");
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "location", 3, NULL, -FLT_MAX, FLT_MAX, "Location",
	                            "The location on the object closest to the point", -1e4, 1e4);
	RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
	RNA_def_function_output(func, parm);
	parm = RNA_def_float_vector(func, "normal", 3, NULL, -FLT_MAX, FLT_MAX, "Normal",
	                            "The face normal at the closest point", -1e4, 1e4);
	RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
	RNA_def_function_output(func, parm);

	parm = RNA_def_int(func, "index", 0, 0, 0, "", "The face index, -1 when original data isn't available", 0, 0);
	RNA_def_function_output(func, parm);

	/* View */
	func = RNA_def_function(srna, "is_visible", "rna_Object_is_visible");
	RNA_def_function_ui_description(func, "Determine if object is visible in a given scene");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_boolean(func, "result", 0, "", "Object visibility");
	RNA_def_function_return(func, parm);

	/* utility function for checking if the object is modified */
	func = RNA_def_function(srna, "is_modified", "rna_Object_is_modified");
	RNA_def_function_ui_description(func, "Determine if this object is modified from the base mesh data");
	parm = RNA_def_pointer(func, "scene", "Scene", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_enum(func, "settings", mesh_type_items, 0, "", "Modifier settings to apply");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_boolean(func, "result", 0, "", "Object visibility");
	RNA_def_function_return(func, parm);

#ifndef NDEBUG
	/* mesh */
	func = RNA_def_function(srna, "dm_info", "rna_Object_dm_info");
	RNA_def_function_ui_description(func, "Returns a string for derived mesh data");

	parm = RNA_def_enum(func, "type", mesh_dm_info_items, 0, "", "Modifier settings to apply");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	/* weak!, no way to return dynamic string type */
	parm = RNA_def_string(func, "result", NULL, 16384, "result", "");
	RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0); /* needed for string return value */
	RNA_def_function_output(func, parm);
#endif /* NDEBUG */

	func = RNA_def_function(srna, "update_from_editmode", "rna_Object_update_from_editmode");
	RNA_def_function_ui_description(func, "Load the objects edit-mode data into the object data");
	RNA_def_function_flag(func, FUNC_USE_MAIN);
	parm = RNA_def_boolean(func, "result", 0, "", "Success");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "cache_release", "BKE_object_free_caches");
	RNA_def_function_ui_description(func, "Release memory used by caches associated with this object. Intended to be used by render engines only");
}


void RNA_api_object_base(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

	func = RNA_def_function(srna, "layers_from_view", "rna_ObjectBase_layers_from_view");
	RNA_def_function_ui_description(func,
	                                "Sets the object layers from a 3D View (use when adding an object in local view)");
	parm = RNA_def_pointer(func, "view", "SpaceView3D", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
}

#endif /* RNA_RUNTIME */
