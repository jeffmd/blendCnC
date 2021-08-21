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
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/editors/physics/physics_ops.c
 *  \ingroup edphys
 */

#include <stdlib.h>

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_physics.h"
#include "ED_object.h"

#include "physics_intern.h" // own include


/***************************** particles ***********************************/

static void operatortypes_particle(void)
{

	WM_operatortype_append(RIGIDBODY_OT_object_add);
	WM_operatortype_append(RIGIDBODY_OT_object_remove);

	WM_operatortype_append(RIGIDBODY_OT_objects_add);
	WM_operatortype_append(RIGIDBODY_OT_objects_remove);

	WM_operatortype_append(RIGIDBODY_OT_shape_change);
	WM_operatortype_append(RIGIDBODY_OT_mass_calculate);

	WM_operatortype_append(RIGIDBODY_OT_constraint_add);
	WM_operatortype_append(RIGIDBODY_OT_constraint_remove);

	WM_operatortype_append(RIGIDBODY_OT_world_add);
	WM_operatortype_append(RIGIDBODY_OT_world_remove);
//	WM_operatortype_append(RIGIDBODY_OT_world_export);
}

static void keymap_particle(wmKeyConfig *keyconf)
{
	wmKeyMapItem *kmi;
	wmKeyMap *keymap;

	keymap = WM_keymap_ensure(keyconf, "Particle", 0, 0);
	keymap->poll = NULL;

	/* Shift+LMB behavior first, so it has priority over KM_ANY item below. */
	kmi = WM_keymap_add_item(keymap, "VIEW3D_OT_manipulator", LEFTMOUSE, KM_PRESS, KM_SHIFT, 0);
	RNA_boolean_set(kmi->ptr, "release_confirm", true);
	RNA_boolean_set(kmi->ptr, "use_planar_constraint", true);
	RNA_boolean_set(kmi->ptr, "use_accurate", false);

	kmi = WM_keymap_add_item(keymap, "VIEW3D_OT_manipulator", LEFTMOUSE, KM_PRESS, KM_SHIFT, 0);
	RNA_boolean_set(kmi->ptr, "release_confirm", true);
	RNA_boolean_set(kmi->ptr, "use_planar_constraint", false);
	RNA_boolean_set(kmi->ptr, "use_accurate", true);

	/* Using KM_ANY here to allow holding modifiers before starting to transform. */
	kmi = WM_keymap_add_item(keymap, "VIEW3D_OT_manipulator", LEFTMOUSE, KM_PRESS, KM_ANY, 0);
	RNA_boolean_set(kmi->ptr, "release_confirm", true);
	RNA_boolean_set(kmi->ptr, "use_planar_constraint", false);
	RNA_boolean_set(kmi->ptr, "use_accurate", false);

	/* size radial control */
	kmi = WM_keymap_add_item(keymap, "WM_OT_radial_control", FKEY, KM_PRESS, 0, 0);
	RNA_string_set(kmi->ptr, "data_path_primary", "tool_settings.particle_edit.brush.size");

	/* size radial control */
	kmi = WM_keymap_add_item(keymap, "WM_OT_radial_control", FKEY, KM_PRESS, KM_SHIFT, 0);
	RNA_string_set(kmi->ptr, "data_path_primary", "tool_settings.particle_edit.brush.strength");

	WM_keymap_add_menu(keymap, "VIEW3D_MT_particle_specials", WKEY, KM_PRESS, 0, 0);

	ED_keymap_proportional_cycle(keyconf, keymap);
	ED_keymap_proportional_editmode(keyconf, keymap, false);
}

/****************************** general ************************************/

void ED_operatortypes_physics(void)
{
	operatortypes_particle();
}

void ED_keymap_physics(wmKeyConfig *keyconf)
{
	keymap_particle(keyconf);
}
