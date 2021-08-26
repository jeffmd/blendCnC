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

/** \file DNA_ID.h
 *  \ingroup DNA
 *  \brief ID and Library types, which are fundamental for sdna.
 */

#ifndef __DNA_ID_H__
#define __DNA_ID_H__

#include "DNA_listBase.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FileData;
struct GPUTexture;
struct ID;
struct Library;
struct PackedFile;

typedef struct IDPropertyData {
	void *pointer;
	ListBase group;
	int val, val2;  /* note, we actually fit a double into these two ints */
} IDPropertyData;

typedef struct IDProperty {
	struct IDProperty *next, *prev;
	char type, subtype;
	short flag;
	char name[64];  /* MAX_IDPROP_NAME */

	/* saved is used to indicate if this struct has been saved yet.
	 * seemed like a good idea as a pad var was needed anyway :) */
	int saved;
	IDPropertyData data;  /* note, alignment for 64 bits */

	/* array length, also (this is important!) string length + 1.
	 * the idea is to be able to reuse array realloc functions on strings.*/
	int len;

	/* Strings and arrays are both buffered, though the buffer isn't saved. */
	/* totallen is total length of allocated array/string, including a buffer.
	 * Note that the buffering is mild; the code comes from python's list implementation. */
	int totallen;
} IDProperty;

#define MAX_IDPROP_NAME 64
#define DEFAULT_ALLOC_FOR_NULL_STRINGS  64

/*->type*/
enum {
	IDP_STRING           = 0,
	IDP_INT              = 1,
	IDP_FLOAT            = 2,
	IDP_ARRAY            = 5,
	IDP_GROUP            = 6,
	IDP_ID               = 7,
	IDP_DOUBLE           = 8,
	IDP_IDPARRAY         = 9,
	IDP_NUMTYPES         = 10,
};

/*->subtype */

/* IDP_STRING */
enum {
	IDP_STRING_SUB_UTF8  = 0,  /* default */
	IDP_STRING_SUB_BYTE  = 1,  /* arbitrary byte array, _not_ null terminated */
};

/*->flag*/
enum {
	IDP_FLAG_GHOST       = 1 << 7,  /* this means the property is set but RNA will return false when checking
	                                 * 'RNA_property_is_set', currently this is a runtime flag */
};

/* add any future new id property types here.*/

/* watch it: Sequence has identical beginning. */
/**
 * ID is the first thing included in all serializable types. It
 * provides a common handle to place all data in double-linked lists.
 * */

/* 2 characters for ID code and 64 for actual name */
#define MAX_ID_NAME  66

/* There's a nasty circular dependency here.... 'void *' to the rescue! I
 * really wonder why this is needed. */
typedef struct ID {
	void *next, *prev;
	struct ID *newid;
	struct Library *lib;
	char name[66]; /* MAX_ID_NAME */
	/**
	 * LIB_... flags report on status of the datablock this ID belongs to (persistent, saved to and read from .blend).
	 */
	short flag;
	/**
	 * LIB_TAG_... tags (runtime only, cleared at read time).
	 */
	int tag;
	int us;
	int icon_id;
	int recalc;
	int pad;
	IDProperty *properties;

	void *py_instance;
} ID;

/**
 * For each library file used, a Library struct is added to Main
 * WARNING: readfile.c, expand_doit() reads this struct without DNA check!
 */
typedef struct Library {
	ID id;
	struct FileData *filedata;
	char name[1024];  /* path name used for reading, can be relative and edited in the outliner */

	/* absolute filepath, this is only for convenience, 'name' is the real path used on file read but in
	 * some cases its useful to access the absolute one.
	 * This is set on file read.
	 * Use BKE_library_filepath_set() rather than setting 'name' directly and it will be kept in sync - campbell */
	char filepath[1024];

	struct Library *parent;	/* set for indirectly linked libs, used in the outliner and while reading */

	struct PackedFile *packedfile;

	/* Temp data needed by read/write code. */
	int temp_index;
	short versionfile, subversionfile;  /* see BLENDER_VERSION, BLENDER_SUBVERSION, needed for do_versions */
} Library;

enum eIconSizes {
	ICON_SIZE_ICON = 0,
	ICON_SIZE_PREVIEW = 1,

	NUM_ICON_SIZES
};

/* for PreviewImage->flag */
enum ePreviewImage_Flag {
	PRV_CHANGED          = (1 << 0),
	PRV_USER_EDITED      = (1 << 1),  /* if user-edited, do not auto-update this anymore! */
};

/* for PreviewImage->tag */
enum  {
	PRV_TAG_DEFFERED           = (1 << 0),  /* Actual loading of preview is deferred. */
	PRV_TAG_DEFFERED_RENDERING = (1 << 1),  /* Deferred preview is being loaded. */
	PRV_TAG_DEFFERED_DELETE    = (1 << 2),  /* Deferred preview should be deleted asap. */
};

typedef struct PreviewImage {
	/* All values of 2 are really NUM_ICON_SIZES */
	unsigned int w[2];
	unsigned int h[2];
	short flag[2];
	short changed_timestamp[2];
	unsigned int *rect[2];

	/* Runtime-only data. */
	struct GPUTexture *gputexture[2];
	int icon_id;  /* Used by previews outside of ID context. */

	short tag;  /* Runtime data. */
	char pad[2];
} PreviewImage;

#define PRV_DEFERRED_DATA(prv) \
	(CHECK_TYPE_INLINE(prv, PreviewImage *), BLI_assert((prv)->tag & PRV_TAG_DEFFERED), (void *)((prv) + 1))

/**
 * Defines for working with IDs.
 *
 * The tags represent types! This is a dirty way of enabling RTTI. The
 * sig_byte end endian defines aren't really used much.
 *
 **/

#ifdef __BIG_ENDIAN__
   /* big endian */
#  define MAKE_ID2(c, d)  ((c) << 8 | (d))
#else
   /* little endian  */
#  define MAKE_ID2(c, d)  ((d) << 8 | (c))
#endif

/**
 * ID from database.
 *
 * Written to #BHead.code (for file IO)
 * and the first 2 bytes of #ID.name (for runtime checks, see #GS macro).
 */
typedef enum ID_Type {
	ID_SCE  = MAKE_ID2('S', 'C'), /* Scene */
	ID_LI   = MAKE_ID2('L', 'I'), /* Library */
	ID_OB   = MAKE_ID2('O', 'B'), /* Object */
	ID_ME   = MAKE_ID2('M', 'E'), /* Mesh */
	ID_CU   = MAKE_ID2('C', 'U'), /* Curve */
	ID_MA   = MAKE_ID2('M', 'A'), /* Material */
	ID_TE   = MAKE_ID2('T', 'E'), /* Tex (Texture) */
	ID_IM   = MAKE_ID2('I', 'M'), /* Image */
	ID_LA   = MAKE_ID2('L', 'A'), /* Lamp */
	ID_CA   = MAKE_ID2('C', 'A'), /* Camera */
	ID_WO   = MAKE_ID2('W', 'O'), /* World */
	ID_SCR  = MAKE_ID2('S', 'R'), /* Screen */
	ID_VF   = MAKE_ID2('V', 'F'), /* VFont (Vector Font) */
	ID_TXT  = MAKE_ID2('T', 'X'), /* Text */
	ID_GR   = MAKE_ID2('G', 'R'), /* Group */
	ID_WM   = MAKE_ID2('W', 'M'), /* WindowManager */
	ID_CF   = MAKE_ID2('C', 'F'), /* CacheFile */
} ID_Type;

/* Only used as 'placeholder' in .blend files for directly linked datablocks. */
#define ID_ID       MAKE_ID2('I', 'D') /* (internal use only) */

/* Deprecated. */
#define ID_SCRN	    MAKE_ID2('S', 'N')

/* NOTE! Fake IDs, needed for g.sipo->blocktype or outliner */
/* constraint */
#define ID_CO		MAKE_ID2('C', 'O')
/* used in outliner... */

#define ID_FAKE_USERS(id) ((((ID *)id)->flag & LIB_FAKEUSER) ? 1 : 0)
#define ID_REAL_USERS(id) (((ID *)id)->us - ID_FAKE_USERS(id))
#define ID_EXTRA_USERS(id) (((ID *)id)->tag & LIB_TAG_EXTRAUSER ? 1 : 0)

#define ID_CHECK_UNDO(id) ((GS((id)->name) != ID_SCR) && (GS((id)->name) != ID_WM))

#define ID_BLEND_PATH(_bmain, _id) ((_id)->lib ? (_id)->lib->filepath : BKE_main_blendfile_path((_bmain)))
#define ID_BLEND_PATH_FROM_GLOBAL(_id) ((_id)->lib ? (_id)->lib->filepath : BKE_main_blendfile_path_from_global())

#define ID_MISSING(_id) (((_id)->tag & LIB_TAG_MISSING) != 0)

#define ID_IS_LINKED(_id) (((ID *)(_id))->lib != NULL)

#ifdef GS
#  undef GS
#endif
#define GS(a)	(CHECK_TYPE_ANY(a, char *, const char *, char [66], const char[66]), (ID_Type)(*((const short *)(a))))

#define ID_NEW_SET(_id, _idn) \
	(((ID *)(_id))->newid = (ID *)(_idn), ((ID *)(_id))->newid->tag |= LIB_TAG_NEW, (void *)((ID *)(_id))->newid)
#define ID_NEW_REMAP(a) if ((a) && (a)->id.newid) (a) = (void *)(a)->id.newid

/* id->flag (persitent). */
enum {
	LIB_FAKEUSER        = 1 << 9,
};

/**
 * id->tag (runtime-only).
 *
 * Those flags belong to three different categories, which have different expected handling in code:
 *
 * - RESET_BEFORE_USE: piece of code that wants to use such flag
 *   has to ensure they are properly 'reset' first.
 * - RESET_AFTER_USE: piece of code that wants to use such flag has to ensure they are properly
 *   'reset' after usage
 *   (though 'lifetime' of those flags is a bit fuzzy, e.g. _RECALC ones are reset on depsgraph
 *   evaluation...).
 * - RESET_NEVER: those flags are 'status' one, and never actually need any reset
 *   (except on initialization during .blend file reading).
 */
enum {
	/* RESET_NEVER Datablock is from current .blend file. */
	LIB_TAG_LOCAL           = 0,
	/* RESET_NEVER Datablock is from a library,
	 * but is used (linked) directly by current .blend file. */
	LIB_TAG_EXTERN          = 1 << 0,
	/* RESET_NEVER Datablock is from a library,
	 * and is only used (linked) inderectly through other libraries. */
	LIB_TAG_INDIRECT        = 1 << 1,

	/* RESET_AFTER_USE Flag used internally in readfile.c,
	 * to mark IDs needing to be expanded (only done once). */
	LIB_TAG_NEED_EXPAND     = 1 << 3,
	/* RESET_AFTER_USE Flag used internally in readfile.c to mark ID
	 * placeholders for linked datablocks needing to be read. */
	LIB_TAG_ID_ID           = 1 << 4,
	/* RESET_AFTER_USE */
	LIB_TAG_NEED_LINK       = 1 << 5,

	/* RESET_NEVER tag datablock as a place-holder
	 * (because the real one could not be linked from its library e.g.). */
	LIB_TAG_MISSING         = 1 << 6,

	/* tag datablock has having an extra user. */
	LIB_TAG_EXTRAUSER       = 1 << 2,
	/* tag datablock has having actually increased usercount for the extra virtual user. */
	LIB_TAG_EXTRAUSER_SET   = 1 << 7,

	/* RESET_AFTER_USE tag newly duplicated/copied IDs.
	 * Also used internally in readfile.c to mark datablocks needing do_versions. */
	LIB_TAG_NEW             = 1 << 8,
	/* RESET_BEFORE_USE free test flag.
	 * TODO make it a RESET_AFTER_USE too. */
	LIB_TAG_DOIT            = 1 << 10,
	/* RESET_AFTER_USE tag existing data before linking so we know what is new. */
	LIB_TAG_PRE_EXISTING    = 1 << 11,

	/* RESET_NEVER tag datablock for freeing etc. behavior (usually set when copying real one into temp/runtime one). */
	LIB_TAG_NO_MAIN          = 1 << 12,  /* Datablock is not listed in Main database. */
	LIB_TAG_NO_USER_REFCOUNT = 1 << 13,  /* Datablock does not refcount usages of other IDs. */
	/* Datablock was not allocated by standard system (BKE_libblock_alloc), do not free its memory
	 * (usual type-specific freeing is called though). */
	LIB_TAG_NOT_ALLOCATED     = 1 << 14,
};

enum {
	/* RESET_AFTER_USE, used by update code (depsgraph). */
	ID_RECALC_NONE  = 0,
	ID_RECALC       = 1 << 0,
	ID_RECALC_DATA  = 1 << 1,
	ID_RECALC_SKIP_ANIM_TAG  = 1 << 2,
	ID_RECALC_ALL   = (ID_RECALC | ID_RECALC_DATA),
};

/* To filter ID types (filter_id) */
/* XXX We cannot put all needed IDs inside an enum...
 *     We'll have to see whether we can fit all needed ones inside 32 values,
 *     or if we need to fallback to longlong defines :/
 */
enum {
	FILTER_ID_CA        = (1 << 3),
	FILTER_ID_CU        = (1 << 4),
	FILTER_ID_GR        = (1 << 6),
	FILTER_ID_IM        = (1 << 7),
	FILTER_ID_LA        = (1 << 8),
	FILTER_ID_MA        = (1 << 11),
	FILTER_ID_ME        = (1 << 14),
	FILTER_ID_OB        = (1 << 17),
	FILTER_ID_SCE       = (1 << 20),
	FILTER_ID_TE        = (1 << 23),
	FILTER_ID_TXT       = (1 << 24),
	FILTER_ID_VF        = (1 << 25),
	FILTER_ID_WO        = (1 << 26),
	FILTER_ID_CF        = (1 << 28),
};

/* IMPORTANT: this enum matches the order currently use in set_listbasepointers,
 * keep them in sync! */
enum {
	INDEX_ID_LI = 0,
	INDEX_ID_IM,
	INDEX_ID_TE,
	INDEX_ID_MA,
	INDEX_ID_VF,
	INDEX_ID_CF,
	INDEX_ID_ME,
	INDEX_ID_CU,
	INDEX_ID_LA,
	INDEX_ID_CA,
	INDEX_ID_TXT,
	INDEX_ID_GR,
	INDEX_ID_WO,
	INDEX_ID_SCR,
	INDEX_ID_OB,
	INDEX_ID_SCE,
	INDEX_ID_WM,
	INDEX_ID_NULL,
};

#ifdef __cplusplus
}
#endif

#endif
