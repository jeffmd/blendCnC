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

/** \file blender/python/intern/bpy_rna_driver.c
 *  \ingroup pythonintern
 *
 * This file defines utility functions that use the RNA API, from PyDrivers.
 */

#include <Python.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "RNA_access.h"

#include "bpy_rna.h"
#include "bpy_rna_driver.h"  /* own include */


/**
 * A version of #driver_get_variable_value which returns a PyObject.
 */
PyObject *pyrna_driver_get_variable_value(
        struct ChannelDriver *driver, struct DriverTarget *dtar)
{
	PyObject *driver_arg = NULL;

	{
		/* can't resolve path, pass */
	}

	return driver_arg;
}

PyObject *pyrna_driver_self_from_anim_rna(PathResolvedRNA *anim_rna)
{
	return pyrna_struct_CreatePyObject(&anim_rna->ptr);
}

bool pyrna_driver_is_equal_anim_rna(const PathResolvedRNA *anim_rna, const PyObject *py_anim_rna)
{
	if (BPy_StructRNA_Check(py_anim_rna)) {
		const PointerRNA *ptr_a = &anim_rna->ptr;
		const PointerRNA *ptr_b = &(((const BPy_StructRNA *)py_anim_rna)->ptr);

		if ((ptr_a->id.data == ptr_b->id.data) &&
		    (ptr_a->type == ptr_b->type) &&
		    (ptr_a->data == ptr_b->data))
		{
			return true;
		}
	}
	return false;
}
