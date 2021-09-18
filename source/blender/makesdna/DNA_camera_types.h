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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file DNA_camera_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_CAMERA_TYPES_H__
#define __DNA_CAMERA_TYPES_H__

#include "DNA_defs.h"
#include "DNA_ID.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Object;

/* ------------------------------------------- */
typedef struct Camera {
	ID id;

	char type; /* CAM_PERSP, CAM_ORTHO or CAM_PANO */
	char dtx; /* draw type extra */
	short flag;
	float passepartalpha;
	float clipsta, clipend;
	float lens, ortho_scale, drawsize;
	float sensor_x, sensor_y;
	float shiftx, shifty;

	char sensor_fit;
	char pad[3];

} Camera;

/* **************** CAMERA ********************* */

/* type */
enum {
	CAM_PERSP       = 0,
	CAM_ORTHO       = 1,
	CAM_PANO        = 2,
};

/* dtx */
enum {
	CAM_DTX_CENTER          = (1 << 0),
	CAM_DTX_CENTER_DIAG     = (1 << 1),
	CAM_DTX_THIRDS          = (1 << 2),
	CAM_DTX_GOLDEN          = (1 << 3),
	CAM_DTX_GOLDEN_TRI_A    = (1 << 4),
	CAM_DTX_GOLDEN_TRI_B    = (1 << 5),
	CAM_DTX_HARMONY_TRI_A   = (1 << 6),
	CAM_DTX_HARMONY_TRI_B   = (1 << 7),
};

/* flag */
enum {
	CAM_SHOWLIMITS          = (1 << 0),
	CAM_SHOWMIST            = (1 << 1),
	CAM_SHOWPASSEPARTOUT    = (1 << 2),
	CAM_SHOW_SAFE_MARGINS       = (1 << 3),
	CAM_SHOWNAME            = (1 << 4),
	CAM_ANGLETOGGLE         = (1 << 5),
	CAM_DS_EXPAND           = (1 << 6),
#ifdef DNA_DEPRECATED
	CAM_PANORAMA            = (1 << 7), /* deprecated */
#endif
	CAM_SHOWSENSOR          = (1 << 8),
	CAM_SHOW_SAFE_CENTER    = (1 << 9),
};

/* Sensor fit */
enum {
	CAMERA_SENSOR_FIT_AUTO  = 0,
	CAMERA_SENSOR_FIT_HOR   = 1,
	CAMERA_SENSOR_FIT_VERT  = 2,
};

#define DEFAULT_SENSOR_WIDTH	32.0f
#define DEFAULT_SENSOR_HEIGHT	18.0f

#ifdef __cplusplus
}
#endif

#endif
