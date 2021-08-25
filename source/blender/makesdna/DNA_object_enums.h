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

/** \file DNA_object_enums.h
 *  \ingroup DNA
 *
 * Enums typedef's for use in public headers.
 */

#ifndef __DNA_OBJECT_ENUMS_H__
#define __DNA_OBJECT_ENUMS_H__

/* Object.mode */
typedef enum eObjectMode {
	OB_MODE_OBJECT        = 0,
	OB_MODE_EDIT          = 1 << 0,
} eObjectMode;

/* Object->rotmode */
typedef enum eRotationModes {
	/* quaternion rotations (default, and for older Blender versions) */
	ROT_MODE_QUAT   = 0,
	/* euler rotations - keep in sync with enum in BLI_math.h */
	ROT_MODE_EUL = 1,       /* Blender 'default' (classic) - must be as 1 to sync with BLI_math_rotation.h defines */
	ROT_MODE_XYZ = 1,
	ROT_MODE_XZY = 2,
	ROT_MODE_YXZ = 3,
	ROT_MODE_YZX = 4,
	ROT_MODE_ZXY = 5,
	ROT_MODE_ZYX = 6,
	/* NOTE: space is reserved here for 18 other possible
	 * euler rotation orders not implemented
	 */
	/* axis angle rotations */
	ROT_MODE_AXISANGLE = -1,

	ROT_MODE_MIN = ROT_MODE_AXISANGLE,  /* sentinel for Py API */
	ROT_MODE_MAX = ROT_MODE_ZYX,
} eRotationModes;

#endif  /* __DNA_OBJECT_ENUMS_H__ */
