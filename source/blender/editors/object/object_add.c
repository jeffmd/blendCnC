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

/** \file blender/editors/object/object_add.c
 *  \ingroup edobj
 */


#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_camera_types.h"
#include "DNA_curve_types.h"
#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_vfont_types.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"

#include "BLT_translation.h"

#include "BKE_camera.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_DerivedMesh.h"
#include "BKE_displist.h"
#include "BKE_font.h"
#include "BKE_group.h"
#include "BKE_lamp.h"
#include "BKE_lattice.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_curve.h"
#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_physics.h"
#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_view3d.h"

#include "UI_resources.h"

#include "GPU_material.h"

#include "object_intern.h"

/* this is an exact copy of the define in rna_lamp.c
 * kept here because of linking order.
 * Icons are only defined here */
const EnumPropertyItem rna_enum_lamp_type_items[] = {
	{LA_LOCAL, "POINT", ICON_LAMP_POINT, "Point", "Omnidirectional point light source"},
	{LA_SUN, "SUN", ICON_LAMP_SUN, "Sun", "Constant direction parallel ray light source"},
	{LA_SPOT, "SPOT", ICON_LAMP_SPOT, "Spot", "Directional cone light source"},
	{LA_HEMI, "HEMI", ICON_LAMP_HEMI, "Hemi", "180 degree constant light source"},
	{LA_AREA, "AREA", ICON_LAMP_AREA, "Area", "Directional area light source"},
	{0, NULL, 0, NULL, NULL}
};

/************************** Exported *****************************/

void ED_object_location_from_view(bContext *C, float loc[3])
{
	View3D *v3d = CTX_wm_view3d(C);
	Scene *scene = CTX_data_scene(C);
	const float *cursor;

	cursor = ED_view3d_cursor3d_get(scene, v3d);

	copy_v3_v3(loc, cursor);
}

void ED_object_rotation_from_quat(float rot[3], const float viewquat[4], const char align_axis)
{
	BLI_assert(align_axis >= 'X' && align_axis <= 'Z');

	switch (align_axis) {
		case 'X':
		{
			/* Same as 'rv3d->viewinv[1]' */
			float axis_y[4] = {0.0f, 1.0f, 0.0f};
			float quat_y[4], quat[4];
			axis_angle_to_quat(quat_y, axis_y, M_PI_2);
			mul_qt_qtqt(quat, viewquat, quat_y);
			quat_to_eul(rot, quat);
			break;
		}
		case 'Y':
		{
			quat_to_eul(rot, viewquat);
			rot[0] -= (float)M_PI_2;
			break;
		}
		case 'Z':
		{
			quat_to_eul(rot, viewquat);
			break;
		}
	}
}

void ED_object_rotation_from_view(bContext *C, float rot[3], const char align_axis)
{
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	BLI_assert(align_axis >= 'X' && align_axis <= 'Z');
	if (rv3d) {
		float viewquat[4];
		copy_qt_qt(viewquat, rv3d->viewquat);
		viewquat[0] *= -1.0f;
		ED_object_rotation_from_quat(rot, viewquat, align_axis);
	}
	else {
		zero_v3(rot);
	}
}

void ED_object_base_init_transform(bContext *C, Base *base, const float loc[3], const float rot[3])
{
	Object *ob = base->object;
	Scene *scene = CTX_data_scene(C);

	if (!scene) return;

	if (loc)
		copy_v3_v3(ob->loc, loc);

	if (rot)
		copy_v3_v3(ob->rot, rot);

	BKE_object_where_is_calc(scene, ob);
}

/* Uses context to figure out transform for primitive.
 * Returns standard diameter. */
float ED_object_new_primitive_matrix(
        bContext *C, Object *obedit,
        const float loc[3], const float rot[3], float primmat[4][4])
{
	Scene *scene = CTX_data_scene(C);
	View3D *v3d = CTX_wm_view3d(C);
	float mat[3][3], rmat[3][3], cmat[3][3], imat[3][3];

	unit_m4(primmat);

	eul_to_mat3(rmat, rot);
	invert_m3(rmat);

	/* inverse transform for initial rotation and object */
	copy_m3_m4(mat, obedit->obmat);
	mul_m3_m3m3(cmat, rmat, mat);
	invert_m3_m3(imat, cmat);
	copy_m4_m3(primmat, imat);

	/* center */
	copy_v3_v3(primmat[3], loc);
	sub_v3_v3v3(primmat[3], primmat[3], obedit->obmat[3]);
	invert_m3_m3(imat, mat);
	mul_m3_v3(imat, primmat[3]);

	{
		const float dia = v3d ? ED_view3d_grid_scale(scene, v3d, NULL) : ED_scene_grid_scale(scene, NULL);
		return dia;
	}

	// return 1.0f;
}

/********************* Add Object Operator ********************/

static void view_align_update(struct Main *UNUSED(main), struct Scene *UNUSED(scene), struct PointerRNA *ptr)
{
	RNA_struct_idprops_unset(ptr, "rotation");
}

void ED_object_add_unit_props(wmOperatorType *ot)
{
	RNA_def_float_distance(ot->srna, "radius", 1.0f, 0.0, OBJECT_ADD_SIZE_MAXF, "Radius", "", 0.001, 100.00);
}

void ED_object_add_generic_props(wmOperatorType *ot, bool do_editmode)
{
	PropertyRNA *prop;

	/* note: this property gets hidden for add-camera operator */
	prop = RNA_def_boolean(ot->srna, "view_align", 0, "Align to View", "Align the new object to the view");
	RNA_def_property_update_runtime(prop, view_align_update);

	if (do_editmode) {
		prop = RNA_def_boolean(ot->srna, "enter_editmode", 0, "Enter Editmode",
		                       "Enter editmode when adding this object");
		RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
	}

	prop = RNA_def_float_vector_xyz(ot->srna, "location", 3, NULL, -OBJECT_ADD_SIZE_MAXF, OBJECT_ADD_SIZE_MAXF,
	                                "Location", "Location for the newly added object", -1000.0f, 1000.0f);
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	prop = RNA_def_float_rotation(ot->srna, "rotation", 3, NULL, -OBJECT_ADD_SIZE_MAXF, OBJECT_ADD_SIZE_MAXF,
	                              "Rotation", "Rotation for the newly added object",
	                              DEG2RADF(-360.0f), DEG2RADF(360.0f));
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);

	prop = RNA_def_boolean_layer_member(ot->srna, "layers", 20, NULL, "Layer", "");
	RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}

void ED_object_add_mesh_props(wmOperatorType *ot)
{
	RNA_def_boolean(ot->srna, "calc_uvs", false, "Generate UVs", "Generate a default UV map");
}

bool ED_object_add_generic_get_opts(bContext *C, wmOperator *op, const char view_align_axis,
                                    float loc[3], float rot[3],
                                    bool *enter_editmode, unsigned int *layer, bool *is_view_aligned)
{
	View3D *v3d = CTX_wm_view3d(C);
	unsigned int _layer;
	PropertyRNA *prop;

	/* Switch to Edit mode? optional prop */
	if ((prop = RNA_struct_find_property(op->ptr, "enter_editmode"))) {
		bool _enter_editmode;
		if (!enter_editmode)
			enter_editmode = &_enter_editmode;

		if (RNA_property_is_set(op->ptr, prop) && enter_editmode)
			*enter_editmode = RNA_property_boolean_get(op->ptr, prop);
		else {
			*enter_editmode = (U.flag & USER_ADD_EDITMODE) != 0;
			RNA_property_boolean_set(op->ptr, prop, *enter_editmode);
		}
	}

	/* Get layers! */
	{
		int a;
		bool layer_values[20];
		if (!layer)
			layer = &_layer;

		prop = RNA_struct_find_property(op->ptr, "layers");
		if (RNA_property_is_set(op->ptr, prop)) {
			RNA_property_boolean_get_array(op->ptr, prop, layer_values);
			*layer = 0;
			for (a = 0; a < 20; a++) {
				if (layer_values[a])
					*layer |= (1 << a);
			}
		}
		else {
			Scene *scene = CTX_data_scene(C);
			*layer = BKE_screen_view3d_layer_active_ex(v3d, scene, false);
			for (a = 0; a < 20; a++) {
				layer_values[a] = (*layer & (1 << a)) != 0;
			}
			RNA_property_boolean_set_array(op->ptr, prop, layer_values);
		}

		/* in local view we additionally add local view layers,
		 * not part of operator properties */
		if (v3d && v3d->localvd)
			*layer |= v3d->lay;
	}

	/* Location! */
	{
		float _loc[3];
		if (!loc)
			loc = _loc;

		if (RNA_struct_property_is_set(op->ptr, "location")) {
			RNA_float_get_array(op->ptr, "location", loc);
		}
		else {
			ED_object_location_from_view(C, loc);
			RNA_float_set_array(op->ptr, "location", loc);
		}
	}

	/* Rotation! */
	{
		bool _is_view_aligned;
		float _rot[3];
		if (!is_view_aligned)
			is_view_aligned = &_is_view_aligned;
		if (!rot)
			rot = _rot;

		if (RNA_struct_property_is_set(op->ptr, "rotation"))
			*is_view_aligned = false;
		else if (RNA_struct_property_is_set(op->ptr, "view_align"))
			*is_view_aligned = RNA_boolean_get(op->ptr, "view_align");
		else {
			*is_view_aligned = (U.flag & USER_ADD_VIEWALIGNED) != 0;
			RNA_boolean_set(op->ptr, "view_align", *is_view_aligned);
		}

		if (*is_view_aligned) {
			ED_object_rotation_from_view(C, rot, view_align_axis);
			RNA_float_set_array(op->ptr, "rotation", rot);
		}
		else
			RNA_float_get_array(op->ptr, "rotation", rot);
	}

	if (layer && *layer == 0) {
		BKE_report(op->reports, RPT_ERROR, "Property 'layer' has no values set");
		return false;
	}

	return true;
}

/* For object add primitive operators.
 * Do not call undo push in this function (users of this function have to). */
Object *ED_object_add_type(
        bContext *C,
        int type, const char *name,
        const float loc[3], const float rot[3],
        bool enter_editmode, unsigned int layer)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob;

	/* for as long scene has editmode... */
	if (CTX_data_edit_object(C)) {
		ED_object_editmode_exit(C, EM_FREEDATA | EM_WAITCURSOR);
	}

	/* deselects all, sets scene->basact */
	ob = BKE_object_add(bmain, scene, type, name);
	BASACT->lay = ob->lay = layer;
	/* editor level activate, notifiers */
	ED_base_object_activate(C, BASACT);

	/* more editor stuff */
	ED_object_base_init_transform(C, BASACT, loc, rot);

	/* Ignore collisions by default for non-mesh objects */
	if (type != OB_MESH) {
		ob->body_type = OB_BODY_TYPE_NO_COLLISION;
	}

	if (enter_editmode)
		ED_object_editmode_enter(C, EM_IGNORE_LAYER);

	WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

	return ob;
}

/* for object add operator */
static int object_add_exec(bContext *C, wmOperator *op)
{
	Object *ob;
	bool enter_editmode;
	unsigned int layer;
	float loc[3], rot[3], radius;

	WM_operator_view3d_unit_defaults(C, op);
	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, &enter_editmode, &layer, NULL))
		return OPERATOR_CANCELLED;

	radius = RNA_float_get(op->ptr, "radius");
	ob = ED_object_add_type(C, RNA_enum_get(op->ptr, "type"), NULL, loc, rot, enter_editmode, layer);

	if (ob->type == OB_LATTICE) {
		/* lattice is a special case!
		 * we never want to scale the obdata since that is the rest-state */
		copy_v3_fl(ob->size, radius);
	}
	else {
		BKE_object_obdata_size_init(ob, radius);
	}

	return OPERATOR_FINISHED;
}

void OBJECT_OT_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Object";
	ot->description = "Add an object to the scene";
	ot->idname = "OBJECT_OT_add";

	/* api callbacks */
	ot->exec = object_add_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ED_object_add_unit_props(ot);
	RNA_def_enum(ot->srna, "type", rna_enum_object_type_items, 0, "Type", "");

	ED_object_add_generic_props(ot, true);
}

/********************* Add Camera Operator ********************/

static int object_camera_add_exec(bContext *C, wmOperator *op)
{
	View3D *v3d = CTX_wm_view3d(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob;
	Camera *cam;
	bool enter_editmode;
	unsigned int layer;
	float loc[3], rot[3];

	/* force view align for cameras */
	RNA_boolean_set(op->ptr, "view_align", true);

	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, &enter_editmode, &layer, NULL))
		return OPERATOR_CANCELLED;

	ob = ED_object_add_type(C, OB_CAMERA, NULL, loc, rot, false, layer);

	if (v3d) {
		if (v3d->camera == NULL)
			v3d->camera = ob;
		if (v3d->scenelock && scene->camera == NULL) {
			scene->camera = ob;
		}
	}

	cam = ob->data;
	cam->drawsize = v3d ? ED_view3d_grid_scale(scene, v3d, NULL) : ED_scene_grid_scale(scene, NULL);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_camera_add(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Add Camera";
	ot->description = "Add a camera object to the scene";
	ot->idname = "OBJECT_OT_camera_add";

	/* api callbacks */
	ot->exec = object_camera_add_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	ED_object_add_generic_props(ot, true);

	/* hide this for cameras, default */
	prop = RNA_struct_type_find_property(ot->srna, "view_align");
	RNA_def_property_flag(prop, PROP_HIDDEN);
}

/********************* Add Text Operator ********************/

static int object_add_text_exec(bContext *C, wmOperator *op)
{
	Object *obedit = CTX_data_edit_object(C);
	bool enter_editmode;
	unsigned int layer;
	float loc[3], rot[3];

	WM_operator_view3d_unit_defaults(C, op);
	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, &enter_editmode, &layer, NULL))
		return OPERATOR_CANCELLED;

	if (obedit && obedit->type == OB_FONT)
		return OPERATOR_CANCELLED;

	obedit = ED_object_add_type(C, OB_FONT, NULL, loc, rot, enter_editmode, layer);
	BKE_object_obdata_size_init(obedit, RNA_float_get(op->ptr, "radius"));

	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, obedit);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_text_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Text";
	ot->description = "Add a text object to the scene";
	ot->idname = "OBJECT_OT_text_add";

	/* api callbacks */
	ot->exec = object_add_text_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ED_object_add_unit_props(ot);
	ED_object_add_generic_props(ot, true);
}

/********************* Add Empty Operator ********************/

static int object_empty_add_exec(bContext *C, wmOperator *op)
{
	Object *ob;
	int type = RNA_enum_get(op->ptr, "type");
	unsigned int layer;
	float loc[3], rot[3];

	WM_operator_view3d_unit_defaults(C, op);
	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, NULL, &layer, NULL))
		return OPERATOR_CANCELLED;

	ob = ED_object_add_type(C, OB_EMPTY, NULL, loc, rot, false, layer);

	BKE_object_empty_draw_type_set(ob, type);
	BKE_object_obdata_size_init(ob, RNA_float_get(op->ptr, "radius"));

	return OPERATOR_FINISHED;
}

void OBJECT_OT_empty_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Empty";
	ot->description = "Add an empty object to the scene";
	ot->idname = "OBJECT_OT_empty_add";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = object_empty_add_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ot->prop = RNA_def_enum(ot->srna, "type", rna_enum_object_empty_drawtype_items, 0, "Type", "");

	ED_object_add_unit_props(ot);
	ED_object_add_generic_props(ot, false);
}

static int empty_drop_named_image_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);

	Base *base = NULL;
	Image *ima = NULL;
	Object *ob = NULL;

	ima = (Image *)WM_operator_drop_load_path(C, op, ID_IM);
	if (!ima) {
		return OPERATOR_CANCELLED;
	}
	/* handled below */
	id_us_min((ID *)ima);

	base = ED_view3d_give_base_under_cursor(C, event->mval);

	/* if empty under cursor, then set object */
	if (base && base->object->type == OB_EMPTY) {
		ob = base->object;
		WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
	}
	else {
		/* add new empty */
		unsigned int layer;
		float rot[3];

		if (!ED_object_add_generic_get_opts(C, op, 'Z', NULL, rot, NULL, &layer, NULL))
			return OPERATOR_CANCELLED;

		ob = ED_object_add_type(C, OB_EMPTY, NULL, NULL, rot, false, layer);

		/* add under the mouse */
		ED_object_location_from_view(C, ob->loc);
		ED_view3d_cursor3d_position(C, event->mval, ob->loc);
	}

	BKE_object_empty_draw_type_set(ob, OB_EMPTY_IMAGE);

	id_us_min(ob->data);
	ob->data = ima;
	id_us_plus(ob->data);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_drop_named_image(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Add Empty Image/Drop Image To Empty";
	ot->description = "Add an empty image type to scene with data";
	ot->idname = "OBJECT_OT_drop_named_image";

	/* api callbacks */
	ot->invoke = empty_drop_named_image_invoke;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	prop = RNA_def_string(ot->srna, "filepath", NULL, FILE_MAX, "Filepath", "Path to image file");
	RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
	RNA_def_boolean(ot->srna, "relative_path", true, "Relative Path", "Select the file relative to the blend file");
	RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
	prop = RNA_def_string(ot->srna, "name", NULL, MAX_ID_NAME - 2, "Name", "Image name to assign");
	RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
	ED_object_add_generic_props(ot, false);
}

/********************* Add Lamp Operator ********************/

static const char *get_lamp_defname(int type)
{
	switch (type) {
		case LA_LOCAL: return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Point");
		case LA_SUN: return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Sun");
		case LA_SPOT: return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Spot");
		case LA_HEMI: return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Hemi");
		case LA_AREA: return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Area");
		default:
			return CTX_DATA_(BLT_I18NCONTEXT_ID_LAMP, "Lamp");
	}
}

static int object_lamp_add_exec(bContext *C, wmOperator *op)
{
	Object *ob;
	Lamp *la;
	int type = RNA_enum_get(op->ptr, "type");
	unsigned int layer;
	float loc[3], rot[3];

	WM_operator_view3d_unit_defaults(C, op);
	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, NULL, &layer, NULL))
		return OPERATOR_CANCELLED;

	ob = ED_object_add_type(C, OB_LAMP, get_lamp_defname(type), loc, rot, false, layer);
	BKE_object_obdata_size_init(ob, RNA_float_get(op->ptr, "radius"));

	la = (Lamp *)ob->data;
	la->type = type;

	return OPERATOR_FINISHED;
}

void OBJECT_OT_lamp_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Lamp";
	ot->description = "Add a lamp object to the scene";
	ot->idname = "OBJECT_OT_lamp_add";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = object_lamp_add_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ot->prop = RNA_def_enum(ot->srna, "type", rna_enum_lamp_type_items, 0, "Type", "");
	RNA_def_property_translation_context(ot->prop, BLT_I18NCONTEXT_ID_LAMP);

	ED_object_add_unit_props(ot);
	ED_object_add_generic_props(ot, false);
}

/********************* Add Group Instance Operator ********************/

static int group_instance_add_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Group *group;
	unsigned int layer;
	float loc[3], rot[3];

	if (RNA_struct_property_is_set(op->ptr, "name")) {
		char name[MAX_ID_NAME - 2];

		RNA_string_get(op->ptr, "name", name);
		group = (Group *)BKE_libblock_find_name(bmain, ID_GR, name);

		if (0 == RNA_struct_property_is_set(op->ptr, "location")) {
			const wmEvent *event = CTX_wm_window(C)->eventstate;
			ARegion *ar = CTX_wm_region(C);
			const int mval[2] = {event->x - ar->winrct.xmin,
			                     event->y - ar->winrct.ymin};
			ED_object_location_from_view(C, loc);
			ED_view3d_cursor3d_position(C, mval, loc);
			RNA_float_set_array(op->ptr, "location", loc);
		}
	}
	else
		group = BLI_findlink(&CTX_data_main(C)->group, RNA_enum_get(op->ptr, "group"));

	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, NULL, &layer, NULL))
		return OPERATOR_CANCELLED;

	if (group) {
		Scene *scene = CTX_data_scene(C);
		Object *ob = ED_object_add_type(C, OB_EMPTY, group->id.name + 2, loc, rot, false, layer);
		ob->dup_group = group;
		id_us_plus(&group->id);

		WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);

		return OPERATOR_FINISHED;
	}

	return OPERATOR_CANCELLED;
}

/* only used as menu */
void OBJECT_OT_group_instance_add(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Add Group Instance";
	ot->description = "Add a dupligroup instance";
	ot->idname = "OBJECT_OT_group_instance_add";

	/* api callbacks */
	ot->invoke = WM_enum_search_invoke;
	ot->exec = group_instance_add_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	RNA_def_string(ot->srna, "name", "Group", MAX_ID_NAME - 2, "Name", "Group name to add");
	prop = RNA_def_enum(ot->srna, "group", DummyRNA_NULL_items, 0, "Group", "");
	RNA_def_enum_funcs(prop, RNA_group_itemf);
	RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE);
	ot->prop = prop;
	ED_object_add_generic_props(ot, false);
}

/**************************** Delete Object *************************/

static void object_delete_check_glsl_update(Object *ob)
{
	/* some objects could affect on GLSL shading, make sure GLSL settings
	 * are being tagged to be updated when object is removing from scene
	 */
	if (ob->type == OB_LAMP) {
		if (ob->gpulamp.first)
			GPU_lamp_free(ob);
	}
}

/* remove base from a specific scene */
/* note: now unlinks constraints as well */
void ED_base_object_free_and_unlink(Main *bmain, Scene *scene, Base *base)
{
	if (BKE_library_ID_is_indirectly_used(bmain, base->object) &&
	    ID_REAL_USERS(base->object) <= 1 && ID_EXTRA_USERS(base->object) == 0)
	{
		/* We cannot delete indirectly used object... */
		printf("WARNING, undeletable object '%s', should have been catched before reaching this function!",
		       base->object->id.name + 2);
		return;
	}

	BKE_scene_base_unlink(scene, base);
	object_delete_check_glsl_update(base->object);
	BKE_libblock_free_us(bmain, base->object);
	MEM_freeN(base);
}

static int object_delete_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *win;
	const bool use_global = RNA_boolean_get(op->ptr, "use_global");
	bool changed = false;

	if (CTX_data_edit_object(C))
		return OPERATOR_CANCELLED;

	CTX_DATA_BEGIN (C, Base *, base, selected_bases)
	{
		const bool is_indirectly_used = BKE_library_ID_is_indirectly_used(bmain, base->object);
		if (base->object->id.tag & LIB_TAG_INDIRECT) {
			/* Can this case ever happen? */
			BKE_reportf(op->reports, RPT_WARNING, "Cannot delete indirectly linked object '%s'", base->object->id.name + 2);
			continue;
		}
		else if (is_indirectly_used && ID_REAL_USERS(base->object) <= 1 && ID_EXTRA_USERS(base->object) == 0) {
			BKE_reportf(op->reports, RPT_WARNING,
			        "Cannot delete object '%s' from scene '%s', indirectly used objects need at least one user",
			        base->object->id.name + 2, scene->id.name + 2);
			continue;
		}

		/* This is sort of a quick hack to address T51243 - Proper thing to do here would be to nuke most of all this
		 * custom scene/object/base handling, and use generic lib remap/query for that.
		 * But this is for later (aka 2.8, once layers & co are settled and working).
		 */
		if (use_global && base->object->id.lib == NULL) {
			/* We want to nuke the object, let's nuke it the easy way (not for linked data though)... */
			BKE_libblock_delete(bmain, &base->object->id);
			changed = true;
			continue;
		}

		/* deselect object -- it could be used in other scenes */
		base->object->flag &= ~SELECT;

		/* remove from current scene only */
		ED_base_object_free_and_unlink(bmain, scene, base);
		changed = true;

		if (use_global) {
			Scene *scene_iter;
			Base *base_other;

			for (scene_iter = bmain->scene.first; scene_iter; scene_iter = scene_iter->id.next) {
				if (scene_iter != scene && !ID_IS_LINKED(scene_iter)) {
					base_other = BKE_scene_base_find(scene_iter, base->object);
					if (base_other) {
						if (is_indirectly_used && ID_REAL_USERS(base->object) <= 1 && ID_EXTRA_USERS(base->object) == 0) {
							BKE_reportf(op->reports, RPT_WARNING,
							            "Cannot delete object '%s' from scene '%s', indirectly used objects need at least one user",
							            base->object->id.name + 2, scene_iter->id.name + 2);
							break;
						}
						ED_base_object_free_and_unlink(bmain, scene_iter, base_other);
					}
				}
			}
		}
		/* end global */
	}
	CTX_DATA_END;

	if (!changed)
		return OPERATOR_CANCELLED;

	/* delete has to handle all open scenes */
	BKE_main_id_tag_listbase(&bmain->scene, LIB_TAG_DOIT, true);
	for (win = wm->windows.first; win; win = win->next) {
		scene = win->screen->scene;

		if (scene->id.tag & LIB_TAG_DOIT) {
			scene->id.tag &= ~LIB_TAG_DOIT;

			WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
			WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);
		}
	}

	return OPERATOR_FINISHED;
}

void OBJECT_OT_delete(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Delete";
	ot->description = "Delete selected objects";
	ot->idname = "OBJECT_OT_delete";

	/* api callbacks */
	ot->invoke = WM_operator_confirm;
	ot->exec = object_delete_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "use_global", 0, "Delete Globally", "Remove object from all scenes");
}

/**************************** Copy Utilities ******************************/

/* after copying objects, copied data should get new pointers */
static void copy_object_set_idnew(bContext *C)
{
	Main *bmain = CTX_data_main(C);

	CTX_DATA_BEGIN (C, Object *, ob, selected_editable_objects)
	{
		BKE_libblock_relink_to_newid(&ob->id);
	}
	CTX_DATA_END;

	BKE_main_id_clear_newpoins(bmain);
}

/**************************** Convert **************************/

static const EnumPropertyItem convert_target_items[] = {
	{OB_CURVE, "CURVE", ICON_OUTLINER_OB_CURVE, "Curve from Mesh/Text", ""},
	{OB_MESH, "MESH", ICON_OUTLINER_OB_MESH, "Mesh from Curve/Meta/Surf/Text", ""},
	{0, NULL, 0, NULL, NULL}
};

static void convert_ensure_curve_cache(Main *bmain, Scene *scene, Object *ob)
{
	if (ob->curve_cache == NULL) {
		/* Force creation. This is normally not needed but on operator
		 * redo we might end up with an object which isn't evaluated yet.
		 */
		if (ELEM(ob->type, OB_SURF, OB_CURVE, OB_FONT)) {
			BKE_displist_make_curveTypes(scene, ob, false);
		}
	}
}

static void curvetomesh(Main *bmain, Scene *scene, Object *ob)
{
	convert_ensure_curve_cache(bmain, scene, ob);
	BKE_mesh_from_nurbs(bmain, ob); /* also does users */

	if (ob->type == OB_MESH) {
		BKE_object_free_modifiers(ob, 0);

		/* Game engine defaults for mesh objects */
		ob->body_type = OB_BODY_TYPE_STATIC;
	}
}

static bool convert_poll(bContext *C)
{
	Object *obact = CTX_data_active_object(C);
	Scene *scene = CTX_data_scene(C);

	return (!ID_IS_LINKED(scene) && obact && scene->obedit != obact &&
	        (obact->flag & SELECT) && !ID_IS_LINKED(obact));
}

/* Helper for convert_exec */
static Base *duplibase_for_convert(Main *bmain, Scene *scene, Base *base, Object *ob)
{
	Object *obn;
	Base *basen;

	if (ob == NULL) {
		ob = base->object;
	}

	obn = BKE_object_copy(bmain, ob);

	basen = MEM_mallocN(sizeof(Base), "duplibase");
	*basen = *base;
	BLI_addhead(&scene->base, basen);   /* addhead: otherwise eternal loop */
	basen->object = obn;
	basen->flag |= SELECT;
	obn->flag |= SELECT;
	base->flag &= ~SELECT;
	ob->flag &= ~SELECT;

	return basen;
}

static int convert_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Base *basen = NULL, *basact = NULL;
	Object *ob, *ob1, *obact = CTX_data_active_object(C);
	DerivedMesh *dm;
	Curve *cu;
	Nurb *nu;
	Mesh *me;
	const short target = RNA_enum_get(op->ptr, "target");
	bool keep_original = RNA_boolean_get(op->ptr, "keep_original");

	/* don't forget multiple users! */

	{
		Base *base;

		for (base = scene->base.first; base; base = base->next) {
			ob = base->object;
			ob->flag &= ~OB_DONE;

			/* flag data thats not been edited (only needed for !keep_original) */
			if (ob->data) {
				((ID *)ob->data)->tag |= LIB_TAG_DOIT;
			}

		}
	}

	ListBase selected_editable_bases = CTX_data_collection_get(C, "selected_editable_bases");

	/* Ensure we get all meshes calculated with a sufficient data-mask,
	 * needed since re-evaluating single modifiers causes bugs if they depend
	 * on other objects data masks too, see: T50950. */
	{
		for (CollectionPointerLink *link = selected_editable_bases.first; link; link = link->next) {
			Base *base = link->ptr.data;
			ob = base->object;

			/* The way object type conversion works currently (enforcing conversion of *all* objects using converted
			 * object-data, even some un-selected/hidden/another scene ones, sounds totally bad to me.
			 * However, changing this is more design than bug-fix, not to mention convoluted code below,
			 * so that will be for later.
			 * But at the very least, do not do that with linked IDs! */
			if ((ID_IS_LINKED(ob) || (ob->data && ID_IS_LINKED(ob->data))) && !keep_original) {
				keep_original = true;
				BKE_reportf(op->reports, RPT_INFO,
				            "Converting some linked object/object data, enforcing 'Keep Original' option to True");
			}

		}

		uint64_t customdata_mask_prev = scene->customdata_mask;
		scene->customdata_mask |= CD_MASK_MESH;
		BKE_scene_update_tagged(bmain, scene);
		scene->customdata_mask = customdata_mask_prev;
	}

	for (CollectionPointerLink *link = selected_editable_bases.first; link; link = link->next) {
		Object *newob = NULL;
		Base *base = link->ptr.data;
		ob = base->object;

		if (ob->flag & OB_DONE || !IS_TAGGED(ob->data)) {
			if (ob->type != target) {
				base->flag &= ~SELECT;
				ob->flag &= ~SELECT;
			}

			/* obdata already modified */
			if (!IS_TAGGED(ob->data)) {
				/* When 2 objects with linked data are selected, converting both
				 * would keep modifiers on all but the converted object [#26003] */
				if (ob->type == OB_MESH) {
					BKE_object_free_modifiers(ob, 0);  /* after derivedmesh calls! */
				}
			}
		}
		else if (ob->type == OB_MESH && target == OB_CURVE) {
			ob->flag |= OB_DONE;

			if (keep_original) {
				basen = duplibase_for_convert(bmain, scene, base, NULL);
				newob = basen->object;

				/* decrement original mesh's usage count  */
				me = newob->data;
				id_us_min(&me->id);

				/* make a new copy of the mesh */
				newob->data = BKE_mesh_copy(bmain, me);
			}
			else {
				newob = ob;
			}

			BKE_mesh_to_curve(bmain, scene, newob);

			if (newob->type == OB_CURVE) {
				BKE_object_free_modifiers(newob, 0);   /* after derivedmesh calls! */
				ED_rigidbody_object_remove(bmain, scene, newob);
			}
		}
		else if (ob->type == OB_MESH) {
			ob->flag |= OB_DONE;

			if (keep_original) {
				basen = duplibase_for_convert(bmain, scene, base, NULL);
				newob = basen->object;

				/* decrement original mesh's usage count  */
				me = newob->data;
				id_us_min(&me->id);

				/* make a new copy of the mesh */
				newob->data = BKE_mesh_copy(bmain, me);
			}
			else {
				newob = ob;
			}

			/* make new mesh data from the original copy */
			/* note: get the mesh from the original, not from the copy in some
			 * cases this doesnt give correct results (when MDEF is used for eg)
			 */
			dm = mesh_get_derived_final(scene, newob, CD_MASK_MESH);

			DM_to_mesh(dm, newob->data, newob, CD_MASK_MESH, true);

			/* re-tessellation is called by DM_to_mesh */

			BKE_object_free_modifiers(newob, 0);   /* after derivedmesh calls! */
		}
		else if (ob->type == OB_FONT) {
			ob->flag |= OB_DONE;

			if (keep_original) {
				basen = duplibase_for_convert(bmain, scene, base, NULL);
				newob = basen->object;

				/* decrement original curve's usage count  */
				id_us_min(&((Curve *)newob->data)->id);

				/* make a new copy of the curve */
				newob->data = BKE_curve_copy(bmain, ob->data);
			}
			else {
				newob = ob;
			}

			cu = newob->data;

			/* TODO(sergey): Ideally DAG will create nurbs list for a curve data
			 *               datablock, but for until we've got granular update
			 *               lets take care by selves.
			 */
			BKE_vfont_to_curve(newob, FO_EDIT);

			newob->type = OB_CURVE;
			cu->type = OB_CURVE;

			if (cu->vfont) {
				id_us_min(&cu->vfont->id);
				cu->vfont = NULL;
			}
			if (cu->vfontb) {
				id_us_min(&cu->vfontb->id);
				cu->vfontb = NULL;
			}
			if (cu->vfonti) {
				id_us_min(&cu->vfonti->id);
				cu->vfonti = NULL;
			}
			if (cu->vfontbi) {
				id_us_min(&cu->vfontbi->id);
				cu->vfontbi = NULL;
			}

			if (!keep_original) {
				/* other users */
				if (cu->id.us > 1) {
					for (ob1 = bmain->object.first; ob1; ob1 = ob1->id.next) {
						if (ob1->data == ob->data) {
							ob1->type = OB_CURVE;
						}
					}
				}
			}

			for (nu = cu->nurb.first; nu; nu = nu->next)
				nu->charidx = 0;

			cu->flag &= ~CU_3D;
			BKE_curve_curve_dimension_update(cu);

			if (target == OB_MESH) {
				curvetomesh(bmain, scene, newob);

				/* meshes doesn't use displist */
				BKE_object_free_curve_cache(newob);
			}
		}
		else if (ELEM(ob->type, OB_CURVE, OB_SURF)) {
			ob->flag |= OB_DONE;

			if (target == OB_MESH) {
				if (keep_original) {
					basen = duplibase_for_convert(bmain, scene, base, NULL);
					newob = basen->object;

					/* decrement original curve's usage count  */
					id_us_min(&((Curve *)newob->data)->id);

					/* make a new copy of the curve */
					newob->data = BKE_curve_copy(bmain, ob->data);
				}
				else {
					newob = ob;
				}

				curvetomesh(bmain, scene, newob);

				/* meshes doesn't use displist */
				BKE_object_free_curve_cache(newob);
			}
		}

		/* Ensure new object has consistent material data with its new obdata. */
		if (newob) {
			test_object_materials(bmain, newob, newob->data);
		}

		/* tag obdata if it was been changed */

		/* If the original object is active then make this object active */
		if (basen) {
			if (ob == obact) {
				/* store new active base to update BASACT */
				basact = basen;
			}

			basen = NULL;
		}

		if (!keep_original && (ob->flag & OB_DONE)) {
			((ID *)ob->data)->tag &= ~LIB_TAG_DOIT; /* flag not to convert this datablock again */
		}
	}
	BLI_freelistN(&selected_editable_bases);


	if (basact) {
		/* active base was changed */
		ED_base_object_activate(C, basact);
		BASACT = basact;
	}
	else if (BASACT->object->flag & OB_DONE) {
		WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, BASACT->object);
		WM_event_add_notifier(C, NC_OBJECT | ND_DATA, BASACT->object);
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, scene);
	WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);

	return OPERATOR_FINISHED;
}


void OBJECT_OT_convert(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Convert to";
	ot->description = "Convert selected objects to another type";
	ot->idname = "OBJECT_OT_convert";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = convert_exec;
	ot->poll = convert_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ot->prop = RNA_def_enum(ot->srna, "target", convert_target_items, OB_MESH, "Target", "Type of object to convert to");
	RNA_def_boolean(ot->srna, "keep_original", 0, "Keep Original", "Keep original objects instead of replacing them");
}

/**************************** Duplicate ************************/

/*
 * dupflag: a flag made from constants declared in DNA_userdef_types.h
 * The flag tells adduplicate() whether to copy data linked to the object, or to reference the existing data.
 * U.dupflag for default operations or you can construct a flag as python does
 * if the dupflag is 0 then no data will be copied (linked duplicate) */

/* used below, assumes id.new is correct */
/* leaves selection of base/object unaltered */
/* Does set ID->newid pointers. */
static Base *object_add_duplicate_internal(Main *bmain, Scene *scene, Base *base, int dupflag)
{
#define ID_NEW_REMAP_US(a)	if (      (a)->id.newid) { (a) = (void *)(a)->id.newid;       (a)->id.us++; }
#define ID_NEW_REMAP_US2(a)	if (((ID *)a)->newid)    { (a) = ((ID  *)a)->newid;     ((ID *)a)->us++;    }

	Base *basen = NULL;
	Material ***matarar;
	Object *ob, *obn;
	ID *id;
	int a, didit;

	ob = base->object;
	{
		obn = ID_NEW_SET(ob, BKE_object_copy(bmain, ob));

		basen = MEM_mallocN(sizeof(Base), "duplibase");
		*basen = *base;
		BLI_addhead(&scene->base, basen);   /* addhead: prevent eternal loop */
		basen->object = obn;

		/* 1) duplis should end up in same group as the original
		 * 2) Rigid Body sim participants MUST always be part of a group...
		 */
		// XXX: is 2) really a good measure here?
		if ((basen->flag & OB_FROMGROUP) || ob->rigidbody_object || ob->rigidbody_constraint) {
			Group *group;
			for (group = bmain->group.first; group; group = group->id.next) {
				if (BKE_group_object_exists(group, ob))
					BKE_group_object_add(group, obn, scene, basen);
			}
		}

		/* duplicates using userflags */

		if (dupflag & USER_DUP_MAT) {
			for (a = 0; a < obn->totcol; a++) {
				id = (ID *)obn->mat[a];
				if (id) {
					ID_NEW_REMAP_US(obn->mat[a])
					else {
						obn->mat[a] = ID_NEW_SET(obn->mat[a], BKE_material_copy(bmain, obn->mat[a]));
					}
					id_us_min(id);

				}
			}
		}

		id = obn->data;
		didit = 0;

		switch (obn->type) {
			case OB_MESH:
				if (dupflag & USER_DUP_MESH) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_mesh_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_CURVE:
				if (dupflag & USER_DUP_CURVE) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_curve_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_SURF:
				if (dupflag & USER_DUP_SURF) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_curve_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_FONT:
				if (dupflag & USER_DUP_FONT) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_curve_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_LAMP:
				if (dupflag & USER_DUP_LAMP) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_lamp_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_LATTICE:
				if (dupflag != 0) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_lattice_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
			case OB_CAMERA:
				if (dupflag != 0) {
					ID_NEW_REMAP_US2(obn->data)
					else {
						obn->data = ID_NEW_SET(obn->data, BKE_camera_copy(bmain, obn->data));
						didit = 1;
					}
					id_us_min(id);
				}
				break;
		}

		/* check if obdata is copied */
		if (didit) {
			if (dupflag & USER_DUP_MAT) {
				matarar = give_matarar(obn);
				if (matarar) {
					for (a = 0; a < obn->totcol; a++) {
						id = (ID *)(*matarar)[a];
						if (id) {
							ID_NEW_REMAP_US((*matarar)[a])
							else {
								(*matarar)[a] = ID_NEW_SET((*matarar)[a], BKE_material_copy(bmain, (*matarar)[a]));
							}
							id_us_min(id);
						}
					}
				}
			}
		}
	}
	return basen;

#undef ID_NEW_REMAP_US
#undef ID_NEW_REMAP_US2
}

/* single object duplicate, if dupflag==0, fully linked, else it uses the flags given */
/* leaves selection of base/object unaltered.
 * note: don't call this within a loop since clear_* funcs loop over the entire database.
 * note: caller must do DAG_relations_tag_update(bmain);
 *       this is not done automatic since we may duplicate many objects in a batch */
Base *ED_object_add_duplicate(Main *bmain, Scene *scene, Base *base, int dupflag)
{
	Base *basen;
	Object *ob;

	basen = object_add_duplicate_internal(bmain, scene, base, dupflag);
	if (basen == NULL) {
		return NULL;
	}

	ob = basen->object;

	/* link own references to the newly duplicated data [#26816] */
	BKE_libblock_relink_to_newid(&ob->id);

	BKE_main_id_clear_newpoins(bmain);

	return basen;
}

/* contextual operator dupli */
static int duplicate_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	const bool linked = RNA_boolean_get(op->ptr, "linked");
	int dupflag = (linked) ? 0 : U.dupflag;

	CTX_DATA_BEGIN (C, Base *, base, selected_bases)
	{
		Base *basen = object_add_duplicate_internal(bmain, scene, base, dupflag);

		/* note that this is safe to do with this context iterator,
		 * the list is made in advance */
		ED_base_object_select(base, BA_DESELECT);

		if (basen == NULL) {
			continue;
		}

		/* new object becomes active */
		if (BASACT == base)
			ED_base_object_activate(C, basen);

	}
	CTX_DATA_END;

	copy_object_set_idnew(C);

	BKE_main_id_clear_newpoins(bmain);

	WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_duplicate(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Duplicate Objects";
	ot->description = "Duplicate selected objects";
	ot->idname = "OBJECT_OT_duplicate";

	/* api callbacks */
	ot->exec = duplicate_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* to give to transform */
	RNA_def_boolean(ot->srna, "linked", 0, "Linked", "Duplicate object but not object data, linking to the original data");
	prop = RNA_def_enum(ot->srna, "mode", rna_enum_transform_mode_types, TFM_TRANSLATION, "Mode", "");
	RNA_def_property_flag(prop, PROP_HIDDEN);
}

/* **************** add named object, for dragdrop ************* */

static int add_named_exec(bContext *C, wmOperator *op)
{
	wmWindow *win = CTX_wm_window(C);
	const wmEvent *event = win ? win->eventstate : NULL;
	Main *bmain = CTX_data_main(C);
	View3D *v3d = CTX_wm_view3d(C);  /* may be NULL */
	Scene *scene = CTX_data_scene(C);
	Base *basen, *base;
	Object *ob;
	const bool linked = RNA_boolean_get(op->ptr, "linked");
	int dupflag = (linked) ? 0 : U.dupflag;
	char name[MAX_ID_NAME - 2];

	/* find object, create fake base */
	RNA_string_get(op->ptr, "name", name);
	ob = (Object *)BKE_libblock_find_name(bmain, ID_OB, name);

	if (ob == NULL) {
		BKE_report(op->reports, RPT_ERROR, "Object not found");
		return OPERATOR_CANCELLED;
	}

	base = MEM_callocN(sizeof(Base), "duplibase");
	base->object = ob;
	base->flag = ob->flag;

	/* prepare dupli */
	basen = object_add_duplicate_internal(bmain, scene, base, dupflag);

	if (basen == NULL) {
		MEM_freeN(base);
		BKE_report(op->reports, RPT_ERROR, "Object could not be duplicated");
		return OPERATOR_CANCELLED;
	}

	basen->lay = basen->object->lay = BKE_screen_view3d_layer_active(v3d, scene);
	basen->object->restrictflag &= ~OB_RESTRICT_VIEW;

	if (event) {
		ARegion *ar = CTX_wm_region(C);
		const int mval[2] = {event->x - ar->winrct.xmin,
		                     event->y - ar->winrct.ymin};
		ED_object_location_from_view(C, basen->object->loc);
		ED_view3d_cursor3d_position(C, mval, basen->object->loc);
	}

	ED_base_object_select(basen, BA_SELECT);
	ED_base_object_activate(C, basen);

	copy_object_set_idnew(C);

	BKE_main_id_clear_newpoins(bmain);

	MEM_freeN(base);

	WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
	WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_add_named(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Named Object";
	ot->description = "Add named object";
	ot->idname = "OBJECT_OT_add_named";

	/* api callbacks */
	ot->exec = add_named_exec;
	ot->poll = ED_operator_objectmode;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "linked", 0, "Linked", "Duplicate object but not object data, linking to the original data");
	RNA_def_string(ot->srna, "name", NULL, MAX_ID_NAME - 2, "Name", "Object name to add");
}

/**************************** Join *************************/

static bool join_poll(bContext *C)
{
	Object *ob = CTX_data_active_object(C);

	if (!ob || ID_IS_LINKED(ob)) return 0;

	if (ELEM(ob->type, OB_MESH, OB_CURVE, OB_SURF))
		return ED_operator_screenactive(C);
	else
		return 0;
}

static int join_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);

	if (scene->obedit) {
		BKE_report(op->reports, RPT_ERROR, "This data does not support joining in edit mode");
		return OPERATOR_CANCELLED;
	}
	else if (BKE_object_obdata_is_libdata(ob)) {
		BKE_report(op->reports, RPT_ERROR, "Cannot edit external libdata");
		return OPERATOR_CANCELLED;
	}

	if (ob->type == OB_MESH)
		return join_mesh_exec(C, op);
	else if (ELEM(ob->type, OB_CURVE, OB_SURF))
		return join_curve_exec(C, op);

	return OPERATOR_CANCELLED;
}

void OBJECT_OT_join(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Join";
	ot->description = "Join selected objects into active object";
	ot->idname = "OBJECT_OT_join";

	/* api callbacks */
	ot->exec = join_exec;
	ot->poll = join_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
