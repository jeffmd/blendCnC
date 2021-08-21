# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# The Original Code is Copyright (C) 2006, Blender Foundation
# All rights reserved.
#
# The Original Code is: all of this file.
#
# Contributor(s): Jacques Beaurainm, Campbell Barton
#
# ***** END GPL LICENSE BLOCK *****

set(INC
	..
	../../blenkernel
	../../blenlib
	../../blenloader
	../../blentranslation
	../../editors/include
	../../gpu
	../../imbuf
	../../makesdna
	../../makesrna
	../../windowmanager
	../../../../intern/clog
	../../../../intern/guardedalloc
	../../../../intern/opencolorio
)

set(INC_SYS
	${PYTHON_INCLUDE_DIRS}
)

set(SRC
	bpy.c
	bpy_app.c
	bpy_app_build_options.c
	bpy_app_handlers.c
	bpy_app_ocio.c
	bpy_app_oiio.c
	bpy_app_opensubdiv.c
	bpy_app_translations.c
	bpy_capi_utils.c
	bpy_driver.c
	bpy_interface.c
	bpy_interface_atexit.c
	bpy_intern_string.c
	bpy_library_load.c
	bpy_library_write.c
	bpy_operator.c
	bpy_operator_wrap.c
	bpy_path.c
	bpy_props.c
	bpy_rna.c
	bpy_rna_array.c
	bpy_rna_callback.c
	bpy_rna_driver.c
	bpy_rna_id_collection.c
	bpy_traceback.c
	bpy_utils_previews.c
	bpy_utils_units.c
	gpu.c
	gpu_offscreen.c
	stubs.c

	bpy.h
	bpy_app.h
	bpy_app_build_options.h
	bpy_app_handlers.h
	bpy_app_ocio.h
	bpy_app_oiio.h
	bpy_app_opensubdiv.h
	bpy_app_translations.h
	bpy_capi_utils.h
	bpy_driver.h
	bpy_intern_string.h
	bpy_library.h
	bpy_operator.h
	bpy_operator_wrap.h
	bpy_path.h
	bpy_props.h
	bpy_rna.h
	bpy_rna_callback.h
	bpy_rna_driver.h
	bpy_rna_id_collection.h
	bpy_traceback.h
	bpy_utils_previews.h
	bpy_utils_units.h
	gpu.h
	../BPY_extern.h
	../BPY_extern_clog.h
)

# only to check if buildinfo is available
if(WITH_BUILDINFO)
	add_definitions(-DBUILD_DATE)
endif()

if(WITH_PYTHON_MODULE)
	add_definitions(-DWITH_PYTHON_MODULE)
endif()

if(WITH_PYTHON_SAFETY)
	add_definitions(-DWITH_PYTHON_SAFETY)
endif()

if(WITH_BULLET)
	add_definitions(-DWITH_BULLET)
endif()

if(WITH_IMAGE_CINEON)
	add_definitions(-DWITH_CINEON)
endif()

if(WITH_IMAGE_DDS)
	add_definitions(-DWITH_DDS)
endif()

if(WITH_IMAGE_HDR)
	add_definitions(-DWITH_HDR)
endif()

if(WITH_IMAGE_OPENEXR)
	add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_OPENIMAGEIO)
	add_definitions(-DWITH_OPENIMAGEIO)
endif()

if(WITH_IMAGE_OPENJPEG)
	add_definitions(-DWITH_OPENJPEG)
endif()

if(WITH_IMAGE_TIFF)
	add_definitions(-DWITH_TIFF)
endif()

if(WITH_INPUT_NDOF)
	add_definitions(-DWITH_INPUT_NDOF)
endif()

if(WITH_INTERNATIONAL)
	add_definitions(-DWITH_INTERNATIONAL)
endif()

if(WITH_MOD_REMESH)
	add_definitions(-DWITH_MOD_REMESH)
endif()

if(WITH_OPENCOLORIO)
	add_definitions(-DWITH_OCIO)
endif()

if(WITH_OPENIMAGEIO)
	add_definitions(-DWITH_OPENIMAGEIO)
	list(APPEND INC
		../../imbuf/intern/oiio
	)
endif()

add_definitions(${GL_DEFINITIONS})

blender_add_lib(bf_python "${SRC}" "${INC}" "${INC_SYS}")
