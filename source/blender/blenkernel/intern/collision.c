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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 */

/** \file blender/blenkernel/intern/collision.c
 *  \ingroup bke
 */


#include "MEM_guardedalloc.h"

#include "DNA_group_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_utildefines.h"
#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_edgehash.h"

#include "BKE_modifier.h"
#include "BKE_scene.h"

#ifdef WITH_BULLET
#include "Bullet-C-Api.h"
#endif
#include "BLI_kdopbvh.h"
#include "BKE_collision.h"

#ifdef WITH_ELTOPO
#include "eltopo-capi.h"
#endif


/***********************************
Collision modifier code start
***********************************/

/* step is limited from 0 (frame start position) to 1 (frame end position) */
void collision_move_object(CollisionModifierData *collmd, float step, float prevstep)
{
	float tv[3] = {0, 0, 0};
	unsigned int i = 0;

	/* the collider doesn't move this frame */
	if (collmd->is_static) {
		for (i = 0; i < collmd->mvert_num; i++) {
			zero_v3(collmd->current_v[i].co);
		}

		return;
	}

	for (i = 0; i < collmd->mvert_num; i++) {
		sub_v3_v3v3(tv, collmd->xnew[i].co, collmd->x[i].co);
		VECADDS(collmd->current_x[i].co, collmd->x[i].co, tv, prevstep);
		VECADDS(collmd->current_xnew[i].co, collmd->x[i].co, tv, step);
		sub_v3_v3v3(collmd->current_v[i].co, collmd->current_xnew[i].co, collmd->current_x[i].co);
	}

	bvhtree_update_from_mvert(
	        collmd->bvhtree, collmd->current_x, collmd->current_xnew,
	        collmd->tri, collmd->tri_num, true);
}

BVHTree *bvhtree_build_from_mvert(
        const MVert *mvert,
        const struct MVertTri *tri, int tri_num,
        float epsilon)
{
	BVHTree *tree;
	const MVertTri *vt;
	int i;

	tree = BLI_bvhtree_new(tri_num, epsilon, 4, 26);

	/* fill tree */
	for (i = 0, vt = tri; i < tri_num; i++, vt++) {
		float co[3][3];

		copy_v3_v3(co[0], mvert[vt->tri[0]].co);
		copy_v3_v3(co[1], mvert[vt->tri[1]].co);
		copy_v3_v3(co[2], mvert[vt->tri[2]].co);

		BLI_bvhtree_insert(tree, i, co[0], 3);
	}

	/* balance tree */
	BLI_bvhtree_balance(tree);

	return tree;
}

void bvhtree_update_from_mvert(
        BVHTree *bvhtree,
        const MVert *mvert, const MVert *mvert_moving,
        const MVertTri *tri, int tri_num,
        bool moving)
{
	const MVertTri *vt;
	int i;

	if ((bvhtree == NULL) || (mvert == NULL)) {
		return;
	}

	if (mvert_moving == NULL) {
		moving = false;
	}

	for (i = 0, vt = tri; i < tri_num; i++, vt++) {
		float co[3][3];
		bool ret;

		copy_v3_v3(co[0], mvert[vt->tri[0]].co);
		copy_v3_v3(co[1], mvert[vt->tri[1]].co);
		copy_v3_v3(co[2], mvert[vt->tri[2]].co);

		/* copy new locations into array */
		if (moving) {
			float co_moving[3][3];
			/* update moving positions */
			copy_v3_v3(co_moving[0], mvert_moving[vt->tri[0]].co);
			copy_v3_v3(co_moving[1], mvert_moving[vt->tri[1]].co);
			copy_v3_v3(co_moving[2], mvert_moving[vt->tri[2]].co);

			ret = BLI_bvhtree_update_node(bvhtree, i, &co[0][0], &co_moving[0][0], 3);
		}
		else {
			ret = BLI_bvhtree_update_node(bvhtree, i, &co[0][0], NULL, 3);
		}

		/* check if tree is already full */
		if (ret == false) {
			break;
		}
	}

	BLI_bvhtree_update_tree(bvhtree);
}

/***********************************
Collision modifier code end
***********************************/

// w3 is not perfect
static void collision_compute_barycentric ( float pv[3], float p1[3], float p2[3], float p3[3], float *w1, float *w2, float *w3 )
{
	/* dot_v3v3 */
#define INPR(v1, v2) ( (v1)[0] * (v2)[0] + (v1)[1] * (v2)[1] + (v1)[2] * (v2)[2])

	double tempV1[3], tempV2[3], tempV4[3];
	double a, b, c, d, e, f;

	VECSUB ( tempV1, p1, p3 );
	VECSUB ( tempV2, p2, p3 );
	VECSUB ( tempV4, pv, p3 );

	a = INPR ( tempV1, tempV1 );
	b = INPR ( tempV1, tempV2 );
	c = INPR ( tempV2, tempV2 );
	e = INPR ( tempV1, tempV4 );
	f = INPR ( tempV2, tempV4 );

	d = ( a * c - b * b );

	if ( ABS ( d ) < (double)ALMOST_ZERO ) {
		*w1 = *w2 = *w3 = 1.0 / 3.0;
		return;
	}

	w1[0] = ( float ) ( ( e * c - b * f ) / d );

	if ( w1[0] < 0 )
		w1[0] = 0;

	w2[0] = ( float ) ( ( f - b * ( double ) w1[0] ) / c );

	if ( w2[0] < 0 )
		w2[0] = 0;

	w3[0] = 1.0f - w1[0] - w2[0];

#undef INPR
}

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif

DO_INLINE void collision_interpolateOnTriangle ( float to[3], float v1[3], float v2[3], float v3[3], double w1, double w2, double w3 )
{
	zero_v3(to);
	VECADDMUL(to, v1, w1);
	VECADDMUL(to, v2, w2);
	VECADDMUL(to, v3, w3);
}

static void add_collision_object(Object ***objs, unsigned int *numobj, unsigned int *maxobj, Object *ob, Object *self, int level, unsigned int modifier_type)
{
	CollisionModifierData *cmd= NULL;

	if (ob == self)
		return;

	/* only get objects with collision modifier */
	if (((modifier_type == eModifierType_Collision) ) || (modifier_type != eModifierType_Collision))
		cmd= (CollisionModifierData *)modifiers_findByType(ob, modifier_type);

	if (cmd) {
		/* extend array */
		if (*numobj >= *maxobj) {
			*maxobj *= 2;
			*objs= MEM_reallocN(*objs, sizeof(Object *)*(*maxobj));
		}

		(*objs)[*numobj] = ob;
		(*numobj)++;
	}

	/* objects in dupli groups, one level only for now */
	if (ob->dup_group && level == 0) {
		GroupObject *go;
		Group *group= ob->dup_group;

		/* add objects */
		for (go= group->gobject.first; go; go= go->next)
			add_collision_object(objs, numobj, maxobj, go->ob, self, level+1, modifier_type);
	}
}

// return all collision objects in scene
// collision object will exclude self
Object **get_collisionobjects_ext(Scene *scene, Object *self, Group *group, int layer, unsigned int *numcollobj, unsigned int modifier_type, bool dupli)
{
	Base *base;
	Object **objs;
	GroupObject *go;
	unsigned int numobj= 0, maxobj= 100;
	int level = dupli ? 0 : 1;

	objs= MEM_callocN(sizeof(Object *)*maxobj, "CollisionObjectsArray");

	/* gather all collision objects */
	if (group) {
		/* use specified group */
		for (go= group->gobject.first; go; go= go->next)
			add_collision_object(&objs, &numobj, &maxobj, go->ob, self, level, modifier_type);
	}
	else {
		Scene *sce_iter;
		/* add objects in same layer in scene */
		for (SETLOOPER(scene, sce_iter, base)) {
			if ( base->lay & layer )
				add_collision_object(&objs, &numobj, &maxobj, base->object, self, level, modifier_type);

		}
	}

	*numcollobj= numobj;

	return objs;
}

Object **get_collisionobjects(Scene *scene, Object *self, Group *group, unsigned int *numcollobj, unsigned int modifier_type)
{
	/* Need to check for active layers, too.
	   Otherwise this check fails if the objects are not on the same layer - DG */
	return get_collisionobjects_ext(scene, self, group, self->lay | scene->lay, numcollobj, modifier_type, true);
}

static void add_collider_cache_object(ListBase **objs, Object *ob, Object *self, int level)
{
	CollisionModifierData *cmd= NULL;
	ColliderCache *col;

	if (ob == self)
		return;

	if (cmd && cmd->bvhtree) {
		if (*objs == NULL)
			*objs = MEM_callocN(sizeof(ListBase), "ColliderCache array");

		col = MEM_callocN(sizeof(ColliderCache), "ColliderCache");
		col->ob = ob;
		col->collmd = cmd;
		/* make sure collider is properly set up */
		collision_move_object(cmd, 1.0, 0.0);
		BLI_addtail(*objs, col);
	}

	/* objects in dupli groups, one level only for now */
	if (ob->dup_group && level == 0) {
		GroupObject *go;
		Group *group= ob->dup_group;

		/* add objects */
		for (go= group->gobject.first; go; go= go->next)
			add_collider_cache_object(objs, go->ob, self, level+1);
	}
}

ListBase *get_collider_cache(Scene *scene, Object *self, Group *group)
{
	GroupObject *go;
	ListBase *objs= NULL;

	/* add object in same layer in scene */
	if (group) {
		for (go= group->gobject.first; go; go= go->next)
			add_collider_cache_object(&objs, go->ob, self, 0);
	}
	else {
		Scene *sce_iter;
		Base *base;

		/* add objects in same layer in scene */
		for (SETLOOPER(scene, sce_iter, base)) {
			if (!self || (base->lay & self->lay))
				add_collider_cache_object(&objs, base->object, self, 0);

		}
	}

	return objs;
}

void free_collider_cache(ListBase **colliders)
{
	if (*colliders) {
		BLI_freelistN(*colliders);
		MEM_freeN(*colliders);
		*colliders = NULL;
	}
}


BLI_INLINE void max_v3_v3v3(float r[3], const float a[3], const float b[3])
{
	r[0] = max_ff(a[0], b[0]);
	r[1] = max_ff(a[1], b[1]);
	r[2] = max_ff(a[2], b[2]);
}

void collision_get_collider_velocity(float vel_old[3], float vel_new[3], CollisionModifierData *collmd, CollPair *collpair)
{
	float u1, u2, u3;

	/* compute barycentric coordinates */
	collision_compute_barycentric(collpair->pb,
	                              collmd->current_x[collpair->bp1].co,
	                              collmd->current_x[collpair->bp2].co,
	                              collmd->current_x[collpair->bp3].co,
	                              &u1, &u2, &u3);

	collision_interpolateOnTriangle(vel_new, collmd->current_v[collpair->bp1].co, collmd->current_v[collpair->bp2].co, collmd->current_v[collpair->bp3].co, u1, u2, u3);
	/* XXX assume constant velocity of the collider for now */
	copy_v3_v3(vel_old, vel_new);
}
