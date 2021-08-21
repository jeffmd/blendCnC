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

/** \file DNA_scene_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_SCENE_TYPES_H__
#define __DNA_SCENE_TYPES_H__

#include "DNA_defs.h"

/* XXX, temp feature - campbell */
#define DURIAN_CAMERA_SWITCH

#ifdef __cplusplus
extern "C" {
#endif

#include "DNA_color_types.h"  /* color management */
#include "DNA_vec_types.h"
#include "DNA_listBase.h"
#include "DNA_ID.h"
#include "DNA_gpu_types.h"
#include "DNA_userdef_types.h"

struct ColorSpace;
struct CurveMapping;
struct Editing;
struct Group;
struct Image;
struct Object;
struct Scene;
struct SceneStats;
struct Text;
struct World;

/* ************************************************************* */
/* Scene Data */

/* Base - Wrapper for referencing Objects in a Scene */
typedef struct Base {
	struct Base *next, *prev;
	unsigned int lay, selcol;
	int flag;
	short sx, sy;
	struct Object *object;
} Base;

/* View - MultiView */
typedef struct SceneRenderView {
	struct SceneRenderView *next, *prev;

	char name[64];	/* MAX_NAME */
	char suffix[64];	/* MAX_NAME */

	int viewflag;
	int pad[2];
	char pad2[4];

} SceneRenderView;

/* SceneRenderView.viewflag */
#define SCE_VIEW_DISABLE		(1<<0)

/* RenderData.views_format */
enum {
	SCE_VIEWS_FORMAT_STEREO_3D = 0,
	SCE_VIEWS_FORMAT_MULTIVIEW = 1,
};

/* ImageFormatData.views_format (also used for Sequence.views_format) */
enum {
	R_IMF_VIEWS_INDIVIDUAL = 0,
	R_IMF_VIEWS_STEREO_3D  = 1,
	R_IMF_VIEWS_MULTIVIEW  = 2,
};


/* *************************************************************** */

/* Generic image format settings,
 * this is used for NodeImageFile and IMAGE_OT_save_as operator too.
 *
 * note: its a bit strange that even though this is an image format struct
 * the imtype can still be used to select video formats.
 * RNA ensures these enum's are only selectable for render output.
 */
typedef struct ImageFormatData {
	char imtype;   /* R_IMF_IMTYPE_PNG, R_... */
	               /* note, video types should only ever be set from this
	                * structure when used from RenderData */
	char depth;    /* bits per channel, R_IMF_CHAN_DEPTH_8 -> 32,
	                * not a flag, only set 1 at a time */

	char planes;   /* - R_IMF_PLANES_BW, R_IMF_PLANES_RGB, R_IMF_PLANES_RGBA */
	char flag;     /* generic options for all image types, alpha zbuffer */

	char quality;  /* (0 - 100), eg: jpeg quality */
	char compress; /* (0 - 100), eg: png compression */


	/* --- format specific --- */

	/* OpenEXR */
	char  exr_codec;

	/* Cineon */
	char  cineon_flag;
	short cineon_white, cineon_black;
	float cineon_gamma;

	/* Jpeg2000 */
	char  jp2_flag;
	char jp2_codec;

	/* TIFF */
	char tiff_codec;

	char pad[5];

	/* color management */
	ColorManagedViewSettings view_settings;
	ColorManagedDisplaySettings display_settings;
} ImageFormatData;


/* ImageFormatData.imtype */
#define R_IMF_IMTYPE_TARGA           0
#define R_IMF_IMTYPE_IRIS            1
/* #define R_HAMX                    2 */ /* hamx is nomore */
/* #define R_FTYPE                   3 */ /* ftype is nomore */
#define R_IMF_IMTYPE_JPEG90          4
/* #define R_MOVIE                   5 */ /* movie is nomore */
#define R_IMF_IMTYPE_IRIZ            7
#define R_IMF_IMTYPE_RAWTGA         14
#define R_IMF_IMTYPE_AVIRAW         15
#define R_IMF_IMTYPE_AVIJPEG        16
#define R_IMF_IMTYPE_PNG            17
/* #define R_IMF_IMTYPE_AVICODEC    18 */ /* avicodec is nomore */
/* #define R_IMF_IMTYPE_QUICKTIME   19 */ /* quicktime is nomore */
#define R_IMF_IMTYPE_BMP            20
#define R_IMF_IMTYPE_RADHDR         21
#define R_IMF_IMTYPE_TIFF           22
#define R_IMF_IMTYPE_OPENEXR        23
#define R_IMF_IMTYPE_FFMPEG         24
#define R_IMF_IMTYPE_FRAMESERVER    25
#define R_IMF_IMTYPE_CINEON         26
#define R_IMF_IMTYPE_DPX            27
#define R_IMF_IMTYPE_MULTILAYER     28
#define R_IMF_IMTYPE_DDS            29
#define R_IMF_IMTYPE_JP2            30
#define R_IMF_IMTYPE_H264           31
#define R_IMF_IMTYPE_XVID           32
#define R_IMF_IMTYPE_THEORA         33
#define R_IMF_IMTYPE_PSD            34

#define R_IMF_IMTYPE_INVALID        255

/* ImageFormatData.flag */
#define R_IMF_FLAG_ZBUF         (1<<0)   /* was R_OPENEXR_ZBUF */
#define R_IMF_FLAG_PREVIEW_JPG  (1<<1)   /* was R_PREVIEW_JPG */

/* return values from BKE_imtype_valid_depths, note this is depts per channel */
#define R_IMF_CHAN_DEPTH_1  (1<<0) /* 1bits  (unused) */
#define R_IMF_CHAN_DEPTH_8  (1<<1) /* 8bits  (default) */
#define R_IMF_CHAN_DEPTH_10 (1<<2) /* 10bits (uncommon, Cineon/DPX support) */
#define R_IMF_CHAN_DEPTH_12 (1<<3) /* 12bits (uncommon, jp2/DPX support) */
#define R_IMF_CHAN_DEPTH_16 (1<<4) /* 16bits (tiff, halff float exr) */
#define R_IMF_CHAN_DEPTH_24 (1<<5) /* 24bits (unused) */
#define R_IMF_CHAN_DEPTH_32 (1<<6) /* 32bits (full float exr) */

/* ImageFormatData.planes */
#define R_IMF_PLANES_RGB   24
#define R_IMF_PLANES_RGBA  32
#define R_IMF_PLANES_BW    8

/* ImageFormatData.exr_codec */
#define R_IMF_EXR_CODEC_NONE  0
#define R_IMF_EXR_CODEC_PXR24 1
#define R_IMF_EXR_CODEC_ZIP   2
#define R_IMF_EXR_CODEC_PIZ   3
#define R_IMF_EXR_CODEC_RLE   4
#define R_IMF_EXR_CODEC_ZIPS  5
#define R_IMF_EXR_CODEC_B44   6
#define R_IMF_EXR_CODEC_B44A  7
#define R_IMF_EXR_CODEC_DWAA  8
#define R_IMF_EXR_CODEC_DWAB  9
#define R_IMF_EXR_CODEC_MAX  10

/* ImageFormatData.jp2_flag */
#define R_IMF_JP2_FLAG_YCC          (1<<0)  /* when disabled use RGB */ /* was R_JPEG2K_YCC */
#define R_IMF_JP2_FLAG_CINE_PRESET  (1<<1)  /* was R_JPEG2K_CINE_PRESET */
#define R_IMF_JP2_FLAG_CINE_48      (1<<2)  /* was R_JPEG2K_CINE_48FPS */

/* ImageFormatData.jp2_codec */
#define R_IMF_JP2_CODEC_JP2  0
#define R_IMF_JP2_CODEC_J2K  1

/* ImageFormatData.cineon_flag */
#define R_IMF_CINEON_FLAG_LOG (1<<0)  /* was R_CINEON_LOG */

/* ImageFormatData.tiff_codec */
enum {
	R_IMF_TIFF_CODEC_DEFLATE   = 0,
	R_IMF_TIFF_CODEC_LZW       = 1,
	R_IMF_TIFF_CODEC_PACKBITS  = 2,
	R_IMF_TIFF_CODEC_NONE      = 3,
};

/* *************************************************************** */
/* Render Conversion/Simplfication Settings */

/* control render convert and shading engine */
typedef struct RenderProfile {
	struct RenderProfile *next, *prev;
	char name[32];

	short particle_perc;
	short subsurf_max;
	short shadbufsample_max;
	short pad1;

	float ao_error, pad2;

} RenderProfile;


/* *************************************************************** */
/* Transform Orientations */

typedef struct TransformOrientation {
	struct TransformOrientation *next, *prev;
	char name[64];	/* MAX_NAME */
	float mat[3][3];
	int pad;
} TransformOrientation;

/* *************************************************************** */
/* Stats */

/* Stats for Meshes */
typedef struct MeshStatVis {
	char type;
	char _pad1[2];

	/* overhang */
	char  overhang_axis;
	float overhang_min, overhang_max;

	/* thickness */
	float thickness_min, thickness_max;
	char thickness_samples;
	char _pad2[3];

	/* distort */
	float distort_min, distort_max;

	/* sharp */
	float sharp_min, sharp_max;
} MeshStatVis;


/* *************************************************************** */
/* Tool Settings */

typedef struct ToolSettings {
	float doublimit;	/* remove doubles limit */
	float normalsize;	/* size of normals */
	short automerge;

	/* Selection Mode for Mesh */
	short selectmode;

	/* Transform Proportional Area of Effect */
	float proportional_size;

	/* Select Group Threshold */
	float select_thresh;

	/* Multires */
	char multires_subdiv_type;

	/* Alt+RMB option */
	char edge_mode;
	char edge_mode_live_unwrap;

	/* Transform */
	char snap_mode, snap_node_mode;
	char snap_uv_mode;
	short snap_flag, snap_target;
	short proportional, prop_mode;
	char proportional_objects; /* proportional edit, object mode */
	char pad4[2];

	char auto_normalize; /*auto normalizing mode in wpaint*/
	char weightuser;
	char vgroupsubset; /* subset selection filter in wpaint */

	struct MeshStatVis statvis;
	int pad2;
} ToolSettings;

/* *************************************************************** */
/* Assorted Scene Data */

/* ------------------------------------------- */
/* Stats (show in Info header) */

typedef struct bStats {
	/* scene totals for visible layers */
	int totobj, totlamp, totobjsel, totcurve, totmesh, totarmature;
	int totvert, totface;
} bStats;

/* ------------------------------------------- */
/* Unit Settings */

typedef struct UnitSettings {
	/* Display/Editing unit options for each scene */
	float scale_length; /* maybe have other unit conversions? */
	char system; /* imperial, metric etc */
	char system_rotation; /* not implemented as a proper unit system yet */
	short flag;
} UnitSettings;

/* ------------------------------------------- */
/* Global/Common Physics Settings */

typedef struct PhysicsSettings {
	float gravity[3];
	int flag, quick_cache_step, rt;
} PhysicsSettings;

/* ------------------------------------------- */
/* Safe Area options used in Camera View & VSE
 */
typedef struct DisplaySafeAreas {
	/* each value represents the (x,y) margins as a multiplier.
	 * 'center' in this context is just the name for a different kind of safe-area */

	float title[2];		/* Title Safe */
	float action[2];	/* Image/Graphics Safe */

	/* use for alternate aspect ratio */
	float title_center[2];
	float action_center[2];
} DisplaySafeAreas;

/* *************************************************************** */
/* Scene ID-Block */

typedef struct Scene {
	ID id;

	struct Object *camera;
	struct World *world;

	struct Scene *set;

	ListBase base;
	struct Base *basact;		/* active base */
	struct Object *obedit;		/* name replaces old G.obedit */

	float cursor[3];			/* 3d cursor location */
	char _pad[4];

	unsigned int lay;			/* bitflags for layer visibility */
	int layact;		/* active layer */
	unsigned int lay_updated;       /* runtime flag, has layer ever been updated since load? */

	short flag;								/* various settings */
	short pad01;

	struct ToolSettings *toolsettings;		/* default allocated now */
	struct SceneStats *stats;				/* default allocated now */
	struct DisplaySafeAreas safe_areas;

	ListBase transform_spaces;

	void *fps_info;					/* (runtime) info/cache used for presenting playback framerate info to the user */

	/* Units */
	struct UnitSettings unit;

	/* Physics simulation settings */
	struct PhysicsSettings physics_settings;

	uint64_t customdata_mask;	/* XXX. runtime flag for drawing, actually belongs in the window, only used by BKE_object_handle_update() */
	uint64_t customdata_mask_modal; /* XXX. same as above but for temp operator use (gl renders) */

	/* Color Management */
	ColorManagedViewSettings view_settings;
	ColorManagedDisplaySettings display_settings;
	ColorManagedColorspaceSettings sequencer_colorspace_settings;

	/* RigidBody simulation world+settings */
	struct RigidBodyWorld *rigidbody_world;

	struct PreviewImage *preview;
} Scene;

/* **************** SCENE ********************* */

/* note that much higher maxframes give imprecise sub-frames, see: T46859 */
/* Current precision is 16 for the sub-frames closer to MAXFRAME. */

/* depricate this! */
#define TESTBASE(v3d, base)  (                                                \
	((base)->flag & SELECT) &&                                                \
	((base)->lay & v3d->lay) &&                                               \
	(((base)->object->restrictflag & OB_RESTRICT_VIEW) == 0))
#define TESTBASELIB(v3d, base)  (                                             \
	((base)->flag & SELECT) &&                                                \
	((base)->lay & v3d->lay) &&                                               \
	((base)->object->id.lib == NULL) &&                                       \
	(((base)->object->restrictflag & OB_RESTRICT_VIEW) == 0))
#define TESTBASELIB_BGMODE(v3d, scene, base)  (                               \
	((base)->flag & SELECT) &&                                                \
	((base)->lay & (v3d ? v3d->lay : scene->lay)) &&                          \
	((base)->object->id.lib == NULL) &&                                       \
	(((base)->object->restrictflag & OB_RESTRICT_VIEW) == 0))
#define BASE_EDITABLE_BGMODE(v3d, scene, base)  (                             \
	((base)->lay & (v3d ? v3d->lay : scene->lay)) &&                          \
	((base)->object->id.lib == NULL) &&                                       \
	(((base)->object->restrictflag & OB_RESTRICT_VIEW) == 0))
#define BASE_SELECTABLE(v3d, base)  (                                         \
	(base->lay & v3d->lay) &&                                                 \
	(base->object->restrictflag & (OB_RESTRICT_SELECT | OB_RESTRICT_VIEW)) == 0)
#define BASE_VISIBLE(v3d, base)  (                                            \
	(base->lay & v3d->lay) &&                                                 \
	(base->object->restrictflag & OB_RESTRICT_VIEW) == 0)
#define BASE_VISIBLE_BGMODE(v3d, scene, base)  (                              \
	(base->lay & (v3d ? v3d->lay : scene->lay)) &&                            \
	(base->object->restrictflag & OB_RESTRICT_VIEW) == 0)

#define FIRSTBASE		scene->base.first
#define LASTBASE		scene->base.last
#define BASACT			(scene->basact)
#define OBACT			(BASACT ? BASACT->object: NULL)

#define V3D_CAMERA_LOCAL(v3d) ((!(v3d)->scenelock && (v3d)->camera) ? (v3d)->camera : NULL)
#define V3D_CAMERA_SCENE(scene, v3d) ((!(v3d)->scenelock && (v3d)->camera) ? (v3d)->camera : (scene)->camera)

/* Base.flag is in DNA_object_types.h */

/* ToolSettings.snap_flag */
#define SCE_SNAP				(1 << 0)
#define SCE_SNAP_ROTATE			(1 << 1)
#define SCE_SNAP_PEEL_OBJECT	(1 << 2)
#define SCE_SNAP_PROJECT		(1 << 3)
#define SCE_SNAP_NO_SELF		(1 << 4)
#define SCE_SNAP_ABS_GRID		(1 << 5)

/* ToolSettings.snap_target */
#define SCE_SNAP_TARGET_CLOSEST	0
#define SCE_SNAP_TARGET_CENTER	1
#define SCE_SNAP_TARGET_MEDIAN	2
#define SCE_SNAP_TARGET_ACTIVE	3
/* ToolSettings.snap_mode */
#define SCE_SNAP_MODE_INCREMENT	0
#define SCE_SNAP_MODE_VERTEX	1
#define SCE_SNAP_MODE_EDGE		2
#define SCE_SNAP_MODE_FACE		3
#define SCE_SNAP_MODE_VOLUME	4
#define SCE_SNAP_MODE_NODE_X	5
#define SCE_SNAP_MODE_NODE_Y	6
#define SCE_SNAP_MODE_NODE_XY	7
#define SCE_SNAP_MODE_GRID		8

/* ToolSettings.selectmode */
#define SCE_SELECT_VERTEX	(1 << 0) /* for mesh */
#define SCE_SELECT_EDGE		(1 << 1)
#define SCE_SELECT_FACE		(1 << 2)

/* MeshStatVis.type */
#define SCE_STATVIS_OVERHANG	0
#define SCE_STATVIS_THICKNESS	1
#define SCE_STATVIS_INTERSECT	2
#define SCE_STATVIS_DISTORT		3
#define SCE_STATVIS_SHARP		4

/* ParticleEditSettings.selectmode for particles */
#define SCE_SELECT_PATH		(1 << 0)
#define SCE_SELECT_POINT	(1 << 1)
#define SCE_SELECT_END		(1 << 2)

/* ToolSettings.prop_mode (proportional falloff) */
#define PROP_SMOOTH            0
#define PROP_SPHERE            1
#define PROP_ROOT              2
#define PROP_SHARP             3
#define PROP_LIN               4
#define PROP_CONST             5
#define PROP_RANDOM            6
#define PROP_INVSQUARE         7
#define PROP_MODE_MAX          8

/* ToolSettings.proportional */
#define PROP_EDIT_OFF			0
#define PROP_EDIT_ON			1
#define PROP_EDIT_CONNECTED		2
#define PROP_EDIT_PROJECTED		3

/* ToolSettings.weightuser */
enum {
	OB_DRAW_GROUPUSER_NONE      = 0,
	OB_DRAW_GROUPUSER_ACTIVE    = 1,
	OB_DRAW_GROUPUSER_ALL       = 2,
};

/* object_vgroup.c */
/* ToolSettings.vgroupsubset */
typedef enum eVGroupSelect {
	WT_VGROUP_ALL = 0,
	WT_VGROUP_ACTIVE = 1,
} eVGroupSelect;

#define WT_VGROUP_MASK_ALL \
	((1 << WT_VGROUP_ACTIVE) | \
	 (1 << WT_VGROUP_ALL))


/* Scene.flag */
#define SCE_DS_SELECTED			(1<<0)
#define SCE_DS_COLLAPSED		(1<<1)

	/* return flag BKE_scene_base_iter_next functions */
/* #define F_ERROR			-1 */  /* UNUSED */
#define F_START			0
#define F_SCENE			1
#define F_DUPLI			3

/* Paint.flags */
typedef enum ePaintFlags {
	PAINT_SHOW_BRUSH = (1 << 0),
	PAINT_FAST_NAVIGATE = (1 << 1),
	PAINT_SHOW_BRUSH_ON_SURFACE = (1 << 2),
	PAINT_USE_CAVITY_MASK = (1 << 3),
} ePaintFlags;

/* Paint.symmetry_flags
 * (for now just a duplicate of sculpt symmetry flags) */
typedef enum ePaintSymmetryFlags {
	PAINT_SYMM_X = (1 << 0),
	PAINT_SYMM_Y = (1 << 1),
	PAINT_SYMM_Z = (1 << 2),
	PAINT_SYMMETRY_FEATHER = (1 << 3),
	PAINT_TILE_X = (1 << 4),
	PAINT_TILE_Y = (1 << 5),
	PAINT_TILE_Z = (1 << 6),
} ePaintSymmetryFlags;

#define PAINT_SYMM_AXIS_ALL (PAINT_SYMM_X | PAINT_SYMM_Y | PAINT_SYMM_Z)

/* ToolSettings.edge_mode */
#define EDGE_MODE_SELECT				0
#define EDGE_MODE_TAG_SEAM				1
#define EDGE_MODE_TAG_SHARP				2
#define EDGE_MODE_TAG_CREASE			3
#define EDGE_MODE_TAG_BEVEL				4
#define EDGE_MODE_TAG_FREESTYLE			5

/* PhysicsSettings.flag */
#define PHYS_GLOBAL_GRAVITY		1

/* UnitSettings */

/* UnitSettings.system */
#define	USER_UNIT_NONE			0
#define	USER_UNIT_METRIC		1
#define	USER_UNIT_IMPERIAL		2
/* UnitSettings.flag */
#define	USER_UNIT_OPT_SPLIT		1
#define USER_UNIT_ROT_RADIANS	2

#ifdef __cplusplus
}
#endif

#endif  /* __DNA_SCENE_TYPES_H__ */
