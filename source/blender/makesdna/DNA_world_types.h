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

/** \file DNA_world_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_WORLD_TYPES_H__
#define __DNA_WORLD_TYPES_H__

#include "DNA_defs.h"
#include "DNA_ID.h"

struct MTex;

#ifndef MAX_MTEX
#define MAX_MTEX	18
#endif


/**
 * World defines general modeling data such as a background fill,
 * color model etc. It mixes modeling data. */
typedef struct World {
	ID id;

	short colormodel, totex;
	short texact, mistype;

	float horr, horg, horb;
	float zenr, zeng, zenb;
	float ambr, ambg, ambb;

	/**
	 * Exposure= mult factor. unused now, but maybe back later. Kept in to be upward compat.
	 * New is exp/range control. linfac & logfac are constants... don't belong in
	 * file, but allocating 8 bytes for temp mem isn't useful either.
	 */
	float exposure, exp, range;
	float linfac, logfac;

	/**
	 * Radius of the activity bubble, in Manhattan length. Objects
	 * outside the box are activity-culled. */
	float activityBoxRadius; // XXX moved to scene->gamedata in 2.5

	short skytype;
	/**
	 * Some world modes
	 * bit 0: Do mist
	 * bit 1: Do stars
	 * bit 2: (reserved) depth of field
	 * bit 3: (gameengine): Activity culling is enabled.
	 * bit 4: ambient occlusion
	 * bit 5: (gameengine) : enable Bullet DBVT tree for view frustum culling
	 */
	short mode;												// partially moved to scene->gamedata in 2.5

	/* assorted settings (in the middle of ambient occlusion settings for padding reasons) */
	short flag;
	short pr_texture;
	int pad;
	struct MTex *mtex[18];		/* MAX_MTEX */

	/* previews */
	struct PreviewImage *preview;
	ListBase gpumaterial;		/* runtime */
} World;

/* **************** WORLD ********************* */

/* skytype */
#define WO_SKYBLEND     (1 << 0)
#define WO_SKYREAL      (1 << 1)
#define WO_SKYPAPER     (1 << 2)
/* while render: */
#define WO_SKYTEX       (1 << 3)
#define WO_ZENUP        (1 << 4)

/* mode */
#define WO_ACTIVITY_CULLING    (1 << 3)
#define WO_ENV_LIGHT          (1 << 4)
#define WO_DBVT_CULLING       (1 << 5)

/* texco (also in DNA_material_types.h) */
#define TEXCO_ANGMAP      (1 << 6)
#define TEXCO_H_SPHEREMAP (1 << 8)
#define TEXCO_H_TUBEMAP   (1 << 10)
#define TEXCO_EQUIRECTMAP (1 << 11)

/* mapto */
#define WOMAP_BLEND     (1 << 0)
#define WOMAP_HORIZ     (1 << 1)
#define WOMAP_ZENUP     (1 << 2)
#define WOMAP_ZENDOWN   (1 << 3)

/* flag */
#define WO_DS_EXPAND	(1<<0)
	/* NOTE: this must have the same value as MA_DS_SHOW_TEXS,
	 * otherwise anim-editors will not read correctly
	 */
#define WO_DS_SHOW_TEXS	(1<<2)

#endif
