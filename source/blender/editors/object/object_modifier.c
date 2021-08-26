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

/** \file blender/editors/object/object_modifier.c
 *  \ingroup edobj
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_bitmap.h"
#include "BLI_math.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_path_util.h"
#include "BLI_utildefines.h"

#include "BKE_curve.h"
#include "BKE_context.h"
#include "BKE_displist.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_modifier.h"
#include "BKE_report.h"
#include "BKE_object.h"
#include "BKE_object_deform.h"
#include "BKE_editmesh.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_object.h"
#include "ED_screen.h"
#include "ED_mesh.h"

#include "WM_api.h"
#include "WM_types.h"

#include "object_intern.h"

/******************************** API ****************************/

ModifierData *ED_object_modifier_add(ReportList *reports, Main *bmain, Scene *scene, Object *ob, const char *name, int type)
{
	ModifierData *md = NULL, *new_md = NULL;
	const ModifierTypeInfo *mti = modifierType_getInfo(type);

	/* Check compatibility of modifier [T25291, T50373]. */
	if (!BKE_object_support_modifier_type_check(ob, type)) {
		BKE_reportf(reports, RPT_WARNING, "Modifiers cannot be added to object '%s'", ob->id.name + 2);
		return NULL;
	}

	if (mti->flags & eModifierTypeFlag_Single) {
		if (modifiers_findByType(ob, type)) {
			BKE_report(reports, RPT_WARNING, "Only one modifier of this type is allowed");
			return NULL;
		}
	}

	{
		/* get new modifier data to add */
		new_md = modifier_new(type);

		if (mti->flags & eModifierTypeFlag_RequiresOriginalData) {
			md = ob->modifiers.first;

			while (md && modifierType_getInfo(md->type)->type == eModifierTypeType_OnlyDeform)
				md = md->next;

			BLI_insertlinkbefore(&ob->modifiers, md, new_md);
		}
		else
			BLI_addtail(&ob->modifiers, new_md);

		if (name) {
			BLI_strncpy_utf8(new_md->name, name, sizeof(new_md->name));
		}

		/* make sure modifier data has unique name */

		modifier_unique_name(&ob->modifiers, new_md);

		/* special cases */
		if (type == eModifierType_Surface) {
			/* pass */
		}
	}

	return new_md;
}

/* If the object data of 'orig_ob' has other users, run 'callback' on
 * each of them.
 *
 * If include_orig is true, the callback will run on 'orig_ob' too.
 *
 * If the callback ever returns true, iteration will stop and the
 * function value will be true. Otherwise the function returns false.
 */
bool ED_object_iter_other(Main *bmain, Object *orig_ob, const bool include_orig,
                          bool (*callback)(Object *ob, void *callback_data),
                          void *callback_data)
{
	ID *ob_data_id = orig_ob->data;
	int users = ob_data_id->us;

	if (ob_data_id->flag & LIB_FAKEUSER)
		users--;

	/* First check that the object's data has multiple users */
	if (users > 1) {
		Object *ob;
		int totfound = include_orig ? 0 : 1;

		for (ob = bmain->object.first; ob && totfound < users;
		     ob = ob->id.next)
		{
			if (((ob != orig_ob) || include_orig) &&
			    (ob->data == orig_ob->data))
			{
				if (callback(ob, callback_data))
					return true;

				totfound++;
			}
		}
	}
	else if (include_orig) {
		return callback(orig_ob, callback_data);
	}

	return false;
}

static bool object_modifier_remove(Main *bmain, Object *ob, ModifierData *md,
                                   bool *r_sort_depsgraph)
{
	/* It seems on rapid delete it is possible to
	 * get called twice on same modifier, so make
	 * sure it is in list. */
	if (BLI_findindex(&ob->modifiers, md) == -1) {
		return 0;
	}

	/* special cases */
	if (md->type == eModifierType_Collision) {
		*r_sort_depsgraph = true;
	}
	else if (md->type == eModifierType_Surface) {
		*r_sort_depsgraph = true;
	}


	BLI_remlink(&ob->modifiers, md);
	modifier_free(md);
	BKE_object_free_derived_caches(ob);

	return 1;
}

bool ED_object_modifier_remove(ReportList *reports, Main *bmain, Object *ob, ModifierData *md)
{
	bool sort_depsgraph = false;
	bool ok;

	ok = object_modifier_remove(bmain, ob, md, &sort_depsgraph);

	if (!ok) {
		BKE_reportf(reports, RPT_ERROR, "Modifier '%s' not in object '%s'", md->name, ob->id.name);
		return 0;
	}

	return 1;
}

void ED_object_modifier_clear(Main *bmain, Object *ob)
{
	ModifierData *md = ob->modifiers.first;
	bool sort_depsgraph = false;

	if (!md)
		return;

	while (md) {
		ModifierData *next_md;

		next_md = md->next;

		object_modifier_remove(bmain, ob, md, &sort_depsgraph);

		md = next_md;
	}

}

int ED_object_modifier_move_up(ReportList *reports, Object *ob, ModifierData *md)
{
	if (md->prev) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

		if (mti->type != eModifierTypeType_OnlyDeform) {
			const ModifierTypeInfo *nmti = modifierType_getInfo(md->prev->type);

			if (nmti->flags & eModifierTypeFlag_RequiresOriginalData) {
				BKE_report(reports, RPT_WARNING, "Cannot move above a modifier requiring original data");
				return 0;
			}
		}

		BLI_remlink(&ob->modifiers, md);
		BLI_insertlinkbefore(&ob->modifiers, md->prev, md);
	}

	return 1;
}

int ED_object_modifier_move_down(ReportList *reports, Object *ob, ModifierData *md)
{
	if (md->next) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

		if (mti->flags & eModifierTypeFlag_RequiresOriginalData) {
			const ModifierTypeInfo *nmti = modifierType_getInfo(md->next->type);

			if (nmti->type != eModifierTypeType_OnlyDeform) {
				BKE_report(reports, RPT_WARNING, "Cannot move beyond a non-deforming modifier");
				return 0;
			}
		}

		BLI_remlink(&ob->modifiers, md);
		BLI_insertlinkafter(&ob->modifiers, md->next, md);
	}

	return 1;
}

static int modifier_apply_shape(Main *bmain, ReportList *reports, Scene *scene, Object *ob, ModifierData *md)
{
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

	md->scene = scene;

	if (mti->isDisabled && mti->isDisabled(md, 0)) {
		BKE_report(reports, RPT_ERROR, "Modifier is disabled, skipping apply");
		return 0;
	}

	/*
	 * It should be ridiculously easy to extract the original verts that we want
	 * and form the shape data.  We can probably use the CD KEYINDEX layer (or
	 * whatever I ended up calling it, too tired to check now), though this would
	 * by necessity have to make some potentially ugly assumptions about the order
	 * of the mesh data :-/  you can probably assume in 99% of cases that the first
	 * element of a given index is the original, and any subsequent duplicates are
	 * copies/interpolates, but that's an assumption that would need to be tested
	 * and then predominantly stated in comments in a half dozen headers.
	 */

	if (ob->type == OB_MESH) {
		DerivedMesh *dm;

		if (!modifier_isSameTopology(md) || mti->type == eModifierTypeType_NonGeometrical) {
			BKE_report(reports, RPT_ERROR, "Only deforming modifiers can be applied to shapes");
			return 0;
		}

		dm = mesh_create_derived_for_modifier(scene, ob, md, 0);
		if (!dm) {
			BKE_report(reports, RPT_ERROR, "Modifier is disabled or returned error, skipping apply");
			return 0;
		}

		dm->release(dm);
	}
	else {
		BKE_report(reports, RPT_ERROR, "Cannot apply modifier for this object type");
		return 0;
	}
	return 1;
}

static int modifier_apply_obdata(ReportList *reports, Scene *scene, Object *ob, ModifierData *md)
{
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

	md->scene = scene;

	if (mti->isDisabled && mti->isDisabled(md, 0)) {
		BKE_report(reports, RPT_ERROR, "Modifier is disabled, skipping apply");
		return 0;
	}

	if (ob->type == OB_MESH) {
		DerivedMesh *dm;
		Mesh *me = ob->data;
		{
			dm = mesh_create_derived_for_modifier(scene, ob, md, 1);
			if (!dm) {
				BKE_report(reports, RPT_ERROR, "Modifier returned error, skipping apply");
				return 0;
			}

			DM_to_mesh(dm, me, ob, CD_MASK_MESH, true);

		}
	}
	else if (ELEM(ob->type, OB_CURVE, OB_SURF)) {
		Curve *cu;
		int numVerts;
		float (*vertexCos)[3];

		if (ELEM(mti->type, eModifierTypeType_Constructive, eModifierTypeType_Nonconstructive)) {
			BKE_report(reports, RPT_ERROR, "Cannot apply constructive modifiers on curve");
			return 0;
		}

		cu = ob->data;
		BKE_report(reports, RPT_INFO, "Applied modifier only changed CV points, not tessellated/bevel vertices");

		vertexCos = BKE_curve_nurbs_vertexCos_get(&cu->nurb, &numVerts);
		mti->deformVerts(md, ob, NULL, vertexCos, numVerts, 0);
		BK_curve_nurbs_vertexCos_apply(&cu->nurb, vertexCos);

		MEM_freeN(vertexCos);

	}
	else {
		BKE_report(reports, RPT_ERROR, "Cannot apply modifier for this object type");
		return 0;
	}

	return 1;
}

int ED_object_modifier_apply(Main *bmain, ReportList *reports, Scene *scene, Object *ob, ModifierData *md, int mode)
{
	int prev_mode;

	if (scene->obedit) {
		BKE_report(reports, RPT_ERROR, "Modifiers cannot be applied in edit mode");
		return 0;
	}
	else if (((ID *) ob->data)->us > 1) {
		BKE_report(reports, RPT_ERROR, "Modifiers cannot be applied to multi-user data");
		return 0;
	}

	if (md != ob->modifiers.first)
		BKE_report(reports, RPT_INFO, "Applied modifier was not first, result may not be as expected");

	/* allow apply of a not-realtime modifier, by first re-enabling realtime. */
	prev_mode = md->mode;
	md->mode |= eModifierMode_Realtime;

	if (mode == MODIFIER_APPLY_SHAPE) {
		if (!modifier_apply_shape(bmain, reports, scene, ob, md)) {
			md->mode = prev_mode;
			return 0;
		}
	}
	else {
		if (!modifier_apply_obdata(reports, scene, ob, md)) {
			md->mode = prev_mode;
			return 0;
		}
	}

	BLI_remlink(&ob->modifiers, md);
	modifier_free(md);

	BKE_object_free_derived_caches(ob);

	return 1;
}

int ED_object_modifier_copy(ReportList *UNUSED(reports), Object *ob, ModifierData *md)
{
	ModifierData *nmd;

	nmd = modifier_new(md->type);
	modifier_copyData(md, nmd);
	BLI_insertlinkafter(&ob->modifiers, md, nmd);
	modifier_unique_name(&ob->modifiers, nmd);

	return 1;
}

/************************ add modifier operator *********************/

static int modifier_add_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob = ED_object_active_context(C);
	int type = RNA_enum_get(op->ptr, "type");

	if (!ED_object_modifier_add(op->reports, bmain, scene, ob, NULL, type))
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static const EnumPropertyItem *modifier_add_itemf(
        bContext *C, PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop), bool *r_free)
{
	Object *ob = ED_object_active_context(C);
	EnumPropertyItem *item = NULL;
	const EnumPropertyItem *md_item, *group_item = NULL;
	const ModifierTypeInfo *mti;
	int totitem = 0, a;

	if (!ob)
		return rna_enum_object_modifier_type_items;

	for (a = 0; rna_enum_object_modifier_type_items[a].identifier; a++) {
		md_item = &rna_enum_object_modifier_type_items[a];

		if (md_item->identifier[0]) {
			mti = modifierType_getInfo(md_item->value);

			if (mti->flags & eModifierTypeFlag_NoUserAdd)
				continue;

			if (!BKE_object_support_modifier_type_check(ob, md_item->value))
				continue;
		}
		else {
			group_item = md_item;
			md_item = NULL;

			continue;
		}

		if (group_item) {
			RNA_enum_item_add(&item, &totitem, group_item);
			group_item = NULL;
		}

		RNA_enum_item_add(&item, &totitem, md_item);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

void OBJECT_OT_modifier_add(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Add Modifier";
	ot->description = "Add a procedural operation/effect to the active object";
	ot->idname = "OBJECT_OT_modifier_add";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = modifier_add_exec;
	ot->poll = ED_operator_object_active_editable;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	prop = RNA_def_enum(ot->srna, "type", rna_enum_object_modifier_type_items, eModifierType_Subsurf, "Type", "");
	RNA_def_enum_funcs(prop, modifier_add_itemf);
	ot->prop = prop;
}

/************************ generic functions for operators using mod names and data context *********************/

bool edit_modifier_poll_generic(bContext *C, StructRNA *rna_type, int obtype_flag)
{
	PointerRNA ptr = CTX_data_pointer_get_type(C, "modifier", rna_type);
	Object *ob = (ptr.id.data) ? ptr.id.data : ED_object_active_context(C);

	if (!ob || ID_IS_LINKED(ob)) return 0;
	if (obtype_flag && ((1 << ob->type) & obtype_flag) == 0) return 0;
	if (ptr.id.data && ID_IS_LINKED(ptr.id.data)) return 0;

	return 1;
}

bool edit_modifier_poll(bContext *C)
{
	return edit_modifier_poll_generic(C, &RNA_Modifier, 0);
}

void edit_modifier_properties(wmOperatorType *ot)
{
	RNA_def_string(ot->srna, "modifier", NULL, MAX_NAME, "Modifier", "Name of the modifier to edit");
}

int edit_modifier_invoke_properties(bContext *C, wmOperator *op)
{
	ModifierData *md;

	if (RNA_struct_property_is_set(op->ptr, "modifier")) {
		return true;
	}
	else {
		PointerRNA ptr = CTX_data_pointer_get_type(C, "modifier", &RNA_Modifier);
		if (ptr.data) {
			md = ptr.data;
			RNA_string_set(op->ptr, "modifier", md->name);
			return true;
		}
	}

	return false;
}

ModifierData *edit_modifier_property_get(wmOperator *op, Object *ob, int type)
{
	char modifier_name[MAX_NAME];
	ModifierData *md;
	RNA_string_get(op->ptr, "modifier", modifier_name);

	md = modifiers_findByName(ob, modifier_name);

	if (md && type != 0 && md->type != type)
		md = NULL;

	return md;
}

/************************ remove modifier operator *********************/

static int modifier_remove_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);

	if (!md || !ED_object_modifier_remove(op->reports, bmain, ob, md))
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_remove_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_remove_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_modifier_remove(wmOperatorType *ot)
{
	ot->name = "Remove Modifier";
	ot->description = "Remove a modifier from the active object";
	ot->idname = "OBJECT_OT_modifier_remove";

	ot->invoke = modifier_remove_invoke;
	ot->exec = modifier_remove_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ move up modifier operator *********************/

static int modifier_move_up_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);

	if (!md || !ED_object_modifier_move_up(op->reports, ob, md))
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_move_up_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_move_up_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_modifier_move_up(wmOperatorType *ot)
{
	ot->name = "Move Up Modifier";
	ot->description = "Move modifier up in the stack";
	ot->idname = "OBJECT_OT_modifier_move_up";

	ot->invoke = modifier_move_up_invoke;
	ot->exec = modifier_move_up_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ move down modifier operator *********************/

static int modifier_move_down_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);

	if (!md || !ED_object_modifier_move_down(op->reports, ob, md))
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_move_down_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_move_down_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_modifier_move_down(wmOperatorType *ot)
{
	ot->name = "Move Down Modifier";
	ot->description = "Move modifier down in the stack";
	ot->idname = "OBJECT_OT_modifier_move_down";

	ot->invoke = modifier_move_down_invoke;
	ot->exec = modifier_move_down_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ apply modifier operator *********************/

static int modifier_apply_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);
	int apply_as = RNA_enum_get(op->ptr, "apply_as");

	if (!md || !ED_object_modifier_apply(bmain, op->reports, scene, ob, md, apply_as)) {
		return OPERATOR_CANCELLED;
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_apply_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_apply_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

static const EnumPropertyItem modifier_apply_as_items[] = {
	{MODIFIER_APPLY_DATA, "DATA", 0, "Object Data", "Apply modifier to the object's data"},
	{MODIFIER_APPLY_SHAPE, "SHAPE", 0, "New Shape", "Apply deform-only modifier to a new shape on this object"},
	{0, NULL, 0, NULL, NULL}
};

void OBJECT_OT_modifier_apply(wmOperatorType *ot)
{
	ot->name = "Apply Modifier";
	ot->description = "Apply modifier and remove from the stack";
	ot->idname = "OBJECT_OT_modifier_apply";

	ot->invoke = modifier_apply_invoke;
	ot->exec = modifier_apply_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;

	RNA_def_enum(ot->srna, "apply_as", modifier_apply_as_items, MODIFIER_APPLY_DATA, "Apply as", "How to apply the modifier to the geometry");
	edit_modifier_properties(ot);
}

/************************ convert modifier operator *********************/

static int modifier_convert_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);

	if (!md)
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_convert_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_convert_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_modifier_convert(wmOperatorType *ot)
{
	ot->name = "Convert Modifier";
	ot->description = "Convert particles to a mesh object";
	ot->idname = "OBJECT_OT_modifier_convert";

	ot->invoke = modifier_convert_invoke;
	ot->exec = modifier_convert_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ copy modifier operator *********************/

static int modifier_copy_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	ModifierData *md = edit_modifier_property_get(op, ob, 0);

	if (!md || !ED_object_modifier_copy(op->reports, ob, md))
		return OPERATOR_CANCELLED;

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int modifier_copy_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return modifier_copy_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_modifier_copy(wmOperatorType *ot)
{
	ot->name = "Copy Modifier";
	ot->description = "Duplicate modifier at the same position in the stack";
	ot->idname = "OBJECT_OT_modifier_copy";

	ot->invoke = modifier_copy_invoke;
	ot->exec = modifier_copy_exec;
	ot->poll = edit_modifier_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ delta mush bind operator *********************/

static bool correctivesmooth_poll(bContext *C)
{
	return edit_modifier_poll_generic(C, &RNA_CorrectiveSmoothModifier, 0);
}

static int correctivesmooth_bind_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = ED_object_active_context(C);
	CorrectiveSmoothModifierData *csmd = (CorrectiveSmoothModifierData *)edit_modifier_property_get(op, ob, eModifierType_CorrectiveSmooth);
	bool is_bind;

	if (!csmd) {
		return OPERATOR_CANCELLED;
	}

	if (!modifier_isEnabled(scene, &csmd->modifier, eModifierMode_Realtime)) {
		BKE_report(op->reports, RPT_ERROR, "Modifier is disabled");
		return OPERATOR_CANCELLED;
	}

	is_bind = (csmd->bind_coords != NULL);

	MEM_SAFE_FREE(csmd->bind_coords);
	MEM_SAFE_FREE(csmd->delta_cache);

	if (is_bind) {
		/* toggle off */
		csmd->bind_coords_num = 0;
	}
	else {
		/* signal to modifier to recalculate */
		csmd->bind_coords_num = (unsigned int)-1;
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int correctivesmooth_bind_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return correctivesmooth_bind_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_correctivesmooth_bind(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Corrective Smooth Bind";
	ot->description = "Bind base pose in Corrective Smooth modifier";
	ot->idname = "OBJECT_OT_correctivesmooth_bind";

	/* api callbacks */
	ot->poll = correctivesmooth_poll;
	ot->invoke = correctivesmooth_bind_invoke;
	ot->exec = correctivesmooth_bind_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ mdef bind operator *********************/

static bool meshdeform_poll(bContext *C)
{
	return edit_modifier_poll_generic(C, &RNA_MeshDeformModifier, 0);
}

static int meshdeform_bind_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	MeshDeformModifierData *mmd = (MeshDeformModifierData *)edit_modifier_property_get(op, ob, eModifierType_MeshDeform);

	if (!mmd)
		return OPERATOR_CANCELLED;

	if (mmd->bindcagecos) {
		MEM_freeN(mmd->bindcagecos);
		if (mmd->dyngrid) MEM_freeN(mmd->dyngrid);
		if (mmd->dyninfluences) MEM_freeN(mmd->dyninfluences);
		if (mmd->bindinfluences) MEM_freeN(mmd->bindinfluences);
		if (mmd->bindoffsets) MEM_freeN(mmd->bindoffsets);
		if (mmd->dynverts) MEM_freeN(mmd->dynverts);
		if (mmd->bindweights) MEM_freeN(mmd->bindweights);  /* deprecated */
		if (mmd->bindcos) MEM_freeN(mmd->bindcos);  /* deprecated */

		mmd->bindcagecos = NULL;
		mmd->dyngrid = NULL;
		mmd->dyninfluences = NULL;
		mmd->bindinfluences = NULL;
		mmd->bindoffsets = NULL;
		mmd->dynverts = NULL;
		mmd->bindweights = NULL; /* deprecated */
		mmd->bindcos = NULL; /* deprecated */
		mmd->totvert = 0;
		mmd->totcagevert = 0;
		mmd->totinfluence = 0;

		WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);
	}

	return OPERATOR_FINISHED;
}

static int meshdeform_bind_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return meshdeform_bind_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_meshdeform_bind(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Mesh Deform Bind";
	ot->description = "Bind mesh to cage in mesh deform modifier";
	ot->idname = "OBJECT_OT_meshdeform_bind";

	/* api callbacks */
	ot->poll = meshdeform_poll;
	ot->invoke = meshdeform_bind_invoke;
	ot->exec = meshdeform_bind_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ LaplacianDeform bind operator *********************/

static bool laplaciandeform_poll(bContext *C)
{
	return edit_modifier_poll_generic(C, &RNA_LaplacianDeformModifier, 0);
}

static int laplaciandeform_bind_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	LaplacianDeformModifierData *lmd = (LaplacianDeformModifierData *)edit_modifier_property_get(op, ob, eModifierType_LaplacianDeform);

	if (!lmd)
		return OPERATOR_CANCELLED;
	if (lmd->flag & MOD_LAPLACIANDEFORM_BIND) {
		lmd->flag &= ~MOD_LAPLACIANDEFORM_BIND;
	}
	else {
		lmd->flag |= MOD_LAPLACIANDEFORM_BIND;
	}
	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);
	return OPERATOR_FINISHED;
}

static int laplaciandeform_bind_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return laplaciandeform_bind_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_laplaciandeform_bind(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Laplacian Deform Bind";
	ot->description = "Bind mesh to system in laplacian deform modifier";
	ot->idname = "OBJECT_OT_laplaciandeform_bind";

	/* api callbacks */
	ot->poll = laplaciandeform_poll;
	ot->invoke = laplaciandeform_bind_invoke;
	ot->exec = laplaciandeform_bind_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}

/************************ sdef bind operator *********************/

static bool surfacedeform_bind_poll(bContext *C)
{
	return edit_modifier_poll_generic(C, &RNA_SurfaceDeformModifier, 0);
}

static int surfacedeform_bind_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_active_context(C);
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *)edit_modifier_property_get(op, ob, eModifierType_SurfaceDeform);

	if (!smd)
		return OPERATOR_CANCELLED;

	if (smd->flags & MOD_SDEF_BIND) {
		smd->flags &= ~MOD_SDEF_BIND;
	}
	else if (smd->target) {
		smd->flags |= MOD_SDEF_BIND;
	}

	WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

	return OPERATOR_FINISHED;
}

static int surfacedeform_bind_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (edit_modifier_invoke_properties(C, op))
		return surfacedeform_bind_exec(C, op);
	else
		return OPERATOR_CANCELLED;
}

void OBJECT_OT_surfacedeform_bind(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Surface Deform Bind";
	ot->description = "Bind mesh to target in surface deform modifier";
	ot->idname = "OBJECT_OT_surfacedeform_bind";

	/* api callbacks */
	ot->poll = surfacedeform_bind_poll;
	ot->invoke = surfacedeform_bind_invoke;
	ot->exec = surfacedeform_bind_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;
	edit_modifier_properties(ot);
}
