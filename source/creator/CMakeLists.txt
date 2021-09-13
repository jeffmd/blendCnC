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

setup_libdirs()

blender_include_dirs(
	../../intern/clog
	../../intern/guardedalloc
	../../intern/glew-mx
	../blender/blenlib
	../blender/blenkernel
	../blender/blenloader
	../blender/editors/include
	../blender/makesrna
	../blender/imbuf
	../blender/makesdna
	../blender/gpu
	../blender/windowmanager
)

add_definitions(${GL_DEFINITIONS})
blender_include_dirs("${GLEW_INCLUDE_PATH}")

if(WITH_PYTHON)
	blender_include_dirs(../blender/python)
	add_definitions(-DWITH_PYTHON)

	if(WITH_PYTHON_SECURITY)
		add_definitions(-DWITH_PYTHON_SECURITY)
	endif()
endif()

if(WITH_BINRELOC)
	add_definitions(-DWITH_BINRELOC)
	blender_include_dirs(${BINRELOC_INCLUDE_DIRS})
endif()

# Setup the exe sources and buildinfo
set(SRC
	creator.c
	creator_args.c
	creator_signals.c

	creator_intern.h
)

if(WITH_BUILDINFO)
	add_definitions(-DWITH_BUILDINFO)
	# --------------------------------------------------------------------------
	# These defines could all be moved into the header below
	string(REPLACE " " "\ " BUILDINFO_CFLAGS "${CMAKE_C_FLAGS}")
	string(REPLACE " " "\ " BUILDINFO_CXXFLAGS "${CMAKE_CXX_FLAGS}")
	string(REPLACE " " "\ " BUILDINFO_LINKFLAGS "${PLATFORM_LINKFLAGS}")
	add_definitions(
		# # define in header now, else these get out of date on rebuilds.
		# -DBUILD_DATE="${BUILD_DATE}"
		# -DBUILD_TIME="${BUILD_TIME}"
		# -DBUILD_COMMIT_TIMESTAMP="${BUILD_COMMIT_TIMESTAMP}"
		# -DBUILD_COMMIT_TIME="${BUILD_COMMIT_TIME}"
		# -DBUILD_COMMIT_DATE="${BUILD_COMMIT_DATE}"
		# -DBUILD_HASH="${BUILD_HASH}"
		# -DBUILD_BRANCH="${BUILD_BRANCH}"
		-DWITH_BUILDINFO_HEADER # alternative to lines above
		-DBUILD_PLATFORM="${CMAKE_SYSTEM_NAME}"
		-DBUILD_TYPE="${CMAKE_BUILD_TYPE}"
		-DBUILD_CFLAGS="${BUILDINFO_CFLAGS}"
		-DBUILD_CXXFLAGS="${BUILDINFO_CXXFLAGS}"
		-DBUILD_LINKFLAGS="${BUILDINFO_LINKFLAGS}"
		-DBUILD_SYSTEM="CMake"
	)

	# --------------------------------------------------------------------------
	# write header for values that change each build
	# note, generaed file is in build dir's source/creator
	#       except when used as an include path.

	# include the output directory, where the buildinfo.h file is generated
	include_directories(${CMAKE_CURRENT_BINARY_DIR})


	# XXX, ${buildinfo_h_fake} is used here,
	# because we rely on that file being detected as missing
	# every build so that the real header "buildinfo.h" is updated.
	#
	# Keep this until we find a better way to resolve!

	set(buildinfo_h_real "${CMAKE_CURRENT_BINARY_DIR}/buildinfo.h")
	set(buildinfo_h_fake "${CMAKE_CURRENT_BINARY_DIR}/buildinfo.h_fake")

	if(EXISTS ${buildinfo_h_fake})
		message(FATAL_ERROR "File \"${buildinfo_h_fake}\" found, this should never be created, remove!")
	endif()

	# a custom target that is always built
	add_custom_target(buildinfo ALL
		DEPENDS ${buildinfo_h_fake})

	# creates buildinfo.h using cmake script
	add_custom_command(
		OUTPUT
			${buildinfo_h_fake}  # ensure we always run
			${buildinfo_h_real}
		COMMAND ${CMAKE_COMMAND}
		-DSOURCE_DIR=${CMAKE_SOURCE_DIR}
		# overrides only used when non-empty strings
		-DBUILD_DATE=${BUILDINFO_OVERRIDE_DATE}
		-DBUILD_TIME=${BUILDINFO_OVERRIDE_TIME}
		-P ${CMAKE_SOURCE_DIR}/build_files/cmake/buildinfo.cmake)

	# buildinfo.h is a generated file
	set_source_files_properties(
		${buildinfo_h_real}
		PROPERTIES GENERATED TRUE
		HEADER_FILE_ONLY TRUE)

	unset(buildinfo_h_real)
	unset(buildinfo_h_fake)

	# add deps below, after adding blender
	# -------------- done with header values.

	list(APPEND SRC
		buildinfo.c
	)

	# make an object library so can load with it in tests
	add_library(buildinfoobj OBJECT buildinfo.c)
	add_dependencies(buildinfoobj buildinfo)
endif()

add_cc_flags_custom_test(blendcnc)

# message(STATUS "Configuring blendcnc")
	add_executable(blendcnc ${EXETYPE} ${SRC})
	WINDOWS_SIGN_TARGET(blendcnc)

if(WITH_BUILDINFO)
	# explicitly say that the executable depends on the buildinfo
	add_dependencies(blendcnc buildinfo)
endif()


set(BLENDER_TEXT_FILES
	${CMAKE_SOURCE_DIR}/release/text/GPL-license.txt
	${CMAKE_SOURCE_DIR}/release/text/GPL3-license.txt
	${CMAKE_SOURCE_DIR}/release/text/copyright.txt
	# generate this file
	# ${CMAKE_SOURCE_DIR}/release/text/readme.html
	${CMAKE_SOURCE_DIR}/release/datafiles/LICENSE-bfont.ttf.txt
)

if(WITH_PYTHON)
	list(APPEND BLENDER_TEXT_FILES
		${CMAKE_SOURCE_DIR}/release/text/Python-license.txt
	)
endif()

if(WITH_MEM_JEMALLOC)
	list(APPEND BLENDER_TEXT_FILES
		${CMAKE_SOURCE_DIR}/release/text/jemalloc-license.txt
	)
endif()

if(WITH_INTERNATIONAL)
	list(APPEND BLENDER_TEXT_FILES
		${CMAKE_SOURCE_DIR}/release/datafiles/LICENSE-droidsans.ttf.txt
		${CMAKE_SOURCE_DIR}/release/datafiles/LICENSE-bmonofont-i18n.ttf.txt
	)
endif()


# -----------------------------------------------------------------------------
# Platform Specific Var: TARGETDIR_VER

if(UNIX AND NOT APPLE)
	if(WITH_INSTALL_PORTABLE)
		set(TARGETDIR_VER ${BLENDER_VERSION})
	else()
		set(TARGETDIR_VER share/blender/${BLENDER_VERSION})
	endif()

endif()


# -----------------------------------------------------------------------------
# Install Targets (Generic, All Platforms)


# important to make a clean  install each time, else old scripts get loaded.
install(
	CODE
	"file(REMOVE_RECURSE ${TARGETDIR_VER})"
)

if(WITH_PYTHON)
	# install(CODE "message(\"copying blendcnc scripts...\")")

	# exclude addons_contrib if release
	if("${BLENDER_VERSION_CYCLE}" STREQUAL "release" OR
	   "${BLENDER_VERSION_CYCLE}" STREQUAL "rc")
		set(ADDON_EXCLUDE_CONDITIONAL "addons_contrib/*")
	else()
		set(ADDON_EXCLUDE_CONDITIONAL "_addons_contrib/*")  # dummy, wont do anything
	endif()

	# do not install freestyle dir if disabled
	if(NOT WITH_FREESTYLE)
		set(FREESTYLE_EXCLUDE_CONDITIONAL "freestyle/*")
	else()
		set(FREESTYLE_EXCLUDE_CONDITIONAL "_freestyle/*")  # dummy, wont do anything
	endif()

	install(
		DIRECTORY ${CMAKE_SOURCE_DIR}/release/scripts
		DESTINATION ${TARGETDIR_VER}
		PATTERN ".git" EXCLUDE
		PATTERN ".gitignore" EXCLUDE
		PATTERN ".arcconfig" EXCLUDE
		PATTERN "__pycache__" EXCLUDE
		PATTERN "${ADDON_EXCLUDE_CONDITIONAL}" EXCLUDE
		PATTERN "${FREESTYLE_EXCLUDE_CONDITIONAL}" EXCLUDE
	)

	unset(ADDON_EXCLUDE_CONDITIONAL)
	unset(FREESTYLE_EXCLUDE_CONDITIONAL)
endif()

# localization
if(WITH_INTERNATIONAL)
	install(
		DIRECTORY
			${CMAKE_SOURCE_DIR}/release/datafiles/fonts
		DESTINATION ${TARGETDIR_VER}/datafiles
	)

	set(_locale_dir "${CMAKE_SOURCE_DIR}/release/datafiles/locale")
	set(_locale_target_dir ${TARGETDIR_VER}/datafiles/locale)

	file(GLOB _po_files "${_locale_dir}/po/*.po")
	foreach(_po_file ${_po_files})
		msgfmt_simple(${_po_file} _all_mo_files)
	endforeach()

	# Create a custom target which will compile all po to mo
	add_custom_target(
		locales
		DEPENDS ${_all_mo_files})

	add_dependencies(blendcnc locales)

	# Generate INSTALL rules
	install(
		FILES ${_locale_dir}/languages
		DESTINATION ${_locale_target_dir}
	)

	foreach(_mo_file ${_all_mo_files})
		get_filename_component(_locale_name ${_mo_file} NAME_WE)
		install(
			FILES ${_mo_file}
			DESTINATION ${_locale_target_dir}/${_locale_name}/LC_MESSAGES
			RENAME blender.mo
		)
		unset(_locale_name)
	endforeach()

	unset(_all_mo_files)
	unset(_po_files)
	unset(_po_file)
	unset(_mo_file)
	unset(_locale_target_dir)

	unset(_locale_dir)
endif()

# helpful tip when using make
if("${CMAKE_GENERATOR}" MATCHES ".*Makefiles.*")
	# message after building.
	add_custom_command(
		TARGET blendcnc POST_BUILD MAIN_DEPENDENCY blendcnc
		COMMAND ${CMAKE_COMMAND} -E
		        echo 'now run: \"make install\" to copy runtime files and scripts to ${TARGETDIR_VER}'
	)
endif()


# -----------------------------------------------------------------------------
# Install Targets (Platform Specific)

if(UNIX AND NOT APPLE)

	if(WITH_DOC_MANPAGE)
		add_custom_target(
			blender_man_page ALL
			COMMAND ${CMAKE_SOURCE_DIR}/doc/manpage/blender.1.py
			        ${EXECUTABLE_OUTPUT_PATH}/blender
			        ${CMAKE_CURRENT_BINARY_DIR}/blender.1)
		add_dependencies(blender_man_page blender)
	endif()

	# there are a few differences between portable and system install
	if(WITH_INSTALL_PORTABLE)
		install(
			TARGETS blendcnc
			DESTINATION "."
		)

		if(WITH_DOC_MANPAGE)
			install(
				FILES ${CMAKE_CURRENT_BINARY_DIR}/blender.1
				DESTINATION "."
			)
		endif()
		install(
			FILES
				${CMAKE_SOURCE_DIR}/release/freedesktop/blender.desktop
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/scalable/apps/blender.svg
			DESTINATION "."
		)

		install(
			PROGRAMS
			${CMAKE_SOURCE_DIR}/release/bin/blender-thumbnailer.py
			DESTINATION "."
		)

		set(BLENDER_TEXT_FILES_DESTINATION ".")
	else()
		# main blendcnc binary
		install(
			TARGETS blendcnc
			DESTINATION bin
		)
		if(WITH_DOC_MANPAGE)
			# manpage only with 'blender' binary
			install(
				FILES ${CMAKE_CURRENT_BINARY_DIR}/blender.1
				DESTINATION share/man/man1
			)
		endif()

		# misc files
		install(
			FILES ${CMAKE_SOURCE_DIR}/release/freedesktop/blender.desktop
			DESTINATION share/applications
		)
		install(
			DIRECTORY
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/16x16
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/22x22
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/24x24
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/32x32
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/48x48
				${CMAKE_SOURCE_DIR}/release/freedesktop/icons/256x256
			DESTINATION share/icons/hicolor
			PATTERN "*.svg" EXCLUDE
		)
		install(
			FILES ${CMAKE_SOURCE_DIR}/release/freedesktop/icons/scalable/apps/blender.svg
			DESTINATION share/icons/hicolor/scalable/apps
		)
		install(
			PROGRAMS ${CMAKE_SOURCE_DIR}/release/bin/blender-thumbnailer.py
			DESTINATION bin
		)
		set(BLENDER_TEXT_FILES_DESTINATION share/doc/blender)
	endif()

	if(WITH_PYTHON)
		if(WITH_PYTHON_INSTALL)

			install(
				PROGRAMS ${PYTHON_EXECUTABLE}
				DESTINATION ${TARGETDIR_VER}/python/bin
			)

			# on some platforms (like openSUSE) Python is linked
			# to be used from lib64 folder.
			# determine this from Python's libraries path
			#
			# ugh, its possible 'lib64' is just a symlink to 'lib' which causes incorrect use of 'lib64'
			get_filename_component(_pypath_real ${PYTHON_LIBPATH} REALPATH)
			if(${_pypath_real} MATCHES "lib64$")
				set(_target_LIB "lib64")
			else()
				set(_target_LIB "lib")
			endif()
			unset(_pypath_real)

			# Copy the systems python into the install directory
			# install(CODE "message(\"copying a subset of the systems python...\")")
			install(
				DIRECTORY ${PYTHON_LIBPATH}/python${PYTHON_VERSION}
				DESTINATION ${TARGETDIR_VER}/python/${_target_LIB}
				PATTERN "__pycache__" EXCLUDE               # * any cache *
				PATTERN "config-${PYTHON_VERSION}m/*.a" EXCLUDE  # static lib
				PATTERN "lib2to3" EXCLUDE                   # ./lib2to3
				PATTERN "site-packages/*" EXCLUDE           # ./site-packages/*
				PATTERN "tkinter" EXCLUDE                   # ./tkinter
				PATTERN "lib-dynload/_tkinter.*" EXCLUDE    # ./lib-dynload/_tkinter.co
				PATTERN "idlelib" EXCLUDE                   # ./idlelib
				PATTERN "test" EXCLUDE                      # ./test
				PATTERN "turtledemo" EXCLUDE                # ./turtledemo
				PATTERN "turtle.py" EXCLUDE                 # ./turtle.py
			)

			# Needed for distutils/pip
			# get the last part of the include dir, will be 'python{version}{abiflag}',
			get_filename_component(_py_inc_suffix ${PYTHON_INCLUDE_DIR} NAME)
			install(
				FILES ${PYTHON_INCLUDE_DIR}/pyconfig.h
				DESTINATION ${TARGETDIR_VER}/python/include/${_py_inc_suffix}
			)
			unset(_py_inc_suffix)

			if(WITH_PYTHON_INSTALL_NUMPY)
				# Install to the same directory as the source, so debian-like
				# distros are happy with their policy.
				set(_suffix "site-packages")
				if(${PYTHON_NUMPY_PATH} MATCHES "dist-packages")
					set(_suffix "dist-packages")
				endif()
				install(
					DIRECTORY ${PYTHON_NUMPY_PATH}/numpy
					DESTINATION ${TARGETDIR_VER}/python/${_target_LIB}/python${PYTHON_VERSION}/${_suffix}
					PATTERN ".svn" EXCLUDE
					PATTERN "__pycache__" EXCLUDE           # * any cache *
					PATTERN "*.pyc" EXCLUDE                 # * any cache *
					PATTERN "*.pyo" EXCLUDE                 # * any cache *
					PATTERN "oldnumeric" EXCLUDE            # ./oldnumeric
					PATTERN "doc" EXCLUDE                   # ./doc
					PATTERN "tests" EXCLUDE                 # ./tests
					PATTERN "f2py" EXCLUDE                  # ./f2py - fortran/python interface code, not for blender.
					PATTERN "include" EXCLUDE               # include dirs all over, we wont use NumPy/CAPI
					PATTERN "*.h" EXCLUDE                   # some includes are not in include dirs
					PATTERN "*.a" EXCLUDE                   # ./core/lib/libnpymath.a - for linking, we dont need.
				)
				unset(_suffix)
			endif()

			# Copy requests, we need to generalize site-packages
			if(WITH_PYTHON_INSTALL_REQUESTS)
				set(_suffix "site-packages")
				if(${PYTHON_REQUESTS_PATH} MATCHES "dist-packages")
					set(_suffix "dist-packages")
				endif()
				install(
					DIRECTORY ${PYTHON_REQUESTS_PATH}/requests
					DESTINATION ${TARGETDIR_VER}/python/${_target_LIB}/python${PYTHON_VERSION}/${_suffix}
					PATTERN ".svn" EXCLUDE
					PATTERN "__pycache__" EXCLUDE           # * any cache *
					PATTERN "*.pyc" EXCLUDE                 # * any cache *
					PATTERN "*.pyo" EXCLUDE                 # * any cache *
				)
				# On some platforms requests does have extra dependencies.
				set(_requests_deps "certifi" "chardet" "idna" "urllib3")
				foreach(_requests_dep ${_requests_deps})
					if(EXISTS ${PYTHON_REQUESTS_PATH}/${_requests_dep})
						install(
							DIRECTORY ${PYTHON_REQUESTS_PATH}/${_requests_dep}
							DESTINATION ${TARGETDIR_VER}/python/${_target_LIB}/python${PYTHON_VERSION}/${_suffix}
							PATTERN ".svn" EXCLUDE
							PATTERN "__pycache__" EXCLUDE           # * any cache *
							PATTERN "*.pyc" EXCLUDE                 # * any cache *
							PATTERN "*.pyo" EXCLUDE                 # * any cache *
						)
					endif()
				endforeach()
				if(EXISTS ${PYTHON_REQUESTS_PATH}/six.py)
					install(
						FILES ${PYTHON_REQUESTS_PATH}/six.py
						DESTINATION ${TARGETDIR_VER}/python/${_target_LIB}/python${PYTHON_VERSION}/${_suffix}
					)
				endif()
				unset(_requests_dep)
				unset(_requests_deps)
				unset(_suffix)
			endif()
			unset(_target_LIB)

		endif()
	endif()
endif()

# -----------------------------------------------------------------------------
# Generic Install, for all targets

if(DEFINED BLENDER_TEXT_FILES_DESTINATION)

	install(
		CODE
		"
		file(READ \"${CMAKE_SOURCE_DIR}/release/text/readme.html\" DATA_SRC)
		string(REGEX REPLACE \"BLENDER_VERSION\" \"${BLENDER_VERSION}\" DATA_DST \"\${DATA_SRC}\")
		file(WRITE \"${CMAKE_BINARY_DIR}/release/text/readme.html\" \"\${DATA_DST}\")
		unset(DATA_SRC)
		unset(DATA_DST)
		"
	)
	list(APPEND BLENDER_TEXT_FILES
		${CMAKE_BINARY_DIR}/release/text/readme.html
	)

	install(
		FILES ${BLENDER_TEXT_FILES}
		DESTINATION "${BLENDER_TEXT_FILES_DESTINATION}"
	)
endif()

# install more files specified elsewhere
delayed_do_install(${TARGETDIR_VER})

unset(BLENDER_TEXT_FILES)
unset(BLENDER_TEXT_FILES_DESTINATION)


# -----------------------------------------------------------------------------
# Setup link libs

add_dependencies(blendcnc makesdna)

setup_blender_sorted_libs()
target_link_libraries(blendcnc ${BLENDER_SORTED_LIBS})

setup_liblinks(blendcnc)
