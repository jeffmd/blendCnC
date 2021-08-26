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

/** \file blender/windowmanager/intern/wm_keymap_utils.c
 *  \ingroup wm
 *
 * Utilities to help define keymaps.
 */

#include <string.h>

#include "DNA_object_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_utildefines.h"

#include "BKE_context.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

/* menu wrapper for WM_keymap_add_item */

/* -------------------------------------------------------------------- */
/** \name Wrappers for #WM_keymap_add_item
 * \{ */

wmKeyMapItem *WM_keymap_add_menu(wmKeyMap *keymap, const char *idname, int type, int val, int modifier, int keymodifier)
{
	wmKeyMapItem *kmi = WM_keymap_add_item(keymap, "WM_OT_call_menu", type, val, modifier, keymodifier);
	RNA_string_set(kmi->ptr, "name", idname);
	return kmi;
}

wmKeyMapItem *WM_keymap_add_menu_pie(wmKeyMap *keymap, const char *idname, int type, int val, int modifier, int keymodifier)
{
	wmKeyMapItem *kmi = WM_keymap_add_item(keymap, "WM_OT_call_menu_pie", type, val, modifier, keymodifier);
	RNA_string_set(kmi->ptr, "name", idname);
	return kmi;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Introspection
 * \{ */

/* Guess an appropriate keymap from the operator name */
/* Needs to be kept up to date with Keymap and Operator naming */
wmKeyMap *WM_keymap_guess_opname(const bContext *C, const char *opname)
{
	/* Op types purposely skipped  for now:
	 *     BRUSH_OT
	 *     BOID_OT
	 *     BUTTONS_OT
	 *     PAINT_OT
	 *     ED_OT
	 *     TEXTURE_OT
	 *     UI_OT
	 *     VIEW2D_OT
	 *     WORLD_OT
	 */

	wmKeyMap *km = NULL;
	SpaceLink *sl = CTX_wm_space_data(C);

	/* Window */
	if (STRPREFIX(opname, "WM_OT")) {
		km = WM_keymap_find_all(C, "Window", 0, 0);
	}
	/* Screen & Render */
	else if (STRPREFIX(opname, "SCREEN_OT") ||
	         STRPREFIX(opname, "RENDER_OT") ||
	         STRPREFIX(opname, "SCENE_OT"))
	{
		km = WM_keymap_find_all(C, "Screen", 0, 0);
	}
	/* Import/Export*/
	else if (STRPREFIX(opname, "IMPORT_") ||
	         STRPREFIX(opname, "EXPORT_"))
	{
		km = WM_keymap_find_all(C, "Window", 0, 0);
	}


	/* 3D View */
	else if (STRPREFIX(opname, "VIEW3D_OT")) {
		km = WM_keymap_find_all(C, "3D View", sl->spacetype, 0);
	}
	else if (STRPREFIX(opname, "OBJECT_OT")) {
		/* exception, this needs to work outside object mode too */
		if (STRPREFIX(opname, "OBJECT_OT_mode_set"))
			km = WM_keymap_find_all(C, "Object Non-modal", 0, 0);
		else
			km = WM_keymap_find_all(C, "Object Mode", 0, 0);
	}
	/* Object mode related */
	else if (STRPREFIX(opname, "GROUP_OT") ||
	         STRPREFIX(opname, "MATERIAL_OT") ||
	         STRPREFIX(opname, "RIGIDBODY_OT"))
	{
		km = WM_keymap_find_all(C, "Object Mode", 0, 0);
	}

	/* Editing Modes */
	else if (STRPREFIX(opname, "MESH_OT")) {
		km = WM_keymap_find_all(C, "Mesh", 0, 0);

		/* some mesh operators are active in object mode too, like add-prim */
		if (km && !WM_keymap_poll((bContext *)C, km)) {
			km = WM_keymap_find_all(C, "Object Mode", 0, 0);
		}
	}
	else if (STRPREFIX(opname, "CURVE_OT") ||
	         STRPREFIX(opname, "SURFACE_OT"))
	{
		km = WM_keymap_find_all(C, "Curve", 0, 0);

		/* some curve operators are active in object mode too, like add-prim */
		if (km && !WM_keymap_poll((bContext *)C, km)) {
			km = WM_keymap_find_all(C, "Object Mode", 0, 0);
		}
	}
	else if (STRPREFIX(opname, "FONT_OT")) {
		km = WM_keymap_find_all(C, "Font", 0, 0);
	}
	/* Image Editor */
	else if (STRPREFIX(opname, "IMAGE_OT")) {
		km = WM_keymap_find_all(C, "Image", sl->spacetype, 0);
	}
	/* Script */
	else if (STRPREFIX(opname, "SCRIPT_OT")) {
		km = WM_keymap_find_all(C, "Script", sl->spacetype, 0);
	}
	/* Text */
	else if (STRPREFIX(opname, "TEXT_OT")) {
		km = WM_keymap_find_all(C, "Text", sl->spacetype, 0);
	}
	/* Console */
	else if (STRPREFIX(opname, "CONSOLE_OT")) {
		km = WM_keymap_find_all(C, "Console", sl->spacetype, 0);
	}
	/* Console */
	else if (STRPREFIX(opname, "INFO_OT")) {
		km = WM_keymap_find_all(C, "Info", sl->spacetype, 0);
	}
	/* File browser */
	else if (STRPREFIX(opname, "FILE_OT")) {
		km = WM_keymap_find_all(C, "File Browser", sl->spacetype, 0);
	}
	/* Outliner */
	else if (STRPREFIX(opname, "OUTLINER_OT")) {
		km = WM_keymap_find_all(C, "Outliner", sl->spacetype, 0);
	}
	/* Transform */
	else if (STRPREFIX(opname, "TRANSFORM_OT")) {
		/* check for relevant editor */
		switch (sl->spacetype) {
			case SPACE_VIEW3D:
				km = WM_keymap_find_all(C, "3D View", sl->spacetype, 0);
				break;
			case SPACE_IMAGE:
				km = WM_keymap_find_all(C, "UV Editor", 0, 0);
				break;
		}
	}

	return km;
}

/** \} */
