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

/** \file blender/python/intern/bpy_app_build_options.c
 *  \ingroup pythonintern
 */

#include <Python.h>

#include "BLI_utildefines.h"

#include "bpy_app_build_options.h"

static PyTypeObject BlenderAppBuildOptionsType;

static PyStructSequence_Field app_builtopts_info_fields[] = {
	/* names mostly follow CMake options, lowercase, after WITH_ */
	{(char *)"bullet", NULL},
	{(char *)"image_cineon", NULL},
	{(char *)"image_dds", NULL},
	{(char *)"image_hdr", NULL},
	{(char *)"image_openexr", NULL},
	{(char *)"image_openjpeg", NULL},
	{(char *)"image_tiff", NULL},
	{(char *)"international", NULL},
	{(char *)"mod_remesh", NULL},
	{(char *)"openmp", NULL},
	{NULL}
};


static PyStructSequence_Desc app_builtopts_info_desc = {
	(char *)"bpy.app.build_options",     /* name */
	(char *)"This module contains information about options blender is built with",    /* doc */
	app_builtopts_info_fields,    /* fields */
	ARRAY_SIZE(app_builtopts_info_fields) - 1
};

static PyObject *make_builtopts_info(void)
{
	PyObject *builtopts_info;
	int pos = 0;

	builtopts_info = PyStructSequence_New(&BlenderAppBuildOptionsType);
	if (builtopts_info == NULL) {
		return NULL;
	}

#define SetObjIncref(item) \
	PyStructSequence_SET_ITEM(builtopts_info, pos++, (Py_IncRef(item), item))

#ifdef WITH_BULLET
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_CINEON
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_DDS
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_HDR
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_OPENEXR
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_OPENJPEG
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_TIFF
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_INTERNATIONAL
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef WITH_MOD_REMESH
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#ifdef _OPENMP
	SetObjIncref(Py_True);
#else
	SetObjIncref(Py_False);
#endif

#undef SetObjIncref

	return builtopts_info;
}

PyObject *BPY_app_build_options_struct(void)
{
	PyObject *ret;

	PyStructSequence_InitType(&BlenderAppBuildOptionsType, &app_builtopts_info_desc);

	ret = make_builtopts_info();

	/* prevent user from creating new instances */
	BlenderAppBuildOptionsType.tp_init = NULL;
	BlenderAppBuildOptionsType.tp_new = NULL;
	BlenderAppBuildOptionsType.tp_hash = (hashfunc)_Py_HashPointer; /* without this we can't do set(sys.modules) [#29635] */

	return ret;
}
