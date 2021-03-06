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

/** \file blender/makesrna/intern/rna_object.c
 *  \ingroup RNA
 */

#include <stdio.h>
#include <stdlib.h>

#include "DNA_customdata_types.h"
#include "DNA_group_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_property_types.h"
#include "DNA_scene_types.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "BKE_camera.h"
#include "BKE_editmesh.h"
#include "BKE_group.h" /* needed for BKE_group_object_exists() */
#include "BKE_object_deform.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "BLI_sys_types.h" /* needed for intptr_t used in ED_mesh.h */
#include "ED_mesh.h"

#include "WM_api.h"
#include "WM_types.h"

const EnumPropertyItem rna_enum_object_mode_items[] = {
	{OB_MODE_OBJECT, "OBJECT", ICON_OBJECT_DATAMODE, "Object Mode", ""},
	{OB_MODE_EDIT, "EDIT", ICON_EDITMODE_HLT, "Edit Mode", ""},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_object_empty_drawtype_items[] = {
	{OB_PLAINAXES, "PLAIN_AXES", 0, "Plain Axes", ""},
	{OB_ARROWS, "ARROWS", 0, "Arrows", ""},
	{OB_SINGLE_ARROW, "SINGLE_ARROW", 0, "Single Arrow", ""},
	{OB_CIRCLE, "CIRCLE", 0, "Circle", ""},
	{OB_CUBE, "CUBE", 0, "Cube", ""},
	{OB_EMPTY_SPHERE, "SPHERE", 0, "Sphere", ""},
	{OB_EMPTY_CONE, "CONE", 0, "Cone", ""},
	{OB_EMPTY_IMAGE, "IMAGE", 0, "Image", ""},
	{0, NULL, 0, NULL, NULL}
};


static const EnumPropertyItem parent_type_items[] = {
	{PAROBJECT, "OBJECT", 0, "Object", "The object is parented to an object"},
	{PARVERT1, "VERTEX", 0, "Vertex", "The object is parented to a vertex"},
	{PARVERT3, "VERTEX_3", 0, "3 Vertices", ""},
	{0, NULL, 0, NULL, NULL}
};


/* used for 2 enums */
#define OBTYPE_CU_CURVE {OB_CURVE, "CURVE", 0, "Curve", ""}
#define OBTYPE_CU_SURF {OB_SURF, "SURFACE", 0, "Surface", ""}
#define OBTYPE_CU_FONT {OB_FONT, "FONT", 0, "Font", ""}

const EnumPropertyItem rna_enum_object_type_items[] = {
	{OB_MESH, "MESH", 0, "Mesh", ""},
	OBTYPE_CU_CURVE,
	OBTYPE_CU_SURF,
	OBTYPE_CU_FONT,
	{0, "", 0, NULL, NULL},
	{OB_EMPTY, "EMPTY", 0, "Empty", ""},
	{0, "", 0, NULL, NULL},
	{OB_CAMERA, "CAMERA", 0, "Camera", ""},
	{OB_LAMP, "LAMP", 0, "Lamp", ""},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_object_type_curve_items[] = {
	OBTYPE_CU_CURVE,
	OBTYPE_CU_SURF,
	OBTYPE_CU_FONT,
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_object_axis_items[] = {
	{OB_POSX, "POS_X", 0, "+X", ""},
	{OB_POSY, "POS_Y", 0, "+Y", ""},
	{OB_POSZ, "POS_Z", 0, "+Z", ""},
	{OB_NEGX, "NEG_X", 0, "-X", ""},
	{OB_NEGY, "NEG_Y", 0, "-Y", ""},
	{OB_NEGZ, "NEG_Z", 0, "-Z", ""},
	{0, NULL, 0, NULL, NULL}
};

#ifdef RNA_RUNTIME

#include "BLI_math.h"

#include "DNA_modifier_types.h"

#include "BKE_bullet.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_object.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_scene.h"
#include "BKE_deform.h"

#include "ED_object.h"
#include "ED_curve.h"

static void rna_Object_internal_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	((Object *)ptr->id.data)->id.mod_id++;
}

static void rna_Object_matrix_world_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	/* don't use compat so we get predictable rotation */
	BKE_object_apply_mat4(ptr->id.data, ((Object *)ptr->id.data)->obmat, false, true);
	rna_Object_internal_update(bmain, scene, ptr);
}


static void rna_Object_matrix_local_get(PointerRNA *ptr, float values[16])
{
	Object *ob = ptr->id.data;
	BKE_object_matrix_local_get(ob, (float(*)[4])values);
}

static void rna_Object_matrix_local_set(PointerRNA *ptr, const float values[16])
{
	Object *ob = ptr->id.data;
	float local_mat[4][4];

	/* localspace matrix is truly relative to the parent, but parameters stored in object are
	 * relative to parentinv matrix. Undo the parent inverse part before applying it as local matrix. */
	if (ob->parent) {
		float invmat[4][4];
		invert_m4_m4(invmat, ob->parentinv);
		mul_m4_m4m4(local_mat, invmat, (float(*)[4])values);
	}
	else {
		copy_m4_m4(local_mat, (float(*)[4])values);
	}

	/* don't use compat so we get predictable rotation, and do not use parenting either, because it's a local matrix! */
	BKE_object_apply_mat4(ob, local_mat, false, false);
}

static void rna_Object_matrix_basis_get(PointerRNA *ptr, float values[16])
{
	Object *ob = ptr->id.data;
	BKE_object_to_mat4(ob, (float(*)[4])values);
}

static void rna_Object_matrix_basis_set(PointerRNA *ptr, const float values[16])
{
	Object *ob = ptr->id.data;
	BKE_object_apply_mat4(ob, (float(*)[4])values, false, false);
}

void rna_Object_internal_update_data(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	rna_Object_internal_update(bmain, scene, ptr);
	WM_main_add_notifier(NC_OBJECT | ND_DRAW, ptr->id.data);
}

static void rna_Object_dependency_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	rna_Object_internal_update(bmain, scene, ptr);
	WM_main_add_notifier(NC_OBJECT | ND_PARENT, ptr->id.data);
}

/* when changing the selection flag the scene needs updating */
static void rna_Object_select_update(Main *UNUSED(bmain), Scene *scene, PointerRNA *ptr)
{
	if (scene) {
		Object *ob = (Object *)ptr->id.data;
		short mode = (ob->flag & SELECT) ? BA_SELECT : BA_DESELECT;
		ED_base_object_select(BKE_scene_base_find(scene, ob), mode);
	}
}

static void rna_Base_select_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	Base *base = (Base *)ptr->data;
	short mode = (base->flag & BA_SELECT) ? BA_SELECT : BA_DESELECT;
	ED_base_object_select(base, mode);
}

static void rna_Object_layer_update__internal(Main *bmain, Scene *scene, Base *base, Object *ob)
{
	/* try to avoid scene sort */
	if (scene == NULL) {
		/* pass - unlikely but when running scripts on startup it happens */
	}
	else if ((ob->lay & scene->lay) && (base->lay & scene->lay)) {
		/* pass */
	}
	else if ((ob->lay & scene->lay) == 0 && (base->lay & scene->lay) == 0) {
		/* pass */
	}
}

static void rna_Object_layer_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	Base *base;

	base = scene ? BKE_scene_base_find(scene, ob) : NULL;
	if (!base)
		return;

	SWAP(unsigned int, base->lay, ob->lay);

	rna_Object_layer_update__internal(bmain, scene, base, ob);
	ob->lay = base->lay;

	WM_main_add_notifier(NC_SCENE | ND_LAYER_CONTENT, scene);
}

static void rna_Base_layer_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	Base *base = (Base *)ptr->data;
	Object *ob = (Object *)base->object;

	rna_Object_layer_update__internal(bmain, scene, base, ob);
	ob->lay = base->lay;

	WM_main_add_notifier(NC_SCENE | ND_LAYER_CONTENT, scene);
}

static void rna_Object_data_set(PointerRNA *ptr, PointerRNA value)
{
	Object *ob = (Object *)ptr->data;
	ID *id = value.data;

	if (ob->mode & OB_MODE_EDIT) {
		return;
	}

	/* assigning NULL only for empties */
	if ((id == NULL) && (ob->type != OB_EMPTY)) {
		return;
	}

	BLI_assert(BKE_id_is_in_gobal_main(&ob->id));
	BLI_assert(BKE_id_is_in_gobal_main(id));

	if (ob->type == OB_EMPTY) {
		if (ob->data) {
			id_us_min((ID *)ob->data);
			ob->data = NULL;
		}

		if (!id || GS(id->name) == ID_IM) {
			id_us_plus(id);
			ob->data = id;
		}
	}
	else if (ob->type == OB_MESH) {
		BKE_mesh_assign_object(G_MAIN, ob, (Mesh *)id);
	}
	else {
		if (ob->data) {
			id_us_min((ID *)ob->data);
		}

		/* no need to type-check here ID. this is done in the _typef() function */
		BLI_assert(OB_DATA_SUPPORT_ID(GS(id->name)));
		id_us_plus(id);

		ob->data = id;
		test_object_materials(G_MAIN, ob, id);

		if (GS(id->name) == ID_CU)
			BKE_curve_type_test(ob);
	}
}

static StructRNA *rna_Object_data_typef(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->data;

	/* keep in sync with OB_DATA_SUPPORT_ID() macro */
	switch (ob->type) {
		case OB_EMPTY: return &RNA_Image;
		case OB_MESH: return &RNA_Mesh;
		case OB_CURVE: return &RNA_Curve;
		case OB_SURF: return &RNA_Curve;
		case OB_FONT: return &RNA_Curve;
		case OB_LAMP: return &RNA_Lamp;
		case OB_CAMERA: return &RNA_Camera;
		default: return &RNA_ID;
	}
}

static void rna_Object_parent_set(PointerRNA *ptr, PointerRNA value)
{
	Object *ob = (Object *)ptr->data;
	Object *par = (Object *)value.data;

	{
		ED_object_parent(ob, par, ob->partype, ob->parsubstr);
	}
}

static void rna_Object_parent_type_set(PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->data;

	ED_object_parent(ob, ob->parent, value, ob->parsubstr);
}

static const EnumPropertyItem *rna_Object_parent_type_itemf(bContext *UNUSED(C), PointerRNA *ptr,
                                                      PropertyRNA *UNUSED(prop), bool *r_free)
{
	Object *ob = (Object *)ptr->data;
	EnumPropertyItem *item = NULL;
	int totitem = 0;

	RNA_enum_items_add_value(&item, &totitem, parent_type_items, PAROBJECT);

	if (ob->parent) {
		Object *par = ob->parent;

		if (OB_TYPE_SUPPORT_PARVERT(par->type)) {
			RNA_enum_items_add_value(&item, &totitem, parent_type_items, PARVERT1);
			RNA_enum_items_add_value(&item, &totitem, parent_type_items, PARVERT3);
		}
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static void rna_Object_empty_draw_type_set(PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->data;

	BKE_object_empty_draw_type_set(ob, value);
}

static void rna_VertexGroup_name_set(PointerRNA *ptr, const char *value)
{
	Object *ob = (Object *)ptr->id.data;
	bDeformGroup *dg = (bDeformGroup *)ptr->data;
	BLI_strncpy_utf8(dg->name, value, sizeof(dg->name));
	defgroup_unique_name(dg, ob);
}

static int rna_VertexGroup_index_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;

	return BLI_findindex(&ob->defbase, ptr->data);
}

static PointerRNA rna_Object_active_vertex_group_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	return rna_pointer_inherit_refine(ptr, &RNA_VertexGroup, BLI_findlink(&ob->defbase, ob->actdef - 1));
}

static int rna_Object_active_vertex_group_index_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	return ob->actdef - 1;
}

static void rna_Object_active_vertex_group_index_set(PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	ob->actdef = value + 1;
}

static void rna_Object_active_vertex_group_index_range(PointerRNA *ptr, int *min, int *max,
                                                       int *UNUSED(softmin), int *UNUSED(softmax))
{
	Object *ob = (Object *)ptr->id.data;

	*min = 0;
	*max = max_ii(0, BLI_listbase_count(&ob->defbase) - 1);
}

void rna_object_vgroup_name_index_get(PointerRNA *ptr, char *value, int index)
{
	Object *ob = (Object *)ptr->id.data;
	bDeformGroup *dg;

	dg = BLI_findlink(&ob->defbase, index - 1);

	if (dg) BLI_strncpy(value, dg->name, sizeof(dg->name));
	else value[0] = '\0';
}

int rna_object_vgroup_name_index_length(PointerRNA *ptr, int index)
{
	Object *ob = (Object *)ptr->id.data;
	bDeformGroup *dg;

	dg = BLI_findlink(&ob->defbase, index - 1);
	return (dg) ? strlen(dg->name) : 0;
}

void rna_object_vgroup_name_index_set(PointerRNA *ptr, const char *value, short *index)
{
	Object *ob = (Object *)ptr->id.data;
	*index = defgroup_name_index(ob, value) + 1;
}

void rna_object_vgroup_name_set(PointerRNA *ptr, const char *value, char *result, int maxlen)
{
	Object *ob = (Object *)ptr->id.data;
	bDeformGroup *dg = defgroup_find_name(ob, value);
	if (dg) {
		BLI_strncpy(result, value, maxlen); /* no need for BLI_strncpy_utf8, since this matches an existing group */
		return;
	}

	result[0] = '\0';
}

void rna_object_vcollayer_name_set(PointerRNA *ptr, const char *value, char *result, int maxlen)
{
	Object *ob = (Object *)ptr->id.data;
	Mesh *me;
	CustomDataLayer *layer;
	int a;

	if (ob->type == OB_MESH && ob->data) {
		me = (Mesh *)ob->data;

		for (a = 0; a < me->fdata.totlayer; a++) {
			layer = &me->fdata.layers[a];

			if (layer->type == CD_MCOL && STREQ(layer->name, value)) {
				BLI_strncpy(result, value, maxlen);
				return;
			}
		}
	}

	result[0] = '\0';
}

static int rna_Object_active_material_index_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	return MAX2(ob->actcol - 1, 0);
}

static void rna_Object_active_material_index_set(PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	ob->actcol = value + 1;

	if (ob->type == OB_MESH) {
		Mesh *me = ob->data;

		if (me->edit_btmesh)
			me->edit_btmesh->mat_nr = value;
	}
}

static void rna_Object_active_material_index_range(PointerRNA *ptr, int *min, int *max,
                                                   int *UNUSED(softmin), int *UNUSED(softmax))
{
	Object *ob = (Object *)ptr->id.data;
	*min = 0;
	*max = max_ii(ob->totcol - 1, 0);
}

/* returns active base material */
static PointerRNA rna_Object_active_material_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	Material *ma;

	ma = (ob->totcol) ? give_current_material(ob, ob->actcol) : NULL;
	return rna_pointer_inherit_refine(ptr, &RNA_Material, ma);
}

static void rna_Object_active_material_set(PointerRNA *ptr, PointerRNA value)
{
	Object *ob = (Object *)ptr->id.data;

	BLI_assert(BKE_id_is_in_gobal_main(&ob->id));
	BLI_assert(BKE_id_is_in_gobal_main(value.data));
	assign_material(G_MAIN, ob, value.data, ob->actcol, BKE_MAT_ASSIGN_EXISTING);
}

static int rna_Object_active_material_editable(PointerRNA *ptr, const char **UNUSED(r_info))
{
	Object *ob = (Object *)ptr->id.data;
	bool is_editable;

	if ((ob->matbits == NULL) || (ob->actcol == 0) || ob->matbits[ob->actcol - 1]) {
		is_editable = !ID_IS_LINKED(ob);
	}
	else {
		is_editable = ob->data ? !ID_IS_LINKED(ob->data) : false;
	}

	return is_editable ? PROP_EDITABLE : 0;
}


/* rotation - axis-angle */
static void rna_Object_rotation_axis_angle_get(PointerRNA *ptr, float *value)
{
	Object *ob = ptr->data;

	/* for now, assume that rotation mode is axis-angle */
	value[0] = ob->rotAngle;
	copy_v3_v3(&value[1], ob->rotAxis);
}

/* rotation - axis-angle */
static void rna_Object_rotation_axis_angle_set(PointerRNA *ptr, const float *value)
{
	Object *ob = ptr->data;

	/* for now, assume that rotation mode is axis-angle */
	ob->rotAngle = value[0];
	copy_v3_v3(ob->rotAxis, &value[1]);

	/* TODO: validate axis? */
}

static void rna_Object_rotation_mode_set(PointerRNA *ptr, int value)
{
	Object *ob = ptr->data;

	/* use API Method for conversions... */
	BKE_object_rotMode_change_values(ob->quat, ob->rot, ob->rotAxis, &ob->rotAngle, ob->rotmode, (short)value);

	/* finally, set the new rotation type */
	ob->rotmode = value;
}

static void rna_Object_dimensions_get(PointerRNA *ptr, float *value)
{
	Object *ob = ptr->data;
	BKE_object_dimensions_get(ob, value);
}

static void rna_Object_dimensions_set(PointerRNA *ptr, const float *value)
{
	Object *ob = ptr->data;
	BKE_object_dimensions_set(ob, value);
}

static int rna_Object_location_editable(PointerRNA *ptr, int index)
{
	Object *ob = (Object *)ptr->data;

	/* only if the axis in question is locked, not editable... */
	if ((index == 0) && (ob->protectflag & OB_LOCK_LOCX))
		return 0;
	else if ((index == 1) && (ob->protectflag & OB_LOCK_LOCY))
		return 0;
	else if ((index == 2) && (ob->protectflag & OB_LOCK_LOCZ))
		return 0;
	else
		return PROP_EDITABLE;
}

static int rna_Object_scale_editable(PointerRNA *ptr, int index)
{
	Object *ob = (Object *)ptr->data;

	/* only if the axis in question is locked, not editable... */
	if ((index == 0) && (ob->protectflag & OB_LOCK_SCALEX))
		return 0;
	else if ((index == 1) && (ob->protectflag & OB_LOCK_SCALEY))
		return 0;
	else if ((index == 2) && (ob->protectflag & OB_LOCK_SCALEZ))
		return 0;
	else
		return PROP_EDITABLE;
}

static int rna_Object_rotation_euler_editable(PointerRNA *ptr, int index)
{
	Object *ob = (Object *)ptr->data;

	/* only if the axis in question is locked, not editable... */
	if ((index == 0) && (ob->protectflag & OB_LOCK_ROTX))
		return 0;
	else if ((index == 1) && (ob->protectflag & OB_LOCK_ROTY))
		return 0;
	else if ((index == 2) && (ob->protectflag & OB_LOCK_ROTZ))
		return 0;
	else
		return PROP_EDITABLE;
}

static int rna_Object_rotation_4d_editable(PointerRNA *ptr, int index)
{
	Object *ob = (Object *)ptr->data;

	/* only consider locks if locking components individually... */
	if (ob->protectflag & OB_LOCK_ROT4D) {
		/* only if the axis in question is locked, not editable... */
		if ((index == 0) && (ob->protectflag & OB_LOCK_ROTW))
			return 0;
		else if ((index == 1) && (ob->protectflag & OB_LOCK_ROTX))
			return 0;
		else if ((index == 2) && (ob->protectflag & OB_LOCK_ROTY))
			return 0;
		else if ((index == 3) && (ob->protectflag & OB_LOCK_ROTZ))
			return 0;
	}

	return PROP_EDITABLE;
}


static PointerRNA rna_MaterialSlot_material_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	Material *ma;
	int index = (Material **)ptr->data - ob->mat;

	ma = give_current_material(ob, index + 1);
	return rna_pointer_inherit_refine(ptr, &RNA_Material, ma);
}

static void rna_MaterialSlot_material_set(PointerRNA *ptr, PointerRNA value)
{
	Object *ob = (Object *)ptr->id.data;
	int index = (Material **)ptr->data - ob->mat;

	BLI_assert(BKE_id_is_in_gobal_main(&ob->id));
	BLI_assert(BKE_id_is_in_gobal_main(value.data));
	assign_material(G_MAIN, ob, value.data, index + 1, BKE_MAT_ASSIGN_EXISTING);
}

static int rna_MaterialSlot_link_get(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	int index = (Material **)ptr->data - ob->mat;

	return ob->matbits[index] != 0;
}

static void rna_MaterialSlot_link_set(PointerRNA *ptr, int value)
{
	Object *ob = (Object *)ptr->id.data;
	int index = (Material **)ptr->data - ob->mat;

	if (value) {
		ob->matbits[index] = 1;
		/* ob->colbits |= (1 << index); */ /* DEPRECATED */
	}
	else {
		ob->matbits[index] = 0;
		/* ob->colbits &= ~(1 << index); */ /* DEPRECATED */
	}
}

static int rna_MaterialSlot_name_length(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	Material *ma;
	int index = (Material **)ptr->data - ob->mat;

	ma = give_current_material(ob, index + 1);

	if (ma)
		return strlen(ma->id.name + 2);

	return 0;
}

static void rna_MaterialSlot_name_get(PointerRNA *ptr, char *str)
{
	Object *ob = (Object *)ptr->id.data;
	Material *ma;
	int index = (Material **)ptr->data - ob->mat;

	ma = give_current_material(ob, index + 1);

	if (ma)
		strcpy(str, ma->id.name + 2);
	else
		str[0] = '\0';
}

static void rna_MaterialSlot_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	rna_Object_internal_update(bmain, scene, ptr);
	WM_main_add_notifier(NC_OBJECT | ND_OB_SHADING, ptr->id.data);
	WM_main_add_notifier(NC_MATERIAL | ND_SHADING_LINKS, NULL);
}

static char *rna_MaterialSlot_path(PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;
	int index = (Material **)ptr->data - ob->mat;

	return BLI_sprintfN("material_slots[%d]", index);
}

static unsigned int rna_Object_layer_validate__internal(const bool *values, unsigned int lay)
{
	int i, tot = 0;

	/* ensure we always have some layer selected */
	for (i = 0; i < 20; i++)
		if (values[i])
			tot++;

	if (tot == 0)
		return 0;

	for (i = 0; i < 20; i++) {
		if (values[i]) lay |=  (1 << i);
		else           lay &= ~(1 << i);
	}

	return lay;
}

static void rna_Object_layer_set(PointerRNA *ptr, const bool *values)
{
	Object *ob = (Object *)ptr->data;
	unsigned int lay;

	lay = rna_Object_layer_validate__internal(values, ob->lay);
	if (lay)
		ob->lay = lay;
}

static void rna_Base_layer_set(PointerRNA *ptr, const bool *values)
{
	Base *base = (Base *)ptr->data;

	unsigned int lay;
	lay = rna_Object_layer_validate__internal(values, base->lay);
	if (lay)
		base->lay = lay;

	/* rna_Base_layer_update updates the objects layer */
}

static ModifierData *rna_Object_modifier_new(Object *object, bContext *C, ReportList *reports,
                                             const char *name, int type)
{
	return ED_object_modifier_add(reports, CTX_data_main(C), CTX_data_scene(C), object, name, type);
}

static void rna_Object_modifier_remove(Object *object, bContext *C, ReportList *reports, PointerRNA *md_ptr)
{
	ModifierData *md = md_ptr->data;
	if (ED_object_modifier_remove(reports, CTX_data_main(C), object, md) == false) {
		/* error is already set */
		return;
	}

	RNA_POINTER_INVALIDATE(md_ptr);

	WM_main_add_notifier(NC_OBJECT | ND_MODIFIER | NA_REMOVED, object);
}

static void rna_Object_modifier_clear(Object *object, bContext *C)
{
	ED_object_modifier_clear(CTX_data_main(C), object);

	WM_main_add_notifier(NC_OBJECT | ND_MODIFIER | NA_REMOVED, object);
}

static void rna_Object_boundbox_get(PointerRNA *ptr, float *values)
{
	Object *ob = (Object *)ptr->id.data;
	BoundBox *bb = BKE_object_boundbox_get(ob);
	if (bb) {
		memcpy(values, bb->vec, sizeof(bb->vec));
	}
	else {
		copy_vn_fl(values, sizeof(bb->vec) / sizeof(float), 0.0f);
	}

}

static bDeformGroup *rna_Object_vgroup_new(Object *ob, const char *name)
{
	bDeformGroup *defgroup = BKE_object_defgroup_add_name(ob, name);

	WM_main_add_notifier(NC_OBJECT | ND_DRAW, ob);

	return defgroup;
}

static void rna_Object_vgroup_remove(Object *ob, ReportList *reports, PointerRNA *defgroup_ptr)
{
	bDeformGroup *defgroup = defgroup_ptr->data;
	if (BLI_findindex(&ob->defbase, defgroup) == -1) {
		BKE_reportf(reports, RPT_ERROR, "DeformGroup '%s' not in object '%s'", defgroup->name, ob->id.name + 2);
		return;
	}

	BKE_object_defgroup_remove(ob, defgroup);
	RNA_POINTER_INVALIDATE(defgroup_ptr);

	WM_main_add_notifier(NC_OBJECT | ND_DRAW, ob);
}

static void rna_Object_vgroup_clear(Object *ob)
{
	BKE_object_defgroup_remove_all(ob);

	WM_main_add_notifier(NC_OBJECT | ND_DRAW, ob);
}

static void rna_VertexGroup_vertex_add(ID *id, bDeformGroup *def, ReportList *reports, int index_len,
                                       int *index, float weight, int assignmode)
{
	Object *ob = (Object *)id;

	if (BKE_object_is_in_editmode_vgroup(ob)) {
		BKE_report(reports, RPT_ERROR, "VertexGroup.add(): cannot be called while object is in edit mode");
		return;
	}

	while (index_len--)
		ED_vgroup_vert_add(ob, def, *index++, weight, assignmode);  /* XXX, not efficient calling within loop*/

	WM_main_add_notifier(NC_GEOM | ND_DATA, (ID *)ob->data);
}

static void rna_VertexGroup_vertex_remove(ID *id, bDeformGroup *dg, ReportList *reports, int index_len, int *index)
{
	Object *ob = (Object *)id;

	if (BKE_object_is_in_editmode_vgroup(ob)) {
		BKE_report(reports, RPT_ERROR, "VertexGroup.remove(): cannot be called while object is in edit mode");
		return;
	}

	while (index_len--)
		ED_vgroup_vert_remove(ob, dg, *index++);

	WM_main_add_notifier(NC_GEOM | ND_DATA, (ID *)ob->data);
}

static float rna_VertexGroup_weight(ID *id, bDeformGroup *dg, ReportList *reports, int index)
{
	float weight = ED_vgroup_vert_weight((Object *)id, dg, index);

	if (weight < 0) {
		BKE_report(reports, RPT_ERROR, "Vertex not in group");
	}
	return weight;
}

/* generic poll functions */

bool rna_Curve_object_poll(PointerRNA *UNUSED(ptr), PointerRNA value)
{
	return ((Object *)value.id.data)->type == OB_CURVE;
}

bool rna_Mesh_object_poll(PointerRNA *UNUSED(ptr), PointerRNA value)
{
	return ((Object *)value.id.data)->type == OB_MESH;
}

bool rna_Camera_object_poll(PointerRNA *UNUSED(ptr), PointerRNA value)
{
	return ((Object *)value.id.data)->type == OB_CAMERA;
}

bool rna_Lamp_object_poll(PointerRNA *UNUSED(ptr), PointerRNA value)
{
	return ((Object *)value.id.data)->type == OB_LAMP;
}

#else

static void rna_def_vertex_group(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	FunctionRNA *func;
	PropertyRNA *parm;

	static const EnumPropertyItem assign_mode_items[] = {
		{WEIGHT_REPLACE,  "REPLACE",  0, "Replace",  "Replace"},
		{WEIGHT_ADD,      "ADD",      0, "Add",      "Add"},
		{WEIGHT_SUBTRACT, "SUBTRACT", 0, "Subtract", "Subtract"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "VertexGroup", NULL);
	RNA_def_struct_sdna(srna, "bDeformGroup");
	RNA_def_struct_ui_text(srna, "Vertex Group", "Group of vertices, used for other purposes");
	RNA_def_struct_ui_icon(srna, ICON_GROUP_VERTEX);

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Name", "Vertex group name");
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_VertexGroup_name_set");
	/* update data because modifiers may use [#24761] */
	RNA_def_property_update(prop, NC_GEOM | ND_DATA | NA_RENAME, "rna_Object_internal_update_data");

	prop = RNA_def_property(srna, "lock_weight", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "", "Maintain the relative weights for the group");
	RNA_def_property_boolean_sdna(prop, NULL, "flag", 0);
	/* update data because modifiers may use [#24761] */
	RNA_def_property_update(prop, NC_GEOM | ND_DATA | NA_RENAME, "rna_Object_internal_update_data");

	prop = RNA_def_property(srna, "index", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_int_funcs(prop, "rna_VertexGroup_index_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Index", "Index number of the vertex group");

	func = RNA_def_function(srna, "add", "rna_VertexGroup_vertex_add");
	RNA_def_function_ui_description(func, "Add vertices to the group");
	RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_SELF_ID);
	/* TODO, see how array size of 0 works, this shouldnt be used */
	parm = RNA_def_int_array(func, "index", 1, NULL, 0, 0, "", "Index List", 0, 0);
	RNA_def_parameter_flags(parm, PROP_DYNAMIC, PARM_REQUIRED);
	parm = RNA_def_float(func, "weight", 0, 0.0f, 1.0f, "", "Vertex weight", 0.0f, 1.0f);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_enum(func, "type", assign_mode_items, 0, "", "Vertex assign mode");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

	func = RNA_def_function(srna, "remove", "rna_VertexGroup_vertex_remove");
	RNA_def_function_ui_description(func, "Remove a vertex from the group");
	RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_SELF_ID);
	/* TODO, see how array size of 0 works, this shouldnt be used */
	parm = RNA_def_int_array(func, "index", 1, NULL, 0, 0, "", "Index List", 0, 0);
	RNA_def_parameter_flags(parm, PROP_DYNAMIC, PARM_REQUIRED);

	func = RNA_def_function(srna, "weight", "rna_VertexGroup_weight");
	RNA_def_function_ui_description(func, "Get a vertex weight from the group");
	RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_SELF_ID);
	parm = RNA_def_int(func, "index", 0, 0, INT_MAX, "Index", "The index of the vertex", 0, INT_MAX);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_float(func, "weight", 0, 0.0f, 1.0f, "", "Vertex weight", 0.0f, 1.0f);
	RNA_def_function_return(func, parm);
}

static void rna_def_material_slot(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem link_items[] = {
		{1, "OBJECT", 0, "Object", ""},
		{0, "DATA", 0, "Data", ""},
		{0, NULL, 0, NULL, NULL}
	};

	/* NOTE: there is no MaterialSlot equivalent in DNA, so the internal
	 * pointer data points to ob->mat + index, and we manually implement
	 * get/set for the properties. */

	srna = RNA_def_struct(brna, "MaterialSlot", NULL);
	RNA_def_struct_ui_text(srna, "Material Slot", "Material slot in an object");
	RNA_def_struct_ui_icon(srna, ICON_MATERIAL_DATA);

	prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Material");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_pointer_funcs(prop, "rna_MaterialSlot_material_get", "rna_MaterialSlot_material_set", NULL, NULL);
	RNA_def_property_ui_text(prop, "Material", "Material data-block used by this material slot");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_MaterialSlot_update");

	prop = RNA_def_property(srna, "link", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, link_items);
	RNA_def_property_enum_funcs(prop, "rna_MaterialSlot_link_get", "rna_MaterialSlot_link_set", NULL);
	RNA_def_property_ui_text(prop, "Link", "Link material to object or the object's data");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_MaterialSlot_update");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_string_funcs(prop, "rna_MaterialSlot_name_get", "rna_MaterialSlot_name_length", NULL);
	RNA_def_property_ui_text(prop, "Name", "Material slot name");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_struct_name_property(srna, prop);

	RNA_def_struct_path_func(srna, "rna_MaterialSlot_path");
}


/* object.modifiers */
static void rna_def_object_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "ObjectModifiers");
	srna = RNA_def_struct(brna, "ObjectModifiers", NULL);
	RNA_def_struct_sdna(srna, "Object");
	RNA_def_struct_ui_text(srna, "Object Modifiers", "Collection of object modifiers");

	/* add modifier */
	func = RNA_def_function(srna, "new", "rna_Object_modifier_new");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
	RNA_def_function_ui_description(func, "Add a new modifier");
	parm = RNA_def_string(func, "name", "Name", 0, "", "New name for the modifier");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	/* modifier to add */
	parm = RNA_def_enum(func, "type", rna_enum_object_modifier_type_items, 1, "", "Modifier type to add");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	/* return type */
	parm = RNA_def_pointer(func, "modifier", "Modifier", "", "Newly created modifier");
	RNA_def_function_return(func, parm);

	/* remove modifier */
	func = RNA_def_function(srna, "remove", "rna_Object_modifier_remove");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
	RNA_def_function_ui_description(func, "Remove an existing modifier from the object");
	/* modifier to remove */
	parm = RNA_def_pointer(func, "modifier", "Modifier", "", "Modifier to remove");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	/* clear all modifiers */
	func = RNA_def_function(srna, "clear", "rna_Object_modifier_clear");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT);
	RNA_def_function_ui_description(func, "Remove all modifiers from the object");
}

/* object.vertex_groups */
static void rna_def_object_vertex_groups(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;

	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "VertexGroups");
	srna = RNA_def_struct(brna, "VertexGroups", NULL);
	RNA_def_struct_sdna(srna, "Object");
	RNA_def_struct_ui_text(srna, "Vertex Groups", "Collection of vertex groups");

	prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "VertexGroup");
	RNA_def_property_pointer_funcs(prop, "rna_Object_active_vertex_group_get",
	                               "rna_Object_active_vertex_group_set", NULL, NULL);
	RNA_def_property_ui_text(prop, "Active Vertex Group", "Vertex groups of the object");
	RNA_def_property_update(prop, NC_GEOM | ND_DATA, "rna_Object_internal_update_data");

	prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_int_sdna(prop, NULL, "actdef");
	RNA_def_property_int_funcs(prop, "rna_Object_active_vertex_group_index_get",
	                           "rna_Object_active_vertex_group_index_set",
	                           "rna_Object_active_vertex_group_index_range");
	RNA_def_property_ui_text(prop, "Active Vertex Group Index", "Active index in vertex group array");
	RNA_def_property_update(prop, NC_GEOM | ND_DATA, "rna_Object_internal_update_data");

	/* vertex groups */ /* add_vertex_group */
	func = RNA_def_function(srna, "new", "rna_Object_vgroup_new");
	RNA_def_function_ui_description(func, "Add vertex group to object");
	RNA_def_string(func, "name", "Group", 0, "", "Vertex group name"); /* optional */
	parm = RNA_def_pointer(func, "group", "VertexGroup", "", "New vertex group");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_Object_vgroup_remove");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	RNA_def_function_ui_description(func, "Delete vertex group from object");
	parm = RNA_def_pointer(func, "group", "VertexGroup", "", "Vertex group to remove");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	func = RNA_def_function(srna, "clear", "rna_Object_vgroup_clear");
	RNA_def_function_ui_description(func, "Delete all vertex groups from object");
}

static void rna_def_object(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem drawtype_items[] = {
		{OB_BOUNDBOX, "BOUNDS", 0, "Bounds", "Draw the bounds of the object"},
		{OB_WIRE, "WIRE", 0, "Wire", "Draw the object as a wireframe"},
		{OB_SOLID, "SOLID", 0, "Solid", "Draw the object as a solid (if solid drawing is enabled in the viewport)"},
		{OB_TEXTURE, "TEXTURED", 0, "Textured",
		             "Draw the object with textures (if textures are enabled in the viewport)"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem boundtype_items[] = {
		{OB_BOUND_BOX, "BOX", 0, "Box", "Draw bounds as box"},
		{OB_BOUND_SPHERE, "SPHERE", 0, "Sphere", "Draw bounds as sphere"},
		{OB_BOUND_CYLINDER, "CYLINDER", 0, "Cylinder", "Draw bounds as cylinder"},
		{OB_BOUND_CONE, "CONE", 0, "Cone", "Draw bounds as cone"},
		{OB_BOUND_CAPSULE, "CAPSULE", 0, "Capsule", "Draw bounds as capsule"},
		{0, NULL, 0, NULL, NULL}
	};

	/* XXX: this RNA enum define is currently duplicated for objects,
	 *      since there is some text here which is not applicable */
	static const EnumPropertyItem prop_rotmode_items[] = {
		{ROT_MODE_QUAT, "QUATERNION", 0, "Quaternion (WXYZ)", "No Gimbal Lock"},
		{ROT_MODE_XYZ, "XYZ", 0, "XYZ Euler", "XYZ Rotation Order - prone to Gimbal Lock (default)"},
		{ROT_MODE_XZY, "XZY", 0, "XZY Euler", "XZY Rotation Order - prone to Gimbal Lock"},
		{ROT_MODE_YXZ, "YXZ", 0, "YXZ Euler", "YXZ Rotation Order - prone to Gimbal Lock"},
		{ROT_MODE_YZX, "YZX", 0, "YZX Euler", "YZX Rotation Order - prone to Gimbal Lock"},
		{ROT_MODE_ZXY, "ZXY", 0, "ZXY Euler", "ZXY Rotation Order - prone to Gimbal Lock"},
		{ROT_MODE_ZYX, "ZYX", 0, "ZYX Euler", "ZYX Rotation Order - prone to Gimbal Lock"},
		{ROT_MODE_AXISANGLE, "AXIS_ANGLE", 0, "Axis Angle",
		                     "Axis Angle (W+XYZ), defines a rotation around some axis defined by 3D-Vector"},
		{0, NULL, 0, NULL, NULL}
	};

	static float default_quat[4] = {1, 0, 0, 0};    /* default quaternion values */
	static float default_axisAngle[4] = {0, 0, 1, 0};   /* default axis-angle rotation values */
	static float default_scale[3] = {1, 1, 1}; /* default scale values */
	static int boundbox_dimsize[] = {8, 3};

	srna = RNA_def_struct(brna, "Object", "ID");
	RNA_def_struct_ui_text(srna, "Object", "Object data-block defining an object in a scene");
	RNA_def_struct_clear_flag(srna, STRUCT_ID_REFCOUNT);
	RNA_def_struct_ui_icon(srna, ICON_OBJECT_DATA);

	prop = RNA_def_property(srna, "data", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "ID");
	RNA_def_property_pointer_funcs(prop, NULL, "rna_Object_data_set", "rna_Object_data_typef", NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
	RNA_def_property_ui_text(prop, "Data", "Object data");
	RNA_def_property_update(prop, 0, "rna_Object_internal_update_data");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, rna_enum_object_type_items);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Type", "Type of Object");

	prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "mode");
	RNA_def_property_enum_items(prop, rna_enum_object_mode_items);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Mode", "Object interaction mode");

	prop = RNA_def_property(srna, "layers", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_ui_text(prop, "Layers", "Layers the object is on");
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Object_layer_set");
	RNA_def_property_flag(prop, PROP_LIB_EXCEPTION);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_layer_update");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

	prop = RNA_def_property(srna, "layers_local_view", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 0x01000000);
	RNA_def_property_array(prop, 8);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Local View Layers", "3D local view layers the object is on");

	prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", SELECT);
	RNA_def_property_ui_text(prop, "Select", "Object selection state");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_select_update");

	/* for data access */
	prop = RNA_def_property(srna, "bound_box", PROP_FLOAT, PROP_NONE);
	RNA_def_property_multi_array(prop, 2, boundbox_dimsize);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_float_funcs(prop, "rna_Object_boundbox_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Bounding Box",
	                         "Object's bounding box in object-space coordinates, all values are -1.0 when "
	                         "not available");

	/* parent */
	prop = RNA_def_property(srna, "parent", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_funcs(prop, NULL, "rna_Object_parent_set", NULL, NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
	RNA_def_property_ui_text(prop, "Parent", "Parent Object");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_dependency_update");

	prop = RNA_def_property(srna, "parent_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "partype");
	RNA_def_property_enum_items(prop, parent_type_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Object_parent_type_set", "rna_Object_parent_type_itemf");
	RNA_def_property_ui_text(prop, "Parent Type", "Type of parent relation");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_dependency_update");

	prop = RNA_def_property(srna, "parent_vertices", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "par1");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Parent Vertices", "Indices of vertices in case of a vertex parenting relation");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_dependency_update");

	/* proxy */
	prop = RNA_def_property(srna, "proxy", PROP_POINTER, PROP_NONE);
	RNA_def_property_ui_text(prop, "Proxy", "Library object this proxy object controls");

	prop = RNA_def_property(srna, "proxy_group", PROP_POINTER, PROP_NONE);
	RNA_def_property_ui_text(prop, "Proxy Group", "Library group duplicator object this proxy object controls");

	/* materials */
	prop = RNA_def_property(srna, "material_slots", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "mat", "totcol");
	RNA_def_property_struct_type(prop, "MaterialSlot");
	/* don't dereference pointer! */
	RNA_def_property_collection_funcs(prop, NULL, NULL, NULL, "rna_iterator_array_get", NULL, NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Material Slots", "Material slots in the object");

	prop = RNA_def_property(srna, "active_material", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Material");
	RNA_def_property_pointer_funcs(prop, "rna_Object_active_material_get",
	                               "rna_Object_active_material_set", NULL, NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_editable_func(prop, "rna_Object_active_material_editable");
	RNA_def_property_ui_text(prop, "Active Material", "Active material being displayed");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_MaterialSlot_update");

	prop = RNA_def_property(srna, "active_material_index", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "actcol");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_int_funcs(prop, "rna_Object_active_material_index_get", "rna_Object_active_material_index_set",
	                           "rna_Object_active_material_index_range");
	RNA_def_property_ui_text(prop, "Active Material Index", "Index of active material slot");
	RNA_def_property_update(prop, NC_MATERIAL | ND_SHADING_LINKS, NULL);

	/* transform */
	prop = RNA_def_property(srna, "location", PROP_FLOAT, PROP_TRANSLATION);
	RNA_def_property_float_sdna(prop, NULL, "loc");
	RNA_def_property_editable_array_func(prop, "rna_Object_location_editable");
	RNA_def_property_ui_text(prop, "Location", "Location of the object");
	RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "rotation_quaternion", PROP_FLOAT, PROP_QUATERNION);
	RNA_def_property_float_sdna(prop, NULL, "quat");
	RNA_def_property_editable_array_func(prop, "rna_Object_rotation_4d_editable");
	RNA_def_property_float_array_default(prop, default_quat);
	RNA_def_property_ui_text(prop, "Quaternion Rotation", "Rotation in Quaternions");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/* XXX: for axis-angle, it would have been nice to have 2 separate fields for UI purposes, but
	 * having a single one is better for Keyframing and other property-management situations...
	 */
	prop = RNA_def_property(srna, "rotation_axis_angle", PROP_FLOAT, PROP_AXISANGLE);
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_Object_rotation_axis_angle_get",
	                             "rna_Object_rotation_axis_angle_set", NULL);
	RNA_def_property_editable_array_func(prop, "rna_Object_rotation_4d_editable");
	RNA_def_property_float_array_default(prop, default_axisAngle);
	RNA_def_property_ui_text(prop, "Axis-Angle Rotation", "Angle of Rotation for Axis-Angle rotation representation");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "rotation_euler", PROP_FLOAT, PROP_EULER);
	RNA_def_property_float_sdna(prop, NULL, "rot");
	RNA_def_property_editable_array_func(prop, "rna_Object_rotation_euler_editable");
	RNA_def_property_ui_text(prop, "Euler Rotation", "Rotation in Eulers");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "rotation_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "rotmode");
	RNA_def_property_enum_items(prop, prop_rotmode_items); /* XXX move to using a single define of this someday */
	RNA_def_property_enum_funcs(prop, NULL, "rna_Object_rotation_mode_set", NULL);
	RNA_def_property_ui_text(prop, "Rotation Mode", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, NULL);

	prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "size");
	RNA_def_property_flag(prop, PROP_PROPORTIONAL);
	RNA_def_property_editable_array_func(prop, "rna_Object_scale_editable");
	RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, 3);
	RNA_def_property_float_array_default(prop, default_scale);
	RNA_def_property_ui_text(prop, "Scale", "Scaling of the object");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "dimensions", PROP_FLOAT, PROP_XYZ_LENGTH);
	RNA_def_property_array(prop, 3);
	/* only for the transform-panel and conflicts with animating scale */
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_float_funcs(prop, "rna_Object_dimensions_get", "rna_Object_dimensions_set", NULL);
	RNA_def_property_ui_range(prop, 0.0f, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
	RNA_def_property_ui_text(prop, "Dimensions", "Absolute bounding box dimensions of the object");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");


	/* delta transforms */
	prop = RNA_def_property(srna, "delta_location", PROP_FLOAT, PROP_TRANSLATION);
	RNA_def_property_float_sdna(prop, NULL, "dloc");
	RNA_def_property_ui_text(prop, "Delta Location", "Extra translation added to the location of the object");
	RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "delta_rotation_euler", PROP_FLOAT, PROP_EULER);
	RNA_def_property_float_sdna(prop, NULL, "drot");
	RNA_def_property_ui_text(prop, "Delta Rotation (Euler)",
	                         "Extra rotation added to the rotation of the object (when using Euler rotations)");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "delta_rotation_quaternion", PROP_FLOAT, PROP_QUATERNION);
	RNA_def_property_float_sdna(prop, NULL, "dquat");
	RNA_def_property_float_array_default(prop, default_quat);
	RNA_def_property_ui_text(prop, "Delta Rotation (Quaternion)",
	                         "Extra rotation added to the rotation of the object (when using Quaternion rotations)");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "delta_scale", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "dscale");
	RNA_def_property_flag(prop, PROP_PROPORTIONAL);
	RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, 3);
	RNA_def_property_float_array_default(prop, default_scale);
	RNA_def_property_ui_text(prop, "Delta Scale", "Extra scaling added to the scale of the object");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/* transform locks */
	prop = RNA_def_property(srna, "lock_location", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "protectflag", OB_LOCK_LOCX);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Lock Location", "Lock editing of location in the interface");
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "lock_rotation", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "protectflag", OB_LOCK_ROTX);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Lock Rotation", "Lock editing of rotation in the interface");
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/* XXX this is sub-optimal - it really should be included above,
	 *     but due to technical reasons we can't do this! */
	prop = RNA_def_property(srna, "lock_rotation_w", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "protectflag", OB_LOCK_ROTW);
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_ui_text(prop, "Lock Rotation (4D Angle)",
	                         "Lock editing of 'angle' component of four-component rotations in the interface");
	/* XXX this needs a better name */
	prop = RNA_def_property(srna, "lock_rotations_4d", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "protectflag", OB_LOCK_ROT4D);
	RNA_def_property_ui_text(prop, "Lock Rotations (4D)",
	                         "Lock editing of four component rotations by components (instead of as Eulers)");

	prop = RNA_def_property(srna, "lock_scale", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "protectflag", OB_LOCK_SCALEX);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Lock Scale", "Lock editing of scale in the interface");
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/* matrix */
	prop = RNA_def_property(srna, "matrix_world", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "obmat");
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_ui_text(prop, "Matrix World", "Worldspace transformation matrix");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_matrix_world_update");

	prop = RNA_def_property(srna, "matrix_local", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_ui_text(prop, "Local Matrix", "Parent relative transformation matrix - "
	                         "WARNING: Only takes into account 'Object' parenting");
	RNA_def_property_float_funcs(prop, "rna_Object_matrix_local_get", "rna_Object_matrix_local_set", NULL);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, NULL);

	prop = RNA_def_property(srna, "matrix_basis", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_ui_text(prop, "Input Matrix",
	                         "Matrix access to location, rotation and scale (including deltas), "
	                         "before constraints and parenting are applied");
	RNA_def_property_float_funcs(prop, "rna_Object_matrix_basis_get", "rna_Object_matrix_basis_set", NULL);
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/*parent_inverse*/
	prop = RNA_def_property(srna, "matrix_parent_inverse", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "parentinv");
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(prop, "Matrix", "Inverse of object's parent matrix at time of parenting");
	RNA_def_property_update(prop, NC_OBJECT | ND_TRANSFORM, "rna_Object_internal_update");

	/* modifiers */
	prop = RNA_def_property(srna, "modifiers", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "Modifier");
	RNA_def_property_ui_text(prop, "Modifiers", "Modifiers affecting the geometric data of the object");
	rna_def_object_modifiers(brna, prop);

	/* vertex groups */
	prop = RNA_def_property(srna, "vertex_groups", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "defbase", NULL);
	RNA_def_property_struct_type(prop, "VertexGroup");
	RNA_def_property_ui_text(prop, "Vertex Groups", "Vertex groups of the object");
	rna_def_object_vertex_groups(brna, prop);

	/* empty */
	prop = RNA_def_property(srna, "empty_draw_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "empty_drawtype");
	RNA_def_property_enum_items(prop, rna_enum_object_empty_drawtype_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_Object_empty_draw_type_set", NULL);
	RNA_def_property_ui_text(prop, "Empty Display Type", "Viewport display style for empties");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "empty_draw_size", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "empty_drawsize");
	RNA_def_property_range(prop, 0.0001f, 1000.0f);
	RNA_def_property_ui_range(prop, 0.01, 100, 1, 2);
	RNA_def_property_ui_text(prop, "Empty Display Size", "Size of display for empties in the viewport");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "empty_image_offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "ima_ofs");
	RNA_def_property_ui_text(prop, "Origin Offset", "Origin offset distance");
	RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 0.1f, 2);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "image_user", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "iuser");
	RNA_def_property_ui_text(prop, "Image User",
	                         "Parameters defining which layer, pass and frame of the image is displayed");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	/* render */
	prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "col");
	RNA_def_property_ui_text(prop, "Color", "Object color and alpha, used when faces have the ObColor mode enabled");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	/* physics */
	prop = RNA_def_property(srna, "rigid_body", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "rigidbody_object");
	RNA_def_property_struct_type(prop, "RigidBodyObject");
	RNA_def_property_ui_text(prop, "Rigid Body Settings", "Settings for rigid body simulation");

	prop = RNA_def_property(srna, "rigid_body_constraint", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "rigidbody_constraint");
	RNA_def_property_struct_type(prop, "RigidBodyConstraint");
	RNA_def_property_ui_text(prop, "Rigid Body Constraint", "Constraint constraining rigid bodies");

	/* restrict */
	prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "restrictflag", OB_RESTRICT_VIEW);
	RNA_def_property_ui_text(prop, "Restrict View", "Restrict visibility in the viewport");
	RNA_def_property_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "hide_select", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "restrictflag", OB_RESTRICT_SELECT);
	RNA_def_property_ui_text(prop, "Restrict Select", "Restrict selection in the viewport");
	RNA_def_property_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "hide_render", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "restrictflag", OB_RESTRICT_RENDER);
	RNA_def_property_ui_text(prop, "Restrict Render", "Restrict renderability");
	RNA_def_property_ui_icon(prop, ICON_RESTRICT_RENDER_OFF, 1);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	/* drawing */
	prop = RNA_def_property(srna, "draw_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "dt");
	RNA_def_property_enum_items(prop, drawtype_items);
	RNA_def_property_ui_text(prop, "Maximum Draw Type",  "Maximum draw type to display object with in viewport");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Object_internal_update");

	prop = RNA_def_property(srna, "show_bounds", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAWBOUNDOX);
	RNA_def_property_ui_text(prop, "Draw Bounds", "Display the object's bounds");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "draw_bounds_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "boundtype");
	RNA_def_property_enum_items(prop, boundtype_items);
	RNA_def_property_ui_text(prop, "Draw Bounds Type", "Object boundary display type");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_name", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAWNAME);
	RNA_def_property_ui_text(prop, "Draw Name", "Display the object's name");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_AXIS);
	RNA_def_property_ui_text(prop, "Draw Axes", "Display the object's origin and axes");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_texture_space", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_TEXSPACE);
	RNA_def_property_ui_text(prop, "Draw Texture Space", "Display the object's texture space");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_wire", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAWWIRE);
	RNA_def_property_ui_text(prop, "Draw Wire", "Add the object's wireframe over solid drawing");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_all_edges", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAW_ALL_EDGES);
	RNA_def_property_ui_text(prop, "Draw All Edges", "Display all edges for mesh objects");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_transparent", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAWTRANSP);
	RNA_def_property_ui_text(prop, "Draw Transparent",
	                         "Display material transparency in the object (unsupported for duplicator drawing)");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	prop = RNA_def_property(srna, "show_x_ray", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dtx", OB_DRAWXRAY);
	RNA_def_property_ui_text(prop, "X-Ray",
	                         "Make the object draw in front of others (unsupported for duplicator drawing)");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

	RNA_api_object(srna);
}

static void rna_def_object_base(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ObjectBase", NULL);
	RNA_def_struct_sdna(srna, "Base");
	RNA_def_struct_ui_text(srna, "Object Base", "An object instance in a scene");
	RNA_def_struct_ui_icon(srna, ICON_OBJECT_DATA);

	prop = RNA_def_property(srna, "object", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "object");
	RNA_def_property_ui_text(prop, "Object", "Object this base links to");

	/* same as object layer */
	prop = RNA_def_property(srna, "layers", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_ui_text(prop, "Layers", "Layers the object base is on");
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Base_layer_set");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Base_layer_update");

	prop = RNA_def_property(srna, "layers_local_view", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 0x01000000);
	RNA_def_property_array(prop, 8);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Local View Layers", "3D local view layers the object base is on");

	prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BA_SELECT);
	RNA_def_property_ui_text(prop, "Select", "Object base selection state");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_Base_select_update");

	RNA_api_object_base(srna);
}

void RNA_def_object(BlenderRNA *brna)
{
	rna_def_object(brna);

	RNA_define_animate_sdna(false);
	rna_def_object_base(brna);
	rna_def_vertex_group(brna);
	rna_def_material_slot(brna);
	RNA_define_animate_sdna(true);
}

#endif
