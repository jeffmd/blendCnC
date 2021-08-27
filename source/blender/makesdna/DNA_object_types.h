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

/** \file DNA_object_types.h
 *  \ingroup DNA
 *  \brief Object is a sort of wrapper for general info.
 */

#ifndef __DNA_OBJECT_TYPES_H__
#define __DNA_OBJECT_TYPES_H__

#include "DNA_object_enums.h"

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_ID.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BoundBox;
struct DerivedMesh;
struct Material;
struct Object;
struct Path;
struct RigidBodyOb;


/* Vertex Groups - Name Info */
typedef struct bDeformGroup {
	struct bDeformGroup *next, *prev;
	char name[64];	/* MAX_VGROUP_NAME */
	/* need this flag for locking weights */
	char flag, pad[7];
} bDeformGroup;
#define MAX_VGROUP_NAME 64

/* bDeformGroup->flag */
#define DG_LOCK_WEIGHT 1

/**
 * The following illustrates the orientation of the
 * bounding box in local space
 *
 * <pre>
 *
 * Z  Y
 * | /
 * |/
 * .-----X
 *     2----------6
 *    /|         /|
 *   / |        / |
 *  1----------5  |
 *  |  |       |  |
 *  |  3-------|--7
 *  | /        | /
 *  |/         |/
 *  0----------4
 * </pre>
 */
typedef struct BoundBox {
	float vec[8][3];
	int flag, pad;
} BoundBox;

/* boundbox flag */
enum {
	BOUNDBOX_DISABLED = (1 << 0),
	BOUNDBOX_DIRTY  = (1 << 1),
};

typedef struct LodLevel {
	struct LodLevel *next, *prev;
	struct Object *source;
	int flags;
	float distance, pad;
	int obhysteresis;
} LodLevel;

typedef struct Object {
	ID id;
	void *pad01;

	short type, partype;
	int par1, par2, par3;	/* can be vertexnrs */
	char parsubstr[64];	/* String describing subobject info, MAX_ID_NAME-2 */
	struct Object *parent, *track;
	/* if ob->proxy (or proxy_group), this object is proxy for object ob->proxy */
	/* proxy_from is set in target back to the proxy. */
	struct Object *proxy, *proxy_group, *proxy_from;
	/* struct Path *path; */
	struct BoundBox *bb;  /* axis aligned boundbox (in localspace) */
	void *data;  /* pointer to objects data - an 'ID' or NULL */

	void *pad1;

	ListBase defbase;   /* list of bDeformGroup (vertex groups) names and flag only */
	ListBase modifiers; /* list of ModifierData structures */

	int mode;           /* Local object mode */
	int restore_mode;   /* Keep track of what mode to return to after toggling a mode */

	/* materials */
	struct Material **mat;	/* material slots */
	char *matbits;			/* a boolean field, with each byte 1 if corresponding material is linked to object */
	int totcol;				/* copy of mesh, curve & meta struct member of same name (keep in sync) */
	int actcol;				/* currently selected material in the UI */

	/* rot en drot have to be together! (transform('r' en 's')) */
	float loc[3], dloc[3], orig[3];
	float size[3];              /* scale in fact */
	float dsize[3] DNA_DEPRECATED ; /* DEPRECATED, 2.60 and older only */
	float dscale[3];            /* ack!, changing */
	float rot[3], drot[3];		/* euler rotation */
	float quat[4], dquat[4];	/* quaternion rotation */
	float rotAxis[3], drotAxis[3];	/* axis angle rotation - axis part */
	float rotAngle, drotAngle;	/* axis angle rotation - angle part */
	float obmat[4][4];		/* final worldspace matrix with constraints & animsys applied */
	float parentinv[4][4]; /* inverse result of parent, so that object doesn't 'stick' to parent */
	float constinv[4][4]; /* inverse result of constraints. doesn't include effect of parent or object local transform */
	float imat[4][4];	/* inverse matrix of 'obmat' for any other use than rendering! */
	                    /* note: this isn't assured to be valid as with 'obmat',
	                     *       before using this value you should do...
	                     *       invert_m4_m4(ob->imat, ob->obmat); */

	/* Previously 'imat' was used at render time, but as other places use it too
	 * the interactive ui of 2.5 creates problems. So now only 'imat_ren' should
	 * be used when ever the inverse of ob->obmat * re->viewmat is needed! - jahka
	 */
	float imat_ren[4][4];

	unsigned int lay;	/* copy of Base's layer in the scene */

	short flag;			/* copy of Base */
	short colbits DNA_DEPRECATED;		/* deprecated, use 'matbits' */

	short transflag, protectflag;	/* transformation settings and transform locks  */
	short scaflag;				/* ui state for game logic */
	char scavisflag;			/* more display settings for game logic */
	char depsflag;

	/* did last modifier stack generation need mapping support? */
	char lastNeedMapping;  /* bool */
	char pad[3];

	/* during realtime */

	/* note that inertia is only called inertia for historical reasons
	 * and is not changed to avoid DNA surgery. It actually reflects the
	 * Size value in the GameButtons (= radius) */

	float mass, damping, inertia;
	/* The form factor k is introduced to give the user more control
	 * and to fix incompatibility problems.
	 * For rotational symmetric objects, the inertia value can be
	 * expressed as: Theta = k * m * r^2
	 * where m = Mass, r = Radius
	 * For a Sphere, the form factor is by default = 0.4
	 */

	float formfactor;
	float rdamping;
	float margin;
	float max_vel; /* clamp the maximum velocity 0.0 is disabled */
	float min_vel; /* clamp the minimum velocity 0.0 is disabled */
	float max_angvel; /* clamp the maximum angular velocity, 0.0 is disabled */
	float min_angvel; /* clamp the minimum angular velocity, 0.0 is disabled */
	float obstacleRad;

	/* "Character" physics properties */
	float step_height;
	float jump_speed;
	float fall_speed;
	unsigned char max_jumps;
	char pad2[3];

	/** Collision mask settings */
	unsigned short col_group, col_mask;

	short rotmode;		/* rotation mode */

	char boundtype;            /* bounding box use for drawing */
	char collision_boundtype;  /* bounding box type used for collision */

	short dtx;			/* viewport draw extra settings */
	char dt;			/* viewport draw type */
	char empty_drawtype;
	float empty_drawsize;

	float sf; /* sf is time-offset */

	short index;			/* custom index, for renderpasses */
	unsigned short actdef;	/* current deformation group, note: index starts at 1 */
	float col[4];			/* object color */

	char restrictflag;		/* for restricting view, select, render etc. accessible in outliner */
	char recalc;			/* dependency flag */
	short pad20;
	float anisotropicFriction[3];

	struct Group *dup_group;	/* object duplicator for group */

	char  body_type;			/* for now used to temporarily holds the type of collision object */
	char  shapeflag;			/* flag for pinning */
	short pad21;
	float smoothresh;			/* smoothresh is phong interpolation ray_shadow correction in render */

	/* Runtime valuated curve-specific data, not stored in the file */
	struct CurveCache *curve_cache;

	struct DerivedMesh *derivedDeform, *derivedFinal;
	void *pad22;
	uint64_t lastDataMask;   /* the custom data layer mask that was last used to calculate derivedDeform and derivedFinal */
	uint64_t customdata_mask; /* (extra) custom data layer mask to use for creating derivedmesh, set by depsgraph */
	unsigned int state;			/* bit masks of game controllers that are active */
	unsigned int init_state;	/* bit masks of initial state as recorded by the users */

	ListBase gpulamp;		/* runtime, for glsl lamp display only */
	ListBase pc_ids;

	struct RigidBodyOb *rigidbody_object;		/* settings for Bullet rigid body */
	struct RigidBodyCon *rigidbody_constraint;	/* settings for Bullet constraint */

	float ima_ofs[2];		/* offset for image empties */
	struct ImageUser *iuser;	/* must be non-null when oject is an empty image */
	void *pad3;

	ListBase lodlevels;		/* contains data for levels of detail */
	LodLevel *currentlod;

	struct PreviewImage *preview;
} Object;

/* **************** OBJECT ********************* */

/* used many places... should be specialized  */
#define SELECT          1

/* type */
enum {
	OB_EMPTY      = 0,
	OB_MESH       = 1,
	OB_CURVE      = 2,
	OB_SURF       = 3,
	OB_FONT       = 4,

	OB_LAMP       = 10,
	OB_CAMERA     = 11,

};

/* check if the object type supports materials */
#define OB_TYPE_SUPPORT_MATERIAL(_type) \
	((_type) >= OB_MESH && (_type) <= OB_FONT)
#define OB_TYPE_SUPPORT_VGROUP(_type) \
	(ELEM(_type, OB_MESH))
#define OB_TYPE_SUPPORT_EDITMODE(_type) \
	(ELEM(_type, OB_MESH, OB_FONT, OB_CURVE, OB_SURF))
#define OB_TYPE_SUPPORT_PARVERT(_type) \
	(ELEM(_type, OB_MESH, OB_SURF, OB_CURVE))

/** Matches #OB_TYPE_SUPPORT_EDITMODE. */
#define OB_DATA_SUPPORT_EDITMODE(_type) \
	(ELEM(_type, ID_ME, ID_CU))

/* is this ID type used as object data */
#define OB_DATA_SUPPORT_ID(_id_type) \
	(ELEM(_id_type, ID_ME, ID_CU, ID_LA, ID_CA))

#define OB_DATA_SUPPORT_ID_CASE \
	ID_ME: case ID_CU: case ID_LA: case ID_CA

/* partype: first 4 bits: type */
enum {
	PARTYPE       = (1 << 4) - 1,
	PAROBJECT     = 0,
#ifdef DNA_DEPRECATED
	PARCURVE      = 1,  /* Deprecated. */
#endif
	PARVERT1      = 5,
	PARVERT3      = 6,

	/* slow parenting - is not threadsafe and/or may give errors after jumping  */
	PARSLOW       = 16,
};

/* (short) transflag */
/* flags 1 and 2 were unused or relics from past features */
enum {
	OB_NEG_SCALE        = 1 << 2,
	OB_RENDER_DUPLI     = 1 << 12,
	OB_NO_CONSTRAINTS   = 1 << 13,  /* runtime constraints disable */

};

/* (short) trackflag / upflag */
enum {
	OB_POSX = 0,
	OB_POSY = 1,
	OB_POSZ = 2,
	OB_NEGX = 3,
	OB_NEGY = 4,
	OB_NEGZ = 5,
};

/* gameflag in game.h */

/* dt: no flags */
enum {
	OB_BOUNDBOX  = 1,
	OB_WIRE      = 2,
	OB_SOLID     = 3,
	OB_MATERIAL  = 4,
	OB_TEXTURE   = 5,
	OB_RENDER    = 6,

	OB_PAINT     = 100,  /* temporary used in draw code */
};

/* dtx: flags (short) */
enum {
	OB_DRAWBOUNDOX    = 1 << 0,
	OB_AXIS           = 1 << 1,
	OB_TEXSPACE       = 1 << 2,
	OB_DRAWNAME       = 1 << 3,
	OB_DRAWIMAGE      = 1 << 4,
	/* for solid+wire display */
	OB_DRAWWIRE       = 1 << 5,
	/* for overdraw s*/
	OB_DRAWXRAY       = 1 << 6,
	/* enable transparent draw */
	OB_DRAWTRANSP     = 1 << 7,
	OB_DRAW_ALL_EDGES = 1 << 8,  /* only for meshes currently */
};

/* empty_drawtype: no flags */
enum {
	OB_ARROWS        = 1,
	OB_PLAINAXES     = 2,
	OB_CIRCLE        = 3,
	OB_SINGLE_ARROW  = 4,
	OB_CUBE          = 5,
	OB_EMPTY_SPHERE  = 6,
	OB_EMPTY_CONE    = 7,
	OB_EMPTY_IMAGE   = 8,
};

/* boundtype */
enum {
	OB_BOUND_BOX           = 0,
	OB_BOUND_SPHERE        = 1,
	OB_BOUND_CYLINDER      = 2,
	OB_BOUND_CONE          = 3,
	OB_BOUND_TRIANGLE_MESH = 4,
	OB_BOUND_CONVEX_HULL   = 5,
/*	OB_BOUND_DYN_MESH      = 6, */ /*UNUSED*/
	OB_BOUND_CAPSULE       = 7,
};

/* lod flags */
enum {
	OB_LOD_USE_MESH		= 1 << 0,
	OB_LOD_USE_MAT		= 1 << 1,
	OB_LOD_USE_HYST		= 1 << 2,
};

/* **************** BASE ********************* */

/* also needed for base!!!!! or rather, they interfere....*/
/* base->flag and ob->flag */
enum {
	BA_WAS_SEL = (1 << 1),
	BA_SNAP_FIX_DEPS_FIASCO = (1 << 2),  /* Yes, re-use deprecated bit, all fine since it's runtime only. */
};

	/* NOTE: this was used as a proper setting in past, so nullify before using */
#define BA_TEMP_TAG         (1 << 5)

/* #define BA_FROMSET          (1 << 7) */ /*UNUSED*/

#define BA_TRANSFORM_CHILD  (1 << 8)  /* child of a transformed object */
#define BA_TRANSFORM_PARENT (1 << 13)  /* parent of a transformed object */


/* an initial attempt as making selection more specific! */
#define BA_DESELECT     0
#define BA_SELECT       1


#define OB_DONE             (1 << 10)  /* unknown state, clear before use */
#define OB_FROMGROUP        (1 << 12)

/* ob->recalc (flag bits!) */
enum {
	OB_RECALC_OB        = 1 << 0,
	OB_RECALC_DATA      = 1 << 1,
/* only use for matching any flag, NOT as an argument since more flags may be added. */
	OB_RECALC_ALL       = OB_RECALC_OB | OB_RECALC_DATA,
};


/* ob->gameflag2 */
enum {
	OB_NEVER_DO_ACTIVITY_CULLING    = 1 << 0,
	OB_LOCK_RIGID_BODY_X_AXIS       = 1 << 2,
	OB_LOCK_RIGID_BODY_Y_AXIS       = 1 << 3,
	OB_LOCK_RIGID_BODY_Z_AXIS       = 1 << 4,
	OB_LOCK_RIGID_BODY_X_ROT_AXIS   = 1 << 5,
	OB_LOCK_RIGID_BODY_Y_ROT_AXIS   = 1 << 6,
	OB_LOCK_RIGID_BODY_Z_ROT_AXIS   = 1 << 7,

/*	OB_LIFE     = OB_PROP | OB_DYNAMIC | OB_ACTOR | OB_MAINACTOR | OB_CHILD, */
};

/* ob->body_type */
enum {
	OB_BODY_TYPE_NO_COLLISION   = 0,
	OB_BODY_TYPE_STATIC         = 1,
	OB_BODY_TYPE_DYNAMIC        = 2,
	OB_BODY_TYPE_RIGID          = 3,
	OB_BODY_TYPE_NAVMESH        = 7,
	OB_BODY_TYPE_CHARACTER      = 8,
};

/* ob->depsflag */
enum {
	OB_DEPS_EXTRA_OB_RECALC     = 1 << 0,
	OB_DEPS_EXTRA_DATA_RECALC   = 1 << 1,
};

/* ob->restrictflag */
enum {
	OB_RESTRICT_VIEW    = 1 << 0,
	OB_RESTRICT_SELECT  = 1 << 1,
	OB_RESTRICT_RENDER  = 1 << 2,
};

/* ob->shapeflag */
enum {
	OB_SHAPE_LOCK       = 1 << 0,
	// OB_SHAPE_TEMPLOCK   = 1 << 1,  /* deprecated */
	OB_SHAPE_EDIT_MODE  = 1 << 2,
};

/* ob->protectflag */
enum {
	OB_LOCK_LOCX    = 1 << 0,
	OB_LOCK_LOCY    = 1 << 1,
	OB_LOCK_LOCZ    = 1 << 2,
	OB_LOCK_LOC     = OB_LOCK_LOCX | OB_LOCK_LOCY | OB_LOCK_LOCZ,
	OB_LOCK_ROTX    = 1 << 3,
	OB_LOCK_ROTY    = 1 << 4,
	OB_LOCK_ROTZ    = 1 << 5,
	OB_LOCK_ROT     = OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ,
	OB_LOCK_SCALEX  = 1 << 6,
	OB_LOCK_SCALEY  = 1 << 7,
	OB_LOCK_SCALEZ  = 1 << 8,
	OB_LOCK_SCALE   = OB_LOCK_SCALEX | OB_LOCK_SCALEY | OB_LOCK_SCALEZ,
	OB_LOCK_ROTW    = 1 << 9,
	OB_LOCK_ROT4D   = 1 << 10,
};

#define MAX_DUPLI_RECUR 8

#ifdef __cplusplus
}
#endif

#endif
