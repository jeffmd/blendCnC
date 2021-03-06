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

/** \file DNA_material_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_MATERIAL_TYPES_H__
#define __DNA_MATERIAL_TYPES_H__

#include "DNA_defs.h"
#include "DNA_ID.h"
#include "DNA_listBase.h"

#ifndef MAX_MTEX
#define MAX_MTEX	18
#endif

struct ColorBand;
struct Group;
struct Image;
struct MTex;

/* WATCH IT: change type? also make changes in ipo.h  */

typedef struct VolumeSettings {
	float density;
	float emission;
	float scattering;
	float reflection;

	float emission_col[3];
	float transmission_col[3];
	float reflection_col[3];

	float density_scale;
	float depth_cutoff;
	float asymmetry;

	short stepsize_type;
	short shadeflag;
	short shade_type;
	short precache_resolution;

	float stepsize;
	float ms_diff;
	float ms_intensity;
	float ms_spread;
} VolumeSettings;

/* Game Engine Options (old Texface mode, transp and flag) */
typedef struct GameSettings {
	int flag;
	int alpha_blend;
	int face_orientation;
	int pad1;
} GameSettings;

typedef struct TexPaintSlot {
	struct Image *ima; /* image to be painted on */
	char *uvname;      /* customdata index for uv layer, MAX_NAME*/
	int index;         /* index for mtex slot in material for blender internal */
	int pad;
} TexPaintSlot;

typedef struct Material {
	ID id;

	short material_type, flag;
	/* note, keep this below synced with render_types.h */
	float r, g, b;
	float specr, specg, specb;
	float mirr, mirg, mirb;
	float ambr, ambb, ambg;
	float amb, emit, ang, spectra, ray_mirror;
	float alpha, ref, spec, zoffs, add;
	float translucency;
	/* end synced with render_types.h */

	struct VolumeSettings vol;
	struct GameSettings game;

	float fresnel_mir, fresnel_mir_i;
	float fresnel_tra, fresnel_tra_i;
	float filter;		/* filter added, for raytrace transparency and transmissivity */
	float tx_limit, tx_falloff;
	short ray_depth, ray_depth_tra;
	short har;
	char seed1, seed2;

	float gloss_mir, gloss_tra;
	short samp_gloss_mir, samp_gloss_tra;
	float adapt_thresh_mir, adapt_thresh_tra;
	float aniso_gloss_mir;
	float dist_mir;
	short fadeto_mir;
	short shade_flag;		/* like Cubic interpolation */

	int mode, mode_l;		/* mode_l is the or-ed result of all layer modes */
	int mode2, mode2_l;		/* additional mode flags */
	short flarec, starc, linec, ringc;
	float hasize, flaresize, subsize, flareboost;
	float strand_sta, strand_end, strand_ease, strand_surfnor;
	float strand_min, strand_widthfade;
	char strand_uvname[64];	/* MAX_CUSTOMDATA_LAYER_NAME */

	float sbias;			/* shadow bias to prevent terminator prob */
	float lbias;			/* factor to multiply lampbias with (0.0 = no mult) */
	float shad_alpha;		/* in use for irregular shadowbuffer */
	int	septex;

	/* for buttons and render*/
	char rgbsel, texact, pr_type, use_nodes;
	short pr_lamp, pr_texture, ml_flag;	/* ml_flag is for disable base material */

	/* mapping */
	char mapflag, pad;

	/* shaders */
	short diff_shader, spec_shader;
	float roughness, refrac;
	/* XXX param[4] needs review and improvement (shader system as whole anyway)
	 * This is nasty reused variable for different goals and not easy to RNAify nicely. -jesterKing */
	float param[4];		/* size, smooth, size, smooth, for toonshader, 0 (fac) and 1 (fresnel) also for fresnel shader */
	float rms;
	float darkness;

	/* runtime - OR'd from 'mtex' */
	short texco, mapto;

	/* ramp colors */
	struct ColorBand *ramp_col;
	struct ColorBand *ramp_spec;
	char rampin_col, rampin_spec;
	char rampblend_col, rampblend_spec;
	short ramp_show, pad3;
	float rampfac_col, rampfac_spec;

	struct MTex *mtex[18];		/* MAX_MTEX */
	struct Group *group;	/* light group */
	struct PreviewImage *preview;

	/* dynamic properties */
	float friction, fh, reflect;
	float fhdist, xyfrict;
	short dynamode, pad2;

	/* subsurface scattering */
	float sss_radius[3], sss_col[3];
	float sss_error, sss_scale, sss_ior;
	float sss_colfac, sss_texfac;
	float sss_front, sss_back;
	short sss_flag, sss_preset;

	int mapto_textured;	/* render-time cache to optimize texture lookups */
	short shadowonly_flag;  /* "shadowsonly" type */
	short index;            /* custom index for render passes */

	/* Freestyle line settings */
	float line_col[4];
	short line_priority;
	short vcol_alpha;

	/* texture painting */
	short paint_active_slot;
	short paint_clone_slot;
	short tot_slots;
	short pad4[3];

	/* multiple tangent (Normal Map node) */
	char nmap_tangent_names[9][64]; /* [MAX_MTFACE+1][MAX_NAME]; +1 for empty name */
	int nmap_tangent_names_count, pad5;

	struct TexPaintSlot *texpaintslot; /* cached slot for painting. Make sure to recalculate before use
	                                    * with refresh_texpaint_image_cache */
	ListBase gpumaterial;		/* runtime */
} Material;


/* **************** GAME PROPERTIES ********************* */
// Blend Transparency Options - alpha_blend /* match GPU_material::GPUBlendMode */
#define GEMAT_SOLID             0 /* GPU_BLEND_SOLID */
#define GEMAT_ADD               (1 << 0) /* GPU_BLEND_ADD */
#define GEMAT_ALPHA             (1 << 1) /* GPU_BLEND_ALPHA */
#define GEMAT_CLIP              (1 << 2) /* GPU_BLEND_CLIP */
#define GEMAT_ALPHA_SORT        (1 << 3) /* GPU_BLEND_ALPHA_SORT */
#define GEMAT_ALPHA_TO_COVERAGE (1 << 4) /* GPU_BLEND_ALPHA_TO_COVERAGE */

// Game Options - flag
#define GEMAT_BACKCULL      (1 << 4) /* KX_BACKCULL */
#define GEMAT_SHADED        (1 << 5) /* KX_LIGHT */
#define GEMAT_TEXT          (1 << 6) /* RAS_RENDER_3DPOLYGON_TEXT */
#define GEMAT_NOPHYSICS     (1 << 7)
#define GEMAT_INVISIBLE     (1 << 8)

// Face Orientation Options - face_orientation
#define GEMAT_NORMAL        0
#define GEMAT_HALO          (1 << 9)  /* BILLBOARD_SCREENALIGNED  */
#define GEMAT_BILLBOARD     (1 << 10) /* BILLBOARD_AXISALIGNED */
#define GEMAT_SHADOW        (1 << 11) /* SHADOW */

// Use Textures - not defined directly in the UI
#define GEMAT_TEX		4096 /* KX_TEX */


/* **************** MATERIAL ********************* */

/* maximum number of materials per material array.
 * (on object, mesh, lamp, etc.). limited by
 * short mat_nr in verts, faces.
 * -1 because for active material we store the index + 1 */
#define MAXMAT			(32767-1)

/* material_type */
#define MA_TYPE_SURFACE	0
#define MA_TYPE_HALO	1
#define MA_TYPE_VOLUME	2
#define MA_TYPE_WIRE	3

/* flag */
		/* for render */
#define MA_IS_USED      (1 << 0)
		/* for dopesheet */
#define MA_DS_EXPAND    (1 << 1)
		/* for dopesheet (texture stack expander)
		 * NOTE: this must have the same value as other texture stacks,
		 * otherwise anim-editors will not read correctly
		 */
#define MA_DS_SHOW_TEXS (1 << 2)

/* mode (is int) */
#define MA_TRACEBLE     (1 << 0)
#define MA_SHADOW       (1 << 1)
#define MA_SHLESS       (1 << 2)
#define MA_WIRE         (1 << 3)            /* deprecated */
#define MA_VERTEXCOL    (1 << 4)
#define MA_HALO_SOFT    (1 << 4)
#define MA_HALO         (1 << 5)            /* deprecated */
#define MA_ZTRANSP      (1 << 6)
#define MA_VERTEXCOLP   (1 << 7)
#define MA_ZINV         (1 << 8)
#define MA_HALO_RINGS   (1 << 8)
#define MA_ENV          (1 << 9)
#define MA_HALO_LINES   (1 << 9)
#define MA_ONLYSHADOW   (1 << 10)
#define MA_HALO_XALPHA  (1 << 10)
#define MA_STAR         (1 << 11)
#define MA_FACETEXTURE  (1 << 11)
#define MA_HALOTEX      (1 << 12)
#define MA_HALOPUNO     (1 << 13)
#define MA_ONLYCAST     (1 << 13)
#define MA_NOMIST       (1 << 14)
#define MA_HALO_SHADE   (1 << 14)
#define MA_HALO_FLARE   (1 << 15)
#define MA_TRANSP       (1 << 16)
#define MA_RAYTRANSP    (1 << 17)
#define MA_RAYMIRROR    (1 << 18)
#define MA_SHADOW_TRA   (1 << 19)
#define MA_RAMP_COL     (1 << 20)
#define MA_RAMP_SPEC    (1 << 21)
#define MA_RAYBIAS      (1 << 22)
#define MA_FULL_OSA     (1 << 23)
#define MA_TANGENT_STR  (1 << 24)
#define MA_SHADBUF      (1 << 25)
		/* note; we drop MA_TANGENT_STR later to become tangent_u */
#define MA_TANGENT_V    (1 << 26)
/* qdn: a bit clumsy this, tangents needed for normal maps separated from shading */
#define MA_NORMAP_TANG  (1 << 27)
#define MA_GROUP_NOLAY  (1 << 28)
#define MA_FACETEXTURE_ALPHA    (1 << 29)
#define MA_STR_B_UNITS  (1 << 30)
#define MA_STR_SURFDIFF (1 << 31u)

#define MA_MODE_MASK    (((1 << 31u) - 1) & ~(1 << 28u)) /* all valid mode bits */
#define MA_MODE_PIPELINE	(MA_TRANSP | MA_ZTRANSP | MA_RAYTRANSP \
				 | MA_TRACEBLE | MA_FULL_OSA | MA_ENV | MA_ZINV \
				 | MA_ONLYCAST | MA_SHADBUF)

/* mode2 (is int) */
#define MA_CASTSHADOW		(1 << 0)
#define MA_MODE2_PIPELINE	(MA_CASTSHADOW)
#define MA_TANGENT_CONCRETE	(1 << 1)

/* mapflag */
#define MA_MAPFLAG_UVPROJECT (1 << 0)

/* ray mirror fadeout */
#define MA_RAYMIR_FADETOSKY	0
#define MA_RAYMIR_FADETOMAT	1

/* shadowonly_flag */
#define MA_SO_OLD			0
#define MA_SO_SHADOW		1
#define MA_SO_SHADED		2

/* shade_flag */
#define MA_CUBIC			1
#define MA_OBCOLOR			2
#define MA_APPROX_OCCLUSION	4
#define MA_GROUP_LOCAL      8

/* diff_shader */
#define MA_DIFF_LAMBERT		0
#define MA_DIFF_ORENNAYAR	1
#define MA_DIFF_TOON		2
#define MA_DIFF_MINNAERT    3
#define MA_DIFF_FRESNEL     4

/* spec_shader */
#define MA_SPEC_COOKTORR	0
#define MA_SPEC_PHONG		1
#define MA_SPEC_BLINN		2
#define MA_SPEC_TOON		3
#define MA_SPEC_WARDISO		4

/* dynamode */
// #define MA_DRAW_DYNABUTS    1		/* deprecated */
#define MA_FH_NOR	        2

/* ramps */
#define MA_RAMP_IN_SHADER	0
#define MA_RAMP_IN_ENERGY	1
#define MA_RAMP_IN_NOR		2
#define MA_RAMP_IN_RESULT	3

#define MA_RAMP_BLEND		0
#define MA_RAMP_ADD			1
#define MA_RAMP_MULT		2
#define MA_RAMP_SUB			3
#define MA_RAMP_SCREEN		4
#define MA_RAMP_DIV			5
#define MA_RAMP_DIFF		6
#define MA_RAMP_DARK		7
#define MA_RAMP_LIGHT		8
#define MA_RAMP_OVERLAY		9
#define MA_RAMP_DODGE		10
#define MA_RAMP_BURN		11
#define MA_RAMP_HUE			12
#define MA_RAMP_SAT			13
#define MA_RAMP_VAL			14
#define MA_RAMP_COLOR		15
#define MA_RAMP_SOFT        16
#define MA_RAMP_LINEAR      17

/* texco */
#define TEXCO_ORCO      (1 << 0)
#define TEXCO_REFL      (1 << 1)
#define TEXCO_NORM      (1 << 2)
#define TEXCO_GLOB      (1 << 3)
#define TEXCO_UV        (1 << 4)
#define TEXCO_OBJECT    (1 << 5)
#define TEXCO_LAVECTOR  (1 << 6)
#define TEXCO_VIEW      (1 << 7)
#define TEXCO_STICKY_   (1 << 8)  // DEPRECATED
#define TEXCO_OSA       (1 << 9)
#define TEXCO_WINDOW    (1 << 10)
#define NEED_UV         (1 << 11)
#define TEXCO_TANGENT   (1 << 12)
	/* still stored in vertex->accum, 1 D */
#define TEXCO_STRAND    (1 << 13)
/** strand is used for normal materials, particle for halo materials */
#define TEXCO_PARTICLE  (1 << 13)
#define TEXCO_STRESS    (1 << 14)
#define TEXCO_SPEED     (1 << 15)

/* mapto */
#define MAP_COL         (1 << 0)
#define MAP_NORM        (1 << 1)
#define MAP_COLSPEC     (1 << 2)
#define MAP_COLMIR      (1 << 3)
#define MAP_VARS        (((1 << 16) - 1) & ~((1 << 4) - 1))
#define MAP_REF         (1 << 4)
#define MAP_SPEC        (1 << 5)
#define MAP_EMIT        (1 << 6)
#define MAP_ALPHA       (1 << 7)
#define MAP_HAR         (1 << 8)
#define MAP_RAYMIRR     (1 << 9)
#define MAP_TRANSLU     (1 << 10)
#define MAP_AMB         (1 << 11)
#define MAP_DISPLACE    (1 << 12)
#define MAP_WARP        (1 << 13)
// #define MAP_LAYER    (1 << 14)       /* unused */

/* volume mapto - reuse definitions for now - a bit naughty! */
#define MAP_DENSITY             (1 << 7)
#define MAP_EMISSION            (1 << 6)
#define MAP_EMISSION_COL        (1 << 0)
#define MAP_SCATTERING          (1 << 4)
#define MAP_TRANSMISSION_COL    (1 << 3)
#define MAP_REFLECTION_COL      (1 << 2)
#define MAP_REFLECTION          (1 << 5)


/* mapto for halo */
//#define MAP_HA_COL        (1 << 0)
//#define MAP_HA_ALPHA      (1 << 7)
//#define MAP_HA_HAR        (1 << 8)
//#define MAP_HA_SIZE       (1 << 1)
//#define MAP_HA_ADD        (1 << 6)

/* pmapto */
/* init */
#define MAP_PA_INIT     ((1 << 5) - 1)
#define MAP_PA_TIME     (1 << 0)
#define MAP_PA_LIFE     (1 << 1)
#define MAP_PA_DENS     (1 << 2)
#define MAP_PA_SIZE     (1 << 3)
#define MAP_PA_LENGTH   (1 << 4)
/* reset */
#define MAP_PA_IVEL     (1 << 5)
/* physics */
#define MAP_PA_PVEL     (1 << 6)
/* path cache */
#define MAP_PA_CLUMP    (1 << 7)
#define MAP_PA_KINK     (1 << 8)
#define MAP_PA_ROUGH    (1 << 9)
#define MAP_PA_FREQ     (1 << 10)

/* pr_type */
#define MA_FLAT			0
#define MA_SPHERE		1
#define MA_CUBE			2
#define MA_MONKEY		3
#define MA_SPHERE_A		4
#define MA_TEXTURE		5
#define MA_LAMP			6
#define MA_SKY			7
#define MA_HAIR			10
#define MA_ATMOS		11

/* sss_flag */
#define MA_DIFF_SSS		1

/* vol_stepsize_type */
#define MA_VOL_STEP_RANDOMIZED	0
#define MA_VOL_STEP_CONSTANT	1
#define MA_VOL_STEP_ADAPTIVE	2

/* vol_shadeflag */
#define MA_VOL_RECV_EXT_SHADOW	1
#define MA_VOL_PRECACHESHADING	8

/* vol_shading_type */
#define MA_VOL_SHADE_SHADELESS					0
#define MA_VOL_SHADE_SHADOWED					2
#define MA_VOL_SHADE_SHADED						1
#define MA_VOL_SHADE_MULTIPLE					3
#define MA_VOL_SHADE_SHADEDPLUSMULTIPLE			4

#endif
