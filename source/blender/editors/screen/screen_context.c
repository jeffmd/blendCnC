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
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/editors/screen/screen_context.c
 *  \ingroup edscr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_utildefines.h"


#include "BKE_context.h"
#include "BKE_object.h"
#include "BKE_screen.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "UI_interface.h"

#include "screen_intern.h"

static unsigned int context_layers(bScreen *sc, Scene *scene, ScrArea *sa_ctx)
{
	/* needed for 'USE_ALLSELECT' define, otherwise we end up editing off-screen layers. */
	if (sc && sa_ctx && (sa_ctx->spacetype == SPACE_BUTS)) {
		const unsigned int lay = BKE_screen_view3d_layer_all(sc);
		if (lay) {
			return lay;
		}
	}
	return scene->lay;
}

const char *screen_context_dir[] = {
	"scene", "visible_objects", "visible_bases", "selectable_objects", "selectable_bases",
	"selected_objects", "selected_bases",
	"editable_objects", "editable_bases",
	"selected_editable_objects", "selected_editable_bases",
	"active_base", "active_object", "object", "edit_object",
	NULL};

int ed_screen_context(const bContext *C, const char *member, bContextDataResult *result)
{
	bScreen *sc = CTX_wm_screen(C);
	ScrArea *sa = CTX_wm_area(C);
	Scene *scene = sc->scene;
	Base *base;

#if 0  /* Using the context breaks adding objects in the UI. Need to find out why - campbell */
	Object *obact = CTX_data_active_object(C);
	Object *obedit = CTX_data_edit_object(C);
	base = CTX_data_active_base(C);
#else
	Object *obedit = scene->obedit;
	Object *obact = OBACT;
	base = BASACT;
#endif

	if (CTX_data_dir(member)) {
		CTX_data_dir_set(result, screen_context_dir);
		return 1;
	}
	else if (CTX_data_equals(member, "scene")) {
		CTX_data_id_pointer_set(result, &scene->id);
		return 1;
	}
	else if (CTX_data_equals(member, "visible_objects") || CTX_data_equals(member, "visible_bases")) {
		const unsigned int lay = context_layers(sc, scene, sa);
		const bool visible_objects = CTX_data_equals(member, "visible_objects");

		for (base = scene->base.first; base; base = base->next) {
			if (((base->object->restrictflag & OB_RESTRICT_VIEW) == 0) && (base->lay & lay)) {
				if (visible_objects)
					CTX_data_id_list_add(result, &base->object->id);
				else
					CTX_data_list_add(result, &scene->id, &RNA_ObjectBase, base);
			}
		}
		CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
		return 1;
	}
	else if (CTX_data_equals(member, "selectable_objects") || CTX_data_equals(member, "selectable_bases")) {
		const unsigned int lay = context_layers(sc, scene, sa);
		const bool selectable_objects = CTX_data_equals(member, "selectable_objects");

		for (base = scene->base.first; base; base = base->next) {
			if (base->lay & lay) {
				if ((base->object->restrictflag & OB_RESTRICT_VIEW) == 0 && (base->object->restrictflag & OB_RESTRICT_SELECT) == 0) {
					if (selectable_objects)
						CTX_data_id_list_add(result, &base->object->id);
					else
						CTX_data_list_add(result, &scene->id, &RNA_ObjectBase, base);
				}
			}
		}
		CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
		return 1;
	}
	else if (CTX_data_equals(member, "selected_objects") || CTX_data_equals(member, "selected_bases")) {
		const unsigned int lay = context_layers(sc, scene, sa);
		const bool selected_objects = CTX_data_equals(member, "selected_objects");

		for (base = scene->base.first; base; base = base->next) {
			if ((base->flag & SELECT) && (base->lay & lay)) {
				if (selected_objects)
					CTX_data_id_list_add(result, &base->object->id);
				else
					CTX_data_list_add(result, &scene->id, &RNA_ObjectBase, base);
			}
		}
		CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
		return 1;
	}
	else if (CTX_data_equals(member, "selected_editable_objects") || CTX_data_equals(member, "selected_editable_bases")) {
		const unsigned int lay = context_layers(sc, scene, sa);
		const bool selected_editable_objects = CTX_data_equals(member, "selected_editable_objects");

		for (base = scene->base.first; base; base = base->next) {
			if ((base->flag & SELECT) && (base->lay & lay)) {
				if ((base->object->restrictflag & OB_RESTRICT_VIEW) == 0) {
					if (0 == BKE_object_is_libdata(base->object)) {
						if (selected_editable_objects)
							CTX_data_id_list_add(result, &base->object->id);
						else
							CTX_data_list_add(result, &scene->id, &RNA_ObjectBase, base);
					}
				}
			}
		}
		CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
		return 1;
	}
	else if (CTX_data_equals(member, "editable_objects") || CTX_data_equals(member, "editable_bases")) {
		const unsigned int lay = context_layers(sc, scene, sa);
		const bool editable_objects = CTX_data_equals(member, "editable_objects");

		/* Visible + Editable, but not necessarily selected */
		for (base = scene->base.first; base; base = base->next) {
			if (((base->object->restrictflag & OB_RESTRICT_VIEW) == 0) && (base->lay & lay)) {
				if (0 == BKE_object_is_libdata(base->object)) {
					if (editable_objects)
						CTX_data_id_list_add(result, &base->object->id);
					else
						CTX_data_list_add(result, &scene->id, &RNA_ObjectBase, base);
				}
			}
		}
		CTX_data_type_set(result, CTX_DATA_TYPE_COLLECTION);
		return 1;
	}

	else if (CTX_data_equals(member, "active_base")) {
		if (base)
			CTX_data_pointer_set(result, &scene->id, &RNA_ObjectBase, base);

		return 1;
	}
	else if (CTX_data_equals(member, "active_object")) {
		if (obact)
			CTX_data_id_pointer_set(result, &obact->id);

		return 1;
	}
	else if (CTX_data_equals(member, "object")) {
		if (obact)
			CTX_data_id_pointer_set(result, &obact->id);

		return 1;
	}
	else if (CTX_data_equals(member, "edit_object")) {
		/* convenience for now, 1 object per scene in editmode */
		if (obedit)
			CTX_data_id_pointer_set(result, &obedit->id);

		return 1;
	}
	else if (CTX_data_equals(member, "active_operator")) {
		wmOperator *op = NULL;

		SpaceFile *sfile = CTX_wm_space_file(C);
		if (sfile) {
			op = sfile->op;
		}
		else if ((op = UI_context_active_operator_get(C))) {
			/* do nothing */
		}
		else {
			/* note, this checks poll, could be a problem, but this also
			 * happens for the toolbar */
			op = WM_operator_last_redo(C);
		}
		/* TODO, get the operator from popup's */

		if (op && op->ptr) {
			CTX_data_pointer_set(result, NULL, &RNA_Operator, op);
			return 1;
		}
	}
	else {
		return 0; /* not found */
	}

	return -1; /* found but not available */
}
