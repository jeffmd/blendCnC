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
 * The Original Code is Copyright (C) 20014 by Blender Foundation.
 * All rights reserved.
 */

/** \file blender/blenkernel/intern/object_update.c
 *  \ingroup bke
 */

#include "DNA_group_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_threads.h"

#include "BKE_DerivedMesh.h"
#include "BKE_displist.h"
#include "BKE_editmesh.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_lamp.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_object.h"
#include "BKE_scene.h"

static ThreadMutex material_lock = BLI_MUTEX_INITIALIZER;

void BKE_object_eval_local_transform(Object *ob)
{
	/* calculate local matrix */
	BKE_object_to_mat4(ob, ob->obmat);
}

/* Evaluate parent */
/* NOTE: based on solve_parenting(), but with the cruft stripped out */
void BKE_object_eval_parent(Scene *scene,
                            Object *ob)
{
	Object *par = ob->parent;

	float totmat[4][4];
	float tmat[4][4];
	float locmat[4][4];

	/* get local matrix (but don't calculate it, as that was done already!) */
	// XXX: redundant?
	copy_m4_m4(locmat, ob->obmat);

	/* get parent effect matrix */
	BKE_object_get_parent_matrix(scene, ob, par, totmat);

	/* total */
	mul_m4_m4m4(tmat, totmat, ob->parentinv);
	mul_m4_m4m4(ob->obmat, tmat, locmat);

	copy_v3_v3(ob->orig, totmat[3]);
}

void BKE_object_eval_done(Object *ob)
{
	/* Set negative scale flag in object. */
	if (is_negative_m4(ob->obmat)) ob->transflag |= OB_NEG_SCALE;
	else ob->transflag &= ~OB_NEG_SCALE;
}

void BKE_object_handle_data_update(
        Main *bmain,
        Scene *scene,
        Object *ob)
{
	float ctime = 0.0f;

	/* includes all keys and modifiers */
	switch (ob->type) {
		case OB_MESH:
		{
			BMEditMesh *em = (ob == scene->obedit) ? BKE_editmesh_from_object(ob) : NULL;
			uint64_t data_mask = scene->customdata_mask | CD_MASK_BAREMESH;
			if (em) {
				makeDerivedMesh(scene, ob, em,  data_mask, false); /* was CD_MASK_BAREMESH */
			}
			else {
				makeDerivedMesh(scene, ob, NULL, data_mask, false);
			}
			break;
		}
		case OB_CURVE:
		case OB_SURF:
		case OB_FONT:
			BKE_displist_make_curveTypes(scene, ob, 0);
			break;

	}

	/* related materials */
	/* XXX: without depsgraph tagging, this will always need to be run, which will be slow!
	 * However, not doing anything (or trying to hack around this lack) is not an option
	 * anymore, especially due to Cycles [#31834]
	 */
	if (ob->totcol) {
		int a;
		if (ob->totcol != 0) {
			BLI_mutex_lock(&material_lock);
			for (a = 1; a <= ob->totcol; a++) {
				Material *ma = give_current_material(ob, a);
				if (ma) {
					/* recursively update drivers for this material */
					material_drivers_update(scene, ma, ctime);
				}
			}
			BLI_mutex_unlock(&material_lock);
		}
	}
	else if (ob->type == OB_LAMP)
		lamp_drivers_update(scene, ob->data, ctime);


	/* quick cache removed */
}

bool BKE_object_eval_proxy_copy(Object *object)
{
	/* Handle proxy copy for target, */
	if (ID_IS_LINKED(object) && object->proxy_from) {
		if (object->proxy_from->proxy_group) {
			/* Transform proxy into group space. */
			Object *obg = object->proxy_from->proxy_group;
			float imat[4][4];
			invert_m4_m4(imat, obg->obmat);
			mul_m4_m4m4(object->obmat, imat, object->proxy_from->obmat);
			/* Should always be true. */
			if (obg->dup_group) {
				add_v3_v3(object->obmat[3], obg->dup_group->dupli_ofs);
			}
		}
		else {
			copy_m4_m4(object->obmat, object->proxy_from->obmat);
		}
		return true;
	}
	return false;
}

void BKE_object_eval_uber_transform(Object *object)
{
	BKE_object_eval_proxy_copy(object);
	object->recalc &= ~(OB_RECALC_OB | OB_RECALC_TIME);
	if (object->data == NULL) {
		object->recalc &= ~OB_RECALC_DATA;
	}
}

void BKE_object_eval_uber_data(Main *bmain, 
                               Scene *scene,
                               Object *ob)
{
	BKE_object_handle_data_update(bmain, scene, ob);

	ob->recalc &= ~(OB_RECALC_DATA | OB_RECALC_TIME);
}

void BKE_object_eval_transform_all(Scene *scene,
                                   Object *object)
{
	/* This mimics full transform update chain from new depsgraph. */
	BKE_object_eval_local_transform(object);
	if (object->parent != NULL) {
		BKE_object_eval_parent(scene, object);
	}
	BKE_object_eval_uber_transform(object);
	BKE_object_eval_done(object);
}
