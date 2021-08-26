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
# Contributor(s): Jacques Beaurain.
#
# ***** END GPL LICENSE BLOCK *****

set(INC
	.
	../blenfont
	../blenlib
	../blenloader
	../blentranslation
	../gpu
	../imbuf
	../makesdna
	../makesrna
	../bmesh
	../modifiers
	../physics
	../render/extern/include
	../../../intern/ghost
	../../../intern/guardedalloc
	../../../intern/glew-mx
	../../../intern/memutil
	../../../intern/atomic
	../../../intern/clog
	../../../intern/mikktspace
	../../../extern/curve_fit_nd
)

set(INC_SYS
	${GLEW_INCLUDE_PATH}
	${ZLIB_INCLUDE_DIRS}
)

set(SRC
	intern/CCGSubSurf.c
	intern/CCGSubSurf_legacy.c
	intern/CCGSubSurf_util.c
	intern/DerivedMesh.c
	intern/addon.c
	intern/appdir.c
	intern/autoexec.c
	intern/blender.c
	intern/blender_copybuffer.c
	intern/blender_undo.c
	intern/blendfile.c
	intern/bmfont.c
	intern/bpath.c
	intern/bullet.c
	intern/bvhutils.c
	intern/cachefile.c
	intern/camera.c
	intern/cdderivedmesh.c
	intern/collision.c
	intern/colorband.c
	intern/colortools.c
	intern/context.c
	intern/crazyspace.c
	intern/curve.c
	intern/curve_decimate.c
	intern/customdata.c
	intern/customdata_file.c
	intern/data_transfer.c
	intern/deform.c
	intern/displist.c
	intern/editderivedmesh.c
	intern/editmesh.c
	intern/editmesh_bvh.c
	intern/font.c
	intern/group.c
	intern/icons.c
	intern/idcode.c
	intern/idprop.c
	intern/idprop_utils.c
	intern/image.c
	intern/image_gen.c
	intern/lamp.c
	intern/library.c
	intern/library_idmap.c
	intern/library_query.c
	intern/library_remap.c
	intern/material.c
	intern/mesh.c
	intern/mesh_convert.c
	intern/mesh_evaluate.c
	intern/mesh_mapping.c
	intern/mesh_remap.c
	intern/mesh_validate.c
	intern/modifier.c
	intern/modifiers_bmesh.c
	intern/object.c
	intern/object_deform.c
	intern/object_update.c
	intern/outliner_treehash.c
	intern/packedFile.c
	intern/pbvh.c
	intern/pbvh_bmesh.c
	intern/report.c
	intern/rigidbody.c
	intern/scene.c
	intern/screen.c
	intern/shrinkwrap.c
	intern/sketch.c
	intern/subsurf_ccg.c
	intern/suggestions.c
	intern/text.c
	intern/texture.c
	intern/undo_system.c
	intern/unit.c
	intern/world.c

	BKE_DerivedMesh.h
	BKE_addon.h
	BKE_appdir.h
	BKE_autoexec.h
	BKE_blender.h
	BKE_blender_copybuffer.h
	BKE_blender_undo.h
	BKE_blender_version.h
	BKE_blendfile.h
	BKE_bmfont.h
	BKE_bmfont_types.h
	BKE_bpath.h
	BKE_bullet.h
	BKE_bvhutils.h
	BKE_cachefile.h
	BKE_camera.h
	BKE_ccg.h
	BKE_cdderivedmesh.h
	BKE_collision.h
	BKE_colorband.h
	BKE_colortools.h
	BKE_context.h
	BKE_crazyspace.h
	BKE_curve.h
	BKE_customdata.h
	BKE_customdata_file.h
	BKE_data_transfer.h
	BKE_deform.h
	BKE_displist.h
	BKE_editmesh.h
	BKE_editmesh_bvh.h
	BKE_font.h
	BKE_global.h
	BKE_group.h
	BKE_icons.h
	BKE_idcode.h
	BKE_idprop.h
	BKE_image.h
	BKE_lamp.h
	BKE_library.h
	BKE_library_idmap.h
	BKE_library_query.h
	BKE_library_remap.h
	BKE_main.h
	BKE_material.h
	BKE_mesh.h
	BKE_mesh_mapping.h
	BKE_mesh_remap.h
	BKE_modifier.h
	BKE_object.h
	BKE_object_deform.h
	BKE_outliner_treehash.h
	BKE_packedFile.h
	BKE_pbvh.h
	BKE_report.h
	BKE_rigidbody.h
	BKE_scene.h
	BKE_screen.h
	BKE_shrinkwrap.h
	BKE_sketch.h
	BKE_subsurf.h
	BKE_suggestions.h
	BKE_text.h
	BKE_texture.h
	BKE_undo_system.h
	BKE_unit.h
	BKE_world.h

	intern/CCGSubSurf.h
	intern/CCGSubSurf_inline.h
	intern/CCGSubSurf_intern.h
	intern/data_transfer_intern.h
	intern/pbvh_intern.h
)

if(WITH_BINRELOC)
	list(APPEND INC_SYS
		${BINRELOC_INCLUDE_DIRS}
	)
	add_definitions(-DWITH_BINRELOC)
endif()

add_definitions(${GL_DEFINITIONS})

if(WIN32)
	list(APPEND INC
		../../../intern/utfconv
	)
endif()

if(WITH_BULLET)
	list(APPEND INC_SYS
		${BULLET_INCLUDE_DIRS}
	)
	list(APPEND INC
		../../../intern/rigidbody
	)
	add_definitions(-DWITH_BULLET)
endif()

if(WITH_IMAGE_OPENEXR)
	add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_IMAGE_TIFF)
	add_definitions(-DWITH_TIFF)
endif()

if(WITH_OPENIMAGEIO)
	add_definitions(-DWITH_OPENIMAGEIO)
endif()

if(WITH_IMAGE_OPENJPEG)
	add_definitions(-DWITH_OPENJPEG)
endif()

if(WITH_IMAGE_DDS)
	add_definitions(-DWITH_DDS)
endif()

if(WITH_IMAGE_CINEON)
	add_definitions(-DWITH_CINEON)
endif()

if(WITH_IMAGE_FRAMESERVER)
	add_definitions(-DWITH_FRAMESERVER)
endif()

if(WITH_IMAGE_HDR)
	add_definitions(-DWITH_HDR)
endif()

if(WITH_PYTHON)
	list(APPEND INC
		../python
	)
	add_definitions(-DWITH_PYTHON)

	if(WITH_PYTHON_SECURITY)
		add_definitions(-DWITH_PYTHON_SECURITY)
	endif()


	if(PYTHON_EXECUTABLE)
		get_filename_component(_python_exe_name ${PYTHON_EXECUTABLE} NAME)
		add_definitions(-DPYTHON_EXECUTABLE_NAME=${_python_exe_name})
		unset(_python_exe_name)
	endif()
endif()

if(WITH_LZO)
	if(WITH_SYSTEM_LZO)
		list(APPEND INC_SYS
			${LZO_INCLUDE_DIR}
		)
		add_definitions(-DWITH_SYSTEM_LZO)
	else()
		list(APPEND INC_SYS
			../../../extern/lzo/minilzo
		)
	endif()
	add_definitions(-DWITH_LZO)
endif()

if(WITH_LZMA)
	list(APPEND INC_SYS
		../../../extern/lzma
	)
	add_definitions(-DWITH_LZMA)
endif()

if(WITH_FFTW3)
	list(APPEND INC_SYS
		${FFTW3_INCLUDE_DIRS}
	)
	add_definitions(-DFFTW3=1)
endif()

if(WITH_INTERNATIONAL)
	add_definitions(-DWITH_INTERNATIONAL)
endif()

## Warnings as errors, this is too strict!
#if(MSVC)
#	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")
#endif()

blender_add_lib(bf_blenkernel "${SRC}" "${INC}" "${INC_SYS}")
