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

/** \file blender/editors/object/object_edit.c
 *  \ingroup edobj
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <ctype.h>
#include <stddef.h> //for offsetof

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "DNA_curve_types.h"
#include "DNA_group_types.h"
#include "DNA_material_types.h"
#include "DNA_property_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_vfont_types.h"
#include "DNA_mesh_types.h"

#include "IMB_imbuf_types.h"

#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_modifier.h"
#include "BKE_editmesh.h"
#include "BKE_report.h"
#include "BKE_undo_system.h"

#include "ED_curve.h"
#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_undo.h"
#include "ED_image.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

/* for menu/popup icons etc etc*/

#include "UI_interface.h"
#include "WM_api.h"
#include "WM_types.h"

#include "object_intern.h"  // own include

/* ************* XXX **************** */
static void error(const char *UNUSED(arg)) {}
static void waitcursor(int UNUSED(val)) {}
static int pupmenu(const char *UNUSED(msg)) { return 0; }

/* port over here */
static void error_libdata(void) {}

Object *ED_object_context(bContext *C)
{
	return CTX_data_pointer_get_type(C, "object", &RNA_Object).data;
}

/* find the correct active object per context
 * note: context can be NULL when called from a enum with PROP_ENUM_NO_CONTEXT */
Object *ED_object_active_context(bContext *C)
{
	Object *ob = NULL;
	if (C) {
		ob = ED_object_context(C);
		if (!ob) ob = CTX_data_active_object(C);
	}
	return ob;
}


/* ********* clear/set restrict view *********/
static int object_hide_view_clear_exec(bContext *C, wmOperator *op)
{
	ScrArea *sa = CTX_wm_area(C);
	View3D *v3d = sa->spacedata.first;
	Scene *scene = CTX_data_scene(C);
	Base *base;
	bool changed = false;
	const bool select = RNA_boolean_get(op->ptr, "select");

	/* XXX need a context loop to handle such cases */
	for (base = FIRSTBASE; base; base = base->next) {
		if ((base->lay & v3d->lay) && base->object->restrictflag & OB_RESTRICT_VIEW) {
			if (!(base->object->restrictflag & OB_RESTRICT_SELECT)) {
				SET_FLAG_FROM_TEST(base->flag, select, SELECT);
			}
			base->object->flag = base->flag;
			base->object->restrictflag &= ~OB_RESTRICT_VIEW;
			changed = true;
		}
	}
	if (changed) {
		WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
	}

	return OPERATOR_FINISHED;
}

void OBJECT_OT_hide_view_clear(wmOperatorType *ot)
{

	/* identifiers */
	ot->name = "Clear Restrict View";
	ot->description = "Reveal the object by setting the hide flag";
	ot->idname = "OBJECT_OT_hide_view_clear";

	/* api callbacks */
	ot->exec = object_hide_view_clear_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "select", true, "Select", "");
}

static int object_hide_view_set_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	bool changed = false;
	const bool unselected = RNA_boolean_get(op->ptr, "unselected");

	CTX_DATA_BEGIN(C, Base *, base, visible_bases)
	{
		if (!unselected) {
			if (base->flag & SELECT) {
				base->flag &= ~SELECT;
				base->object->flag = base->flag;
				base->object->restrictflag |= OB_RESTRICT_VIEW;
				changed = true;
				if (base == BASACT) {
					ED_base_object_activate(C, NULL);
				}
			}
		}
		else {
			if (!(base->flag & SELECT)) {
				base->object->restrictflag |= OB_RESTRICT_VIEW;
				changed = true;
				if (base == BASACT) {
					ED_base_object_activate(C, NULL);
				}
			}
		}
	}
	CTX_DATA_END;

	if (changed) {

		WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);

	}

	return OPERATOR_FINISHED;
}

void OBJECT_OT_hide_view_set(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Set Restrict View";
	ot->description = "Hide the object by setting the hide flag";
	ot->idname = "OBJECT_OT_hide_view_set";

	/* api callbacks */
	ot->exec = object_hide_view_set_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "unselected", 0, "Unselected", "Hide unselected rather than selected objects");

}

/* 99% same as above except no need for scene refreshing (TODO, update render preview) */
static int object_hide_render_clear_exec(bContext *C, wmOperator *UNUSED(op))
{
	bool changed = false;

	/* XXX need a context loop to handle such cases */
	CTX_DATA_BEGIN(C, Object *, ob, selected_editable_objects)
	{
		if (ob->restrictflag & OB_RESTRICT_RENDER) {
			ob->restrictflag &= ~OB_RESTRICT_RENDER;
			changed = true;
		}
	}
	CTX_DATA_END;

	if (changed)
		WM_event_add_notifier(C, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	return OPERATOR_FINISHED;
}

void OBJECT_OT_hide_render_clear(wmOperatorType *ot)
{

	/* identifiers */
	ot->name = "Clear Restrict Render";
	ot->description = "Reveal the render object by setting the hide render flag";
	ot->idname = "OBJECT_OT_hide_render_clear";

	/* api callbacks */
	ot->exec = object_hide_render_clear_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int object_hide_render_set_exec(bContext *C, wmOperator *op)
{
	const bool unselected = RNA_boolean_get(op->ptr, "unselected");

	CTX_DATA_BEGIN(C, Base *, base, visible_bases)
	{
		if (!unselected) {
			if (base->flag & SELECT) {
				base->object->restrictflag |= OB_RESTRICT_RENDER;
			}
		}
		else {
			if (!(base->flag & SELECT)) {
				base->object->restrictflag |= OB_RESTRICT_RENDER;
			}
		}
	}
	CTX_DATA_END;
	WM_event_add_notifier(C, NC_SPACE | ND_SPACE_OUTLINER, NULL);
	return OPERATOR_FINISHED;
}

void OBJECT_OT_hide_render_set(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Set Restrict Render";
	ot->description = "Hide the render object by setting the hide render flag";
	ot->idname = "OBJECT_OT_hide_render_set";

	/* api callbacks */
	ot->exec = object_hide_render_set_exec;
	ot->poll = ED_operator_view3d_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_boolean(ot->srna, "unselected", 0, "Unselected", "Hide unselected rather than selected objects");
}

/* ******************* toggle editmode operator  ***************** */

static bool mesh_needs_keyindex(Main *bmain, const Mesh *me)
{
	for (const Object *ob = bmain->object.first; ob; ob = ob->id.next) {
		if ((ob->parent) && (ob->parent->data == me) && ELEM(ob->partype, PARVERT1, PARVERT3)) {
			return true;
		}
		if (ob->data == me) {
			for (const ModifierData *md = ob->modifiers.first; md; md = md->next) {
				if (md->type == eModifierType_Hook) {
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * Load EditMode data back into the object,
 * optionally freeing the editmode data.
 */
static bool ED_object_editmode_load_ex(Main *bmain, Object *obedit, const bool freedata)
{
	if (obedit == NULL) {
		return false;
	}

	if (obedit->type == OB_MESH) {
		Mesh *me = obedit->data;
		if (me->edit_btmesh == NULL) {
			return false;
		}

		if (me->edit_btmesh->bm->totvert > MESH_MAX_VERTS) {
			error("Too many vertices");
			return false;
		}

		EDBM_mesh_load(bmain, obedit);

		if (freedata) {
			EDBM_mesh_free(me->edit_btmesh);
			MEM_freeN(me->edit_btmesh);
			me->edit_btmesh = NULL;
		}
		/* will be recalculated as needed. */
		{
			ED_mesh_mirror_spatial_table(NULL, NULL, NULL, NULL, 'e');
			ED_mesh_mirror_topo_table(NULL, NULL, 'e');
		}
	}
	else if (ELEM(obedit->type, OB_CURVE, OB_SURF)) {
		const Curve *cu = obedit->data;
		if (cu->editnurb == NULL) {
			return false;
		}
		ED_curve_editnurb_load(bmain, obedit);
		if (freedata) {
			ED_curve_editnurb_free(obedit);
		}
	}
	else if (obedit->type == OB_FONT) {
		const Curve *cu = obedit->data;
		if (cu->editfont == NULL) {
			return false;
		}
		ED_curve_editfont_load(obedit);
		if (freedata) {
			ED_curve_editfont_free(obedit);
		}
	}

	return true;
}

bool ED_object_editmode_load(Main *bmain, Object *obedit)
{
	return ED_object_editmode_load_ex(bmain, obedit, false);
}

/**
 * \param flag:
 * - If #EM_FREEDATA isn't in the flag, use ED_object_editmode_load directly.
 */
bool ED_object_editmode_exit_ex(Main *bmain, Scene *scene, Object *obedit, int flag)
{
	const bool freedata = (flag & EM_FREEDATA) != 0;

	if (flag & EM_WAITCURSOR) waitcursor(1);

	if (ED_object_editmode_load_ex(bmain, obedit, freedata) == false) {
		/* in rare cases (background mode) its possible active object
		 * is flagged for editmode, without 'obedit' being set [#35489] */
		if (UNLIKELY(scene->basact && (scene->basact->object->mode & OB_MODE_EDIT))) {
			scene->basact->object->mode &= ~OB_MODE_EDIT;
		}
		if (flag & EM_WAITCURSOR) waitcursor(0);
		return true;
	}

	/* freedata only 0 now on file saves and render */
	if (freedata) {
		/* for example; displist make is different in editmode */
		scene->obedit = NULL; // XXX for context

		/* also flush ob recalc, doesn't take much overhead, but used for particles */

		WM_main_add_notifier(NC_SCENE | ND_MODE | NS_MODE_OBJECT, scene);

		obedit->mode &= ~OB_MODE_EDIT;
	}

	if (flag & EM_WAITCURSOR) waitcursor(0);

	return (obedit->mode & OB_MODE_EDIT) == 0;
}

bool ED_object_editmode_exit(bContext *C, int flag)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *obedit = CTX_data_edit_object(C);
	return ED_object_editmode_exit_ex(bmain, scene, obedit, flag);
}

bool ED_object_editmode_enter(bContext *C, int flag)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Base *base = NULL;
	Object *ob;
	ScrArea *sa = CTX_wm_area(C);
	View3D *v3d = NULL;
	bool ok = false;

	if (ID_IS_LINKED(scene)) {
		return false;
	}

	if (sa && sa->spacetype == SPACE_VIEW3D)
		v3d = sa->spacedata.first;

	if ((flag & EM_IGNORE_LAYER) == 0) {
		base = CTX_data_active_base(C); /* active layer checked here for view3d */

		if ((base == NULL) ||
		    (v3d && (base->lay & v3d->lay) == 0) ||
		    (!v3d && (base->lay & scene->lay) == 0))
		{
			return false;
		}
	}
	else {
		base = scene->basact;
	}

	if (ELEM(NULL, base, base->object, base->object->data)) {
		return false;
	}

	ob = base->object;

	/* this checks actual object->data, for cases when other scenes have it in editmode context */
	if (BKE_object_is_in_editmode(ob)) {
		return true;
	}

	if (BKE_object_obdata_is_libdata(ob)) {
		error_libdata();
		return false;
	}

	if (flag & EM_WAITCURSOR) waitcursor(1);

	ob->restore_mode = ob->mode;

	/* note, when switching scenes the object can have editmode data but
	 * not be scene->obedit: bug 22954, this avoids calling self eternally */
	if ((ob->restore_mode & OB_MODE_EDIT) == 0)
		ED_object_mode_toggle(C, ob->mode);

	ob->mode = OB_MODE_EDIT;

	if (ob->type == OB_MESH) {
		BMEditMesh *em;
		ok = 1;
		scene->obedit = ob;  /* context sees this */

		const bool use_key_index = mesh_needs_keyindex(bmain, ob->data);

		EDBM_mesh_make(ob, scene->toolsettings->selectmode, use_key_index);

		em = BKE_editmesh_from_object(ob);
		if (LIKELY(em)) {
			/* order doesn't matter */
			EDBM_mesh_normals_update(em);
			BKE_editmesh_tessface_calc(em);
		}

		WM_event_add_notifier(C, NC_SCENE | ND_MODE | NS_EDITMODE_MESH, scene);
	}
	else if (ob->type == OB_FONT) {
		scene->obedit = ob; /* XXX for context */
		ok = 1;
		ED_curve_editfont_make(ob);

		WM_event_add_notifier(C, NC_SCENE | ND_MODE | NS_EDITMODE_TEXT, scene);
	}
	else if (ob->type == OB_SURF || ob->type == OB_CURVE) {
		ok = 1;
		scene->obedit = ob; /* XXX for context */
		ED_curve_editnurb_make(ob);

		WM_event_add_notifier(C, NC_SCENE | ND_MODE | NS_EDITMODE_CURVE, scene);
	}

	if (ok) {
	}
	else {
		scene->obedit = NULL; /* XXX for context */
		ob->mode &= ~OB_MODE_EDIT;
		WM_event_add_notifier(C, NC_SCENE | ND_MODE | NS_MODE_OBJECT, scene);
	}

	if (flag & EM_WAITCURSOR) waitcursor(0);

	return (ob->mode & OB_MODE_EDIT) != 0;
}

static int editmode_toggle_exec(bContext *C, wmOperator *op)
{
	const int mode_flag = OB_MODE_EDIT;
	const bool is_mode_set = (CTX_data_edit_object(C) != NULL);

	if (!is_mode_set) {
		Object *ob = CTX_data_active_object(C);
		if (!ED_object_mode_compat_set(C, ob, mode_flag, op->reports)) {
			return OPERATOR_CANCELLED;
		}
	}

	if (!is_mode_set) {
		ED_object_editmode_enter(C, EM_WAITCURSOR);
	}
	else {
		ED_object_editmode_exit(C, EM_FREEDATA | EM_WAITCURSOR);
	}

	return OPERATOR_FINISHED;
}

static bool editmode_toggle_poll(bContext *C)
{
	Object *ob = CTX_data_active_object(C);

	/* covers proxies too */
	if (ELEM(NULL, ob, ob->data) || ID_IS_LINKED(ob->data))
		return 0;

	/* if hidden but in edit mode, we still display */
	if ((ob->restrictflag & OB_RESTRICT_VIEW) && !(ob->mode & OB_MODE_EDIT))
		return 0;

	return OB_TYPE_SUPPORT_EDITMODE(ob->type);
}

void OBJECT_OT_editmode_toggle(wmOperatorType *ot)
{

	/* identifiers */
	ot->name = "Toggle Editmode";
	ot->description = "Toggle object's editmode";
	ot->idname = "OBJECT_OT_editmode_toggle";

	/* api callbacks */
	ot->exec = editmode_toggle_exec;
	ot->poll = editmode_toggle_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* *************************** */

/* both pointers should exist */
static void copy_texture_space(Object *to, Object *ob)
{
	float *poin1 = NULL, *poin2 = NULL;
	short texflag = 0;

	if (ob->type == OB_MESH) {
		texflag = ((Mesh *)ob->data)->texflag;
		poin2 = ((Mesh *)ob->data)->loc;
	}
	else if (ELEM(ob->type, OB_CURVE, OB_SURF, OB_FONT)) {
		texflag = ((Curve *)ob->data)->texflag;
		poin2 = ((Curve *)ob->data)->loc;
	}
	else
		return;

	if (to->type == OB_MESH) {
		((Mesh *)to->data)->texflag = texflag;
		poin1 = ((Mesh *)to->data)->loc;
	}
	else if (ELEM(to->type, OB_CURVE, OB_SURF, OB_FONT)) {
		((Curve *)to->data)->texflag = texflag;
		poin1 = ((Curve *)to->data)->loc;
	}
	else
		return;

	memcpy(poin1, poin2, 9 * sizeof(float));  /* this was noted in DNA_mesh, curve, mball */

	if (to->type == OB_MESH) {
		/* pass */
	}
	else {
		BKE_curve_texspace_calc(to->data);
	}

}

/* UNUSED, keep in case we want to copy functionality for use elsewhere */
static void copy_attr(Main *bmain, Scene *scene, View3D *v3d, short event)
{
	Object *ob;
	Base *base;
	Curve *cu, *cu1;
	Nurb *nu;

	if (ID_IS_LINKED(scene)) return;

	if (!(ob = OBACT)) return;

	if (scene->obedit) { // XXX get from context
		/* obedit_copymenu(); */
		return;
	}
	else if (event == 24) {
		/* moved to BKE_object_link_modifiers */
		/* copymenu_modifiers(bmain, scene, v3d, ob); */
		return;
	}

	for (base = FIRSTBASE; base; base = base->next) {
		if (base != BASACT) {
			if (TESTBASELIB(v3d, base)) {

				if (event == 1) {  /* loc */
					copy_v3_v3(base->object->loc, ob->loc);
					copy_v3_v3(base->object->dloc, ob->dloc);
				}
				else if (event == 2) {  /* rot */
					copy_v3_v3(base->object->rot, ob->rot);
					copy_v3_v3(base->object->drot, ob->drot);

					copy_qt_qt(base->object->quat, ob->quat);
					copy_qt_qt(base->object->dquat, ob->dquat);
				}
				else if (event == 3) {  /* size */
					copy_v3_v3(base->object->size, ob->size);
					copy_v3_v3(base->object->dscale, ob->dscale);
				}
				else if (event == 4) {  /* drawtype */
					base->object->dt = ob->dt;
					base->object->dtx = ob->dtx;
					base->object->empty_drawtype = ob->empty_drawtype;
					base->object->empty_drawsize = ob->empty_drawsize;
				}
				else if (event == 17) {   /* tex space */
					copy_texture_space(base->object, ob);
				}
				else if (event == 18) {   /* font settings */

					if (base->object->type == ob->type) {
						cu = ob->data;
						cu1 = base->object->data;

						cu1->spacemode = cu->spacemode;
						cu1->align_y = cu->align_y;
						cu1->spacing = cu->spacing;
						cu1->linedist = cu->linedist;
						cu1->shear = cu->shear;
						cu1->fsize = cu->fsize;
						cu1->xof = cu->xof;
						cu1->yof = cu->yof;
						cu1->textoncurve = cu->textoncurve;
						cu1->wordspace = cu->wordspace;
						cu1->ulpos = cu->ulpos;
						cu1->ulheight = cu->ulheight;
						if (cu1->vfont)
							id_us_min(&cu1->vfont->id);
						cu1->vfont = cu->vfont;
						id_us_plus((ID *)cu1->vfont);
						if (cu1->vfontb)
							id_us_min(&cu1->vfontb->id);
						cu1->vfontb = cu->vfontb;
						id_us_plus((ID *)cu1->vfontb);
						if (cu1->vfonti)
							id_us_min(&cu1->vfonti->id);
						cu1->vfonti = cu->vfonti;
						id_us_plus((ID *)cu1->vfonti);
						if (cu1->vfontbi)
							id_us_min(&cu1->vfontbi->id);
						cu1->vfontbi = cu->vfontbi;
						id_us_plus((ID *)cu1->vfontbi);

						BLI_strncpy(cu1->family, cu->family, sizeof(cu1->family));

					}
				}
				else if (event == 19) {   /* bevel settings */

					if (ELEM(base->object->type, OB_CURVE, OB_FONT)) {
						cu = ob->data;
						cu1 = base->object->data;

						cu1->bevobj = cu->bevobj;
						cu1->taperobj = cu->taperobj;
						cu1->width = cu->width;
						cu1->bevresol = cu->bevresol;
						cu1->ext1 = cu->ext1;
						cu1->ext2 = cu->ext2;

					}
				}
				else if (event == 25) {   /* curve resolution */

					if (ELEM(base->object->type, OB_CURVE, OB_FONT)) {
						cu = ob->data;
						cu1 = base->object->data;

						cu1->resolu = cu->resolu;
						cu1->resolu_ren = cu->resolu_ren;

						nu = cu1->nurb.first;

						while (nu) {
							nu->resolu = cu1->resolu;
							nu = nu->next;
						}

					}
				}
				else if (event == 21) {
					if (base->object->type == OB_MESH) {
						ModifierData *md = modifiers_findByType(ob, eModifierType_Subsurf);

						if (md) {
							ModifierData *tmd = modifiers_findByType(base->object, eModifierType_Subsurf);

							if (!tmd) {
								tmd = modifier_new(eModifierType_Subsurf);
								BLI_addtail(&base->object->modifiers, tmd);
							}

							modifier_copyData(md, tmd);
						}
					}
				}
				else if (event == 27) {   /* autosmooth */
					if (base->object->type == OB_MESH) {
						Mesh *me = ob->data;
						Mesh *cme = base->object->data;
						cme->smoothresh = me->smoothresh;
						if (me->flag & ME_AUTOSMOOTH)
							cme->flag |= ME_AUTOSMOOTH;
						else
							cme->flag &= ~ME_AUTOSMOOTH;
					}
				}
				else if (event == 28) { /* UV orco */
					if (ELEM(base->object->type, OB_CURVE, OB_SURF)) {
						cu = ob->data;
						cu1 = base->object->data;

						if (cu->flag & CU_UV_ORCO)
							cu1->flag |= CU_UV_ORCO;
						else
							cu1->flag &= ~CU_UV_ORCO;
					}
				}
				else if (event == 29) { /* protected bits */
					base->object->protectflag = ob->protectflag;
				}
				else if (event == 30) { /* index object */
					base->object->index = ob->index;
				}
				else if (event == 31) { /* object color */
					copy_v4_v4(base->object->col, ob->col);
				}
			}
		}
	}

}

static void UNUSED_FUNCTION(copy_attr_menu) (Main *bmain, Scene *scene, View3D *v3d)
{
	Object *ob;
	short event;
	char str[512];

	if (!(ob = OBACT)) return;

	if (scene->obedit) { /* XXX get from context */
/*		if (ob->type == OB_MESH) */
/* XXX			mesh_copy_menu(); */
		return;
	}

	/* Object Mode */

	/* If you change this menu, don't forget to update the menu in header_view3d.c
	 * view3d_edit_object_copyattrmenu() and in toolbox.c
	 */

	strcpy(str,
	       "Copy Attributes %t|Location %x1|Rotation %x2|Size %x3|Draw Options %x4|"
	       "Time Offset %x5|Dupli %x6|Object Color %x31|%l|Mass %x7|Damping %x8|All Physical Attributes %x11|Properties %x9|"
	       "Logic Bricks %x10|Protected Transform %x29|%l");

	strcat(str, "|Object Constraints %x22");
	strcat(str, "|NLA Strips %x26");

/* XXX	if (OB_TYPE_SUPPORT_MATERIAL(ob->type)) { */
/*		strcat(str, "|Texture Space %x17"); */
/*	} */

	if (ob->type == OB_FONT) strcat(str, "|Font Settings %x18|Bevel Settings %x19");
	if (ob->type == OB_CURVE) strcat(str, "|Bevel Settings %x19|UV Orco %x28");

	if ((ob->type == OB_FONT) || (ob->type == OB_CURVE)) {
		strcat(str, "|Curve Resolution %x25");
	}

	if (ob->type == OB_MESH) {
		strcat(str, "|Subsurf Settings %x21|AutoSmooth %x27");
	}

	strcat(str, "|Pass Index %x30");

	if (ob->type == OB_MESH || ob->type == OB_CURVE || ob->type == OB_SURF) {
		strcat(str, "|Modifiers ... %x24");
	}

	event = pupmenu(str);
	if (event <= 0) return;

	copy_attr(bmain, scene, v3d, event);
}

/* ********************************************** */
/* Motion Paths */

/* For the objects with animation: update paths for those that have got them
 * This should selectively update paths that exist...
 *
 * To be called from various tools that do incremental updates
 */

/********************** Smooth/Flat *********************/

static int shade_smooth_exec(bContext *C, wmOperator *op)
{
	ID *data;
	Curve *cu;
	Nurb *nu;
	int clear = (STREQ(op->idname, "OBJECT_OT_shade_flat"));
	bool done = false, linked_data = false;

	CTX_DATA_BEGIN(C, Object *, ob, selected_editable_objects)
	{
		data = ob->data;

		if (data && ID_IS_LINKED(data)) {
			linked_data = true;
			continue;
		}

		if (ob->type == OB_MESH) {
			BKE_mesh_smooth_flag_set(ob, !clear);

			WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);

			done = true;
		}
		else if (ELEM(ob->type, OB_SURF, OB_CURVE)) {
			cu = ob->data;

			for (nu = cu->nurb.first; nu; nu = nu->next) {
				if (!clear) nu->flag |= ME_SMOOTH;
				else nu->flag &= ~ME_SMOOTH;
			}

			WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);

			done = true;
		}
	}
	CTX_DATA_END;

	if (linked_data)
		BKE_report(op->reports, RPT_WARNING, "Can't edit linked mesh or curve data");

	return (done) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static bool shade_poll(bContext *C)
{
	return (CTX_data_edit_object(C) == NULL);
}

void OBJECT_OT_shade_flat(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Shade Flat";
	ot->description = "Render and display faces uniform, using Face Normals";
	ot->idname = "OBJECT_OT_shade_flat";

	/* api callbacks */
	ot->poll = shade_poll;
	ot->exec = shade_smooth_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void OBJECT_OT_shade_smooth(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Shade Smooth";
	ot->description = "Render and display faces smooth, using interpolated Vertex Normals";
	ot->idname = "OBJECT_OT_shade_smooth";

	/* api callbacks */
	ot->poll = shade_poll;
	ot->exec = shade_smooth_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ********************** */

static void UNUSED_FUNCTION(image_aspect) (Scene *scene, View3D *v3d)
{
	/* all selected objects with an image map: scale in image aspect */
	Base *base;
	Object *ob;
	Material *ma;
	Tex *tex;
	float x, y, space;
	int a, b, done;

	if (scene->obedit) return;  // XXX get from context
	if (ID_IS_LINKED(scene)) return;

	for (base = FIRSTBASE; base; base = base->next) {
		if (TESTBASELIB(v3d, base)) {
			ob = base->object;
			done = false;

			for (a = 1; a <= ob->totcol; a++) {
				ma = give_current_material(ob, a);
				if (ma) {
					for (b = 0; b < MAX_MTEX; b++) {
						if (ma->mtex[b] && ma->mtex[b]->tex) {
							tex = ma->mtex[b]->tex;
							if (tex->type == TEX_IMAGE && tex->ima) {
								ImBuf *ibuf = BKE_image_acquire_ibuf(tex->ima, NULL, NULL);

								/* texturespace */
								space = 1.0;
								if (ob->type == OB_MESH) {
									float size[3];
									BKE_mesh_texspace_get(ob->data, NULL, NULL, size);
									space = size[0] / size[1];
								}
								else if (ELEM(ob->type, OB_CURVE, OB_FONT, OB_SURF)) {
									float size[3];
									BKE_curve_texspace_get(ob->data, NULL, NULL, size);
									space = size[0] / size[1];
								}

								x = ibuf->x / space;
								y = ibuf->y;

								if (x > y) ob->size[0] = ob->size[1] * x / y;
								else ob->size[1] = ob->size[0] * y / x;

								done = true;

								BKE_image_release_ibuf(tex->ima, ibuf, NULL);
							}
						}
						if (done) break;
					}
				}
				if (done) break;
			}
		}
	}

}

static const EnumPropertyItem *object_mode_set_itemsf(
        bContext *C, PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop), bool *r_free)
{
	const EnumPropertyItem *input = rna_enum_object_mode_items;
	EnumPropertyItem *item = NULL;
	Object *ob;
	int totitem = 0;

	if (!C) /* needed for docs */
		return rna_enum_object_mode_items;

	ob = CTX_data_active_object(C);
	if (ob) {
		while (input->identifier) {
			if ((input->value == OB_MODE_EDIT && OB_TYPE_SUPPORT_EDITMODE(ob->type)) ||
			    (input->value == OB_MODE_OBJECT))
			{
				RNA_enum_item_add(&item, &totitem, input);
			}
			input++;
		}
	}
	else {
		/* We need at least this one! */
		RNA_enum_items_add_value(&item, &totitem, input, OB_MODE_OBJECT);
	}

	RNA_enum_item_end(&item, &totitem);

	*r_free = true;

	return item;
}

static bool object_mode_set_poll(bContext *C)
{
	if (ED_operator_object_active_editable(C))
		return true;
	else
		return false;
}

static int object_mode_set_exec(bContext *C, wmOperator *op)
{
	Object *ob = CTX_data_active_object(C);
	eObjectMode mode = RNA_enum_get(op->ptr, "mode");
	eObjectMode restore_mode = (ob) ? ob->mode : OB_MODE_OBJECT;
	const bool toggle = RNA_boolean_get(op->ptr, "toggle");

	if (!ob || !ED_object_mode_compat_test(ob, mode))
		return OPERATOR_PASS_THROUGH;

	if (ob->mode != mode) {
		/* we should be able to remove this call, each operator calls  */
		ED_object_mode_compat_set(C, ob, mode, op->reports);
	}

	/* Exit current mode if it's not the mode we're setting */
	if (mode != OB_MODE_OBJECT && (ob->mode != mode || toggle)) {
		/* Enter new mode */
		ED_object_mode_toggle(C, mode);
	}

	if (toggle) {
		/* Special case for Object mode! */
		if (mode == OB_MODE_OBJECT && restore_mode == OB_MODE_OBJECT && ob->restore_mode != OB_MODE_OBJECT) {
			ED_object_mode_toggle(C, ob->restore_mode);
		}
		else if (ob->mode == mode) {
			/* For toggling, store old mode so we know what to go back to */
			ob->restore_mode = restore_mode;
		}
		else if (ob->restore_mode != OB_MODE_OBJECT && ob->restore_mode != mode) {
			ED_object_mode_toggle(C, ob->restore_mode);
		}
	}

	return OPERATOR_FINISHED;
}

void OBJECT_OT_mode_set(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Set Object Mode";
	ot->description = "Sets the object interaction mode";
	ot->idname = "OBJECT_OT_mode_set";

	/* api callbacks */
	ot->exec = object_mode_set_exec;

	ot->poll = object_mode_set_poll; //ED_operator_object_active_editable;

	/* flags */
	ot->flag = 0; /* no register/undo here, leave it to operators being called */

	ot->prop = RNA_def_enum(ot->srna, "mode", rna_enum_object_mode_items, OB_MODE_OBJECT, "Mode", "");
	RNA_def_enum_funcs(ot->prop, object_mode_set_itemsf);
	RNA_def_property_flag(ot->prop, PROP_SKIP_SAVE);

	prop = RNA_def_boolean(ot->srna, "toggle", 0, "Toggle", "");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* generic utility function */

bool ED_object_editmode_calc_active_center(Object *obedit, const bool select_only, float r_center[3])
{
	switch (obedit->type) {
		case OB_MESH:
		{
			BMEditMesh *em = BKE_editmesh_from_object(obedit);
			BMEditSelection ese;

			if (BM_select_history_active_get(em->bm, &ese)) {
				BM_editselection_center(&ese, r_center);
				return true;
			}
			break;
		}
		case OB_CURVE:
		case OB_SURF:
		{
			Curve *cu = obedit->data;

			if (ED_curve_active_center(cu, r_center)) {
				return true;
			}
			break;
		}
	}

	return false;
}
