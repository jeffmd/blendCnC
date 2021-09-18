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
 * The Original Code is Copyright (C) 2004 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/editors/space_outliner/outliner_select.c
 *  \ingroup spoutliner
 */

#include <stdlib.h>

#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "BKE_context.h"
#include "BKE_object.h"
#include "BKE_scene.h"

#include "ED_object.h"
#include "ED_screen.h"
#include "ED_undo.h"

#include "WM_api.h"
#include "WM_types.h"


#include "UI_interface.h"
#include "UI_view2d.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "outliner_intern.h"

/* ****************************************************** */
/* Outliner Selection (gray-blue highlight for rows) */

static int outliner_select(SpaceOops *soops, ListBase *lb, int *index, short *selecting)
{
	TreeElement *te;
	TreeStoreElem *tselem;
	bool changed = false;

	for (te = lb->first; te && *index >= 0; te = te->next, (*index)--) {
		tselem = TREESTORE(te);

		/* if we've encountered the right item, set its 'Outliner' selection status */
		if (*index == 0) {
			/* this should be the last one, so no need to do anything with index */
			if ((te->flag & TE_ICONROW) == 0) {
				/* -1 value means toggle testing for now... */
				if (*selecting == -1) {
					if (tselem->flag & TSE_SELECTED)
						*selecting = 0;
					else
						*selecting = 1;
				}

				/* set selection */
				if (*selecting)
					tselem->flag |= TSE_SELECTED;
				else
					tselem->flag &= ~TSE_SELECTED;

				changed |= true;
			}
		}
		else if (TSELEM_OPEN(tselem, soops)) {
			/* Only try selecting sub-elements if we haven't hit the right element yet
			 *
			 * Hack warning:
			 *  Index must be reduced before supplying it to the sub-tree to try to do
			 *  selection, however, we need to increment it again for the next loop to
			 *  function correctly
			 */
			(*index)--;
			changed |= outliner_select(soops, &te->subtree, index, selecting);
			(*index)++;
		}
	}

	return changed;
}

/* ****************************************************** */
/* Outliner Element Selection/Activation on Click */

/**
 * Select object tree:
 * CTRL+LMB: Select/Deselect object and all children.
 * CTRL+SHIFT+LMB: Add/Remove object and all children.
 */
static void do_outliner_object_select_recursive(Scene *scene, Object *ob_parent, bool select)
{
	Base *base;

	for (base = FIRSTBASE; base; base = base->next) {
		Object *ob = base->object;
		if ((((ob->restrictflag & OB_RESTRICT_VIEW) == 0) && BKE_object_is_child_recursive(ob_parent, ob))) {
			ED_base_object_select(base, select ? BA_SELECT : BA_DESELECT);
		}
	}
}

static eOLDrawState tree_element_set_active_object(
        bContext *C, Scene *scene, SpaceOops *soops,
        TreeElement *te, const eOLSetState set, bool recursive)
{
	TreeStoreElem *tselem = TREESTORE(te);
	Scene *sce;
	Base *base;
	Object *ob = NULL;

	/* if id is not object, we search back */
	if (te->idcode == ID_OB) {
		ob = (Object *)tselem->id;
	}
	else {
		ob = (Object *)outliner_search_back(soops, te, ID_OB);
		if (ob == OBACT) {
			return OL_DRAWSEL_NONE;
		}
	}
	if (ob == NULL) {
		return OL_DRAWSEL_NONE;
	}

	sce = (Scene *)outliner_search_back(soops, te, ID_SCE);
	if (sce && scene != sce) {
		ED_screen_set_scene(C, CTX_wm_screen(C), sce);
		scene = sce;
	}

	/* find associated base in current scene */
	base = BKE_scene_base_find(scene, ob);

	if (base) {
		if (set == OL_SETSEL_EXTEND) {
			/* swap select */
			if (base->flag & SELECT)
				ED_base_object_select(base, BA_DESELECT);
			else
				ED_base_object_select(base, BA_SELECT);
		}
		else {
			/* deleselect all */
			BKE_scene_base_deselect_all(scene);
			ED_base_object_select(base, BA_SELECT);
		}

		if (recursive) {
			/* Recursive select/deselect for Object hierarchies */
			do_outliner_object_select_recursive(scene, ob, (ob->flag & SELECT) != 0);
		}

		if (C) {
			ED_base_object_activate(C, base); /* adds notifier */
			WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
		}
	}

	if (ob != scene->obedit)
		ED_object_editmode_exit(C, EM_FREEDATA | EM_WAITCURSOR);

	return OL_DRAWSEL_NORMAL;
}

static eOLDrawState tree_element_active_material(
        bContext *C, Scene *scene, SpaceOops *soops,
        TreeElement *te, const eOLSetState set)
{
	TreeElement *tes;
	Object *ob;

	/* we search for the object parent */
	ob = (Object *)outliner_search_back(soops, te, ID_OB);
	// note: ob->matbits can be NULL when a local object points to a library mesh.
	if (ob == NULL || ob != OBACT || ob->matbits == NULL) {
		return OL_DRAWSEL_NONE;  /* just paranoia */
	}

	/* searching in ob mat array? */
	tes = te->parent;
	if (tes->idcode == ID_OB) {
		if (set != OL_SETSEL_NONE) {
			ob->actcol = te->index + 1;
			ob->matbits[te->index] = 1;  // make ob material active too
		}
		else {
			if (ob->actcol == te->index + 1) {
				if (ob->matbits[te->index]) {
					return OL_DRAWSEL_NORMAL;
				}
			}
		}
	}
	/* or we search for obdata material */
	else {
		if (set != OL_SETSEL_NONE) {
			ob->actcol = te->index + 1;
			ob->matbits[te->index] = 0;  // make obdata material active too
		}
		else {
			if (ob->actcol == te->index + 1) {
				if (ob->matbits[te->index] == 0) {
					return OL_DRAWSEL_NORMAL;
				}
			}
		}
	}
	if (set != OL_SETSEL_NONE) {
		/* Tagging object for update seems a bit stupid here, but looks like we have to do it
		 * for render views to update. See T42973.
		 * Note that RNA material update does it too, see e.g. rna_MaterialSlot_update(). */
		WM_event_add_notifier(C, NC_MATERIAL | ND_SHADING_LINKS, NULL);
	}
	return OL_DRAWSEL_NONE;
}

static eOLDrawState tree_element_active_texture(
        bContext *C, Scene *scene, SpaceOops *UNUSED(soops),
        TreeElement *te, const eOLSetState set)
{
	TreeElement *tep;
	TreeStoreElem /* *tselem,*/ *tselemp;
	Object *ob = OBACT;
	SpaceButs *sbuts = NULL;

	if (ob == NULL) {
		/* no active object */
		return OL_DRAWSEL_NONE;
	}

	/*tselem = TREESTORE(te);*/ /*UNUSED*/

	/* find buttons region (note, this is undefined really still, needs recode in blender) */
	/* XXX removed finding sbuts */

	/* where is texture linked to? */
	tep = te->parent;
	tselemp = TREESTORE(tep);

	if (tep->idcode == ID_WO) {
		World *wrld = (World *)tselemp->id;

		if (set != OL_SETSEL_NONE) {
			if (sbuts) {
				// XXX sbuts->tabo = TAB_SHADING_TEX;	// hack from header_buttonswin.c
				// XXX sbuts->texfrom = 1;
			}
// XXX			extern_set_butspace(F6KEY, 0);	// force shading buttons texture
			wrld->texact = te->index;
		}
		else if (tselemp->id == (ID *)(scene->world)) {
			if (wrld->texact == te->index) {
				return OL_DRAWSEL_NORMAL;
			}
		}
	}
	else if (tep->idcode == ID_LA) {
		Lamp *la = (Lamp *)tselemp->id;
		if (set != OL_SETSEL_NONE) {
			if (sbuts) {
				// XXX sbuts->tabo = TAB_SHADING_TEX;	// hack from header_buttonswin.c
				// XXX sbuts->texfrom = 2;
			}
// XXX			extern_set_butspace(F6KEY, 0);	// force shading buttons texture
			la->texact = te->index;
		}
		else {
			if (tselemp->id == ob->data) {
				if (la->texact == te->index) {
					return OL_DRAWSEL_NORMAL;
				}
			}
		}
	}
	else if (tep->idcode == ID_MA) {
		Material *ma = (Material *)tselemp->id;
		if (set != OL_SETSEL_NONE) {
			if (sbuts) {
				//sbuts->tabo = TAB_SHADING_TEX;	// hack from header_buttonswin.c
				// XXX sbuts->texfrom = 0;
			}
// XXX			extern_set_butspace(F6KEY, 0);	// force shading buttons texture
			ma->texact = (char)te->index;

			/* also set active material */
			ob->actcol = tep->index + 1;
		}
		else if (tep->flag & TE_ACTIVE) {   // this is active material
			if (ma->texact == te->index) {
				return OL_DRAWSEL_NORMAL;
			}
		}
	}

	if (set != OL_SETSEL_NONE) {
		WM_event_add_notifier(C, NC_TEXTURE, NULL);
	}

	/* no active object */
	return OL_DRAWSEL_NONE;
}


static eOLDrawState tree_element_active_lamp(
        bContext *UNUSED(C), Scene *scene, SpaceOops *soops,
        TreeElement *te, const eOLSetState set)
{
	Object *ob;

	/* we search for the object parent */
	ob = (Object *)outliner_search_back(soops, te, ID_OB);
	if (ob == NULL || ob != OBACT) {
		/* just paranoia */
		return OL_DRAWSEL_NONE;
	}

	if (set != OL_SETSEL_NONE) {
// XXX		extern_set_butspace(F5KEY, 0);
	}
	else {
		return OL_DRAWSEL_NORMAL;
	}

	return OL_DRAWSEL_NONE;
}

static eOLDrawState tree_element_active_camera(
        bContext *UNUSED(C), Scene *scene, SpaceOops *soops,
        TreeElement *te, const eOLSetState set)
{
	Object *ob = (Object *)outliner_search_back(soops, te, ID_OB);

	if (set != OL_SETSEL_NONE) {
		return OL_DRAWSEL_NONE;
	}

	return scene->camera == ob;
}

static eOLDrawState tree_element_active_world(
        bContext *C, Scene *scene, SpaceOops *UNUSED(soops),
        TreeElement *te, const eOLSetState set)
{
	TreeElement *tep;
	TreeStoreElem *tselem = NULL;
	Scene *sce = NULL;

	tep = te->parent;
	if (tep) {
		tselem = TREESTORE(tep);
		if (tselem->type == 0)
			sce = (Scene *)tselem->id;
	}

	if (set != OL_SETSEL_NONE) {
		/* make new scene active */
		if (sce && scene != sce) {
			ED_screen_set_scene(C, CTX_wm_screen(C), sce);
		}
	}

	if (tep == NULL || tselem->id == (ID *)scene) {
		if (set != OL_SETSEL_NONE) {
// XXX			extern_set_butspace(F8KEY, 0);
		}
		else {
			return OL_DRAWSEL_NORMAL;
		}
	}
	return OL_DRAWSEL_NONE;
}

static eOLDrawState tree_element_active_defgroup(
        bContext *C, Scene *scene, TreeElement *te, TreeStoreElem *tselem, const eOLSetState set)
{
	Object *ob;

	/* id in tselem is object */
	ob = (Object *)tselem->id;
	if (set != OL_SETSEL_NONE) {
		BLI_assert(te->index + 1 >= 0);
		ob->actdef = te->index + 1;

		WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, ob);
	}
	else {
		if (ob == OBACT) {
			if (ob->actdef == te->index + 1) {
				return OL_DRAWSEL_NORMAL;
			}
		}
	}
	return OL_DRAWSEL_NONE;
}

static eOLDrawState tree_element_active_modifier(
        bContext *C, TreeElement *UNUSED(te), TreeStoreElem *tselem, const eOLSetState set)
{
	if (set != OL_SETSEL_NONE) {
		Object *ob = (Object *)tselem->id;

		WM_event_add_notifier(C, NC_OBJECT | ND_MODIFIER, ob);

// XXX		extern_set_butspace(F9KEY, 0);
	}

	return OL_DRAWSEL_NONE;
}

static eOLDrawState tree_element_active_text(
        bContext *UNUSED(C), Scene *UNUSED(scene), SpaceOops *UNUSED(soops),
        TreeElement *UNUSED(te), int UNUSED(set))
{
	// XXX removed
	return OL_DRAWSEL_NONE;
}



static eOLDrawState tree_element_active_keymap_item(
        bContext *UNUSED(C), TreeElement *te, TreeStoreElem *UNUSED(tselem), const eOLSetState set)
{
	wmKeyMapItem *kmi = te->directdata;

	if (set == OL_SETSEL_NONE) {
		if (kmi->flag & KMI_INACTIVE) {
			return OL_DRAWSEL_NONE;
		}
		return OL_DRAWSEL_NORMAL;
	}
	else {
		kmi->flag ^= KMI_INACTIVE;
	}
	return OL_DRAWSEL_NONE;
}

/* ---------------------------------------------- */

/* generic call for ID data check or make/check active in UI */
eOLDrawState tree_element_active(bContext *C, Scene *scene, SpaceOops *soops, TreeElement *te,
                                 const eOLSetState set, const bool handle_all_types)
{
	switch (te->idcode) {
		/* Note: ID_OB only if handle_all_type is true, else objects are handled specially to allow multiple
		 * selection. See do_outliner_item_activate_from_cursor. */
		case ID_OB:
			if (handle_all_types) {
				return tree_element_set_active_object(C, scene, soops, te, set, false);
			}
			break;
		case ID_MA:
			return tree_element_active_material(C, scene, soops, te, set);
		case ID_WO:
			return tree_element_active_world(C, scene, soops, te, set);
		case ID_LA:
			return tree_element_active_lamp(C, scene, soops, te, set);
		case ID_TE:
			return tree_element_active_texture(C, scene, soops, te, set);
		case ID_TXT:
			return tree_element_active_text(C, scene, soops, te, set);
		case ID_CA:
			return tree_element_active_camera(C, scene, soops, te, set);
	}
	return OL_DRAWSEL_NONE;
}

/**
 * Generic call for non-id data to make/check active in UI
 *
 * \note Context can be NULL when ``(set == OL_SETSEL_NONE)``
 */
eOLDrawState tree_element_type_active(
        bContext *C, Scene *scene, SpaceOops *soops,
        TreeElement *te, TreeStoreElem *tselem, const eOLSetState set, bool recursive)
{
	switch (tselem->type) {
		case TSE_DEFGROUP:
			return tree_element_active_defgroup(C, scene, te, tselem, set);
		case TSE_MODIFIER:
			return tree_element_active_modifier(C, te, tselem, set);
		case TSE_LINKED_OB:
			if (set != OL_SETSEL_NONE) {
				tree_element_set_active_object(C, scene, soops, te, set, false);
			}
			else if (tselem->id == (ID *)OBACT) {
				return OL_DRAWSEL_NORMAL;
			}
			break;
		case TSE_KEYMAP_ITEM:
			return tree_element_active_keymap_item(C, te, tselem, set);

	}
	return OL_DRAWSEL_NONE;
}

/* ================================================ */

/**
 * Action when clicking to activate an item (typically under the mouse cursor),
 * but don't do any cursor intersection checks.
 *
 * Needed to run from operators accessed from a menu.
 */
static void do_outliner_item_activate_tree_element(
        bContext *C, Scene *scene, SpaceOops *soops,
        TreeElement *te, TreeStoreElem *tselem,
        bool extend, bool recursive)
{
	/* always makes active object, except for some specific types.
	 * Note about TSE_EBONE: In case of a same ID_AR datablock shared among several objects, we do not want
	 * to switch out of edit mode (see T48328 for details). */
	{
		tree_element_set_active_object(
		        C, scene, soops, te,
		        (extend && tselem->type == 0) ? OL_SETSEL_EXTEND : OL_SETSEL_NORMAL,
		        recursive && tselem->type == 0);
	}

	if (tselem->type == 0) { // the lib blocks
		/* editmode? */
		if (te->idcode == ID_SCE) {
			if (scene != (Scene *)tselem->id) {
				ED_screen_set_scene(C, CTX_wm_screen(C), (Scene *)tselem->id);
			}
		}
		else if (te->idcode == ID_GR) {
			Group *gr = (Group *)tselem->id;
			GroupObject *gob;

			if (extend) {
				int sel = BA_SELECT;
				for (gob = gr->gobject.first; gob; gob = gob->next) {
					if (gob->ob->flag & SELECT) {
						sel = BA_DESELECT;
						break;
					}
				}

				for (gob = gr->gobject.first; gob; gob = gob->next) {
					ED_base_object_select(BKE_scene_base_find(scene, gob->ob), sel);
				}
			}
			else {
				BKE_scene_base_deselect_all(scene);

				for (gob = gr->gobject.first; gob; gob = gob->next) {
					if ((gob->ob->flag & SELECT) == 0)
						ED_base_object_select(BKE_scene_base_find(scene, gob->ob), BA_SELECT);
				}
			}

			WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
		}
		else if (OB_DATA_SUPPORT_EDITMODE(te->idcode)) {
			WM_operator_name_call(C, "OBJECT_OT_editmode_toggle", WM_OP_INVOKE_REGION_WIN, NULL);
		}
		else {  // rest of types
			tree_element_active(C, scene, soops, te, OL_SETSEL_NORMAL, false);
		}

	}
	else {
		tree_element_type_active(
		        C, scene, soops, te, tselem,
		        extend ? OL_SETSEL_EXTEND : OL_SETSEL_NORMAL,
		        recursive);
	}
}

/**
 * Activates tree items, also handles clicking on arrows.
 */
static bool do_outliner_item_activate_from_cursor(
        bContext *C, Scene *scene, ARegion *ar, SpaceOops *soops,
        TreeElement *te, bool extend, bool recursive, const float mval[2])
{
	if (mval[1] > te->ys && mval[1] < te->ys + UI_UNIT_Y) {
		TreeStoreElem *tselem = TREESTORE(te);
		bool openclose = false;

		/* open close icon */
		if ((te->flag & TE_ICONROW) == 0) {               // hidden icon, no open/close
			if (mval[0] > te->xs && mval[0] < te->xs + UI_UNIT_X)
				openclose = true;
		}

		if (openclose) {
			/* all below close/open? */
			if (extend) {
				tselem->flag &= ~TSE_CLOSED;
				outliner_flag_set(&te->subtree, TSE_CLOSED, !outliner_flag_is_any_test(&te->subtree, TSE_CLOSED, 1));
			}
			else {
				if (tselem->flag & TSE_CLOSED) tselem->flag &= ~TSE_CLOSED;
				else tselem->flag |= TSE_CLOSED;

			}

			return true;
		}
		/* name and first icon */
		else if (mval[0] > te->xs + UI_UNIT_X && mval[0] < te->xend) {
			do_outliner_item_activate_tree_element(
			        C, scene, soops,
			        te, tselem,
			        extend, recursive);
			return true;
		}
	}

	for (te = te->subtree.first; te; te = te->next) {
		if (do_outliner_item_activate_from_cursor(C, scene, ar, soops, te, extend, recursive, mval)) {
			return true;
		}
	}
	return false;
}

/**
 * A version of #outliner_item_do_acticate_from_cursor that takes the tree element directly.
 * and doesn't depend on the pointer position.
 *
 * This allows us to simulate clicking on an item without dealing with the mouse cursor.
 */
void outliner_item_do_activate_from_tree_element(
        bContext *C, TreeElement *te, TreeStoreElem *tselem,
        bool extend, bool recursive)
{
	Scene *scene = CTX_data_scene(C);
	SpaceOops *soops = CTX_wm_space_outliner(C);

	do_outliner_item_activate_tree_element(
	        C, scene, soops,
	        te, tselem,
	        extend, recursive);
}

/**
 * Action to run when clicking in the outliner,
 *
 * May expend/collapse branches or activate items.
 * */
int outliner_item_do_activate_from_cursor(
        bContext *C, const int mval[2],
        bool extend, bool recursive)
{
	Scene *scene = CTX_data_scene(C);
	ARegion *ar = CTX_wm_region(C);
	SpaceOops *soops = CTX_wm_space_outliner(C);
	TreeElement *te;
	float fmval[2];

	UI_view2d_region_to_view(&ar->v2d, mval[0], mval[1], &fmval[0], &fmval[1]);

	if (!ELEM(soops->outlinevis, SO_DATABLOCKS, SO_USERDEF) &&
	    !(soops->flag & SO_HIDE_RESTRICTCOLS) &&
	    (fmval[0] > ar->v2d.cur.xmax - OL_TOG_RESTRICT_VIEWX))
	{
		return OPERATOR_CANCELLED;
	}

	for (te = soops->tree.first; te; te = te->next) {
		if (do_outliner_item_activate_from_cursor(C, scene, ar, soops, te, extend, recursive, fmval)) {
			break;
		}
	}

	if (te) {
		ED_undo_push(C, "Outliner click event");
	}
	else {
		short selecting = -1;
		int row;

		/* get row number - 100 here is just a dummy value since we don't need the column */
		UI_view2d_listview_view_to_cell(&ar->v2d, 1000, UI_UNIT_Y, 0.0f, OL_Y_OFFSET,
		                                fmval[0], fmval[1], NULL, &row);

		/* select relevant row */
		if (outliner_select(soops, &soops->tree, &row, &selecting)) {

			soops->storeflag |= SO_TREESTORE_REDRAW;

			/* no need for undo push here, only changing outliner data which is
			 * scene level - campbell */
			/* ED_undo_push(C, "Outliner selection event"); */
		}
	}

	ED_region_tag_redraw(ar);

	return OPERATOR_FINISHED;
}

/* event can enterkey, then it opens/closes */
static int outliner_item_activate(bContext *C, wmOperator *op, const wmEvent *event)
{
	bool extend    = RNA_boolean_get(op->ptr, "extend");
	bool recursive = RNA_boolean_get(op->ptr, "recursive");
	return outliner_item_do_activate_from_cursor(C, event->mval, extend, recursive);
}

void OUTLINER_OT_item_activate(wmOperatorType *ot)
{
	ot->name = "Activate Item";
	ot->idname = "OUTLINER_OT_item_activate";
	ot->description = "Handle mouse clicks to activate/select items";

	ot->invoke = outliner_item_activate;

	ot->poll = ED_operator_outliner_active;

	RNA_def_boolean(ot->srna, "extend", true, "Extend", "Extend selection for activation");
	RNA_def_boolean(ot->srna, "recursive", false, "Recursive", "Select Objects and their children");
}

/* ****************************************************** */

/* **************** Border Select Tool ****************** */
static void outliner_item_border_select(Scene *scene, rctf *rectf, TreeElement *te, bool select)
{
	TreeStoreElem *tselem = TREESTORE(te);

	if (te->ys <= rectf->ymax && te->ys + UI_UNIT_Y >= rectf->ymin) {
		if (select) {
			tselem->flag |= TSE_SELECTED;
		}
		else {
			tselem->flag &= ~TSE_SELECTED;
		}
	}

	/* Look at its children. */
	if ((tselem->flag & TSE_CLOSED) == 0) {
		for (te = te->subtree.first; te; te = te->next) {
			outliner_item_border_select(scene, rectf, te, select);
		}
	}
}

static int outliner_border_select_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	SpaceOops *soops = CTX_wm_space_outliner(C);
	ARegion *ar = CTX_wm_region(C);
	TreeElement *te;
	rctf rectf;
	bool select = !RNA_boolean_get(op->ptr, "deselect");

	WM_operator_properties_border_to_rctf(op, &rectf);
	UI_view2d_region_to_view_rctf(&ar->v2d, &rectf, &rectf);

	for (te = soops->tree.first; te; te = te->next) {
		outliner_item_border_select(scene, &rectf, te, select);
	}

	WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
	ED_region_tag_redraw(ar);

	return OPERATOR_FINISHED;
}

void OUTLINER_OT_select_border(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Border Select";
	ot->idname = "OUTLINER_OT_select_border";
	ot->description = "Use box selection to select tree elements";

	/* api callbacks */
	ot->invoke = WM_gesture_border_invoke;
	ot->exec = outliner_border_select_exec;
	ot->modal = WM_gesture_border_modal;
	ot->cancel = WM_gesture_border_cancel;

	ot->poll = ED_operator_outliner_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* rna */
	WM_operator_properties_gesture_border_ex(ot, true, false);
}

/* ****************************************************** */
