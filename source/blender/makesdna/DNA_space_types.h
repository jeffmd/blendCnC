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
/** \file DNA_space_types.h
 *  \ingroup DNA
 *  \since mar-2001
 *  \author nzc
 *
 * Structs for each of space type in the user interface.
 */

#ifndef __DNA_SPACE_TYPES_H__
#define __DNA_SPACE_TYPES_H__

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_color_types.h"        /* for Histogram */
#include "DNA_vec_types.h"
#include "DNA_outliner_types.h"     /* for TreeStoreElem */
#include "DNA_image_types.h"        /* ImageUser */
/* Hum ... Not really nice... but needed for spacebuts. */
#include "DNA_view2d_types.h"

struct BLI_mempool;
struct FileLayout;
struct FileList;
struct FileSelectParams;
struct Histogram;
struct ID;
struct Image;
struct Scopes;
struct Script;
struct Text;
struct wmOperator;
struct wmTimer;

/* -------------------------------------------------------------------- */
/** \name SpaceLink (Base)
 * \{ */

/**
 * The base structure all the other spaces
 * are derived (implicitly) from. Would be
 * good to make this explicit.
 */
typedef struct SpaceLink {
	struct SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
} SpaceLink;

/** \} */

/* -------------------------------------------------------------------- */
/** \name Space Info
 * \{ */

/* Info Header */
typedef struct SpaceInfo {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	char rpt_mask;
	char pad[7];
} SpaceInfo;

/* SpaceInfo.rpt_mask */
typedef enum eSpaceInfo_RptMask {
	INFO_RPT_DEBUG  = (1 << 0),
	INFO_RPT_INFO   = (1 << 1),
	INFO_RPT_OP     = (1 << 2),
	INFO_RPT_WARN   = (1 << 3),
	INFO_RPT_ERR    = (1 << 4),
} eSpaceInfo_RptMask;

/** \} */

/* -------------------------------------------------------------------- */
/** \name Properties Editor
 * \{ */

/* Properties Editor */
typedef struct SpaceButs {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	short mainb, mainbo, mainbuser; /* context tabs */
	short re_align, align;          /* align for panels */
	short preview;                  /* preview is signal to refresh */
	/* texture context selector (material, lamp, particles, world, other) */
	short texture_context, texture_context_prev;
	char flag, pad[7];

	void *path;                     /* runtime */
	int pathflag, dataicon;         /* runtime */
	ID *pinid;

	void *texuser;
} SpaceButs;

/* button defines (deprecated) */

/* SpaceButs.mainb new */
typedef enum eSpaceButtons_Context {
	BCONTEXT_CNC = 0,
	BCONTEXT_SCENE = 1,
	BCONTEXT_WORLD = 2,
	BCONTEXT_OBJECT = 3,
	BCONTEXT_DATA = 4,
	BCONTEXT_MATERIAL = 5,
	BCONTEXT_TEXTURE = 6,
	BCONTEXT_PHYSICS = 8,
	BCONTEXT_MODIFIER = 10,
	BCONTEXT_CONSTRAINT = 11,

	/* always as last... */
	BCONTEXT_TOT
} eSpaceButtons_Context;

/* SpaceButs.flag */
typedef enum eSpaceButtons_Flag {
	SB_PRV_OSA = (1 << 0),
	SB_PIN_CONTEXT = (1 << 1),
	SB_TEX_USER_LIMITED = (1 << 3), /* Do not add materials, particles, etc. in TemplateTextureUser list. */
	SB_SHADING_CONTEXT = (1 << 4),
} eSpaceButtons_Flag;

/* SpaceButs.texture_context */
typedef enum eSpaceButtons_Texture_Context {
	SB_TEXC_MATERIAL = 0,
	SB_TEXC_WORLD = 1,
	SB_TEXC_LAMP = 2,
	SB_TEXC_OTHER = 4,
} eSpaceButtons_Texture_Context;

/* SpaceButs.align */
typedef enum eSpaceButtons_Align {
	BUT_FREE = 0,
	BUT_HORIZONTAL = 1,
	BUT_VERTICAL = 2,
	BUT_AUTO = 3,
} eSpaceButtons_Align;

/** \} */

/* -------------------------------------------------------------------- */
/** \name Outliner
 * \{ */

/* Outliner */
typedef struct SpaceOops {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	ListBase tree;

	/* treestore is an ordered list of TreeStoreElem's from outliner tree;
	 * Note that treestore may contain duplicate elements if element
	 * is used multiple times in outliner tree (e. g. linked objects)
	 * Also note that BLI_mempool can not be read/written in DNA directly,
	 * therefore readfile.c/writefile.c linearize treestore into TreeStore structure
	 */
	struct BLI_mempool *treestore;

	/* search stuff */
	char search_string[64];
	struct TreeStoreElem search_tse;

	short flag, outlinevis, storeflag, search_flags;

	/**
	 * Pointers to treestore elements, grouped by (id, type, nr)
	 * in hashtable for faster searching */
	void *treehash;
} SpaceOops;


/* SpaceOops.flag */
typedef enum eSpaceOutliner_Flag {
	SO_TESTBLOCKS           = (1 << 0),
	SO_NEWSELECTED          = (1 << 1),
	SO_HIDE_RESTRICTCOLS    = (1 << 2),
	SO_HIDE_KEYINGSETINFO   = (1 << 3),
	SO_SKIP_SORT_ALPHA      = (1 << 4),
} eSpaceOutliner_Flag;

/* SpaceOops.outlinevis */
typedef enum eSpaceOutliner_Mode {
	SO_ALL_SCENES = 0,
	SO_CUR_SCENE = 1,
	SO_VISIBLE = 2,
	SO_SELECTED = 3,
	SO_ACTIVE = 4,
	SO_SAME_TYPE = 5,
	SO_GROUPS = 6,
	SO_LIBRARIES = 7,
	SO_DATABLOCKS = 11,
	SO_USERDEF = 12,
	SO_ID_ORPHANS = 14,
} eSpaceOutliner_Mode;

/* SpaceOops.storeflag */
typedef enum eSpaceOutliner_StoreFlag {
	/* cleanup tree */
	SO_TREESTORE_CLEANUP    = (1 << 0),
	/* if set, it allows redraws. gets set for some allqueue events */
	SO_TREESTORE_REDRAW     = (1 << 1),
	/* rebuild the tree, similar to cleanup,
	 * but defer a call to BKE_outliner_treehash_rebuild_from_treestore instead */
	SO_TREESTORE_REBUILD    = (1 << 2),
} eSpaceOutliner_StoreFlag;

/* outliner search flags (SpaceOops.search_flags) */
typedef enum eSpaceOutliner_Search_Flags {
	SO_FIND_CASE_SENSITIVE  = (1 << 0),
	SO_FIND_COMPLETE        = (1 << 1),
	SO_SEARCH_RECURSIVE     = (1 << 2),
} eSpaceOutliner_Search_Flags;

/** \} */


/* Pointcache drawing data */
# /* Only store the data array in the cache to avoid constant reallocation. */
# /* No need to store when saved. */
typedef struct SpaceTimeCache {
	struct SpaceTimeCache *next, *prev;
	float *array;
} SpaceTimeCache;

/* SpaceTime.redraws (now bScreen.redraws_flag) */
typedef enum eScreen_Redraws_Flag {
	TIME_REGION            = (1 << 0),
	TIME_ALL_3D_WIN        = (1 << 1),
	TIME_ALL_ANIM_WIN      = (1 << 2),
	TIME_ALL_BUTS_WIN      = (1 << 3),
	TIME_ALL_IMAGE_WIN     = (1 << 6),
	// TIME_CONTINUE_PHYSICS  = (1 << 7), /* UNUSED */

	TIME_FOLLOW            = (1 << 15),
} eScreen_Redraws_Flag;

/* SpaceTime.cache */
typedef enum eTimeline_Cache_Flag {
	TIME_CACHE_DISPLAY       = (1 << 0),
	TIME_CACHE_DYNAMICPAINT  = (1 << 5),
	TIME_CACHE_RIGIDBODY     = (1 << 6),
} eTimeline_Cache_Flag;

/** \} */

/* -------------------------------------------------------------------- */
/** \name File Selector
 * \{ */

/* Config and Input for File Selector */
typedef struct FileSelectParams {
	char title[96]; /* title, also used for the text of the execute button */
	char dir[1090]; /* directory, FILE_MAX_LIBEXTRA, 1024 + 66, this is for extreme case when 1023 length path
	                 * needs to be linked in, where foo.blend/Armature need adding  */
	char pad_c1[2];
	char file[256]; /* file */
	char renamefile[256];
	char renameedit[256]; /* annoying but the first is only used for initialization */

	char filter_glob[256]; /* FILE_MAXFILE */ /* list of filetypes to filter */

	char filter_search[64];  /* text items' name must match to be shown. */
	int filter_id;  /* same as filter, but for ID types (aka library groups). */

	int active_file;    /* active file used for keyboard navigation */
	int highlight_file; /* file under cursor */
	int sel_first;
	int sel_last;
	unsigned short thumbnail_size;
	short pad;

	/* short */
	short type; /* XXXXX for now store type here, should be moved to the operator */
	short flag; /* settings for filter, hiding dots files,...  */
	short sort; /* sort order */
	short display; /* display mode flag */
	int filter; /* filter when (flags & FILE_FILTER) is true */

	short recursion_level;  /* max number of levels in dirtree to show at once, 0 to disable recursion. */

	/* XXX --- still unused -- */
	short f_fp; /* show font preview */
	char fp_str[8]; /* string to use for font preview */

	/* XXX --- end unused -- */
} FileSelectParams;

/* File Browser */
typedef struct SpaceFile {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	char _pad1[4];
	int scroll_offset;

	struct FileSelectParams *params; /* config and input for file select */

	struct FileList *files; /* holds the list of files to show */

	ListBase *folders_prev; /* holds the list of previous directories to show */
	ListBase *folders_next; /* holds the list of next directories (pushed from previous) to show */

	/* operator that is invoking fileselect
	 * op->exec() will be called on the 'Load' button.
	 * if operator provides op->cancel(), then this will be invoked
	 * on the cancel button.
	 */
	struct wmOperator *op;

	struct wmTimer *smoothscroll_timer;
	struct wmTimer *previews_timer;

	struct FileLayout *layout;

	short recentnr, bookmarknr;
	short systemnr, system_bookmarknr;
} SpaceFile;

/* FSMenuEntry's without paths indicate separators */
typedef struct FSMenuEntry {
	struct FSMenuEntry *next;

	char *path;
	char name[256];  /* FILE_MAXFILE */
	short save;
	short valid;
	short pad[2];
} FSMenuEntry;

/* FileSelectParams.display */
enum eFileDisplayType {
	FILE_DEFAULTDISPLAY = 0,
	FILE_SHORTDISPLAY = 1,
	FILE_LONGDISPLAY = 2,
	FILE_IMGDISPLAY = 3,
};

/* FileSelectParams.sort */
enum eFileSortType {
	FILE_SORT_NONE = 0,
	FILE_SORT_ALPHA = 1,
	FILE_SORT_EXTENSION = 2,
	FILE_SORT_TIME = 3,
	FILE_SORT_SIZE = 4,
};

/* these values need to be hardcoded in structs, dna does not recognize defines */
/* also defined in BKE */
#define FILE_MAXDIR         768
#define FILE_MAXFILE        256
#define FILE_MAX            1024

#define FILE_MAX_LIBEXTRA   (FILE_MAX + MAX_ID_NAME)

/* filesel types */
#define FILE_UNIX           8
#define FILE_BLENDER        8 /* don't display relative paths */
#define FILE_SPECIAL        9

#define FILE_LOADLIB        1
#define FILE_MAIN           2
#define FILE_LOADFONT       3

/* filesel op property -> action */
typedef enum eFileSel_Action {
	FILE_OPENFILE = 0,
	FILE_SAVE = 1,
} eFileSel_Action;

/* sfile->params->flag and simasel->flag */
/* Note: short flag, also used as 16 lower bits of flags in link/append code
 *       (WM and BLO code area, see BLO_LibLinkFlags in BLO_readfile.h). */
typedef enum eFileSel_Params_Flag {
	FILE_SHOWSHORT      = (1 << 0),
	FILE_RELPATH        = (1 << 1), /* was FILE_STRINGCODE */
	FILE_LINK           = (1 << 2),
	FILE_HIDE_DOT       = (1 << 3),
	FILE_AUTOSELECT     = (1 << 4),
	FILE_ACTIVELAY      = (1 << 5),
	FILE_DIRSEL_ONLY    = (1 << 7),
	FILE_FILTER         = (1 << 8),
	FILE_BOOKMARKS      = (1 << 9),
	FILE_GROUP_INSTANCE = (1 << 10),
} eFileSel_Params_Flag;


/* files in filesel list: file types
 * Note we could use mere values (instead of bitflags) for file types themselves,
 * but since we do not lack of bytes currently...
 */
typedef enum eFileSel_File_Types {
	FILE_TYPE_BLENDER           = (1 << 2),
	FILE_TYPE_BLENDER_BACKUP    = (1 << 3),
	FILE_TYPE_IMAGE             = (1 << 4),
	FILE_TYPE_PYSCRIPT          = (1 << 6),
	FILE_TYPE_FTFONT            = (1 << 7),
	FILE_TYPE_TEXT              = (1 << 9),
	/* 1 << 10 was FILE_TYPE_MOVIE_ICON, got rid of this so free slot for future type... */
	/** represents folders for filtering */
	FILE_TYPE_FOLDER            = (1 << 11),
	FILE_TYPE_BTX               = (1 << 12),
	/** from filter_glob operator property */
	FILE_TYPE_OPERATOR          = (1 << 14),
	FILE_TYPE_APPLICATIONBUNDLE = (1 << 15),

	/** An FS directory (i.e. S_ISDIR on its path is true). */
	FILE_TYPE_DIR               = (1 << 30),
	FILE_TYPE_BLENDERLIB        = (1u << 31),
} eFileSel_File_Types;

/* Selection Flags in filesel: struct direntry, unsigned char selflag */
typedef enum eDirEntry_SelectFlag {
/*	FILE_SEL_ACTIVE         = (1 << 1), */ /* UNUSED */
	FILE_SEL_HIGHLIGHTED    = (1 << 2),
	FILE_SEL_SELECTED       = (1 << 3),
	FILE_SEL_EDITING        = (1 << 4),
} eDirEntry_SelectFlag;

#define FILE_LIST_MAX_RECURSION 4

/* ***** Related to file browser, but never saved in DNA, only here to help with RNA. ***** */

/* About Unique identifier.
 * Stored in a CustomProps once imported.
 * Each engine is free to use it as it likes - it will be the only thing passed to it by blender to identify
 * asset/variant/version (concatenating the three into a single 48 bytes one).
 * Assumed to be 128bits, handled as four integers due to lack of real bytes proptype in RNA :|.
 */
#define ASSET_UUID_LENGTH     16

/* Used to communicate with asset engines outside of 'import' context. */
typedef struct AssetUUID {
	int uuid_asset[4];
	int uuid_variant[4];
	int uuid_revision[4];
} AssetUUID;

typedef struct AssetUUIDList {
	AssetUUID *uuids;
	int nbr_uuids, pad;
} AssetUUIDList;

/* Container for a revision, only relevant in asset context. */
typedef struct FileDirEntryRevision {
	struct FileDirEntryRevision *next, *prev;

	char *comment;
	void *pad;

	int uuid[4];

	uint64_t size;
	int64_t time;
	/* Temp caching of UI-generated strings... */
	char    size_str[16];
	char    time_str[8];
	char    date_str[16];
} FileDirEntryRevision;

/* Container for a variant, only relevant in asset context.
 * In case there are no variants, a single one shall exist, with NULL name/description. */
typedef struct FileDirEntryVariant {
	struct FileDirEntryVariant *next, *prev;

	int uuid[4];
	char *name;
	char *description;

	ListBase revisions;
	int nbr_revisions;
	int act_revision;
} FileDirEntryVariant;

/* Container for mere direntry, with additional asset-related data. */
typedef struct FileDirEntry {
	struct FileDirEntry *next, *prev;

	int uuid[4];
	char *name;
	char *description;

	/* Either point to active variant/revision if available, or own entry
	 * (in mere filebrowser case). */
	FileDirEntryRevision *entry;

	int typeflag;  /* eFileSel_File_Types */
	int blentype;  /* ID type, in case typeflag has FILE_TYPE_BLENDERLIB set. */

	char *relpath;

	void *poin;  /* TODO: make this a real ID pointer? */
	struct ImBuf *image;

	/* Tags are for info only, most of filtering is done in asset engine. */
	char **tags;
	int nbr_tags;

	short status;
	short flags;

	ListBase variants;
	int nbr_variants;
	int act_variant;
} FileDirEntry;

/* Array of direntries. */
/* This struct is used in various, different contexts.
 * In Filebrowser UI, it stores the total number of available entries, the number of visible (filtered) entries,
 *                    and a subset of those in 'entries' ListBase, from idx_start (included) to idx_end (excluded).
 * In AssetEngine context (i.e. outside of 'browsing' context), entries contain all needed data, there is no filtering,
 *                        so nbr_entries_filtered, entry_idx_start and entry_idx_end should all be set to -1.
 */
typedef struct FileDirEntryArr {
	ListBase entries;
	int nbr_entries;
	int nbr_entries_filtered;
	int entry_idx_start, entry_idx_end;

	char root[1024];	 /* FILE_MAX */
} FileDirEntryArr;

/* FileDirEntry.status */
enum {
	ASSET_STATUS_LOCAL  = 1 << 0,  /* If active uuid is available locally/immediately. */
	ASSET_STATUS_LATEST = 1 << 1,  /* If active uuid is latest available version. */
};

/* FileDirEntry.flags */
enum {
	FILE_ENTRY_INVALID_PREVIEW = 1 << 0,  /* The preview for this entry could not be generated. */
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Image/UV Editor
 * \{ */

/* Image/UV Editor */
typedef struct SpaceImage {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	struct Image *image;
	struct ImageUser iuser;

	struct Scopes scopes;           /* histogram waveform and vectorscope */
	struct Histogram sample_line_hist;  /* sample line histogram */

	struct bGPdata *gpd;            /* grease pencil data */

	float cursor[2];                /* UV editor 2d cursor */
	float xof, yof;                 /* user defined offset, image is centered */
	float zoom;                     /* user defined zoom level */
	float centx, centy;             /* storage for offset while render drawing */

	char  mode;                     /* view/paint/mask */
	char  pin;
	short pad;
	short curtile; /* the currently active tile of the image when tile is enabled, is kept in sync with the active faces tile */
	short lock;
	char dt_uv; /* UV draw type */
	char sticky; /* sticky selection type */
	char dt_uvstretch;
	char around;

	/* Filter settings when editor shows other object's UVs. */
	int other_uv_filter;

	int flag;

} SpaceImage;


/* SpaceImage.dt_uv */
typedef enum eSpaceImage_UVDT {
	SI_UVDT_OUTLINE = 0,
	SI_UVDT_DASH = 1,
	SI_UVDT_BLACK = 2,
	SI_UVDT_WHITE = 3,
} eSpaceImage_UVDT;

/* SpaceImage.dt_uvstretch */
typedef enum eSpaceImage_UVDT_Stretch {
	SI_UVDT_STRETCH_ANGLE = 0,
	SI_UVDT_STRETCH_AREA = 1,
} eSpaceImage_UVDT_Stretch;

/* SpaceImage.mode */
typedef enum eSpaceImage_Mode {
	SI_MODE_VIEW  = 0,
	SI_MODE_PAINT = 1,
	SI_MODE_MASK  = 2   /* note: mesh edit mode overrides mask */
} eSpaceImage_Mode;

/* SpaceImage.sticky
 * Note DISABLE should be 0, however would also need to re-arrange icon order,
 * also, sticky loc is the default mode so this means we don't need to 'do_versions' */
typedef enum eSpaceImage_Sticky {
	SI_STICKY_LOC      = 0,
	SI_STICKY_DISABLE  = 1,
	SI_STICKY_VERTEX   = 2,
} eSpaceImage_Sticky;

/* SpaceImage.flag */
typedef enum eSpaceImage_Flag {
	SI_EDITTILE           = (1 << 1),     /* XXX - not used but should be? */
	SI_CLIP_UV            = (1 << 2),
	SI_NO_DRAWFACES       = (1 << 4),
	SI_DRAWSHADOW         = (1 << 5),
	SI_COORDFLOATS        = (1 << 9),
	SI_PIXELSNAP          = (1 << 10),
	SI_LIVE_UNWRAP        = (1 << 11),
	SI_USE_ALPHA          = (1 << 12),
	SI_SHOW_ALPHA         = (1 << 13),
	SI_SHOW_ZBUF          = (1 << 14),

	/* next two for render window display */
	SI_PREVSPACE          = (1 << 15),
	SI_FULLWINDOW         = (1 << 16),

	/* this means that the image is drawn until it reaches the view edge,
	 * in the image view, it's unrelated to the 'tile' mode for texface
	 */
	SI_DRAW_TILE          = (1 << 19),
	SI_SMOOTH_UV          = (1 << 20),
	SI_DRAW_STRETCH       = (1 << 21),
	SI_DRAW_OTHER         = (1 << 23),

	SI_COLOR_CORRECTION   = (1 << 24),

	SI_NO_DRAW_TEXPAINT   = (1 << 25),
	SI_DRAW_METADATA      = (1 << 26),

	SI_SHOW_R             = (1 << 27),
	SI_SHOW_G             = (1 << 28),
	SI_SHOW_B             = (1 << 29),
} eSpaceImage_Flag;

/* SpaceImage.other_uv_filter */
typedef enum eSpaceImage_OtherUVFilter {
	SI_FILTER_SAME_IMAGE    = 0,
	SI_FILTER_ALL           = 1,
} eSpaceImage_OtherUVFilter;

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Editor
 * \{ */

/* Text Editor */
typedef struct SpaceText {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	struct Text *text;

	int top, viewlines;
	short flags, menunr;

	short lheight;      /* user preference, is font_size! */
	char cwidth, linenrs_tot;       /* runtime computed, character width and the number of chars to use when showing line numbers */
	int left;
	int showlinenrs;
	int tabnumber;

	short showsyntax;
	short line_hlight;
	short overwrite;
	short live_edit; /* run python while editing, evil */
	float pix_per_line;

	struct rcti txtscroll, txtbar;

	int wordwrap, doplugins;

	char findstr[256];      /* ST_MAX_FIND_STR */
	char replacestr[256];   /* ST_MAX_FIND_STR */

	short margin_column;	/* column number to show right margin at */
	short lheight_dpi;		/* actual lineheight, dpi controlled */
	char pad[4];

	void *drawcache; /* cache for faster drawing */

	float scroll_accum[2]; /* runtime, for scroll increments smaller than a line */
} SpaceText;


/* SpaceText flags (moved from DNA_text_types.h) */
typedef enum eSpaceText_Flags {
	/* scrollable */
	ST_SCROLL_SELECT        = (1 << 0),
	/* clear namespace after script execution (BPY_main.c) */
	ST_CLEAR_NAMESPACE      = (1 << 4),

	ST_FIND_WRAP            = (1 << 5),
	ST_FIND_ALL             = (1 << 6),
	ST_SHOW_MARGIN          = (1 << 7),
	ST_MATCH_CASE           = (1 << 8),

	ST_FIND_ACTIVATE		= (1 << 9),
} eSpaceText_Flags;

/* SpaceText.findstr/replacestr */
#define ST_MAX_FIND_STR     256

/** \} */

/* -------------------------------------------------------------------- */
/** \name Script View (Obsolete)
 * \{ */

/* Script Runtime Data - Obsolete (pre 2.5) */
typedef struct Script {
	ID id;

	void *py_draw;
	void *py_event;
	void *py_button;
	void *py_browsercallback;
	void *py_globaldict;

	int flags, lastspace;
	/* store the script file here so we can re-run it on loading blender, if "Enable Scripts" is on */
	char scriptname[1024]; /* 1024 = FILE_MAX */
	char scriptarg[256]; /* 1024 = FILE_MAX */
} Script;
#define SCRIPT_SET_NULL(_script) _script->py_draw = _script->py_event = _script->py_button = _script->py_browsercallback = _script->py_globaldict = NULL; _script->flags = 0

/* Script View - Obsolete (pre 2.5) */
typedef struct SpaceScript {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	struct Script *script;

	short flags, menunr;
	int pad1;

	void *but_refs;
} SpaceScript;

/** \} */

/* -------------------------------------------------------------------- */
/** \name Console
 * \{ */

/* Console content */
typedef struct ConsoleLine {
	struct ConsoleLine *next, *prev;

	/* keep these 3 vars so as to share free, realloc funcs */
	int len_alloc;  /* allocated length */
	int len;    /* real len - strlen() */
	char *line;

	int cursor;
	int type; /* only for use when in the 'scrollback' listbase */
} ConsoleLine;

/* ConsoleLine.type */
typedef enum eConsoleLine_Type {
	CONSOLE_LINE_OUTPUT = 0,
	CONSOLE_LINE_INPUT = 1,
	CONSOLE_LINE_INFO = 2, /* autocomp feedback */
	CONSOLE_LINE_ERROR = 3,
} eConsoleLine_Type;


/* Console View */
typedef struct SpaceConsole {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	/* space vars */
	int lheight, pad;

	ListBase scrollback; /* ConsoleLine; output */
	ListBase history; /* ConsoleLine; command history, current edited line is the first */
	char prompt[256];
	char language[32]; /* multiple consoles are possible, not just python */

	int sel_start;
	int sel_end;
} SpaceConsole;

/** \} */

/* -------------------------------------------------------------------- */
/** \name User Preferences
 * \{ */

typedef struct SpaceUserPref {
	SpaceLink *next, *prev;
	ListBase regionbase;        /* storage of regions for inactive spaces */
	char spacetype;
	char link_flag;
	char _pad0[6];
	/* End 'SpaceLink' header. */

	char _pad1[7];
	char filter_type;
	char filter[64];        /* search term for filtering in the UI */
} SpaceUserPref;

/** \} */


/* -------------------------------------------------------------------- */
/** \name Space Defines (eSpace_Type)
 * \{ */

/* space types, moved from DNA_screen_types.h */
/* Do NOT change order, append on end. types are hardcoded needed */
typedef enum eSpace_Type {
	SPACE_EMPTY    = 0,
	SPACE_VIEW3D   = 1,
	SPACE_OUTLINER = 3,
	SPACE_BUTS     = 4,
	SPACE_FILE     = 5,
	SPACE_IMAGE    = 6,
	SPACE_INFO     = 7,
	SPACE_TEXT     = 9,
	/* TODO: fully deprecate */
	SPACE_SCRIPT   = 14, /* Deprecated */
	SPACE_CONSOLE  = 18,
	SPACE_USERPREF = 19,

	SPACE_TYPE_LAST = SPACE_USERPREF
} eSpace_Type;

/* use for function args */
#define SPACE_TYPE_ANY -1

#define IMG_SIZE_FALLBACK 256

/** \} */

#endif  /* __DNA_SPACE_TYPES_H__ */
