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

/** \file blender/blenkernel/intern/object.c
 *  \ingroup bke
 */


#include <string.h>
#include <math.h>
#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "DNA_camera_types.h"
#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"
#include "DNA_object_types.h"
#include "DNA_property_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_curve_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_linklist.h"
#include "BLI_kdtree.h"

#include "BLT_translation.h"

#include "BKE_pbvh.h"
#include "BKE_main.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_bullet.h"
#include "BKE_deform.h"
#include "BKE_DerivedMesh.h"
#include "BKE_curve.h"
#include "BKE_displist.h"
#include "BKE_group.h"
#include "BKE_icons.h"
#include "BKE_lamp.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_mesh.h"
#include "BKE_editmesh.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_subsurf.h"
#include "BKE_material.h"
#include "BKE_camera.h"
#include "BKE_image.h"

#ifdef WITH_PYTHON
#include "BPY_extern.h"
#endif

#include "CCGSubSurf.h"
#include "atomic_ops.h"

#include "GPU_material.h"

/* Vertex parent modifies original BMesh which is not safe for threading.
 * Ideally such a modification should be handled as a separate DAG update
 * callback for mesh datablock, but for until it is actually supported use
 * simpler solution with a mutex lock.
 *                                               - sergey -
 */
#define VPARENT_THREADING_HACK

#ifdef VPARENT_THREADING_HACK
static ThreadMutex vparent_lock = BLI_MUTEX_INITIALIZER;
#endif

void BKE_object_workob_clear(Object *workob)
{
	memset(workob, 0, sizeof(Object));

	workob->size[0] = workob->size[1] = workob->size[2] = 1.0f;
	workob->dscale[0] = workob->dscale[1] = workob->dscale[2] = 1.0f;
	workob->mode = ROT_MODE_EUL;
}

void BKE_object_update_base_layer(struct Scene *scene, Object *ob)
{
	Base *base = scene->base.first;

	while (base) {
		if (base->object == ob) base->lay = ob->lay;
		base = base->next;
	}
}

void BKE_object_free_curve_cache(Object *ob)
{
	if (ob->curve_cache) {
		BKE_displist_free(&ob->curve_cache->disp);
		BKE_curve_bevelList_free(&ob->curve_cache->bev);
		BKE_nurbList_free(&ob->curve_cache->deformed_nurbs);
		MEM_freeN(ob->curve_cache);
		ob->curve_cache = NULL;
	}
}

void BKE_object_free_modifiers(Object *ob, const int flag)
{
	ModifierData *md;

	while ((md = BLI_pophead(&ob->modifiers))) {
		modifier_free_ex(md, flag);
	}

	/* modifiers may have stored data in the DM cache */
	BKE_object_free_derived_caches(ob);
}

void BKE_object_modifier_hook_reset(Object *ob, HookModifierData *hmd)
{
	/* reset functionality */
	if (hmd->object) {
		invert_m4_m4(hmd->object->imat, hmd->object->obmat);
		mul_m4_m4m4(hmd->parentinv, hmd->object->imat, ob->obmat);
	}
}

bool BKE_object_support_modifier_type_check(const Object *ob, int modifier_type)
{
	const ModifierTypeInfo *mti;

	mti = modifierType_getInfo(modifier_type);

	/* only geometry objects should be able to get modifiers [#25291] */
	if (!ELEM(ob->type, OB_MESH, OB_CURVE, OB_SURF, OB_FONT)) {
		return false;
	}

	if (!((mti->flags & eModifierTypeFlag_AcceptsCVs) ||
	      (ob->type == OB_MESH && (mti->flags & eModifierTypeFlag_AcceptsMesh))))
	{
		return false;
	}

	return true;
}

void BKE_object_link_modifiers(struct Object *ob_dst, const struct Object *ob_src)
{
	ModifierData *md;
	BKE_object_free_modifiers(ob_dst, 0);

	if (!ELEM(ob_dst->type, OB_MESH, OB_CURVE, OB_SURF, OB_FONT)) {
		/* only objects listed above can have modifiers and linking them to objects
		 * which doesn't have modifiers stack is quite silly */
		return;
	}

	for (md = ob_src->modifiers.first; md; md = md->next) {
		ModifierData *nmd = NULL;

		if (ELEM(md->type,
		         eModifierType_Hook,
		         eModifierType_Collision))
		{
			continue;
		}

		if (!BKE_object_support_modifier_type_check(ob_dst, md->type))
			continue;


		nmd = modifier_new(md->type);
		BLI_strncpy(nmd->name, md->name, sizeof(nmd->name));

		modifier_copyData(md, nmd);
		BLI_addtail(&ob_dst->modifiers, nmd);
		modifier_unique_name(&ob_dst->modifiers, nmd);
	}

	/* TODO: smoke?, cloth? */
}

/* free data derived from mesh, called when mesh changes or is freed */
void BKE_object_free_derived_caches(Object *ob)
{
	/* Also serves as signal to remake texspace.
	 *
	 * NOTE: This function can be called from threads on different objects
	 * sharing same data datablock. So we need to ensure atomic nature of
	 * data modification here.
	 */
	if (ob->type == OB_MESH) {
		Mesh *me = ob->data;

		if (me && me->bb) {
			atomic_fetch_and_or_int32(&me->bb->flag, BOUNDBOX_DIRTY);
		}
	}
	else if (ELEM(ob->type, OB_SURF, OB_CURVE, OB_FONT)) {
		Curve *cu = ob->data;

		if (cu && cu->bb) {
			atomic_fetch_and_or_int32(&cu->bb->flag, BOUNDBOX_DIRTY);
		}
	}

	if (ob->bb) {
		MEM_freeN(ob->bb);
		ob->bb = NULL;
	}

	if (ob->derivedFinal) {
		ob->derivedFinal->needsFree = 1;
		ob->derivedFinal->release(ob->derivedFinal);
		ob->derivedFinal = NULL;
	}
	if (ob->derivedDeform) {
		ob->derivedDeform->needsFree = 1;
		ob->derivedDeform->release(ob->derivedDeform);
		ob->derivedDeform = NULL;
	}

	BKE_object_free_curve_cache(ob);
}

void BKE_object_free_caches(Object *object)
{

}

/** Free (or release) any data used by this object (does not free the object itself). */
void BKE_object_free(Object *ob)
{

	/* BKE_<id>_free shall never touch to ID->us. Never ever. */
	BKE_object_free_modifiers(ob, LIB_ID_CREATE_NO_USER_REFCOUNT);

	MEM_SAFE_FREE(ob->mat);
	MEM_SAFE_FREE(ob->matbits);
	MEM_SAFE_FREE(ob->iuser);
	MEM_SAFE_FREE(ob->bb);

	BLI_freelistN(&ob->defbase);

	BKE_rigidbody_free_object(ob);
	BKE_rigidbody_free_constraint(ob);

	GPU_lamp_free(ob);

	BLI_freelistN(&ob->pc_ids);

	BLI_freelistN(&ob->lodlevels);

	/* Free runtime curves data. */
	if (ob->curve_cache) {
		BKE_curve_bevelList_free(&ob->curve_cache->bev);
		MEM_freeN(ob->curve_cache);
		ob->curve_cache = NULL;
	}

	BKE_previewimg_free(&ob->preview);
}

/* actual check for internal data, not context or flags */
bool BKE_object_is_in_editmode(const Object *ob)
{
	if (ob->data == NULL)
		return false;

	if (ob->type == OB_MESH) {
		Mesh *me = ob->data;
		if (me->edit_btmesh)
			return true;
	}
	else if (ob->type == OB_FONT) {
		Curve *cu = ob->data;

		if (cu->editfont)
			return true;
	}
	else if (ob->type == OB_SURF || ob->type == OB_CURVE) {
		Curve *cu = ob->data;

		if (cu->editnurb)
			return true;
	}
	return false;
}

bool BKE_object_is_in_editmode_vgroup(const Object *ob)
{
	return (OB_TYPE_SUPPORT_VGROUP(ob->type) &&
	        BKE_object_is_in_editmode(ob));
}

bool BKE_object_is_in_wpaint_select_vert(const Object *ob)
{
	return false;
}

bool BKE_object_exists_check(Main *bmain, const Object *obtest)
{
	Object *ob;

	if (obtest == NULL) return false;

	ob = bmain->object.first;
	while (ob) {
		if (ob == obtest) return true;
		ob = ob->id.next;
	}
	return false;
}

/* *************************************************** */

static const char *get_obdata_defname(int type)
{
	switch (type) {
		case OB_MESH: return DATA_("Mesh");
		case OB_CURVE: return DATA_("Curve");
		case OB_SURF: return DATA_("Surf");
		case OB_FONT: return DATA_("Text");
		case OB_CAMERA: return DATA_("Camera");
		case OB_LAMP: return DATA_("Lamp");
		case OB_EMPTY: return DATA_("Empty");
		default:
			printf("get_obdata_defname: Internal error, bad type: %d\n", type);
			return DATA_("Empty");
	}
}

void *BKE_object_obdata_add_from_type(Main *bmain, int type, const char *name)
{
	if (name == NULL) {
		name = get_obdata_defname(type);
	}

	switch (type) {
		case OB_MESH:      return BKE_mesh_add(bmain, name);
		case OB_CURVE:     return BKE_curve_add(bmain, name, OB_CURVE);
		case OB_SURF:      return BKE_curve_add(bmain, name, OB_SURF);
		case OB_FONT:      return BKE_curve_add(bmain, name, OB_FONT);
		case OB_CAMERA:    return BKE_camera_add(bmain, name);
		case OB_LAMP:      return BKE_lamp_add(bmain, name);
		case OB_EMPTY:     return NULL;
		default:
			printf("%s: Internal error, bad type: %d\n", __func__, type);
			return NULL;
	}
}

void BKE_object_init(Object *ob)
{
	/* BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(ob, id)); */  /* ob->type is already initialized... */

	ob->col[0] = ob->col[1] = ob->col[2] = 1.0;
	ob->col[3] = 1.0;

	ob->size[0] = ob->size[1] = ob->size[2] = 1.0;
	ob->dscale[0] = ob->dscale[1] = ob->dscale[2] = 1.0;

	/* objects should default to having Euler XYZ rotations,
	 * but rotations default to quaternions
	 */
	ob->rotmode = ROT_MODE_EUL;

	unit_axis_angle(ob->rotAxis, &ob->rotAngle);
	unit_axis_angle(ob->drotAxis, &ob->drotAngle);

	unit_qt(ob->quat);
	unit_qt(ob->dquat);

	/* rotation locks should be 4D for 4 component rotations by default... */
	ob->protectflag = OB_LOCK_ROT4D;

	unit_m4(ob->constinv);
	unit_m4(ob->parentinv);
	unit_m4(ob->obmat);
	ob->dt = OB_TEXTURE;
	ob->empty_drawtype = OB_PLAINAXES;
	ob->empty_drawsize = 1.0;
	if (ob->type == OB_EMPTY) {
		copy_v2_fl(ob->ima_ofs, -0.5f);
	}

	/* Game engine defaults*/
	ob->mass = ob->inertia = 1.0f;
	ob->formfactor = 0.4f;
	ob->damping = 0.04f;
	ob->rdamping = 0.1f;
	ob->anisotropicFriction[0] = 1.0f;
	ob->anisotropicFriction[1] = 1.0f;
	ob->anisotropicFriction[2] = 1.0f;
	ob->margin = 0.04f;
	ob->init_state = 1;
	ob->state = 1;
	ob->obstacleRad = 1.0f;
	ob->step_height = 0.15f;
	ob->jump_speed = 10.0f;
	ob->fall_speed = 55.0f;
	ob->max_jumps = 1;
	ob->col_group = 0x01;
	ob->col_mask = 0xffff;
	ob->preview = NULL;

	BLI_listbase_clear(&ob->pc_ids);

}

/* more general add: creates minimum required data, but without vertices etc. */
Object *BKE_object_add_only_object(Main *bmain, int type, const char *name)
{
	Object *ob;

	if (!name)
		name = get_obdata_defname(type);

	ob = BKE_libblock_alloc(bmain, ID_OB, name, 0);

	/* default object vars */
	ob->type = type;

	BKE_object_init(ob);

	return ob;
}

/* general add: to scene, with layer from area and default name */
/* creates minimum required data, but without vertices etc. */
Object *BKE_object_add(
        Main *bmain, Scene *scene,
        int type, const char *name)
{
	Object *ob;
	Base *base;

	ob = BKE_object_add_only_object(bmain, type, name);

	ob->data = BKE_object_obdata_add_from_type(bmain, type, name);

	ob->lay = scene->lay;

	base = BKE_scene_base_add(scene, ob);
	BKE_scene_base_deselect_all(scene);
	BKE_scene_base_select(scene, base);

	return ob;
}

static void copy_object_lod(Object *obn, const Object *ob, const int UNUSED(flag))
{
	BLI_duplicatelist(&obn->lodlevels, &ob->lodlevels);

	obn->currentlod = (LodLevel *)obn->lodlevels.first;
}

void BKE_object_transform_copy(Object *ob_tar, const Object *ob_src)
{
	copy_v3_v3(ob_tar->loc, ob_src->loc);
	copy_v3_v3(ob_tar->rot, ob_src->rot);
	copy_v3_v3(ob_tar->quat, ob_src->quat);
	copy_v3_v3(ob_tar->rotAxis, ob_src->rotAxis);
	ob_tar->rotAngle = ob_src->rotAngle;
	ob_tar->rotmode = ob_src->rotmode;
	copy_v3_v3(ob_tar->size, ob_src->size);
}

/**
 * Only copy internal data of Object ID from source to already allocated/initialized destination.
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_object_copy_data(Main *UNUSED(bmain), Object *ob_dst, const Object *ob_src, const int flag)
{
	ModifierData *md;

	/* We never handle usercount here for own data. */
	const int flag_subdata = flag | LIB_ID_CREATE_NO_USER_REFCOUNT;

	if (ob_src->totcol) {
		ob_dst->mat = MEM_dupallocN(ob_src->mat);
		ob_dst->matbits = MEM_dupallocN(ob_src->matbits);
		ob_dst->totcol = ob_src->totcol;
	}
	else if (ob_dst->mat != NULL || ob_dst->matbits != NULL) {
		/* This shall not be needed, but better be safe than sorry. */
		BLI_assert(!"Object copy: non-NULL material pointers with zero counter, should not happen.");
		ob_dst->mat = NULL;
		ob_dst->matbits = NULL;
	}

	if (ob_src->iuser) ob_dst->iuser = MEM_dupallocN(ob_src->iuser);

	if (ob_src->bb) ob_dst->bb = MEM_dupallocN(ob_src->bb);
	ob_dst->flag &= ~OB_FROMGROUP;

	BLI_listbase_clear(&ob_dst->modifiers);

	for (md = ob_src->modifiers.first; md; md = md->next) {
		ModifierData *nmd = modifier_new(md->type);
		BLI_strncpy(nmd->name, md->name, sizeof(nmd->name));
		modifier_copyData_ex(md, nmd, flag_subdata);
		BLI_addtail(&ob_dst->modifiers, nmd);
	}

	defgroup_copy_list(&ob_dst->defbase, &ob_src->defbase);

	ob_dst->mode = OB_MODE_OBJECT;

	ob_dst->rigidbody_object = BKE_rigidbody_copy_object(ob_src, flag_subdata);
	ob_dst->rigidbody_constraint = BKE_rigidbody_copy_constraint(ob_src, flag_subdata);

	ob_dst->derivedDeform = NULL;
	ob_dst->derivedFinal = NULL;

	BLI_listbase_clear(&ob_dst->gpulamp);
	BLI_listbase_clear(&ob_dst->pc_ids);

	copy_object_lod(ob_dst, ob_src, flag_subdata);

	/* Do not copy runtime curve data. */
	ob_dst->curve_cache = NULL;

	/* Do not copy object's preview (mostly due to the fact renderers create temp copy of objects). */
	if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0 && false) {  /* XXX TODO temp hack */
		BKE_previewimg_id_copy(&ob_dst->id, &ob_src->id);
	}
	else {
		ob_dst->preview = NULL;
	}
}

/* copy objects, will re-initialize cached simulation data */
Object *BKE_object_copy(Main *bmain, const Object *ob)
{
	Object *ob_copy;
	BKE_id_copy_ex(bmain, &ob->id, (ID **)&ob_copy, 0, false);
	return ob_copy;
}

void BKE_object_make_local_ex(Main *bmain, Object *ob, const bool lib_local, const bool clear_proxy)
{
	bool is_local = false, is_lib = false;

	/* - only lib users: do nothing (unless force_local is set)
	 * - only local users: set flag
	 * - mixed: make copy
	 * In case we make a whole lib's content local, we always want to localize, and we skip remapping (done later).
	 */

	if (!ID_IS_LINKED(ob)) {
		return;
	}

	BKE_library_ID_test_usages(bmain, ob, &is_local, &is_lib);

	if (lib_local || is_local) {
		if (!is_lib) {
			id_clear_lib_data(bmain, &ob->id);
			BKE_id_expand_local(bmain, &ob->id);
			if (clear_proxy) {
				if (ob->proxy_from != NULL) {
					ob->proxy_from->proxy = NULL;
					ob->proxy_from->proxy_group = NULL;
				}
				ob->proxy = ob->proxy_from = ob->proxy_group = NULL;
			}
		}
		else {
			Object *ob_new = BKE_object_copy(bmain, ob);

			ob_new->id.us = 0;
			ob_new->proxy = ob_new->proxy_from = ob_new->proxy_group = NULL;

			/* setting newid is mandatory for complex make_lib_local logic... */
			ID_NEW_SET(ob, ob_new);

			if (!lib_local) {
				BKE_libblock_remap(bmain, ob, ob_new, ID_REMAP_SKIP_INDIRECT_USAGE);
			}
		}
	}
}

void BKE_object_make_local(Main *bmain, Object *ob, const bool lib_local)
{
	BKE_object_make_local_ex(bmain, ob, lib_local, true);
}

/* Returns true if the Object is from an external blend file (libdata) */
bool BKE_object_is_libdata(const Object *ob)
{
	return (ob && ID_IS_LINKED(ob));
}

/* Returns true if the Object data is from an external blend file (libdata) */
bool BKE_object_obdata_is_libdata(const Object *ob)
{
	/* Linked objects with local obdata are forbidden! */
	BLI_assert(!ob || !ob->data || (ID_IS_LINKED(ob) ? ID_IS_LINKED(ob->data) : true));
	return (ob && ob->data && ID_IS_LINKED(ob->data));
}

/* *************** PROXY **************** */

/* proxy rule: lib_object->proxy_from == the one we borrow from, set temporally while object_update */
/*             local_object->proxy == pointer to library object, saved in files and read */
/*             local_object->proxy_group == pointer to group dupli-object, saved in files and read */

void BKE_object_make_proxy(Object *ob, Object *target, Object *gob)
{
	/* paranoia checks */
	if (ID_IS_LINKED(ob) || !ID_IS_LINKED(target)) {
		printf("cannot make proxy\n");
		return;
	}

	ob->proxy = target;
	ob->proxy_group = gob;
	id_lib_extern(&target->id);

	/* copy transform
	 * - gob means this proxy comes from a group, just apply the matrix
	 *   so the object wont move from its dupli-transform.
	 *
	 * - no gob means this is being made from a linked object,
	 *   this is closer to making a copy of the object - in-place. */
	if (gob) {
		ob->rotmode = target->rotmode;
		mul_m4_m4m4(ob->obmat, gob->obmat, target->obmat);
		if (gob->dup_group) { /* should always be true */
			float tvec[3];
			mul_v3_mat3_m4v3(tvec, ob->obmat, gob->dup_group->dupli_ofs);
			sub_v3_v3(ob->obmat[3], tvec);
		}
		BKE_object_apply_mat4(ob, ob->obmat, false, true);
	}
	else {
		BKE_object_transform_copy(ob, target);
		ob->parent = target->parent; /* libdata */
		copy_m4_m4(ob->parentinv, target->parentinv);
	}

	/* set object type and link to data */
	ob->type = target->type;
	ob->data = target->data;
	id_us_plus((ID *)ob->data);     /* ensures lib data becomes LIB_TAG_EXTERN */

	/* copy vertex groups */
	defgroup_copy_list(&ob->defbase, &target->defbase);

	/* copy material and index information */
	ob->actcol = ob->totcol = 0;
	if (ob->mat) MEM_freeN(ob->mat);
	if (ob->matbits) MEM_freeN(ob->matbits);
	ob->mat = NULL;
	ob->matbits = NULL;
	if ((target->totcol) && (target->mat) && OB_TYPE_SUPPORT_MATERIAL(ob->type)) {
		int i;

		ob->actcol = target->actcol;
		ob->totcol = target->totcol;

		ob->mat = MEM_dupallocN(target->mat);
		ob->matbits = MEM_dupallocN(target->matbits);
		for (i = 0; i < target->totcol; i++) {
			/* don't need to run test_object_materials since we know this object is new and not used elsewhere */
			id_us_plus((ID *)ob->mat[i]);
		}
	}

	/* type conversions */
	if (target->type == OB_EMPTY) {
		ob->empty_drawtype = target->empty_drawtype;
		ob->empty_drawsize = target->empty_drawsize;
	}

	/* copy IDProperties */
	if (ob->id.properties) {
		IDP_FreeProperty(ob->id.properties);
		MEM_freeN(ob->id.properties);
		ob->id.properties = NULL;
	}
	if (target->id.properties) {
		ob->id.properties = IDP_CopyProperty(target->id.properties);
	}

	/* copy drawtype info */
	ob->dt = target->dt;
}

/**
 * Use with newly created objects to set their size
 * (used to apply scene-scale).
 */
void BKE_object_obdata_size_init(struct Object *ob, const float size)
{
	/* apply radius as a scale to types that support it */
	switch (ob->type) {
		case OB_EMPTY:
		{
			ob->empty_drawsize *= size;
			break;
		}
		case OB_FONT:
		{
			Curve *cu = ob->data;
			cu->fsize *= size;
			break;
		}
		case OB_CAMERA:
		{
			Camera *cam = ob->data;
			cam->drawsize *= size;
			break;
		}
		case OB_LAMP:
		{
			Lamp *lamp = ob->data;
			lamp->dist *= size;
			lamp->area_size  *= size;
			lamp->area_sizey *= size;
			lamp->area_sizez *= size;
			break;
		}
	}
}

/* *************** CALC ****************** */

void BKE_object_scale_to_mat3(Object *ob, float mat[3][3])
{
	float vec[3];
	mul_v3_v3v3(vec, ob->size, ob->dscale);
	size_to_mat3(mat, vec);
}

void BKE_object_rot_to_mat3(Object *ob, float mat[3][3], bool use_drot)
{
	float rmat[3][3], dmat[3][3];

	/* 'dmat' is the delta-rotation matrix, which will get (pre)multiplied
	 * with the rotation matrix to yield the appropriate rotation
	 */

	/* rotations may either be quats, eulers (with various rotation orders), or axis-angle */
	if (ob->rotmode > 0) {
		/* euler rotations (will cause gimble lock, but this can be alleviated a bit with rotation orders) */
		eulO_to_mat3(rmat, ob->rot, ob->rotmode);
		eulO_to_mat3(dmat, ob->drot, ob->rotmode);
	}
	else if (ob->rotmode == ROT_MODE_AXISANGLE) {
		/* axis-angle - not really that great for 3D-changing orientations */
		axis_angle_to_mat3(rmat, ob->rotAxis, ob->rotAngle);
		axis_angle_to_mat3(dmat, ob->drotAxis, ob->drotAngle);
	}
	else {
		/* quats are normalized before use to eliminate scaling issues */
		float tquat[4];

		normalize_qt_qt(tquat, ob->quat);
		quat_to_mat3(rmat, tquat);

		normalize_qt_qt(tquat, ob->dquat);
		quat_to_mat3(dmat, tquat);
	}

	/* combine these rotations */
	if (use_drot)
		mul_m3_m3m3(mat, dmat, rmat);
	else
		copy_m3_m3(mat, rmat);
}

void BKE_object_mat3_to_rot(Object *ob, float mat[3][3], bool use_compat)
{
	BLI_ASSERT_UNIT_M3(mat);

	switch (ob->rotmode) {
		case ROT_MODE_QUAT:
		{
			float dquat[4];
			mat3_normalized_to_quat(ob->quat, mat);
			normalize_qt_qt(dquat, ob->dquat);
			invert_qt_normalized(dquat);
			mul_qt_qtqt(ob->quat, dquat, ob->quat);
			break;
		}
		case ROT_MODE_AXISANGLE:
		{
			float quat[4];
			float dquat[4];

			/* without drot we could apply 'mat' directly */
			mat3_normalized_to_quat(quat, mat);
			axis_angle_to_quat(dquat, ob->drotAxis, ob->drotAngle);
			invert_qt_normalized(dquat);
			mul_qt_qtqt(quat, dquat, quat);
			quat_to_axis_angle(ob->rotAxis, &ob->rotAngle, quat);
			break;
		}
		default: /* euler */
		{
			float quat[4];
			float dquat[4];

			/* without drot we could apply 'mat' directly */
			mat3_normalized_to_quat(quat, mat);
			eulO_to_quat(dquat, ob->drot, ob->rotmode);
			invert_qt_normalized(dquat);
			mul_qt_qtqt(quat, dquat, quat);
			/* end drot correction */

			if (use_compat) quat_to_compatible_eulO(ob->rot, ob->rot, ob->rotmode, quat);
			else            quat_to_eulO(ob->rot, ob->rotmode, quat);
			break;
		}
	}
}

void BKE_object_tfm_protected_backup(const Object *ob,
                                     ObjectTfmProtectedChannels *obtfm)
{

#define TFMCPY(_v) (obtfm->_v = ob->_v)
#define TFMCPY3D(_v) copy_v3_v3(obtfm->_v, ob->_v)
#define TFMCPY4D(_v) copy_v4_v4(obtfm->_v, ob->_v)

	TFMCPY3D(loc);
	TFMCPY3D(dloc);
	TFMCPY3D(size);
	TFMCPY3D(dscale);
	TFMCPY3D(rot);
	TFMCPY3D(drot);
	TFMCPY4D(quat);
	TFMCPY4D(dquat);
	TFMCPY3D(rotAxis);
	TFMCPY3D(drotAxis);
	TFMCPY(rotAngle);
	TFMCPY(drotAngle);

#undef TFMCPY
#undef TFMCPY3D
#undef TFMCPY4D

}

void BKE_object_tfm_protected_restore(Object *ob,
                                      const ObjectTfmProtectedChannels *obtfm,
                                      const short protectflag)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		if (protectflag & (OB_LOCK_LOCX << i)) {
			ob->loc[i] =  obtfm->loc[i];
			ob->dloc[i] = obtfm->dloc[i];
		}

		if (protectflag & (OB_LOCK_SCALEX << i)) {
			ob->size[i] =  obtfm->size[i];
			ob->dscale[i] = obtfm->dscale[i];
		}

		if (protectflag & (OB_LOCK_ROTX << i)) {
			ob->rot[i] =  obtfm->rot[i];
			ob->drot[i] = obtfm->drot[i];

			ob->quat[i + 1] =  obtfm->quat[i + 1];
			ob->dquat[i + 1] = obtfm->dquat[i + 1];

			ob->rotAxis[i] =  obtfm->rotAxis[i];
			ob->drotAxis[i] = obtfm->drotAxis[i];
		}
	}

	if ((protectflag & OB_LOCK_ROT4D) && (protectflag & OB_LOCK_ROTW)) {
		ob->quat[0] =  obtfm->quat[0];
		ob->dquat[0] = obtfm->dquat[0];

		ob->rotAngle =  obtfm->rotAngle;
		ob->drotAngle = obtfm->drotAngle;
	}
}

void BKE_object_to_mat3(Object *ob, float mat[3][3]) /* no parent */
{
	float smat[3][3];
	float rmat[3][3];
	/*float q1[4];*/

	/* size */
	BKE_object_scale_to_mat3(ob, smat);

	/* rot */
	BKE_object_rot_to_mat3(ob, rmat, true);
	mul_m3_m3m3(mat, rmat, smat);
}

void BKE_object_to_mat4(Object *ob, float mat[4][4])
{
	float tmat[3][3];

	BKE_object_to_mat3(ob, tmat);

	copy_m4_m3(mat, tmat);

	add_v3_v3v3(mat[3], ob->loc, ob->dloc);
}

void BKE_object_matrix_local_get(struct Object *ob, float mat[4][4])
{
	if (ob->parent) {
		float par_imat[4][4];

		BKE_object_get_parent_matrix(NULL, ob, ob->parent, par_imat);
		invert_m4(par_imat);
		mul_m4_m4m4(mat, par_imat, ob->obmat);
	}
	else {
		copy_m4_m4(mat, ob->obmat);
	}
}

/* extern */
int enable_cu_speed = 1;

/**
 * \param scene: Used when curve cache needs to be calculated, or for dupli-frame time.
 * \return success if \a mat is set.
 */
static bool ob_parcurve(Scene *scene, Object *ob, Object *par, float mat[4][4])
{
	Curve *cu = par->data;
	float ctime;

	/* only happens on reload file, but violates depsgraph still... fix! */
	if (par->curve_cache == NULL) {
		if (scene == NULL) {
			return false;
		}
		BKE_displist_make_curveTypes(scene, par, 0);
	}

	if (par->curve_cache->path == NULL) {
		return false;
	}

	/* catch exceptions: curve paths used as a duplicator */
	if (enable_cu_speed) {
		/* ctime is now a proper var setting of Curve which gets set by Animato like any other var that's animated,
		 * but this will only work if it actually is animated...
		 *
		 * we divide the curvetime calculated in the previous step by the length of the path, to get a time
		 * factor, which then gets clamped to lie within 0.0 - 1.0 range
		 */
		if (cu->pathlen) {
			ctime = cu->ctime / cu->pathlen;
		}
		else {
			ctime = cu->ctime;
		}

		CLAMP(ctime, 0.0f, 1.0f);
	}
	else {
		/* For dupli-frames only */
		if (scene == NULL) {
			return false;
		}

		ctime = 0.0f;
		if (cu->pathlen) {
			ctime /= cu->pathlen;
		}

		CLAMP(ctime, 0.0f, 1.0f);
	}

	unit_m4(mat);

	/* vec: 4 items! */

	return true;
}

static void give_parvert(Object *par, int nr, float vec[3])
{
	zero_v3(vec);

	if (par->type == OB_MESH) {
		Mesh *me = par->data;
		BMEditMesh *em = me->edit_btmesh;
		DerivedMesh *dm;

		dm = (em) ? em->derivedFinal : par->derivedFinal;

		if (dm) {
			int count = 0;
			int numVerts = dm->getNumVerts(dm);

			if (nr < numVerts) {
				bool use_special_ss_case = false;

				if (dm->type == DM_TYPE_CCGDM) {
					ModifierData *md;
					VirtualModifierData virtualModifierData;
					use_special_ss_case = true;
					for (md = modifiers_getVirtualModifierList(par, &virtualModifierData);
					     md != NULL;
					     md = md->next)
					{
						const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
						/* TODO(sergey): Check for disabled modifiers. */
						if (mti->type != eModifierTypeType_OnlyDeform && md->next != NULL) {
							use_special_ss_case = false;
							break;
						}
					}
				}

				if (!use_special_ss_case) {
					/* avoid dm->getVertDataArray() since it allocates arrays in the dm (not thread safe) */
					if (em && dm->type == DM_TYPE_EDITBMESH) {
						if (em->bm->elem_table_dirty & BM_VERT) {
#ifdef VPARENT_THREADING_HACK
							BLI_mutex_lock(&vparent_lock);
							if (em->bm->elem_table_dirty & BM_VERT) {
								BM_mesh_elem_table_ensure(em->bm, BM_VERT);
							}
							BLI_mutex_unlock(&vparent_lock);
#else
							BLI_assert(!"Not safe for threading");
							BM_mesh_elem_table_ensure(em->bm, BM_VERT);
#endif
						}
					}
				}

				if (use_special_ss_case) {
					/* Special case if the last modifier is SS and no constructive modifier are in front of it. */
					CCGDerivedMesh *ccgdm = (CCGDerivedMesh *)dm;
					CCGVert *ccg_vert = ccgSubSurf_getVert(ccgdm->ss, POINTER_FROM_INT(nr));
					/* In case we deleted some verts, nr may refer to inexistent one now, see T42557. */
					if (ccg_vert) {
						float *co = ccgSubSurf_getVertData(ccgdm->ss, ccg_vert);
						add_v3_v3(vec, co);
						count++;
					}
				}
				else if (CustomData_has_layer(&dm->vertData, CD_ORIGINDEX) &&
				         !(em && dm->type == DM_TYPE_EDITBMESH))
				{
					int i;

					/* Get the average of all verts with (original index == nr). */
					for (i = 0; i < numVerts; i++) {
						const int *index = dm->getVertData(dm, i, CD_ORIGINDEX);
						if (*index == nr) {
							float co[3];
							dm->getVertCo(dm, i, co);
							add_v3_v3(vec, co);
							count++;
						}
					}
				}
				else {
					if (nr < numVerts) {
						float co[3];
						dm->getVertCo(dm, nr, co);
						add_v3_v3(vec, co);
						count++;
					}
				}
			}

			if (count == 0) {
				/* keep as 0, 0, 0 */
			}
			else if (count > 0) {
				mul_v3_fl(vec, 1.0f / count);
			}
			else {
				/* use first index if its out of range */
				dm->getVertCo(dm, 0, vec);
			}
		}
		else {
			fprintf(stderr,
			        "%s: DerivedMesh is needed to solve parenting, "
			        "object position can be wrong now\n", __func__);
		}
	}
	else if (ELEM(par->type, OB_CURVE, OB_SURF)) {
		ListBase *nurb;

		/* Unless there's some weird depsgraph failure the cache should exist. */
		BLI_assert(par->curve_cache != NULL);

		if (par->curve_cache->deformed_nurbs.first != NULL) {
			nurb = &par->curve_cache->deformed_nurbs;
		}
		else {
			Curve *cu = par->data;
			nurb = BKE_curve_nurbs_get(cu);
		}

		BKE_nurbList_index_get_co(nurb, nr, vec);
	}
}

static void ob_parvert3(Object *ob, Object *par, float mat[4][4])
{

	/* in local ob space */
	if (OB_TYPE_SUPPORT_PARVERT(par->type)) {
		float cmat[3][3], v1[3], v2[3], v3[3], q[4];

		give_parvert(par, ob->par1, v1);
		give_parvert(par, ob->par2, v2);
		give_parvert(par, ob->par3, v3);

		tri_to_quat(q, v1, v2, v3);
		quat_to_mat3(cmat, q);
		copy_m4_m3(mat, cmat);

		mid_v3_v3v3v3(mat[3], v1, v2, v3);
	}
	else {
		unit_m4(mat);
	}
}


void BKE_object_get_parent_matrix(Scene *scene, Object *ob, Object *par, float parentmat[4][4])
{
	float tmat[4][4];
	float vec[3];
	bool ok;

	switch (ob->partype & PARTYPE) {
		case PAROBJECT:
			ok = 0;
			if (par->type == OB_CURVE) {
				if ((((Curve *)par->data)->flag & CU_PATH) &&
				    (ob_parcurve(scene, ob, par, tmat)))
				{
					ok = 1;
				}
			}

			if (ok) mul_m4_m4m4(parentmat, par->obmat, tmat);
			else copy_m4_m4(parentmat, par->obmat);

			break;

		case PARVERT1:
			unit_m4(parentmat);
			give_parvert(par, ob->par1, vec);
			mul_v3_m4v3(parentmat[3], par->obmat, vec);
			break;
		case PARVERT3:
			ob_parvert3(ob, par, tmat);

			mul_m4_m4m4(parentmat, par->obmat, tmat);
			break;

	}

}

/**
 * \param r_originmat: Optional matrix that stores the space the object is in (without its own matrix applied)
 */
static void solve_parenting(Scene *scene, Object *ob, Object *par, float obmat[4][4], float slowmat[4][4],
                            float r_originmat[3][3], const bool set_origin)
{
	float totmat[4][4];
	float tmat[4][4];
	float locmat[4][4];

	BKE_object_to_mat4(ob, locmat);

	if (ob->partype & PARSLOW) copy_m4_m4(slowmat, obmat);

	BKE_object_get_parent_matrix(scene, ob, par, totmat);

	/* total */
	mul_m4_m4m4(tmat, totmat, ob->parentinv);
	mul_m4_m4m4(obmat, tmat, locmat);

	if (r_originmat) {
		/* usable originmat */
		copy_m3_m4(r_originmat, tmat);
	}

	/* origin, for help line */
	if (set_origin) {
		copy_v3_v3(ob->orig, totmat[3]);
	}
}

static bool where_is_object_parslow(Object *ob, float obmat[4][4], float slowmat[4][4])
{
	float *fp1, *fp2;
	float fac1, fac2;
	int a;

	/* include framerate */
	fac1 = (1.0f / (1.0f + fabsf(ob->sf)));
	if (fac1 >= 1.0f) return false;
	fac2 = 1.0f - fac1;

	fp1 = obmat[0];
	fp2 = slowmat[0];
	for (a = 0; a < 16; a++, fp1++, fp2++) {
		fp1[0] = fac1 * fp1[0] + fac2 * fp2[0];
	}

	return true;
}

/* note, scene is the active scene while actual_scene is the scene the object resides in */
void BKE_object_where_is_calc_time_ex(Scene *scene, Object *ob, float ctime,
                                      RigidBodyWorld *rbw, float r_originmat[3][3])
{
	if (ob == NULL) return;

	/* execute drivers only, as animation has already been done */

	if (ob->parent) {
		Object *par = ob->parent;
		float slowmat[4][4];

		/* calculate parent matrix */
		solve_parenting(scene, ob, par, ob->obmat, slowmat, r_originmat, true);

		/* "slow parent" is definitely not threadsafe, and may also give bad results jumping around
		 * An old-fashioned hack which probably doesn't really cut it anymore
		 */
		if (ob->partype & PARSLOW) {
			if (!where_is_object_parslow(ob, ob->obmat, slowmat))
				return;
		}
	}
	else {
		BKE_object_to_mat4(ob, ob->obmat);
	}

	/* try to fall back to the scene rigid body world if none given */
	rbw = rbw ? rbw : scene->rigidbody_world;
	/* read values pushed into RBO from sim/cache... */
	BKE_rigidbody_sync_transforms(rbw, ob, ctime);

	/* set negative scale flag in object */
	if (is_negative_m4(ob->obmat)) ob->transflag |= OB_NEG_SCALE;
	else ob->transflag &= ~OB_NEG_SCALE;
}

void BKE_object_where_is_calc_time(Scene *scene, Object *ob, float ctime)
{
	BKE_object_where_is_calc_time_ex(scene, ob, ctime, NULL, NULL);
}

/* get object transformation matrix without recalculating dependencies and
 * constraints -- assume dependencies are already solved by depsgraph.
 * no changes to object and it's parent would be done.
 * used for bundles orientation in 3d space relative to parented blender camera */
void BKE_object_where_is_calc_mat4(Scene *scene, Object *ob, float obmat[4][4])
{

	if (ob->parent) {
		float slowmat[4][4];

		Object *par = ob->parent;

		solve_parenting(scene, ob, par, obmat, slowmat, NULL, false);

		if (ob->partype & PARSLOW)
			where_is_object_parslow(ob, obmat, slowmat);
	}
	else {
		BKE_object_to_mat4(ob, obmat);
	}
}

void BKE_object_where_is_calc_ex(Scene *scene, RigidBodyWorld *rbw, Object *ob, float r_originmat[3][3])
{
	BKE_object_where_is_calc_time_ex(scene, ob, 0, rbw, r_originmat);
}
void BKE_object_where_is_calc(Scene *scene, Object *ob)
{
	BKE_object_where_is_calc_time_ex(scene, ob, 0, NULL, NULL);
}

/* for calculation of the inverse parent transform, only used for editor */
void BKE_object_workob_calc_parent(Scene *scene, Object *ob, Object *workob)
{
	BKE_object_workob_clear(workob);

	unit_m4(workob->obmat);
	unit_m4(workob->parentinv);
	unit_m4(workob->constinv);
	workob->parent = ob->parent;

	workob->partype = ob->partype;
	workob->par1 = ob->par1;
	workob->par2 = ob->par2;
	workob->par3 = ob->par3;

	BLI_strncpy(workob->parsubstr, ob->parsubstr, sizeof(workob->parsubstr));

	BKE_object_where_is_calc(scene, workob);
}

/* see BKE_pchan_apply_mat4() for the equivalent 'pchan' function */
void BKE_object_apply_mat4(Object *ob, float mat[4][4], const bool use_compat, const bool use_parent)
{
	float rot[3][3];

	if (use_parent && ob->parent) {
		float rmat[4][4], diff_mat[4][4], imat[4][4], parent_mat[4][4];

		BKE_object_get_parent_matrix(NULL, ob, ob->parent, parent_mat);

		mul_m4_m4m4(diff_mat, parent_mat, ob->parentinv);
		invert_m4_m4(imat, diff_mat);
		mul_m4_m4m4(rmat, imat, mat); /* get the parent relative matrix */

		/* same as below, use rmat rather than mat */
		mat4_to_loc_rot_size(ob->loc, rot, ob->size, rmat);
	}
	else {
		mat4_to_loc_rot_size(ob->loc, rot, ob->size, mat);
	}

	BKE_object_mat3_to_rot(ob, rot, use_compat);

	sub_v3_v3(ob->loc, ob->dloc);

	if (ob->dscale[0] != 0.0f) ob->size[0] /= ob->dscale[0];
	if (ob->dscale[1] != 0.0f) ob->size[1] /= ob->dscale[1];
	if (ob->dscale[2] != 0.0f) ob->size[2] /= ob->dscale[2];

	/* BKE_object_mat3_to_rot handles delta rotations */
}

BoundBox *BKE_boundbox_alloc_unit(void)
{
	BoundBox *bb;
	const float min[3] = {-1.0f, -1.0f, -1.0f}, max[3] = {-1.0f, -1.0f, -1.0f};

	bb = MEM_callocN(sizeof(BoundBox), "OB-BoundBox");
	BKE_boundbox_init_from_minmax(bb, min, max);

	return bb;
}

void BKE_boundbox_init_from_minmax(BoundBox *bb, const float min[3], const float max[3])
{
	bb->vec[0][0] = bb->vec[1][0] = bb->vec[2][0] = bb->vec[3][0] = min[0];
	bb->vec[4][0] = bb->vec[5][0] = bb->vec[6][0] = bb->vec[7][0] = max[0];

	bb->vec[0][1] = bb->vec[1][1] = bb->vec[4][1] = bb->vec[5][1] = min[1];
	bb->vec[2][1] = bb->vec[3][1] = bb->vec[6][1] = bb->vec[7][1] = max[1];

	bb->vec[0][2] = bb->vec[3][2] = bb->vec[4][2] = bb->vec[7][2] = min[2];
	bb->vec[1][2] = bb->vec[2][2] = bb->vec[5][2] = bb->vec[6][2] = max[2];
}

void BKE_boundbox_calc_center_aabb(const BoundBox *bb, float r_cent[3])
{
	r_cent[0] = 0.5f * (bb->vec[0][0] + bb->vec[4][0]);
	r_cent[1] = 0.5f * (bb->vec[0][1] + bb->vec[2][1]);
	r_cent[2] = 0.5f * (bb->vec[0][2] + bb->vec[1][2]);
}

void BKE_boundbox_calc_size_aabb(const BoundBox *bb, float r_size[3])
{
	r_size[0] = 0.5f * fabsf(bb->vec[0][0] - bb->vec[4][0]);
	r_size[1] = 0.5f * fabsf(bb->vec[0][1] - bb->vec[2][1]);
	r_size[2] = 0.5f * fabsf(bb->vec[0][2] - bb->vec[1][2]);
}

void BKE_boundbox_minmax(const BoundBox *bb, float obmat[4][4], float r_min[3], float r_max[3])
{
	int i;
	for (i = 0; i < 8; i++) {
		float vec[3];
		mul_v3_m4v3(vec, obmat, bb->vec[i]);
		minmax_v3v3_v3(r_min, r_max, vec);
	}
}

BoundBox *BKE_object_boundbox_get(Object *ob)
{
	BoundBox *bb = NULL;

	if (ob->type == OB_MESH) {
		bb = BKE_mesh_boundbox_get(ob);
	}
	else if (ELEM(ob->type, OB_CURVE, OB_SURF, OB_FONT)) {
		bb = BKE_curve_boundbox_get(ob);
	}
	return bb;
}

/* used to temporally disable/enable boundbox */
void BKE_object_boundbox_flag(Object *ob, int flag, const bool set)
{
	BoundBox *bb = BKE_object_boundbox_get(ob);
	if (bb) {
		if (set) bb->flag |= flag;
		else bb->flag &= ~flag;
	}
}

void BKE_object_dimensions_get(Object *ob, float vec[3])
{
	BoundBox *bb = NULL;

	bb = BKE_object_boundbox_get(ob);
	if (bb) {
		float scale[3];

		mat4_to_size(scale, ob->obmat);

		vec[0] = fabsf(scale[0]) * (bb->vec[4][0] - bb->vec[0][0]);
		vec[1] = fabsf(scale[1]) * (bb->vec[2][1] - bb->vec[0][1]);
		vec[2] = fabsf(scale[2]) * (bb->vec[1][2] - bb->vec[0][2]);
	}
	else {
		zero_v3(vec);
	}
}

void BKE_object_dimensions_set(Object *ob, const float value[3])
{
	BoundBox *bb = NULL;

	bb = BKE_object_boundbox_get(ob);
	if (bb) {
		float scale[3], len[3];

		mat4_to_size(scale, ob->obmat);

		len[0] = bb->vec[4][0] - bb->vec[0][0];
		len[1] = bb->vec[2][1] - bb->vec[0][1];
		len[2] = bb->vec[1][2] - bb->vec[0][2];

		if (len[0] > 0.f) ob->size[0] = value[0] / len[0];
		if (len[1] > 0.f) ob->size[1] = value[1] / len[1];
		if (len[2] > 0.f) ob->size[2] = value[2] / len[2];
	}
}

void BKE_object_minmax(Object *ob, float min_r[3], float max_r[3], const bool use_hidden)
{
	BoundBox bb;
	float vec[3];
	bool changed = false;

	switch (ob->type) {
		case OB_CURVE:
		case OB_FONT:
		case OB_SURF:
		{
			bb = *BKE_curve_boundbox_get(ob);
			BKE_boundbox_minmax(&bb, ob->obmat, min_r, max_r);
			changed = true;
			break;
		}
		case OB_MESH:
		{
			Mesh *me = BKE_mesh_from_object(ob);

			if (me) {
				bb = *BKE_mesh_boundbox_get(ob);
				BKE_boundbox_minmax(&bb, ob->obmat, min_r, max_r);
				changed = true;
			}
			break;
		}
	}

	if (changed == false) {
		float size[3];

		copy_v3_v3(size, ob->size);
		if (ob->type == OB_EMPTY) {
			mul_v3_fl(size, ob->empty_drawsize);
		}

		minmax_v3v3_v3(min_r, max_r, ob->obmat[3]);

		copy_v3_v3(vec, ob->obmat[3]);
		add_v3_v3(vec, size);
		minmax_v3v3_v3(min_r, max_r, vec);

		copy_v3_v3(vec, ob->obmat[3]);
		sub_v3_v3(vec, size);
		minmax_v3v3_v3(min_r, max_r, vec);
	}
}

void BKE_object_empty_draw_type_set(Object *ob, const int value)
{
	ob->empty_drawtype = value;

	if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE) {
		if (!ob->iuser) {
			ob->iuser = MEM_callocN(sizeof(ImageUser), "image user");
			ob->iuser->ok = 1;
			ob->iuser->frames = 100;
			ob->iuser->sfra = 1;
			ob->iuser->fie_ima = 2;
		}
	}
	else {
		if (ob->iuser) {
			MEM_freeN(ob->iuser);
			ob->iuser = NULL;
		}
	}
}

bool BKE_object_minmax_dupli(
        Main *bmain, Scene *scene,
        Object *ob, float r_min[3], float r_max[3], const bool use_hidden)
{
	bool ok = false;
	return ok;
}

void BKE_object_foreach_display_point(
        Object *ob, float obmat[4][4],
        void (*func_cb)(const float[3], void *), void *user_data)
{
	float co[3];

	if (ob->derivedFinal) {
		DerivedMesh *dm = ob->derivedFinal;
		MVert *mv = dm->getVertArray(dm);
		int totvert = dm->getNumVerts(dm);
		int i;

		for (i = 0; i < totvert; i++, mv++) {
			mul_v3_m4v3(co, obmat, mv->co);
			func_cb(co, user_data);
		}
	}
	else if (ob->curve_cache && ob->curve_cache->disp.first) {
		DispList *dl;

		for (dl = ob->curve_cache->disp.first; dl; dl = dl->next) {
			const float *v3 = dl->verts;
			int totvert = dl->nr;
			int i;

			for (i = 0; i < totvert; i++, v3 += 3) {
				mul_v3_m4v3(co, obmat, v3);
				func_cb(co, user_data);
			}
		}
	}
}

void BKE_scene_foreach_display_point(
        Main *bmain, Scene *scene, View3D *v3d, const short flag,
        void (*func_cb)(const float[3], void *), void *user_data)
{
	Base *base;
	Object *ob;

	for (base = FIRSTBASE; base; base = base->next) {
		if (BASE_VISIBLE_BGMODE(v3d, scene, base) && (base->flag & flag) == flag) {
			ob = base->object;

			BKE_object_foreach_display_point(ob, ob->obmat, func_cb, user_data);
		}
	}
}

/* copied from DNA_object_types.h */
typedef struct ObTfmBack {
	float loc[3], dloc[3], orig[3];
	float size[3], dscale[3];   /* scale and delta scale */
	float rot[3], drot[3];      /* euler rotation */
	float quat[4], dquat[4];    /* quaternion rotation */
	float rotAxis[3], drotAxis[3];  /* axis angle rotation - axis part */
	float rotAngle, drotAngle;  /* axis angle rotation - angle part */
	float obmat[4][4];      /* final worldspace matrix with constraints & animsys applied */
	float parentinv[4][4]; /* inverse result of parent, so that object doesn't 'stick' to parent */
	float constinv[4][4]; /* inverse result of constraints. doesn't include effect of parent or object local transform */
	float imat[4][4];   /* inverse matrix of 'obmat' for during render, old game engine, temporally: ipokeys of transform  */
} ObTfmBack;

void *BKE_object_tfm_backup(Object *ob)
{
	ObTfmBack *obtfm = MEM_mallocN(sizeof(ObTfmBack), "ObTfmBack");
	copy_v3_v3(obtfm->loc, ob->loc);
	copy_v3_v3(obtfm->dloc, ob->dloc);
	copy_v3_v3(obtfm->orig, ob->orig);
	copy_v3_v3(obtfm->size, ob->size);
	copy_v3_v3(obtfm->dscale, ob->dscale);
	copy_v3_v3(obtfm->rot, ob->rot);
	copy_v3_v3(obtfm->drot, ob->drot);
	copy_qt_qt(obtfm->quat, ob->quat);
	copy_qt_qt(obtfm->dquat, ob->dquat);
	copy_v3_v3(obtfm->rotAxis, ob->rotAxis);
	copy_v3_v3(obtfm->drotAxis, ob->drotAxis);
	obtfm->rotAngle = ob->rotAngle;
	obtfm->drotAngle = ob->drotAngle;
	copy_m4_m4(obtfm->obmat, ob->obmat);
	copy_m4_m4(obtfm->parentinv, ob->parentinv);
	copy_m4_m4(obtfm->constinv, ob->constinv);
	copy_m4_m4(obtfm->imat, ob->imat);

	return (void *)obtfm;
}

void BKE_object_tfm_restore(Object *ob, void *obtfm_pt)
{
	ObTfmBack *obtfm = (ObTfmBack *)obtfm_pt;
	copy_v3_v3(ob->loc, obtfm->loc);
	copy_v3_v3(ob->dloc, obtfm->dloc);
	copy_v3_v3(ob->orig, obtfm->orig);
	copy_v3_v3(ob->size, obtfm->size);
	copy_v3_v3(ob->dscale, obtfm->dscale);
	copy_v3_v3(ob->rot, obtfm->rot);
	copy_v3_v3(ob->drot, obtfm->drot);
	copy_qt_qt(ob->quat, obtfm->quat);
	copy_qt_qt(ob->dquat, obtfm->dquat);
	copy_v3_v3(ob->rotAxis, obtfm->rotAxis);
	copy_v3_v3(ob->drotAxis, obtfm->drotAxis);
	ob->rotAngle = obtfm->rotAngle;
	ob->drotAngle = obtfm->drotAngle;
	copy_m4_m4(ob->obmat, obtfm->obmat);
	copy_m4_m4(ob->parentinv, obtfm->parentinv);
	copy_m4_m4(ob->constinv, obtfm->constinv);
	copy_m4_m4(ob->imat, obtfm->imat);
}

bool BKE_object_parent_loop_check(const Object *par, const Object *ob)
{
	/* test if 'ob' is a parent somewhere in par's parents */
	if (par == NULL) return false;
	if (ob == par) return true;
	return BKE_object_parent_loop_check(par->parent, ob);
}

static void object_handle_update_proxy(Main *bmain,
                                       Scene *scene,
                                       Object *object,
                                       const bool do_proxy_update)
{
	/* The case when this is a group proxy, object_update is called in group.c */
	if (object->proxy == NULL) {
		return;
	}
	/* set pointer in library proxy target, for copying, but restore it */
	object->proxy->proxy_from = object;
	// printf("set proxy pointer for later group stuff %s\n", ob->id.name);

	/* the no-group proxy case, we call update */
	if (object->proxy_group == NULL) {
		if (do_proxy_update) {
			// printf("call update, lib ob %s proxy %s\n", ob->proxy->id.name, ob->id.name);
			BKE_object_handle_update(bmain, scene, object->proxy);
		}
	}
}

/* proxy rule: lib_object->proxy_from == the one we borrow from, only set temporal and cleared here */
/*           local_object->proxy      == pointer to library object, saved in files and read */

/* function below is polluted with proxy exceptions, cleanup will follow! */

/* the main object update call, for object matrix, constraints, keys and displist (modifiers) */
/* requires flags to be set! */
/* Ideally we shouldn't have to pass the rigid body world, but need bigger restructuring to avoid id */
void BKE_object_handle_update_ex(Main *bmain,
                                 Scene *scene, Object *ob,
                                 RigidBodyWorld *rbw,
                                 const bool do_proxy_update)
{
	if ((ob->recalc & OB_RECALC_ALL) == 0) {
		object_handle_update_proxy(bmain, scene, ob, do_proxy_update);
		return;
	}

	/* XXX new animsys warning: depsgraph tag OB_RECALC_DATA should not skip drivers,
	 * which is only in BKE_object_where_is_calc now */
	/* XXX: should this case be OB_RECALC_OB instead? */
	if (ob->recalc & OB_RECALC_ALL) {
		/* Handle proxy copy for target. */
		if (!BKE_object_eval_proxy_copy(ob)) {
			BKE_object_where_is_calc_ex(scene, rbw, ob, NULL);
		}
	}

	if (ob->recalc & OB_RECALC_DATA) {
		BKE_object_handle_data_update(bmain, scene, ob);
	}

	ob->recalc &= ~OB_RECALC_ALL;

	object_handle_update_proxy(bmain, scene, ob, do_proxy_update);
}

/* WARNING: "scene" here may not be the scene object actually resides in.
 * When dealing with background-sets, "scene" is actually the active scene.
 * e.g. "scene" <-- set 1 <-- set 2 ("ob" lives here) <-- set 3 <-- ... <-- set n
 * rigid bodies depend on their world so use BKE_object_handle_update_ex() to also pass along the corrent rigid body world
 */
void BKE_object_handle_update(Main *bmain, Scene *scene, Object *ob)
{
	BKE_object_handle_update_ex(bmain, scene, ob, NULL, true);
}

int BKE_object_obdata_texspace_get(Object *ob, short **r_texflag, float **r_loc, float **r_size, float **r_rot)
{

	if (ob->data == NULL)
		return 0;

	switch (GS(((ID *)ob->data)->name)) {
		case ID_ME:
		{
			Mesh *me = ob->data;
			if (me->bb == NULL || (me->bb->flag & BOUNDBOX_DIRTY)) {
				BKE_mesh_texspace_calc(me);
			}
			if (r_texflag) *r_texflag = &me->texflag;
			if (r_loc) *r_loc = me->loc;
			if (r_size) *r_size = me->size;
			if (r_rot) *r_rot = me->rot;
			break;
		}
		case ID_CU:
		{
			Curve *cu = ob->data;
			if (cu->bb == NULL || (cu->bb->flag & BOUNDBOX_DIRTY)) {
				BKE_curve_texspace_calc(cu);
			}
			if (r_texflag) *r_texflag = &cu->texflag;
			if (r_loc) *r_loc = cu->loc;
			if (r_size) *r_size = cu->size;
			if (r_rot) *r_rot = cu->rot;
			break;
		}
		default:
			return 0;
	}
	return 1;
}

static int pc_findindex(ListBase *listbase, int index)
{
	LinkData *link = NULL;
	int number = 0;

	if (listbase == NULL) return -1;

	link = listbase->first;
	while (link) {
		if (POINTER_AS_INT(link->data) == index)
			return number;

		number++;
		link = link->next;
	}

	return -1;
}

void BKE_object_delete_ptcache(Object *ob, int index)
{
	int list_index = pc_findindex(&ob->pc_ids, index);
	LinkData *link = BLI_findlink(&ob->pc_ids, list_index);
	BLI_freelinkN(&ob->pc_ids, link);
}

/************************* Mesh ************************/
/************************* Curve ************************/
bool BKE_object_flag_test_recursive(const Object *ob, short flag)
{
	if (ob->flag & flag) {
		return true;
	}
	else if (ob->parent) {
		return BKE_object_flag_test_recursive(ob->parent, flag);
	}
	else {
		return false;
	}
}

bool BKE_object_is_child_recursive(const Object *ob_parent, const Object *ob_child)
{
	for (ob_child = ob_child->parent; ob_child; ob_child = ob_child->parent) {
		if (ob_child == ob_parent) {
			return true;
		}
	}
	return false;
}

/* most important if this is modified it should _always_ return True, in certain
 * cases false positives are hard to avoid (shape keys for example) */
int BKE_object_is_modified(Scene *scene, Object *ob)
{
	int flag = 0;

	{
		ModifierData *md;
		VirtualModifierData virtualModifierData;
		/* cloth */
		for (md = modifiers_getVirtualModifierList(ob, &virtualModifierData);
		     md && (flag != (eModifierMode_Render | eModifierMode_Realtime));
		     md = md->next)
		{
			if ((flag & eModifierMode_Render) == 0 && modifier_isEnabled(scene, md, eModifierMode_Render))
				flag |= eModifierMode_Render;

			if ((flag & eModifierMode_Realtime) == 0 && modifier_isEnabled(scene, md, eModifierMode_Realtime))
				flag |= eModifierMode_Realtime;
		}
	}

	return flag;
}

static bool obrel_list_test(Object *ob)
{
	return ob && !(ob->id.tag & LIB_TAG_DOIT);
}

static void obrel_list_add(LinkNode **links, Object *ob)
{
	BLI_linklist_prepend(links, ob);
	ob->id.tag |= LIB_TAG_DOIT;
}

/*
 * Iterates over all objects of the given scene.
 * Depending on the eObjectSet flag:
 * collect either OB_SET_ALL, OB_SET_VISIBLE or OB_SET_SELECTED objects.
 * If OB_SET_VISIBLE or OB_SET_SELECTED are collected,
 * then also add related objects according to the given includeFilters.
 */
LinkNode *BKE_object_relational_superset(struct Scene *scene, eObjectSet objectSet, eObRelationTypes includeFilter)
{
	LinkNode *links = NULL;

	Base *base;

	/* Remove markers from all objects */
	for (base = scene->base.first; base; base = base->next) {
		base->object->id.tag &= ~LIB_TAG_DOIT;
	}

	/* iterate over all selected and visible objects */
	for (base = scene->base.first; base; base = base->next) {
		if (objectSet == OB_SET_ALL) {
			/* as we get all anyways just add it */
			Object *ob = base->object;
			obrel_list_add(&links, ob);
		}
		else {
			if ((objectSet == OB_SET_SELECTED && TESTBASELIB_BGMODE(((View3D *)NULL), scene, base)) ||
			    (objectSet == OB_SET_VISIBLE  && BASE_EDITABLE_BGMODE(((View3D *)NULL), scene, base)))
			{
				Object *ob = base->object;

				if (obrel_list_test(ob))
					obrel_list_add(&links, ob);

				/* parent relationship */
				if (includeFilter & (OB_REL_PARENT | OB_REL_PARENT_RECURSIVE)) {
					Object *parent = ob->parent;
					if (obrel_list_test(parent)) {

						obrel_list_add(&links, parent);

						/* recursive parent relationship */
						if (includeFilter & OB_REL_PARENT_RECURSIVE) {
							parent = parent->parent;
							while (obrel_list_test(parent)) {

								obrel_list_add(&links, parent);
								parent = parent->parent;
							}
						}
					}
				}

				/* child relationship */
				if (includeFilter & (OB_REL_CHILDREN | OB_REL_CHILDREN_RECURSIVE)) {
					Base *local_base;
					for (local_base = scene->base.first; local_base; local_base = local_base->next) {
						if (BASE_EDITABLE_BGMODE(((View3D *)NULL), scene, local_base)) {

							Object *child = local_base->object;
							if (obrel_list_test(child)) {
								if ((includeFilter & OB_REL_CHILDREN_RECURSIVE && BKE_object_is_child_recursive(ob, child)) ||
								    (includeFilter & OB_REL_CHILDREN && child->parent && child->parent == ob))
								{
									obrel_list_add(&links, child);
								}
							}
						}
					}
				}


			}
		}
	}

	return links;
}

/**
 * return all groups this object is apart of, caller must free.
 */
struct LinkNode *BKE_object_groups(Main *bmain, Object *ob)
{
	LinkNode *group_linknode = NULL;
	Group *group = NULL;
	while ((group = BKE_group_object_find(bmain, group, ob))) {
		BLI_linklist_prepend(&group_linknode, group);
	}

	return group_linknode;
}

void BKE_object_groups_clear(Main *bmain, Scene *scene, Base *base, Object *object)
{
	Group *group = NULL;

	BLI_assert((base == NULL) || (base->object == object));

	if (scene && base == NULL) {
		base = BKE_scene_base_find(scene, object);
	}

	while ((group = BKE_group_object_find(bmain, group, base->object))) {
		BKE_group_object_unlink(bmain, group, object, scene, base);
	}
}

/**
 * Return a KDTree from the deformed object (in worldspace)
 *
 * \note Only mesh objects currently support deforming, others are TODO.
 *
 * \param ob:
 * \param r_tot:
 * \return The kdtree or NULL if it can't be created.
 */
KDTree *BKE_object_as_kdtree(Object *ob, int *r_tot)
{
	KDTree *tree = NULL;
	unsigned int tot = 0;

	switch (ob->type) {
		case OB_MESH:
		{
			Mesh *me = ob->data;
			unsigned int i;

			DerivedMesh *dm = ob->derivedDeform ? ob->derivedDeform : ob->derivedFinal;
			const int *index;

			if (dm && (index = CustomData_get_layer(&dm->vertData, CD_ORIGINDEX))) {
				MVert *mvert = dm->getVertArray(dm);
				unsigned int totvert = dm->getNumVerts(dm);

				/* tree over-allocs in case where some verts have ORIGINDEX_NONE */
				tot = 0;
				tree = BLI_kdtree_new(totvert);

				/* we don't how how many verts from the DM we can use */
				for (i = 0; i < totvert; i++) {
					if (index[i] != ORIGINDEX_NONE) {
						float co[3];
						mul_v3_m4v3(co, ob->obmat, mvert[i].co);
						BLI_kdtree_insert(tree, index[i], co);
						tot++;
					}
				}
			}
			else {
				MVert *mvert = me->mvert;

				tot = me->totvert;
				tree = BLI_kdtree_new(tot);

				for (i = 0; i < tot; i++) {
					float co[3];
					mul_v3_m4v3(co, ob->obmat, mvert[i].co);
					BLI_kdtree_insert(tree, i, co);
				}
			}

			BLI_kdtree_balance(tree);
			break;
		}
		case OB_CURVE:
		case OB_SURF:
		{
			/* TODO: take deformation into account */
			Curve *cu = ob->data;
			unsigned int i, a;

			Nurb *nu;

			tot = BKE_nurbList_verts_count_without_handles(&cu->nurb);
			tree = BLI_kdtree_new(tot);
			i = 0;

			nu = cu->nurb.first;
			while (nu) {
				if (nu->bezt) {
					BezTriple *bezt;

					bezt = nu->bezt;
					a = nu->pntsu;
					while (a--) {
						float co[3];
						mul_v3_m4v3(co, ob->obmat, bezt->vec[1]);
						BLI_kdtree_insert(tree, i++, co);
						bezt++;
					}
				}
				else {
					BPoint *bp;

					bp = nu->bp;
					a = nu->pntsu * nu->pntsv;
					while (a--) {
						float co[3];
						mul_v3_m4v3(co, ob->obmat, bp->vec);
						BLI_kdtree_insert(tree, i++, co);
						bp++;
					}
				}
				nu = nu->next;
			}

			BLI_kdtree_balance(tree);
			break;
		}
	}

	*r_tot = tot;
	return tree;
}

/* Note: this function should eventually be replaced by depsgraph functionality.
 * Avoid calling this in new code unless there is a very good reason for it!
 */
bool BKE_object_modifier_update_subframe(
        Main *bmain, 
        Scene *scene, Object *ob, bool update_mesh,
        int parent_recursion, float frame,
        int type)
{
	/* if object has parents, update them too */
	if (parent_recursion) {
		int recursion = parent_recursion - 1;
		bool no_update = false;
		if (ob->parent) no_update |= BKE_object_modifier_update_subframe(bmain, scene, ob->parent, 0, recursion, frame, type);
		if (ob->track) no_update |= BKE_object_modifier_update_subframe(bmain, scene, ob->track, 0, recursion, frame, type);

		/* skip subframe if object is parented
		 * to vertex of a dynamic paint canvas */
		if (no_update && (ob->partype == PARVERT1 || ob->partype == PARVERT3))
			return false;

	}

	ob->recalc |= OB_RECALC_ALL;
	if (update_mesh) {
		/* ignore cache clear during subframe updates
		 *  to not mess up cache validity */
		BKE_object_handle_update(bmain, scene, ob);
	}
	else
		BKE_object_where_is_calc_time(scene, ob, frame);

	return false;
}

/* **************** Rotation Mode Conversions ****************************** */
/* Used for Objects and Pose Channels, since both can have multiple rotation representations */

/* Called from RNA when rotation mode changes
 * - the result should be that the rotations given in the provided pointers have had conversions
 *   applied (as appropriate), such that the rotation of the element hasn't 'visually' changed  */
void BKE_object_rotMode_change_values(float quat[4], float eul[3], float axis[3], float *angle, short oldMode, short newMode)
{
	/* check if any change - if so, need to convert data */
	if (newMode > 0) { /* to euler */
		if (oldMode == ROT_MODE_AXISANGLE) {
			/* axis-angle to euler */
			axis_angle_to_eulO(eul, newMode, axis, *angle);
		}
		else if (oldMode == ROT_MODE_QUAT) {
			/* quat to euler */
			normalize_qt(quat);
			quat_to_eulO(eul, newMode, quat);
		}
		/* else { no conversion needed } */
	}
	else if (newMode == ROT_MODE_QUAT) { /* to quat */
		if (oldMode == ROT_MODE_AXISANGLE) {
			/* axis angle to quat */
			axis_angle_to_quat(quat, axis, *angle);
		}
		else if (oldMode > 0) {
			/* euler to quat */
			eulO_to_quat(quat, eul, oldMode);
		}
		/* else { no conversion needed } */
	}
	else if (newMode == ROT_MODE_AXISANGLE) { /* to axis-angle */
		if (oldMode > 0) {
			/* euler to axis angle */
			eulO_to_axis_angle(axis, angle, eul, oldMode);
		}
		else if (oldMode == ROT_MODE_QUAT) {
			/* quat to axis angle */
			normalize_qt(quat);
			quat_to_axis_angle(axis, angle, quat);
		}

		/* when converting to axis-angle, we need a special exception for the case when there is no axis */
		if (IS_EQF(axis[0], axis[1]) && IS_EQF(axis[1], axis[2])) {
			/* for now, rotate around y-axis then (so that it simply becomes the roll) */
			axis[1] = 1.0f;
		}
	}
}

