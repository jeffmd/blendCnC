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
 * The Original Code is Copyright (C) 2013 Blender Foundation
 * All rights reserved.
 */

/** \file rigidbody.c
 *  \ingroup blenkernel
 *  \brief Blender-side interface and methods for dealing with Rigid Body simulations
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
#include <math.h>
#include <limits.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"

#ifdef WITH_BULLET
#  include "RBI_api.h"
#endif

#include "DNA_ID.h"
#include "DNA_group_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"

/* ************************************** */
/* Memory Management */

/* Freeing Methods --------------------- */

#ifndef WITH_BULLET

static void RB_dworld_remove_constraint(void *UNUSED(world), void *UNUSED(con)) {}
static void RB_dworld_remove_body(void *UNUSED(world), void *UNUSED(body)) {}
static void RB_dworld_delete(void *UNUSED(world)) {}
static void RB_body_delete(void *UNUSED(body)) {}
static void RB_shape_delete(void *UNUSED(shape)) {}
static void RB_constraint_delete(void *UNUSED(con)) {}

#endif

/* Free rigidbody world */
void BKE_rigidbody_free_world(RigidBodyWorld *rbw)
{
	/* sanity check */
	if (!rbw)
		return;

	if (rbw->physics_world) {
		/* free physics references, we assume that all physics objects in will have been added to the world */
		GroupObject *go;
		if (rbw->constraints) {
			for (go = rbw->constraints->gobject.first; go; go = go->next) {
				if (go->ob && go->ob->rigidbody_constraint) {
					RigidBodyCon *rbc = go->ob->rigidbody_constraint;

					if (rbc->physics_constraint)
						RB_dworld_remove_constraint(rbw->physics_world, rbc->physics_constraint);
				}
			}
		}
		if (rbw->group) {
			for (go = rbw->group->gobject.first; go; go = go->next) {
				if (go->ob && go->ob->rigidbody_object) {
					RigidBodyOb *rbo = go->ob->rigidbody_object;

					if (rbo->physics_object)
						RB_dworld_remove_body(rbw->physics_world, rbo->physics_object);
				}
			}
		}
		/* free dynamics world */
		RB_dworld_delete(rbw->physics_world);
	}
	if (rbw->objects)
		free(rbw->objects);

	/* free rigidbody world itself */
	MEM_freeN(rbw);
}

/* Free RigidBody settings and sim instances */
void BKE_rigidbody_free_object(Object *ob)
{
	RigidBodyOb *rbo = (ob) ? ob->rigidbody_object : NULL;

	/* sanity check */
	if (rbo == NULL)
		return;

	/* free physics references */
	if (rbo->physics_object) {
		RB_body_delete(rbo->physics_object);
		rbo->physics_object = NULL;
	}

	if (rbo->physics_shape) {
		RB_shape_delete(rbo->physics_shape);
		rbo->physics_shape = NULL;
	}

	/* free data itself */
	MEM_freeN(rbo);
	ob->rigidbody_object = NULL;
}

/* Free RigidBody constraint and sim instance */
void BKE_rigidbody_free_constraint(Object *ob)
{
	RigidBodyCon *rbc = (ob) ? ob->rigidbody_constraint : NULL;

	/* sanity check */
	if (rbc == NULL)
		return;

	/* free physics reference */
	if (rbc->physics_constraint) {
		RB_constraint_delete(rbc->physics_constraint);
		rbc->physics_constraint = NULL;
	}

	/* free data itself */
	MEM_freeN(rbc);
	ob->rigidbody_constraint = NULL;
}

#ifdef WITH_BULLET

/* Copying Methods --------------------- */

/* These just copy the data, clearing out references to physics objects.
 * Anything that uses them MUST verify that the copied object will
 * be added to relevant groups later...
 */

RigidBodyOb *BKE_rigidbody_copy_object(const Object *ob, const int UNUSED(flag))
{
	RigidBodyOb *rboN = NULL;

	if (ob->rigidbody_object) {
		/* just duplicate the whole struct first (to catch all the settings) */
		rboN = MEM_dupallocN(ob->rigidbody_object);

		/* tag object as needing to be verified */
		rboN->flag |= RBO_FLAG_NEEDS_VALIDATE;

		/* clear out all the fields which need to be revalidated later */
		rboN->physics_object = NULL;
		rboN->physics_shape = NULL;
	}

	/* return new copy of settings */
	return rboN;
}

RigidBodyCon *BKE_rigidbody_copy_constraint(const Object *ob, const int UNUSED(flag))
{
	RigidBodyCon *rbcN = NULL;

	if (ob->rigidbody_constraint) {
		/* just duplicate the whole struct first (to catch all the settings) */
		rbcN = MEM_dupallocN(ob->rigidbody_constraint);

		/* tag object as needing to be verified */
		rbcN->flag |= RBC_FLAG_NEEDS_VALIDATE;

		/* clear out all the fields which need to be revalidated later */
		rbcN->physics_constraint = NULL;
	}

	/* return new copy of settings */
	return rbcN;
}

/* ************************************** */
/* Setup Utilities - Validate Sim Instances */

/* get the appropriate DerivedMesh based on rigid body mesh source */
static DerivedMesh *rigidbody_get_mesh(Object *ob)
{
	if (ob->rigidbody_object->mesh_source == RBO_MESH_DEFORM) {
		return ob->derivedDeform;
	}
	else if (ob->rigidbody_object->mesh_source == RBO_MESH_FINAL) {
		return ob->derivedFinal;
	}
	else {
		return CDDM_from_mesh(ob->data);
	}
}

/* create collision shape of mesh - convex hull */
static rbCollisionShape *rigidbody_get_shape_convexhull_from_mesh(Object *ob, float margin, bool *can_embed)
{
	rbCollisionShape *shape = NULL;
	DerivedMesh *dm = NULL;
	MVert *mvert = NULL;
	int totvert = 0;

	if (ob->type == OB_MESH && ob->data) {
		dm = rigidbody_get_mesh(ob);
		mvert   = (dm) ? dm->getVertArray(dm) : NULL;
		totvert = (dm) ? dm->getNumVerts(dm) : 0;
	}
	else {
		printf("ERROR: cannot make Convex Hull collision shape for non-Mesh object\n");
	}

	if (totvert) {
		shape = RB_shape_new_convex_hull((float *)mvert, sizeof(MVert), totvert, margin, can_embed);
	}
	else {
		printf("ERROR: no vertices to define Convex Hull collision shape with\n");
	}

	if (dm && ob->rigidbody_object->mesh_source == RBO_MESH_BASE)
		dm->release(dm);

	return shape;
}

/* create collision shape of mesh - triangulated mesh
 * returns NULL if creation fails.
 */
static rbCollisionShape *rigidbody_get_shape_trimesh_from_mesh(Object *ob)
{
	rbCollisionShape *shape = NULL;

	if (ob->type == OB_MESH) {
		DerivedMesh *dm = NULL;
		MVert *mvert;
		const MLoopTri *looptri;
		int totvert;
		int tottri;
		const MLoop *mloop;

		dm = rigidbody_get_mesh(ob);

		/* ensure mesh validity, then grab data */
		if (dm == NULL)
			return NULL;

		mvert   = dm->getVertArray(dm);
		totvert = dm->getNumVerts(dm);
		looptri = dm->getLoopTriArray(dm);
		tottri = dm->getNumLoopTri(dm);
		mloop = dm->getLoopArray(dm);

		/* sanity checking - potential case when no data will be present */
		if ((totvert == 0) || (tottri == 0)) {
			printf("WARNING: no geometry data converted for Mesh Collision Shape (ob = %s)\n", ob->id.name + 2);
		}
		else {
			rbMeshData *mdata;
			int i;

			/* init mesh data for collision shape */
			mdata = RB_trimesh_data_new(tottri, totvert);

			RB_trimesh_add_vertices(mdata, (float *)mvert, totvert, sizeof(MVert));

			/* loop over all faces, adding them as triangles to the collision shape
			 * (so for some faces, more than triangle will get added)
			 */
			if (mvert && looptri) {
				for (i = 0; i < tottri; i++) {
					/* add first triangle - verts 1,2,3 */
					const MLoopTri *lt = &looptri[i];
					int vtri[3];

					vtri[0] = mloop[lt->tri[0]].v;
					vtri[1] = mloop[lt->tri[1]].v;
					vtri[2] = mloop[lt->tri[2]].v;

					RB_trimesh_add_triangle_indices(mdata, i, UNPACK3(vtri));
				}
			}

			RB_trimesh_finish(mdata);

			/* construct collision shape
			 *
			 * These have been chosen to get better speed/accuracy tradeoffs with regards
			 * to limitations of each:
			 *    - BVH-Triangle Mesh: for passive objects only. Despite having greater
			 *                         speed/accuracy, they cannot be used for moving objects.
			 *    - GImpact Mesh:      for active objects. These are slower and less stable,
			 *                         but are more flexible for general usage.
			 */
			if (ob->rigidbody_object->type == RBO_TYPE_PASSIVE) {
				shape = RB_shape_new_trimesh(mdata);
			}
			else {
				shape = RB_shape_new_gimpact_mesh(mdata);
			}
		}

		/* cleanup temp data */
		if (ob->rigidbody_object->mesh_source == RBO_MESH_BASE) {
			dm->release(dm);
		}
	}
	else {
		printf("ERROR: cannot make Triangular Mesh collision shape for non-Mesh object\n");
	}

	return shape;
}

/* Create new physics sim collision shape for object and store it,
 * or remove the existing one first and replace...
 */
static void rigidbody_validate_sim_shape(Object *ob, bool rebuild)
{
	RigidBodyOb *rbo = ob->rigidbody_object;
	rbCollisionShape *new_shape = NULL;
	BoundBox *bb = NULL;
	float size[3] = {1.0f, 1.0f, 1.0f};
	float radius = 1.0f;
	float height = 1.0f;
	float capsule_height;
	float hull_margin = 0.0f;
	bool can_embed = true;
	bool has_volume;

	/* sanity check */
	if (rbo == NULL)
		return;

	/* don't create a new shape if we already have one and don't want to rebuild it */
	if (rbo->physics_shape && !rebuild)
		return;

	/* if automatically determining dimensions, use the Object's boundbox
	 * - assume that all quadrics are standing upright on local z-axis
	 * - assume even distribution of mass around the Object's pivot
	 *   (i.e. Object pivot is centralized in boundbox)
	 */
	// XXX: all dimensions are auto-determined now... later can add stored settings for this
	/* get object dimensions without scaling */
	bb = BKE_object_boundbox_get(ob);
	if (bb) {
		size[0] = (bb->vec[4][0] - bb->vec[0][0]);
		size[1] = (bb->vec[2][1] - bb->vec[0][1]);
		size[2] = (bb->vec[1][2] - bb->vec[0][2]);
	}
	mul_v3_fl(size, 0.5f);

	if (ELEM(rbo->shape, RB_SHAPE_CAPSULE, RB_SHAPE_CYLINDER, RB_SHAPE_CONE)) {
		/* take radius as largest x/y dimension, and height as z-dimension */
		radius = MAX2(size[0], size[1]);
		height = size[2];
	}
	else if (rbo->shape == RB_SHAPE_SPHERE) {
		/* take radius to the largest dimension to try and encompass everything */
		radius = MAX3(size[0], size[1], size[2]);
	}

	/* create new shape */
	switch (rbo->shape) {
		case RB_SHAPE_BOX:
			new_shape = RB_shape_new_box(size[0], size[1], size[2]);
			break;

		case RB_SHAPE_SPHERE:
			new_shape = RB_shape_new_sphere(radius);
			break;

		case RB_SHAPE_CAPSULE:
			capsule_height = (height - radius) * 2.0f;
			new_shape = RB_shape_new_capsule(radius, (capsule_height > 0.0f) ? capsule_height : 0.0f);
			break;
		case RB_SHAPE_CYLINDER:
			new_shape = RB_shape_new_cylinder(radius, height);
			break;
		case RB_SHAPE_CONE:
			new_shape = RB_shape_new_cone(radius, height * 2.0f);
			break;

		case RB_SHAPE_CONVEXH:
			/* try to emged collision margin */
			has_volume = (MIN3(size[0], size[1], size[2]) > 0.0f);

			if (!(rbo->flag & RBO_FLAG_USE_MARGIN) && has_volume)
				hull_margin = 0.04f;
			new_shape = rigidbody_get_shape_convexhull_from_mesh(ob, hull_margin, &can_embed);
			if (!(rbo->flag & RBO_FLAG_USE_MARGIN))
				rbo->margin = (can_embed && has_volume) ? 0.04f : 0.0f;  /* RB_TODO ideally we shouldn't directly change the margin here */
			break;
		case RB_SHAPE_TRIMESH:
			new_shape = rigidbody_get_shape_trimesh_from_mesh(ob);
			break;
	}
	/* use box shape if we can't fall back to old shape */
	if (new_shape == NULL && rbo->physics_shape == NULL) {
		new_shape = RB_shape_new_box(size[0], size[1], size[2]);
	}
	/* assign new collision shape if creation was successful */
	if (new_shape) {
		if (rbo->physics_shape)
			RB_shape_delete(rbo->physics_shape);
		rbo->physics_shape = new_shape;
		RB_shape_set_margin(rbo->physics_shape, RBO_GET_MARGIN(rbo));
	}
}

/* --------------------- */

/* helper function to calculate volume of rigidbody object */
// TODO: allow a parameter to specify method used to calculate this?
void BKE_rigidbody_calc_volume(Object *ob, float *r_vol)
{
	RigidBodyOb *rbo = ob->rigidbody_object;

	float size[3]  = {1.0f, 1.0f, 1.0f};
	float radius = 1.0f;
	float height = 1.0f;

	float volume = 0.0f;

	/* if automatically determining dimensions, use the Object's boundbox
	 * - assume that all quadrics are standing upright on local z-axis
	 * - assume even distribution of mass around the Object's pivot
	 *   (i.e. Object pivot is centralized in boundbox)
	 * - boundbox gives full width
	 */
	// XXX: all dimensions are auto-determined now... later can add stored settings for this
	BKE_object_dimensions_get(ob, size);

	if (ELEM(rbo->shape, RB_SHAPE_CAPSULE, RB_SHAPE_CYLINDER, RB_SHAPE_CONE)) {
		/* take radius as largest x/y dimension, and height as z-dimension */
		radius = MAX2(size[0], size[1]) * 0.5f;
		height = size[2];
	}
	else if (rbo->shape == RB_SHAPE_SPHERE) {
		/* take radius to the largest dimension to try and encompass everything */
		radius = max_fff(size[0], size[1], size[2]) * 0.5f;
	}

	/* calculate volume as appropriate  */
	switch (rbo->shape) {
		case RB_SHAPE_BOX:
			volume = size[0] * size[1] * size[2];
			break;

		case RB_SHAPE_SPHERE:
			volume = 4.0f / 3.0f * (float)M_PI * radius * radius * radius;
			break;

		/* for now, assume that capsule is close enough to a cylinder... */
		case RB_SHAPE_CAPSULE:
		case RB_SHAPE_CYLINDER:
			volume = (float)M_PI * radius * radius * height;
			break;

		case RB_SHAPE_CONE:
			volume = (float)M_PI / 3.0f * radius * radius * height;
			break;

		case RB_SHAPE_CONVEXH:
		case RB_SHAPE_TRIMESH:
		{
			if (ob->type == OB_MESH) {
				DerivedMesh *dm = rigidbody_get_mesh(ob);
				MVert *mvert;
				const MLoopTri *lt = NULL;
				int totvert, tottri = 0;
				const MLoop *mloop = NULL;

				/* ensure mesh validity, then grab data */
				if (dm == NULL)
					return;

				mvert   = dm->getVertArray(dm);
				totvert = dm->getNumVerts(dm);
				lt = dm->getLoopTriArray(dm);
				tottri = dm->getNumLoopTri(dm);
				mloop = dm->getLoopArray(dm);

				if (totvert > 0 && tottri > 0) {
					BKE_mesh_calc_volume(mvert, totvert, lt, tottri, mloop, &volume, NULL);
				}

				/* cleanup temp data */
				if (ob->rigidbody_object->mesh_source == RBO_MESH_BASE) {
					dm->release(dm);
				}
			}
			else {
				/* rough estimate from boundbox as fallback */
				/* XXX could implement other types of geometry here (curves, etc.) */
				volume = size[0] * size[1] * size[2];
			}
			break;
		}

#if 0 // XXX: not defined yet
		case RB_SHAPE_COMPOUND:
			volume = 0.0f;
			break;
#endif
	}

	/* return the volume calculated */
	if (r_vol) *r_vol = volume;
}

void BKE_rigidbody_calc_center_of_mass(Object *ob, float r_center[3])
{
	RigidBodyOb *rbo = ob->rigidbody_object;

	float size[3]  = {1.0f, 1.0f, 1.0f};
	float height = 1.0f;

	zero_v3(r_center);

	/* if automatically determining dimensions, use the Object's boundbox
	 * - assume that all quadrics are standing upright on local z-axis
	 * - assume even distribution of mass around the Object's pivot
	 *   (i.e. Object pivot is centralized in boundbox)
	 * - boundbox gives full width
	 */
	// XXX: all dimensions are auto-determined now... later can add stored settings for this
	BKE_object_dimensions_get(ob, size);

	/* calculate volume as appropriate  */
	switch (rbo->shape) {
		case RB_SHAPE_BOX:
		case RB_SHAPE_SPHERE:
		case RB_SHAPE_CAPSULE:
		case RB_SHAPE_CYLINDER:
			break;

		case RB_SHAPE_CONE:
			/* take radius as largest x/y dimension, and height as z-dimension */
			height = size[2];
			/* cone is geometrically centered on the median,
			 * center of mass is 1/4 up from the base
			 */
			r_center[2] = -0.25f * height;
			break;

		case RB_SHAPE_CONVEXH:
		case RB_SHAPE_TRIMESH:
		{
			if (ob->type == OB_MESH) {
				DerivedMesh *dm = rigidbody_get_mesh(ob);
				MVert *mvert;
				const MLoopTri *looptri;
				int totvert, tottri;
				const MLoop *mloop;

				/* ensure mesh validity, then grab data */
				if (dm == NULL)
					return;

				mvert   = dm->getVertArray(dm);
				totvert = dm->getNumVerts(dm);
				looptri = dm->getLoopTriArray(dm);
				tottri = dm->getNumLoopTri(dm);
				mloop = dm->getLoopArray(dm);

				if (totvert > 0 && tottri > 0) {
					BKE_mesh_calc_volume(mvert, totvert, looptri, tottri, mloop, NULL, r_center);
				}

				/* cleanup temp data */
				if (ob->rigidbody_object->mesh_source == RBO_MESH_BASE) {
					dm->release(dm);
				}
			}
			break;
		}

#if 0 // XXX: not defined yet
		case RB_SHAPE_COMPOUND:
			volume = 0.0f;
			break;
#endif
	}
}

/* --------------------- */

/**
 * Create physics sim representation of object given RigidBody settings
 *
 * \param rebuild: Even if an instance already exists, replace it
 */
static void rigidbody_validate_sim_object(RigidBodyWorld *rbw, Object *ob, bool rebuild)
{
	RigidBodyOb *rbo = (ob) ? ob->rigidbody_object : NULL;
	float loc[3];
	float rot[4];

	/* sanity checks:
	 * - object doesn't have RigidBody info already: then why is it here?
	 */
	if (rbo == NULL)
		return;

	/* make sure collision shape exists */
	/* FIXME we shouldn't always have to rebuild collision shapes when rebuilding objects, but it's needed for constraints to update correctly */
	if (rbo->physics_shape == NULL || rebuild)
		rigidbody_validate_sim_shape(ob, true);

	if (rbo->physics_object && rebuild == false) {
		RB_dworld_remove_body(rbw->physics_world, rbo->physics_object);
	}
	if (!rbo->physics_object || rebuild) {
		/* remove rigid body if it already exists before creating a new one */
		if (rbo->physics_object) {
			RB_body_delete(rbo->physics_object);
		}

		mat4_to_loc_quat(loc, rot, ob->obmat);

		rbo->physics_object = RB_body_new(rbo->physics_shape, loc, rot);

		RB_body_set_friction(rbo->physics_object, rbo->friction);
		RB_body_set_restitution(rbo->physics_object, rbo->restitution);

		RB_body_set_damping(rbo->physics_object, rbo->lin_damping, rbo->ang_damping);
		RB_body_set_sleep_thresh(rbo->physics_object, rbo->lin_sleep_thresh, rbo->ang_sleep_thresh);
		RB_body_set_activation_state(rbo->physics_object, rbo->flag & RBO_FLAG_USE_DEACTIVATION);

		if (rbo->type == RBO_TYPE_PASSIVE || rbo->flag & RBO_FLAG_START_DEACTIVATED)
			RB_body_deactivate(rbo->physics_object);


		RB_body_set_linear_factor(rbo->physics_object,
		                          (ob->protectflag & OB_LOCK_LOCX) == 0,
		                          (ob->protectflag & OB_LOCK_LOCY) == 0,
		                          (ob->protectflag & OB_LOCK_LOCZ) == 0);
		RB_body_set_angular_factor(rbo->physics_object,
		                           (ob->protectflag & OB_LOCK_ROTX) == 0,
		                           (ob->protectflag & OB_LOCK_ROTY) == 0,
		                           (ob->protectflag & OB_LOCK_ROTZ) == 0);

		RB_body_set_mass(rbo->physics_object, RBO_GET_MASS(rbo));
		RB_body_set_kinematic_state(rbo->physics_object, rbo->flag & RBO_FLAG_KINEMATIC || rbo->flag & RBO_FLAG_DISABLED);
	}

	if (rbw && rbw->physics_world)
		RB_dworld_add_body(rbw->physics_world, rbo->physics_object, rbo->col_groups);
}

/* --------------------- */

static void rigidbody_constraint_init_spring(
        RigidBodyCon *rbc, void (*set_spring)(rbConstraint *, int, int),
        void (*set_stiffness)(rbConstraint *, int, float), void (*set_damping)(rbConstraint *, int, float))
{
	set_spring(rbc->physics_constraint, RB_LIMIT_LIN_X, rbc->flag & RBC_FLAG_USE_SPRING_X);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_LIN_X, rbc->spring_stiffness_x);
	set_damping(rbc->physics_constraint, RB_LIMIT_LIN_X, rbc->spring_damping_x);

	set_spring(rbc->physics_constraint, RB_LIMIT_LIN_Y, rbc->flag & RBC_FLAG_USE_SPRING_Y);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_LIN_Y, rbc->spring_stiffness_y);
	set_damping(rbc->physics_constraint, RB_LIMIT_LIN_Y, rbc->spring_damping_y);

	set_spring(rbc->physics_constraint, RB_LIMIT_LIN_Z, rbc->flag & RBC_FLAG_USE_SPRING_Z);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_LIN_Z, rbc->spring_stiffness_z);
	set_damping(rbc->physics_constraint, RB_LIMIT_LIN_Z, rbc->spring_damping_z);

	set_spring(rbc->physics_constraint, RB_LIMIT_ANG_X, rbc->flag & RBC_FLAG_USE_SPRING_ANG_X);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_ANG_X, rbc->spring_stiffness_ang_x);
	set_damping(rbc->physics_constraint, RB_LIMIT_ANG_X, rbc->spring_damping_ang_x);

	set_spring(rbc->physics_constraint, RB_LIMIT_ANG_Y, rbc->flag & RBC_FLAG_USE_SPRING_ANG_Y);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_ANG_Y, rbc->spring_stiffness_ang_y);
	set_damping(rbc->physics_constraint, RB_LIMIT_ANG_Y, rbc->spring_damping_ang_y);

	set_spring(rbc->physics_constraint, RB_LIMIT_ANG_Z, rbc->flag & RBC_FLAG_USE_SPRING_ANG_Z);
	set_stiffness(rbc->physics_constraint, RB_LIMIT_ANG_Z, rbc->spring_stiffness_ang_z);
	set_damping(rbc->physics_constraint, RB_LIMIT_ANG_Z, rbc->spring_damping_ang_z);
}

static void rigidbody_constraint_set_limits(RigidBodyCon *rbc, void (*set_limits)(rbConstraint *, int, float, float))
{
	if (rbc->flag & RBC_FLAG_USE_LIMIT_LIN_X)
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_X, rbc->limit_lin_x_lower, rbc->limit_lin_x_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_X, 0.0f, -1.0f);

	if (rbc->flag & RBC_FLAG_USE_LIMIT_LIN_Y)
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_Y, rbc->limit_lin_y_lower, rbc->limit_lin_y_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_Y, 0.0f, -1.0f);

	if (rbc->flag & RBC_FLAG_USE_LIMIT_LIN_Z)
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_Z, rbc->limit_lin_z_lower, rbc->limit_lin_z_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_LIN_Z, 0.0f, -1.0f);

	if (rbc->flag & RBC_FLAG_USE_LIMIT_ANG_X)
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_X, rbc->limit_ang_x_lower, rbc->limit_ang_x_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_X, 0.0f, -1.0f);

	if (rbc->flag & RBC_FLAG_USE_LIMIT_ANG_Y)
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_Y, rbc->limit_ang_y_lower, rbc->limit_ang_y_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_Y, 0.0f, -1.0f);

	if (rbc->flag & RBC_FLAG_USE_LIMIT_ANG_Z)
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_Z, rbc->limit_ang_z_lower, rbc->limit_ang_z_upper);
	else
		set_limits(rbc->physics_constraint, RB_LIMIT_ANG_Z, 0.0f, -1.0f);
}

/**
 * Create physics sim representation of constraint given rigid body constraint settings
 *
 * \param rebuild: Even if an instance already exists, replace it
 */
static void rigidbody_validate_sim_constraint(RigidBodyWorld *rbw, Object *ob, bool rebuild)
{
	RigidBodyCon *rbc = (ob) ? ob->rigidbody_constraint : NULL;
	float loc[3];
	float rot[4];
	float lin_lower;
	float lin_upper;
	float ang_lower;
	float ang_upper;

	/* sanity checks:
	 * - object should have a rigid body constraint
	 * - rigid body constraint should have at least one constrained object
	 */
	if (rbc == NULL) {
		return;
	}

	if (ELEM(NULL, rbc->ob1, rbc->ob1->rigidbody_object, rbc->ob2, rbc->ob2->rigidbody_object)) {
		if (rbc->physics_constraint) {
			RB_dworld_remove_constraint(rbw->physics_world, rbc->physics_constraint);
			RB_constraint_delete(rbc->physics_constraint);
			rbc->physics_constraint = NULL;
		}
		return;
	}

	if (rbc->physics_constraint && rebuild == false) {
		RB_dworld_remove_constraint(rbw->physics_world, rbc->physics_constraint);
	}
	if (rbc->physics_constraint == NULL || rebuild) {
		rbRigidBody *rb1 = rbc->ob1->rigidbody_object->physics_object;
		rbRigidBody *rb2 = rbc->ob2->rigidbody_object->physics_object;

		/* remove constraint if it already exists before creating a new one */
		if (rbc->physics_constraint) {
			RB_constraint_delete(rbc->physics_constraint);
			rbc->physics_constraint = NULL;
		}

		mat4_to_loc_quat(loc, rot, ob->obmat);

		if (rb1 && rb2) {
			switch (rbc->type) {
				case RBC_TYPE_POINT:
					rbc->physics_constraint = RB_constraint_new_point(loc, rb1, rb2);
					break;
				case RBC_TYPE_FIXED:
					rbc->physics_constraint = RB_constraint_new_fixed(loc, rot, rb1, rb2);
					break;
				case RBC_TYPE_HINGE:
					rbc->physics_constraint = RB_constraint_new_hinge(loc, rot, rb1, rb2);
					if (rbc->flag & RBC_FLAG_USE_LIMIT_ANG_Z) {
						RB_constraint_set_limits_hinge(rbc->physics_constraint, rbc->limit_ang_z_lower, rbc->limit_ang_z_upper);
					}
					else
						RB_constraint_set_limits_hinge(rbc->physics_constraint, 0.0f, -1.0f);
					break;
				case RBC_TYPE_SLIDER:
					rbc->physics_constraint = RB_constraint_new_slider(loc, rot, rb1, rb2);
					if (rbc->flag & RBC_FLAG_USE_LIMIT_LIN_X)
						RB_constraint_set_limits_slider(rbc->physics_constraint, rbc->limit_lin_x_lower, rbc->limit_lin_x_upper);
					else
						RB_constraint_set_limits_slider(rbc->physics_constraint, 0.0f, -1.0f);
					break;
				case RBC_TYPE_PISTON:
					rbc->physics_constraint = RB_constraint_new_piston(loc, rot, rb1, rb2);
					if (rbc->flag & RBC_FLAG_USE_LIMIT_LIN_X) {
						lin_lower = rbc->limit_lin_x_lower;
						lin_upper = rbc->limit_lin_x_upper;
					}
					else {
						lin_lower = 0.0f;
						lin_upper = -1.0f;
					}
					if (rbc->flag & RBC_FLAG_USE_LIMIT_ANG_X) {
						ang_lower = rbc->limit_ang_x_lower;
						ang_upper = rbc->limit_ang_x_upper;
					}
					else {
						ang_lower = 0.0f;
						ang_upper = -1.0f;
					}
					RB_constraint_set_limits_piston(rbc->physics_constraint, lin_lower, lin_upper, ang_lower, ang_upper);
					break;
				case RBC_TYPE_6DOF_SPRING:
					if (rbc->spring_type == RBC_SPRING_TYPE2) {
						rbc->physics_constraint = RB_constraint_new_6dof_spring2(loc, rot, rb1, rb2);

						rigidbody_constraint_init_spring(rbc, RB_constraint_set_spring_6dof_spring2, RB_constraint_set_stiffness_6dof_spring2, RB_constraint_set_damping_6dof_spring2);

						RB_constraint_set_equilibrium_6dof_spring2(rbc->physics_constraint);

						rigidbody_constraint_set_limits(rbc, RB_constraint_set_limits_6dof_spring2);
					}
					else {
						rbc->physics_constraint = RB_constraint_new_6dof_spring(loc, rot, rb1, rb2);

						rigidbody_constraint_init_spring(rbc, RB_constraint_set_spring_6dof_spring, RB_constraint_set_stiffness_6dof_spring, RB_constraint_set_damping_6dof_spring);

						RB_constraint_set_equilibrium_6dof_spring(rbc->physics_constraint);

						rigidbody_constraint_set_limits(rbc, RB_constraint_set_limits_6dof);
					}
					break;
				case RBC_TYPE_6DOF:
					rbc->physics_constraint = RB_constraint_new_6dof(loc, rot, rb1, rb2);

					rigidbody_constraint_set_limits(rbc, RB_constraint_set_limits_6dof);
					break;
				case RBC_TYPE_MOTOR:
					rbc->physics_constraint = RB_constraint_new_motor(loc, rot, rb1, rb2);

					RB_constraint_set_enable_motor(rbc->physics_constraint, rbc->flag & RBC_FLAG_USE_MOTOR_LIN, rbc->flag & RBC_FLAG_USE_MOTOR_ANG);
					RB_constraint_set_max_impulse_motor(rbc->physics_constraint, rbc->motor_lin_max_impulse, rbc->motor_ang_max_impulse);
					RB_constraint_set_target_velocity_motor(rbc->physics_constraint, rbc->motor_lin_target_velocity, rbc->motor_ang_target_velocity);
					break;
			}
		}
		else { /* can't create constraint without both rigid bodies */
			return;
		}

		RB_constraint_set_enabled(rbc->physics_constraint, rbc->flag & RBC_FLAG_ENABLED);

		if (rbc->flag & RBC_FLAG_USE_BREAKING)
			RB_constraint_set_breaking_threshold(rbc->physics_constraint, rbc->breaking_threshold);
		else
			RB_constraint_set_breaking_threshold(rbc->physics_constraint, FLT_MAX);

		if (rbc->flag & RBC_FLAG_OVERRIDE_SOLVER_ITERATIONS)
			RB_constraint_set_solver_iterations(rbc->physics_constraint, rbc->num_solver_iterations);
		else
			RB_constraint_set_solver_iterations(rbc->physics_constraint, -1);
	}

	if (rbw && rbw->physics_world && rbc->physics_constraint) {
		RB_dworld_add_constraint(rbw->physics_world, rbc->physics_constraint, rbc->flag & RBC_FLAG_DISABLE_COLLISIONS);
	}
}

/* --------------------- */

/* Create physics sim world given RigidBody world settings */
// NOTE: this does NOT update object references that the scene uses, in case those aren't ready yet!
void BKE_rigidbody_validate_sim_world(Scene *scene, RigidBodyWorld *rbw, bool rebuild)
{
	/* sanity checks */
	if (rbw == NULL)
		return;

	/* create new sim world */
	if (rebuild || rbw->physics_world == NULL) {
		if (rbw->physics_world)
			RB_dworld_delete(rbw->physics_world);
		rbw->physics_world = RB_dworld_new(scene->physics_settings.gravity);
	}

	RB_dworld_set_solver_iterations(rbw->physics_world, rbw->num_solver_iterations);
	RB_dworld_set_split_impulse(rbw->physics_world, rbw->flag & RBW_FLAG_USE_SPLIT_IMPULSE);
}

/* ************************************** */
/* Setup Utilities - Create Settings Blocks */

/* Set up RigidBody world */
RigidBodyWorld *BKE_rigidbody_create_world(Scene *scene)
{
	/* try to get whatever RigidBody world that might be representing this already */
	RigidBodyWorld *rbw;

	/* sanity checks
	 * - there must be a valid scene to add world to
	 * - there mustn't be a sim world using this group already
	 */
	if (scene == NULL)
		return NULL;

	/* create a new sim world */
	rbw = MEM_callocN(sizeof(RigidBodyWorld), "RigidBodyWorld");

	rbw->ltime = 0.0f;

	rbw->time_scale = 1.0f;

	rbw->steps_per_second = 60; /* Bullet default (60 Hz) */
	rbw->num_solver_iterations = 10; /* 10 is bullet default */

	/* return this sim world */
	return rbw;
}

RigidBodyWorld *BKE_rigidbody_world_copy(RigidBodyWorld *rbw, const int flag)
{
	RigidBodyWorld *rbw_copy = MEM_dupallocN(rbw);

	if ((flag & LIB_ID_CREATE_NO_USER_REFCOUNT) == 0) {
		id_us_plus((ID *)rbw_copy->group);
		id_us_plus((ID *)rbw_copy->constraints);
	}

	rbw_copy->objects = NULL;
	rbw_copy->physics_world = NULL;
	rbw_copy->numbodies = 0;

	return rbw_copy;
}

void BKE_rigidbody_world_groups_relink(RigidBodyWorld *rbw)
{
	ID_NEW_REMAP(rbw->group);
	ID_NEW_REMAP(rbw->constraints);
}

void BKE_rigidbody_world_id_loop(RigidBodyWorld *rbw, RigidbodyWorldIDFunc func, void *userdata)
{
	func(rbw, (ID **)&rbw->group, userdata, IDWALK_CB_NOP);
	func(rbw, (ID **)&rbw->constraints, userdata, IDWALK_CB_NOP);

	if (rbw->objects) {
		int i;
		for (i = 0; i < rbw->numbodies; i++) {
			func(rbw, (ID **)&rbw->objects[i], userdata, IDWALK_CB_NOP);
		}
	}
}

/* Add rigid body settings to the specified object */
RigidBodyOb *BKE_rigidbody_create_object(Scene *scene, Object *ob, short type)
{
	RigidBodyOb *rbo;
	RigidBodyWorld *rbw = scene->rigidbody_world;

	/* sanity checks
	 * - rigidbody world must exist
	 * - object must exist
	 * - cannot add rigid body if it already exists
	 */
	if (ob == NULL || (ob->rigidbody_object != NULL))
		return NULL;

	/* create new settings data, and link it up */
	rbo = MEM_callocN(sizeof(RigidBodyOb), "RigidBodyOb");

	/* set default settings */
	rbo->type = type;

	rbo->mass = 1.0f;

	rbo->friction = 0.5f; /* best when non-zero. 0.5 is Bullet default */
	rbo->restitution = 0.0f; /* best when zero. 0.0 is Bullet default */

	rbo->margin = 0.04f; /* 0.04 (in meters) is Bullet default */

	rbo->lin_sleep_thresh = 0.4f; /* 0.4 is half of Bullet default */
	rbo->ang_sleep_thresh = 0.5f; /* 0.5 is half of Bullet default */

	rbo->lin_damping = 0.04f; /* 0.04 is game engine default */
	rbo->ang_damping = 0.1f; /* 0.1 is game engine default */

	rbo->col_groups = 1;

	/* use triangle meshes for passive objects
	 * use convex hulls for active objects since dynamic triangle meshes are very unstable
	 */
	if (type == RBO_TYPE_ACTIVE)
		rbo->shape = RB_SHAPE_CONVEXH;
	else
		rbo->shape = RB_SHAPE_TRIMESH;

	rbo->mesh_source = RBO_MESH_DEFORM;

	/* set initial transform */
	mat4_to_loc_quat(rbo->pos, rbo->orn, ob->obmat);

	/* flag cache as outdated */
	BKE_rigidbody_cache_reset(rbw);

	/* return this object */
	return rbo;
}

/* Add rigid body constraint to the specified object */
RigidBodyCon *BKE_rigidbody_create_constraint(Scene *scene, Object *ob, short type)
{
	RigidBodyCon *rbc;
	RigidBodyWorld *rbw = scene->rigidbody_world;

	/* sanity checks
	 * - rigidbody world must exist
	 * - object must exist
	 * - cannot add constraint if it already exists
	 */
	if (ob == NULL || (ob->rigidbody_constraint != NULL))
		return NULL;

	/* create new settings data, and link it up */
	rbc = MEM_callocN(sizeof(RigidBodyCon), "RigidBodyCon");

	/* set default settings */
	rbc->type = type;

	rbc->ob1 = NULL;
	rbc->ob2 = NULL;

	rbc->flag |= RBC_FLAG_ENABLED;
	rbc->flag |= RBC_FLAG_DISABLE_COLLISIONS;

	rbc->spring_type = RBC_SPRING_TYPE2;

	rbc->breaking_threshold = 10.0f; /* no good default here, just use 10 for now */
	rbc->num_solver_iterations = 10; /* 10 is Bullet default */

	rbc->limit_lin_x_lower = -1.0f;
	rbc->limit_lin_x_upper = 1.0f;
	rbc->limit_lin_y_lower = -1.0f;
	rbc->limit_lin_y_upper = 1.0f;
	rbc->limit_lin_z_lower = -1.0f;
	rbc->limit_lin_z_upper = 1.0f;
	rbc->limit_ang_x_lower = -M_PI_4;
	rbc->limit_ang_x_upper = M_PI_4;
	rbc->limit_ang_y_lower = -M_PI_4;
	rbc->limit_ang_y_upper = M_PI_4;
	rbc->limit_ang_z_lower = -M_PI_4;
	rbc->limit_ang_z_upper = M_PI_4;

	rbc->spring_damping_x = 0.5f;
	rbc->spring_damping_y = 0.5f;
	rbc->spring_damping_z = 0.5f;
	rbc->spring_damping_ang_x = 0.5f;
	rbc->spring_damping_ang_y = 0.5f;
	rbc->spring_damping_ang_z = 0.5f;
	rbc->spring_stiffness_x = 10.0f;
	rbc->spring_stiffness_y = 10.0f;
	rbc->spring_stiffness_z = 10.0f;
	rbc->spring_stiffness_ang_x = 10.0f;
	rbc->spring_stiffness_ang_y = 10.0f;
	rbc->spring_stiffness_ang_z = 10.0f;

	rbc->motor_lin_max_impulse = 1.0f;
	rbc->motor_lin_target_velocity = 1.0f;
	rbc->motor_ang_max_impulse = 1.0f;
	rbc->motor_ang_target_velocity = 1.0f;

	/* flag cache as outdated */
	BKE_rigidbody_cache_reset(rbw);

	/* return this object */
	return rbc;
}

/* ************************************** */
/* Utilities API */

/* Get RigidBody world for the given scene, creating one if needed
 *
 * \param scene: Scene to find active Rigid Body world for
 */
RigidBodyWorld *BKE_rigidbody_get_world(Scene *scene)
{
	/* sanity check */
	if (scene == NULL)
		return NULL;

	return scene->rigidbody_world;
}

void BKE_rigidbody_remove_object(Scene *scene, Object *ob)
{
	RigidBodyWorld *rbw = scene->rigidbody_world;
	RigidBodyOb *rbo = ob->rigidbody_object;
	RigidBodyCon *rbc;
	GroupObject *go;
	int i;

	if (rbw) {
		/* remove from rigidbody world, free object won't do this */
		if (rbw->physics_world && rbo->physics_object)
			RB_dworld_remove_body(rbw->physics_world, rbo->physics_object);

		/* remove object from array */
		if (rbw && rbw->objects) {
			for (i = 0; i < rbw->numbodies; i++) {
				if (rbw->objects[i] == ob) {
					rbw->objects[i] = NULL;
					break;
				}
			}
		}

		/* remove object from rigid body constraints */
		if (rbw->constraints) {
			for (go = rbw->constraints->gobject.first; go; go = go->next) {
				Object *obt = go->ob;
				if (obt && obt->rigidbody_constraint) {
					rbc = obt->rigidbody_constraint;
					if (ELEM(ob, rbc->ob1, rbc->ob2)) {
						BKE_rigidbody_remove_constraint(scene, obt);
					}
				}
			}
		}
	}

	/* remove object's settings */
	BKE_rigidbody_free_object(ob);

	/* flag cache as outdated */
	BKE_rigidbody_cache_reset(rbw);
}

void BKE_rigidbody_remove_constraint(Scene *scene, Object *ob)
{
	RigidBodyWorld *rbw = scene->rigidbody_world;
	RigidBodyCon *rbc = ob->rigidbody_constraint;

	/* remove from rigidbody world, free object won't do this */
	if (rbw && rbw->physics_world && rbc->physics_constraint) {
		RB_dworld_remove_constraint(rbw->physics_world, rbc->physics_constraint);
	}
	/* remove object's settings */
	BKE_rigidbody_free_constraint(ob);

	/* flag cache as outdated */
	BKE_rigidbody_cache_reset(rbw);
}


/* ************************************** */
/* Simulation Interface - Bullet */

/* Update object array and rigid body count so they're in sync with the rigid body group */
static void rigidbody_update_ob_array(RigidBodyWorld *rbw)
{
	GroupObject *go;
	int i, n;

	n = BLI_listbase_count(&rbw->group->gobject);

	if (rbw->numbodies != n) {
		rbw->numbodies = n;
		rbw->objects = realloc(rbw->objects, sizeof(Object *) * rbw->numbodies);
	}

	for (go = rbw->group->gobject.first, i = 0; go; go = go->next, i++) {
		Object *ob = go->ob;
		rbw->objects[i] = ob;
	}
}

static void rigidbody_update_sim_world(Scene *scene, RigidBodyWorld *rbw)
{
	float adj_gravity[3];

	zero_v3(adj_gravity);

	/* update gravity, since this RNA setting is not part of RigidBody settings */
	RB_dworld_set_gravity(rbw->physics_world, adj_gravity);

	/* update object array in case there are changes */
	rigidbody_update_ob_array(rbw);
}

static void rigidbody_update_sim_ob(Scene *scene, RigidBodyWorld *rbw, Object *ob, RigidBodyOb *rbo)
{
	float loc[3];
	float rot[4];
	float scale[3];

	/* only update if rigid body exists */
	if (rbo->physics_object == NULL)
		return;

	if (rbo->shape == RB_SHAPE_TRIMESH && rbo->flag & RBO_FLAG_USE_DEFORM) {
		DerivedMesh *dm = ob->derivedDeform;
		if (dm) {
			MVert *mvert = dm->getVertArray(dm);
			int totvert = dm->getNumVerts(dm);
			BoundBox *bb = BKE_object_boundbox_get(ob);

			RB_shape_trimesh_update(rbo->physics_shape, (float *)mvert, totvert, sizeof(MVert), bb->vec[0], bb->vec[6]);
		}
	}

	mat4_decompose(loc, rot, scale, ob->obmat);

	/* update scale for all objects */
	RB_body_set_scale(rbo->physics_object, scale);
	/* compensate for embedded convex hull collision margin */
	if (!(rbo->flag & RBO_FLAG_USE_MARGIN) && rbo->shape == RB_SHAPE_CONVEXH)
		RB_shape_set_margin(rbo->physics_shape, RBO_GET_MARGIN(rbo) * MIN3(scale[0], scale[1], scale[2]));

	/* make transformed objects temporarily kinmatic so that they can be moved by the user during simulation */
	if (ob->flag & SELECT && G.moving & G_TRANSFORM_OBJ) {
		RB_body_set_kinematic_state(rbo->physics_object, true);
		RB_body_set_mass(rbo->physics_object, 0.0f);
	}

	/* update rigid body location and rotation for kinematic bodies */
	if (rbo->flag & RBO_FLAG_KINEMATIC || (ob->flag & SELECT && G.moving & G_TRANSFORM_OBJ)) {
		RB_body_activate(rbo->physics_object);
		RB_body_set_loc_rot(rbo->physics_object, loc, rot);
	}
	/* update influence of effectors - but don't do it on an effector */
	/* only dynamic bodies need effector update */
	/* NOTE: passive objects don't need to be updated since they don't move */

	/* NOTE: no other settings need to be explicitly updated here,
	 * since RNA setters take care of the rest :)
	 */
}

/**
 * Updates and validates world, bodies and shapes.
 *
 * \param rebuild: Rebuild entire simulation
 */
static void rigidbody_update_simulation(Scene *scene, RigidBodyWorld *rbw, bool rebuild)
{
	GroupObject *go;

	/* update world */
	if (rebuild)
		BKE_rigidbody_validate_sim_world(scene, rbw, true);
	rigidbody_update_sim_world(scene, rbw);

	/* XXX TODO For rebuild: remove all constraints first.
	 * Otherwise we can end up deleting objects that are still
	 * referenced by constraints, corrupting bullet's internal list.
	 *
	 * Memory management needs redesign here, this is just a dirty workaround.
	 */
	if (rebuild && rbw->constraints) {
		for (go = rbw->constraints->gobject.first; go; go = go->next) {
			Object *ob = go->ob;
			if (ob) {
				RigidBodyCon *rbc = ob->rigidbody_constraint;
				if (rbc && rbc->physics_constraint) {
					RB_dworld_remove_constraint(rbw->physics_world, rbc->physics_constraint);
					RB_constraint_delete(rbc->physics_constraint);
					rbc->physics_constraint = NULL;
				}
			}
		}
	}

	/* update objects */
	for (go = rbw->group->gobject.first; go; go = go->next) {
		Object *ob = go->ob;

		if (ob && ob->type == OB_MESH) {
			/* validate that we've got valid object set up here... */
			RigidBodyOb *rbo = ob->rigidbody_object;
			/* update transformation matrix of the object so we don't get a frame of lag for simple animations */
			BKE_object_where_is_calc(scene, ob);

			if (rbo == NULL) {
				/* Since this object is included in the sim group but doesn't have
				 * rigid body settings (perhaps it was added manually), add!
				 * - assume object to be active? That is the default for newly added settings...
				 */
				ob->rigidbody_object = BKE_rigidbody_create_object(scene, ob, RBO_TYPE_ACTIVE);
				rigidbody_validate_sim_object(rbw, ob, true);

				rbo = ob->rigidbody_object;
			}
			else {
				/* perform simulation data updates as tagged */
				/* refresh object... */
				if (rebuild) {
					/* World has been rebuilt so rebuild object */
					rigidbody_validate_sim_object(rbw, ob, true);
				}
				else if (rbo->flag & RBO_FLAG_NEEDS_VALIDATE) {
					rigidbody_validate_sim_object(rbw, ob, false);
				}
				/* refresh shape... */
				if (rbo->flag & RBO_FLAG_NEEDS_RESHAPE) {
					/* mesh/shape data changed, so force shape refresh */
					rigidbody_validate_sim_shape(ob, true);
					/* now tell RB sim about it */
					// XXX: we assume that this can only get applied for active/passive shapes that will be included as rigidbodies
					RB_body_set_collision_shape(rbo->physics_object, rbo->physics_shape);
				}
				rbo->flag &= ~(RBO_FLAG_NEEDS_VALIDATE | RBO_FLAG_NEEDS_RESHAPE);
			}

			/* update simulation object... */
			rigidbody_update_sim_ob(scene, rbw, ob, rbo);
		}
	}

	/* update constraints */
	if (rbw->constraints == NULL) /* no constraints, move on */
		return;
	for (go = rbw->constraints->gobject.first; go; go = go->next) {
		Object *ob = go->ob;

		if (ob) {
			/* validate that we've got valid object set up here... */
			RigidBodyCon *rbc = ob->rigidbody_constraint;
			/* update transformation matrix of the object so we don't get a frame of lag for simple animations */
			BKE_object_where_is_calc(scene, ob);

			if (rbc == NULL) {
				/* Since this object is included in the group but doesn't have
				 * constraint settings (perhaps it was added manually), add!
				 */
				ob->rigidbody_constraint = BKE_rigidbody_create_constraint(scene, ob, RBC_TYPE_FIXED);
				rigidbody_validate_sim_constraint(rbw, ob, true);

				rbc = ob->rigidbody_constraint;
			}
			else {
				/* perform simulation data updates as tagged */
				if (rebuild) {
					/* World has been rebuilt so rebuild constraint */
					rigidbody_validate_sim_constraint(rbw, ob, true);
				}
				else if (rbc->flag & RBC_FLAG_NEEDS_VALIDATE) {
					rigidbody_validate_sim_constraint(rbw, ob, false);
				}
				rbc->flag &= ~RBC_FLAG_NEEDS_VALIDATE;
			}
		}
	}
}

static void rigidbody_update_simulation_post_step(RigidBodyWorld *rbw)
{
	GroupObject *go;

	for (go = rbw->group->gobject.first; go; go = go->next) {
		Object *ob = go->ob;

		if (ob) {
			RigidBodyOb *rbo = ob->rigidbody_object;
			/* reset kinematic state for transformed objects */
			if (rbo && (ob->flag & SELECT) && (G.moving & G_TRANSFORM_OBJ)) {
				RB_body_set_kinematic_state(rbo->physics_object, rbo->flag & RBO_FLAG_KINEMATIC || rbo->flag & RBO_FLAG_DISABLED);
				RB_body_set_mass(rbo->physics_object, RBO_GET_MASS(rbo));
				/* deactivate passive objects so they don't interfere with deactivation of active objects */
				if (rbo->type == RBO_TYPE_PASSIVE)
					RB_body_deactivate(rbo->physics_object);
			}
		}
	}
}

bool BKE_rigidbody_check_sim_running(RigidBodyWorld *rbw, float ctime)
{
	return (rbw && (rbw->flag & RBW_FLAG_MUTED) == 0 );
}

/* Sync rigid body and object transformations */
void BKE_rigidbody_sync_transforms(RigidBodyWorld *rbw, Object *ob, float ctime)
{
	RigidBodyOb *rbo = ob->rigidbody_object;

	/* keep original transform for kinematic and passive objects */
	if (ELEM(NULL, rbw, rbo) || rbo->flag & RBO_FLAG_KINEMATIC || rbo->type == RBO_TYPE_PASSIVE)
		return;

	/* use rigid body transform after cache start frame if objects is not being transformed */
	if (BKE_rigidbody_check_sim_running(rbw, ctime) && !(ob->flag & SELECT && G.moving & G_TRANSFORM_OBJ)) {
		float mat[4][4], size_mat[4][4], size[3];

		normalize_qt(rbo->orn); // RB_TODO investigate why quaternion isn't normalized at this point
		quat_to_mat4(mat, rbo->orn);
		copy_v3_v3(mat[3], rbo->pos);

		mat4_to_size(size, ob->obmat);
		size_to_mat4(size_mat, size);
		mul_m4_m4m4(mat, mat, size_mat);

		copy_m4_m4(ob->obmat, mat);
	}
	/* otherwise set rigid body transform to current obmat */
	else {
		mat4_to_loc_quat(rbo->pos, rbo->orn, ob->obmat);
	}
}

/* Used when canceling transforms - return rigidbody and object to initial states */
void BKE_rigidbody_aftertrans_update(Object *ob, float loc[3], float rot[3], float quat[4], float rotAxis[3], float rotAngle)
{
	RigidBodyOb *rbo = ob->rigidbody_object;
	bool correct_delta = !(rbo->flag & RBO_FLAG_KINEMATIC || rbo->type == RBO_TYPE_PASSIVE);

	/* return rigid body and object to their initial states */
	copy_v3_v3(rbo->pos, ob->loc);
	copy_v3_v3(ob->loc, loc);

	if (correct_delta) {
		add_v3_v3(rbo->pos, ob->dloc);
	}

	if (ob->rotmode > 0) {
		float qt[4];
		eulO_to_quat(qt, ob->rot, ob->rotmode);

		if (correct_delta) {
			float dquat[4];
			eulO_to_quat(dquat, ob->drot, ob->rotmode);

			mul_qt_qtqt(rbo->orn, dquat, qt);
		}
		else {
			copy_qt_qt(rbo->orn, qt);
		}

		copy_v3_v3(ob->rot, rot);
	}
	else if (ob->rotmode == ROT_MODE_AXISANGLE) {
		float qt[4];
		axis_angle_to_quat(qt, ob->rotAxis, ob->rotAngle);

		if (correct_delta) {
			float dquat[4];
			axis_angle_to_quat(dquat, ob->drotAxis, ob->drotAngle);

			mul_qt_qtqt(rbo->orn, dquat, qt);
		}
		else {
			copy_qt_qt(rbo->orn, qt);
		}

		copy_v3_v3(ob->rotAxis, rotAxis);
		ob->rotAngle = rotAngle;
	}
	else {
		if (correct_delta) {
			mul_qt_qtqt(rbo->orn, ob->dquat, ob->quat);
		}
		else {
			copy_qt_qt(rbo->orn, ob->quat);
		}

		copy_qt_qt(ob->quat, quat);
	}

	if (rbo->physics_object) {
		/* allow passive objects to return to original transform */
		if (rbo->type == RBO_TYPE_PASSIVE)
			RB_body_set_kinematic_state(rbo->physics_object, true);
		RB_body_set_loc_rot(rbo->physics_object, rbo->pos, rbo->orn);
	}
	// RB_TODO update rigid body physics object's loc/rot for dynamic objects here as well (needs to be done outside bullet's update loop)
}

void BKE_rigidbody_cache_reset(RigidBodyWorld *rbw)
{
}

/* ------------------ */

/* Rebuild rigid body world */
/* NOTE: this needs to be called before frame update to work correctly */
void BKE_rigidbody_rebuild_world(Scene *scene, float ctime)
{

}

/* Run RigidBody simulation for the specified physics world */
void BKE_rigidbody_do_simulation(Scene *scene, float ctime)
{
	float timestep;
	RigidBodyWorld *rbw = scene->rigidbody_world;
	int startframe = 0, endframe = 0;

	if (ctime <= startframe) {
		rbw->ltime = startframe;
		return;
	}
	/* make sure we don't go out of cache frame range */
	else if (ctime > endframe) {
		ctime = endframe;
	}

	/* don't try to run the simulation if we don't have a world yet but allow reading baked cache */
	if (rbw->physics_world == NULL)
		return;
	else if (rbw->objects == NULL)
		rigidbody_update_ob_array(rbw);

	/* try to read from cache */
	// RB_TODO deal with interpolated, old and baked results
	bool can_simulate = (ctime == rbw->ltime + 1);

	/* advance simulation, we can only step one frame forward */
	if (can_simulate) {

		/* update and validate simulation */
		rigidbody_update_simulation(scene, rbw, false);

		/* calculate how much time elapsed since last step in seconds */
		timestep = (ctime - rbw->ltime) * rbw->time_scale;
		/* step simulation by the requested timestep, steps per second are adjusted to take time scale into account */
		RB_dworld_step_simulation(rbw->physics_world, timestep, INT_MAX, 1.0f / (float)rbw->steps_per_second * min_ff(rbw->time_scale, 1.0f));

		rigidbody_update_simulation_post_step(rbw);

		/* write cache for current frame */

		rbw->ltime = ctime;
	}
}
/* ************************************** */

#else  /* WITH_BULLET */

/* stubs */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

struct RigidBodyOb *BKE_rigidbody_copy_object(const Object *ob, const int flag) { return NULL; }
struct RigidBodyCon *BKE_rigidbody_copy_constraint(const Object *ob, const int flag) { return NULL; }
void BKE_rigidbody_validate_sim_world(Scene *scene, RigidBodyWorld *rbw, bool rebuild) {}
void BKE_rigidbody_calc_volume(Object *ob, float *r_vol) { if (r_vol) *r_vol = 0.0f; }
void BKE_rigidbody_calc_center_of_mass(Object *ob, float r_center[3]) { zero_v3(r_center); }
struct RigidBodyWorld *BKE_rigidbody_create_world(Scene *scene) { return NULL; }
struct RigidBodyWorld *BKE_rigidbody_world_copy(RigidBodyWorld *rbw, const int flag) { return NULL; }
void BKE_rigidbody_world_groups_relink(struct RigidBodyWorld *rbw) {}
void BKE_rigidbody_world_id_loop(struct RigidBodyWorld *rbw, RigidbodyWorldIDFunc func, void *userdata) {}
struct RigidBodyOb *BKE_rigidbody_create_object(Scene *scene, Object *ob, short type) { return NULL; }
struct RigidBodyCon *BKE_rigidbody_create_constraint(Scene *scene, Object *ob, short type) { return NULL; }
struct RigidBodyWorld *BKE_rigidbody_get_world(Scene *scene) { return NULL; }
void BKE_rigidbody_remove_object(Scene *scene, Object *ob) {}
void BKE_rigidbody_remove_constraint(Scene *scene, Object *ob) {}
void BKE_rigidbody_sync_transforms(RigidBodyWorld *rbw, Object *ob, float ctime) {}
void BKE_rigidbody_aftertrans_update(Object *ob, float loc[3], float rot[3], float quat[4], float rotAxis[3], float rotAngle) {}
bool BKE_rigidbody_check_sim_running(RigidBodyWorld *rbw, float ctime) { return false; }
void BKE_rigidbody_cache_reset(RigidBodyWorld *rbw) {}
void BKE_rigidbody_rebuild_world(Scene *scene, float ctime) {}
void BKE_rigidbody_do_simulation(Scene *scene, float ctime) {}

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#endif  /* WITH_BULLET */

/* -------------------- */
/* Depsgraph evaluation */

void BKE_rigidbody_rebuild_sim(Scene *scene)
{
	float ctime = 0.0f;
	/* rebuild sim data (i.e. after resetting to start of timeline) */
	if (BKE_scene_check_rigidbody_active(scene)) {
		BKE_rigidbody_rebuild_world(scene, ctime);
	}
}

void BKE_rigidbody_eval_simulation(Scene *scene)
{
	float ctime = 0.0f;
	/* evaluate rigidbody sim */
	if (BKE_scene_check_rigidbody_active(scene)) {
		BKE_rigidbody_do_simulation(scene, ctime);
	}
}

void BKE_rigidbody_object_sync_transforms(Scene *scene,
                                          Object *ob)
{
	RigidBodyWorld *rbw = scene->rigidbody_world;
	float ctime = 0.0f;
	/* read values pushed into RBO from sim/cache... */
	BKE_rigidbody_sync_transforms(rbw, ob, ctime);
}
