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

/** \file DNA_mesh_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_MESH_TYPES_H__
#define __DNA_MESH_TYPES_H__

#include "DNA_defs.h"
#include "DNA_ID.h"
#include "DNA_customdata_types.h"

struct MCol;
struct MEdge;
struct MFace;
struct MLoop;
struct MLoopCol;
struct MLoopUV;
struct MPoly;
struct MTexPoly;
struct MVert;
struct Material;
struct Mesh;

typedef struct Mesh {
	ID id;

	struct BoundBox *bb;

	struct Material **mat;
	struct MSelect *mselect;

/* BMESH ONLY */
	/*new face structures*/
	struct MPoly *mpoly;
	struct MTexPoly *mtpoly;
	struct MLoop *mloop;
	struct MLoopUV *mloopuv;
	struct MLoopCol *mloopcol;
/* END BMESH ONLY */

	/* mface stores the tessellation (triangulation) of the mesh,
	 * real faces are now stored in nface.*/
	struct MFace *mface;	/* array of mesh object mode faces for tessellation */
	struct MTFace *mtface;	/* store tessellation face UV's and texture here */
	struct MVert *mvert;	/* array of verts */
	struct MEdge *medge;	/* array of edges */
	struct MDeformVert *dvert;	/* deformgroup vertices */

	/* array of colors for the tessellated faces, must be number of tessellated
	 * faces * 4 in length */
	struct MCol *mcol;
	struct Mesh *texcomesh;

	/* When the object is available, the preferred access method is: BKE_editmesh_from_object(ob) */
	struct BMEditMesh *edit_btmesh;	/* not saved in file! */

	struct CustomData vdata, edata, fdata;

/* BMESH ONLY */
	struct CustomData pdata, ldata;
/* END BMESH ONLY */

	int totvert, totedge, totface, totselect;

/* BMESH ONLY */
	int totpoly, totloop;
/* END BMESH ONLY */

	/* the last selected vertex/edge/face are used for the active face however
	 * this means the active face must always be selected, this is to keep track
	 * of the last selected face and is similar to the old active face flag where
	 * the face does not need to be selected, -1 is inactive */
	int act_face;

	/* texture space, copied as one block in editobject.c */
	float loc[3];
	float size[3];
	float rot[3];

	int drawflag;
	short texflag, flag;
	float smoothresh;
	int pad2;

	/* customdata flag, for bevel-weight and crease, which are now optional */
	char cd_flag, pad;

	char editflag;
	char pad3;
	short totcol;
	short pad4;

} Mesh;


/* **************** MESH ********************* */

/* texflag */
enum {
	ME_AUTOSPACE = 1,
};

/* me->editflag */
enum {
	ME_EDIT_MIRROR_X       = 1 << 0,
	ME_EDIT_MIRROR_Y       = 1 << 1,  /* unused so far */
	ME_EDIT_MIRROR_Z       = 1 << 2,  /* unused so far */

	ME_EDIT_PAINT_FACE_SEL = 1 << 3,
	ME_EDIT_MIRROR_TOPO    = 1 << 4,
	ME_EDIT_PAINT_VERT_SEL = 1 << 5,
};

/* we cant have both flags enabled at once,
 * flags defined in DNA_scene_types.h */
#define ME_EDIT_PAINT_SEL_MODE(_me)  (                                        \
	(_me->editflag & ME_EDIT_PAINT_FACE_SEL) ? SCE_SELECT_FACE :              \
		(_me->editflag & ME_EDIT_PAINT_VERT_SEL) ? SCE_SELECT_VERTEX :        \
			0                                                                 \
	)

/* me->flag */
enum {
	ME_TWOSIDED                = 1 << 2,
	ME_UVEFFECT                = 1 << 3,
	ME_VCOLEFFECT              = 1 << 4,
	ME_AUTOSMOOTH              = 1 << 5,
	ME_SMESH                   = 1 << 6,
	ME_SUBSURF                 = 1 << 7,
	ME_OPT_EDGES               = 1 << 8,
	ME_DS_EXPAND               = 1 << 9,
};

/* me->cd_flag */
enum {
	ME_CDFLAG_VERT_BWEIGHT = 1 << 0,
	ME_CDFLAG_EDGE_BWEIGHT = 1 << 1,
	ME_CDFLAG_EDGE_CREASE  = 1 << 2,
};

/* me->drawflag, short */
enum {
	ME_DRAWEDGES           = 1 << 0,
	ME_DRAWFACES           = 1 << 1,
	ME_DRAWNORMALS         = 1 << 2,
	ME_DRAW_VNORMALS       = 1 << 3,

	ME_DRAWEIGHT           = 1 << 4,

	ME_DRAWCREASES         = 1 << 6,
	ME_DRAWSEAMS           = 1 << 7,
	ME_DRAWSHARP           = 1 << 8,
	ME_DRAWBWEIGHTS        = 1 << 9,

	ME_DRAWEXTRA_EDGELEN   = 1 << 10,
	ME_DRAWEXTRA_FACEAREA  = 1 << 11,
	ME_DRAWEXTRA_FACEANG   = 1 << 12,
	ME_DRAWEXTRA_EDGEANG   = 1 << 13,

/* debug only option */
	ME_DRAWEXTRA_INDICES   = 1 << 14,

/* draw stats */
	ME_DRAW_STATVIS        = 1 << 17,

/* draw loop normals */
	ME_DRAW_LNORMALS       = 1 << 18,
};

/* Subsurf Type */
enum {
	ME_CC_SUBSURF      = 0,
	ME_SIMPLE_SUBSURF  = 1,
};

#define MESH_MAX_VERTS 2000000000L

/* this is so we can save bmesh files that load in trunk, ignoring NGons
 * will eventually be removed */

#define USE_BMESH_SAVE_AS_COMPAT
#define USE_BMESH_SAVE_WITHOUT_MFACE

/* enable this so meshes get tessfaces calculated by default */
/* #define USE_TESSFACE_DEFAULT */

#endif
