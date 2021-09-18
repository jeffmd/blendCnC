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

/** \file blender/blenloader/intern/writefile.c
 *  \ingroup blenloader
 */


/**
 *
 * FILE FORMAT
 * ===========
 *
 * IFF-style structure  (but not IFF compatible!)
 *
 * start file:
 * <pre>
 *     BLENDER_V100    12 bytes  (versie 1.00)
 *                     V = big endian, v = little endian
 *                     _ = 4 byte pointer, - = 8 byte pointer
 * </pre>
 *
 * datablocks: (also see struct #BHead).
 * <pre>
 *     <bh.code>           4 chars
 *     <bh.len>            int,  len data after BHead
 *     <bh.old>            void,  old pointer
 *     <bh.SDNAnr>         int
 *     <bh.nr>             int, in case of array: number of structs
 *     data
 *     ...
 *     ...
 * </pre>
 *
 * Almost all data in Blender are structures. Each struct saved
 * gets a BHead header.  With BHead the struct can be linked again
 * and compared with StructDNA .
 * WRITE
 * =====
 *
 * Preferred writing order: (not really a must, but why would you do it random?)
 * Any case: direct data is ALWAYS after the lib block
 *
 * (Local file data)
 * - for each LibBlock
 *   - write LibBlock
 *   - write associated direct data
 * (External file data)
 * - per library
 *   - write library block
 *   - per LibBlock
 *     - write the ID of LibBlock
 * - write #TEST (#RenderInfo struct. 128x128 blend file preview is optional).
 * - write #GLOB (#FileGlobal struct) (some global vars).
 * - write #DNA1 (#SDNA struct)
 * - write #USER (#UserDef struct) if filename is ``~/.config/blender/X.XX/config/startup.blend``.
 */


#include <math.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>  /* FreeBSD, for write() and close(). */

#include "BLI_utildefines.h"

#include "DNA_cachefile_types.h"
#include "DNA_camera_types.h"
#include "DNA_curve_types.h"
#include "DNA_genfile.h"
#include "DNA_group_types.h"
#include "DNA_fileglobal_types.h"
#include "DNA_lamp_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_property_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_sdna_types.h"
#include "DNA_space_types.h"
#include "DNA_screen_types.h"
#include "DNA_text_types.h"
#include "DNA_view3d_types.h"
#include "DNA_vfont_types.h"
#include "DNA_world_types.h"
#include "DNA_windowmanager_types.h"

#include "MEM_guardedalloc.h" // MEM_freeN
#include "BLI_bitmap.h"
#include "BLI_blenlib.h"
#include "BLI_linklist.h"
#include "BLI_mempool.h"

#include "BKE_blender_version.h"
#include "BKE_bpath.h"
#include "BKE_curve.h"
#include "BKE_global.h" // for G
#include "BKE_idcode.h"
#include "BKE_library.h" // for  set_listbasepointers
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_subsurf.h"
#include "BKE_modifier.h"
#include "BKE_mesh.h"

#include "BLO_writefile.h"
#include "BLO_readfile.h"
#include "BLO_runtime.h"
#include "BLO_undofile.h"
#include "BLO_blend_defs.h"

#include "readfile.h"

/* for SDNA_TYPE_FROM_STRUCT() macro */
#include "dna_type_offsets.h"

#include <errno.h>

/* ********* my write, buffered writing with minimum size chunks ************ */

/* Use optimal allocation since blocks of this size are kept in memory for undo. */
#define MYWRITE_BUFFER_SIZE (MEM_SIZE_OPTIMAL(1 << 17))  /* 128kb */
#define MYWRITE_MAX_CHUNK   (MEM_SIZE_OPTIMAL(1 << 15))  /* ~32kb */

/** Use if we want to store how many bytes have been written to the file. */
// #define USE_WRITE_DATA_LEN

/* -------------------------------------------------------------------- */
/** \name Internal Write Wrapper's (Abstracts Compression)
 * \{ */

typedef enum {
	WW_WRAP_NONE = 1,
	WW_WRAP_ZLIB,
} eWriteWrapType;

typedef struct WriteWrap WriteWrap;
struct WriteWrap {
	/* callbacks */
	bool   (*open)(WriteWrap *ww, const char *filepath);
	bool   (*close)(WriteWrap *ww);
	size_t (*write)(WriteWrap *ww, const char *data, size_t data_len);

	/* internal */
	union {
		int file_handle;
		gzFile gz_handle;
	} _user_data;
};

/* none */
#define FILE_HANDLE(ww) \
	(ww)->_user_data.file_handle

static bool ww_open_none(WriteWrap *ww, const char *filepath)
{
	int file;

	file = BLI_open(filepath, O_BINARY + O_WRONLY + O_CREAT + O_TRUNC, 0666);

	if (file != -1) {
		FILE_HANDLE(ww) = file;
		return true;
	}
	else {
		return false;
	}
}
static bool ww_close_none(WriteWrap *ww)
{
	return (close(FILE_HANDLE(ww)) != -1);
}
static size_t ww_write_none(WriteWrap *ww, const char *buf, size_t buf_len)
{
	return write(FILE_HANDLE(ww), buf, buf_len);
}
#undef FILE_HANDLE

/* zlib */
#define FILE_HANDLE(ww) \
	(ww)->_user_data.gz_handle

static bool ww_open_zlib(WriteWrap *ww, const char *filepath)
{
	gzFile file;

	file = BLI_gzopen(filepath, "wb1");

	if (file != Z_NULL) {
		FILE_HANDLE(ww) = file;
		return true;
	}
	else {
		return false;
	}
}
static bool ww_close_zlib(WriteWrap *ww)
{
	return (gzclose(FILE_HANDLE(ww)) == Z_OK);
}
static size_t ww_write_zlib(WriteWrap *ww, const char *buf, size_t buf_len)
{
	return gzwrite(FILE_HANDLE(ww), buf, buf_len);
}
#undef FILE_HANDLE

/* --- end compression types --- */

static void ww_handle_init(eWriteWrapType ww_type, WriteWrap *r_ww)
{
	memset(r_ww, 0, sizeof(*r_ww));

	switch (ww_type) {
		case WW_WRAP_ZLIB:
		{
			r_ww->open  = ww_open_zlib;
			r_ww->close = ww_close_zlib;
			r_ww->write = ww_write_zlib;
			break;
		}
		default:
		{
			r_ww->open  = ww_open_none;
			r_ww->close = ww_close_none;
			r_ww->write = ww_write_none;
			break;
		}
	}
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Write Data Type & Functions
 * \{ */

typedef struct {
	const struct SDNA *sdna;

	/** Use for file and memory writing (fixed size of #MYWRITE_BUFFER_SIZE). */
	uchar *buf;
	/** Number of bytes used in #WriteData.buf (flushed when exceeded). */
	int buf_used_len;

#ifdef USE_WRITE_DATA_LEN
	/** Total number of bytes written. */
	size_t write_len;
#endif

	/** Set on unlikely case of an error (ignores further file writing).  */
	bool error;

	/** #MemFile writing (used for undo). */
	struct {
		MemFile      *current;
		MemFile      *compare;
		/** Use to de-duplicate chunks when writing. */
		MemFileChunk *compare_chunk;
	} mem;
	/** When true, write to #WriteData.current, could also call 'is_undo'. */
	bool use_memfile;

	/**
	 * Wrap writing, so we can use zlib or
	 * other compression types later, see: G_FILE_COMPRESS
	 * Will be NULL for UNDO.
	 */
	WriteWrap *ww;

#ifdef USE_BMESH_SAVE_AS_COMPAT
	bool use_mesh_compat; /* option to save with older mesh format */
#endif
} WriteData;

static WriteData *writedata_new(WriteWrap *ww)
{
	WriteData *wd = MEM_callocN(sizeof(*wd), "writedata");

	wd->sdna = DNA_sdna_current_get();

	wd->ww = ww;

	wd->buf = MEM_mallocN(MYWRITE_BUFFER_SIZE, "wd->buf");

	return wd;
}

static void writedata_do_write(WriteData *wd, const void *mem, int memlen)
{
	if ((wd == NULL) || wd->error || (mem == NULL) || memlen < 1) {
		return;
	}

	if (UNLIKELY(wd->error)) {
		return;
	}

	/* memory based save */
	if (wd->use_memfile) {
		memfile_chunk_add(wd->mem.current, mem, memlen, &wd->mem.compare_chunk);
	}
	else {
		if (wd->ww->write(wd->ww, mem, memlen) != memlen) {
			wd->error = true;
		}
	}
}

static void writedata_free(WriteData *wd)
{
	MEM_freeN(wd->buf);
	MEM_freeN(wd);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Writing API 'mywrite'
 * \{ */

/**
 * Flush helps the de-duplicating memory for undo-save by logically segmenting data,
 * so differences in one part of memory won't cause unrelated data to be duplicated.
 */
static void mywrite_flush(WriteData *wd)
{
	if (wd->buf_used_len) {
		writedata_do_write(wd, wd->buf, wd->buf_used_len);
		wd->buf_used_len = 0;
	}
}

/**
 * Low level WRITE(2) wrapper that buffers data
 * \param adr: Pointer to new chunk of data
 * \param len: Length of new chunk of data
 */
static void mywrite(WriteData *wd, const void *adr, int len)
{
	if (UNLIKELY(wd->error)) {
		return;
	}

	if (UNLIKELY(adr == NULL)) {
		BLI_assert(0);
		return;
	}

#ifdef USE_WRITE_DATA_LEN
	wd->write_len += len;
#endif

	/* if we have a single big chunk, write existing data in
	 * buffer and write out big chunk in smaller pieces */
	if (len > MYWRITE_MAX_CHUNK) {
		if (wd->buf_used_len) {
			writedata_do_write(wd, wd->buf, wd->buf_used_len);
			wd->buf_used_len = 0;
		}

		do {
			int writelen = MIN2(len, MYWRITE_MAX_CHUNK);
			writedata_do_write(wd, adr, writelen);
			adr = (const char *)adr + writelen;
			len -= writelen;
		} while (len > 0);

		return;
	}

	/* if data would overflow buffer, write out the buffer */
	if (len + wd->buf_used_len > MYWRITE_BUFFER_SIZE - 1) {
		writedata_do_write(wd, wd->buf, wd->buf_used_len);
		wd->buf_used_len = 0;
	}

	/* append data at end of buffer */
	memcpy(&wd->buf[wd->buf_used_len], adr, len);
	wd->buf_used_len += len;
}

/**
 * BeGiN initializer for mywrite
 * \param ww: File write wrapper.
 * \param compare: Previous memory file (can be NULL).
 * \param current: The current memory file (can be NULL).
 * \warning Talks to other functions with global parameters
 */
static WriteData *mywrite_begin(WriteWrap *ww, MemFile *compare, MemFile *current)
{
	WriteData *wd = writedata_new(ww);

	if (current != NULL) {
		wd->mem.current = current;
		wd->mem.compare = compare;
		wd->mem.compare_chunk = compare ? compare->chunks.first : NULL;
		wd->use_memfile = true;
	}

	return wd;
}

/**
 * END the mywrite wrapper
 * \return 1 if write failed
 * \return unknown global variable otherwise
 * \warning Talks to other functions with global parameters
 */
static bool mywrite_end(WriteData *wd)
{
	if (wd->buf_used_len) {
		writedata_do_write(wd, wd->buf, wd->buf_used_len);
		wd->buf_used_len = 0;
	}

	const bool err = wd->error;
	writedata_free(wd);

	return err;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic DNA File Writing
 * \{ */

static void writestruct_at_address_nr(
        WriteData *wd, int filecode, const int struct_nr, int nr,
        const void *adr, const void *data)
{
	BHead bh;
	const short *sp;

	BLI_assert(struct_nr > 0 && struct_nr < SDNA_TYPE_MAX);

	if (adr == NULL || data == NULL || nr == 0) {
		return;
	}

	/* init BHead */
	bh.code = filecode;
	bh.old = adr;
	bh.nr = nr;

	bh.SDNAnr = struct_nr;
	sp = wd->sdna->structs[bh.SDNAnr];

	bh.len = nr * wd->sdna->typelens[sp[0]];

	if (bh.len == 0) {
		return;
	}

	mywrite(wd, &bh, sizeof(BHead));
	mywrite(wd, data, bh.len);
}

static void writestruct_at_address_id(
        WriteData *wd, int filecode, const char *structname, int nr,
        const void *adr, const void *data)
{
	if (adr == NULL || data == NULL || nr == 0) {
		return;
	}

	const int SDNAnr = DNA_struct_find_nr(wd->sdna, structname);
	if (UNLIKELY(SDNAnr == -1)) {
		printf("error: can't find SDNA code <%s>\n", structname);
		return;
	}

	writestruct_at_address_nr(wd, filecode, SDNAnr, nr, adr, data);
}

static void writestruct_nr(
        WriteData *wd, int filecode, const int struct_nr, int nr,
        const void *adr)
{
	writestruct_at_address_nr(wd, filecode, struct_nr, nr, adr, adr);
}

static void writestruct_id(
        WriteData *wd, int filecode, const char *structname, int nr,
        const void *adr)
{
	writestruct_at_address_id(wd, filecode, structname, nr, adr, adr);
}

static void writedata(WriteData *wd, int filecode, int len, const void *adr)  /* do not use for structs */
{
	BHead bh;

	if (adr == NULL || len == 0) {
		return;
	}

	/* align to 4 (writes uninitialized bytes in some cases) */
	len = (len + 3) & ~3;

	/* init BHead */
	bh.code   = filecode;
	bh.old    = adr;
	bh.nr     = 1;
	bh.SDNAnr = 0;
	bh.len    = len;

	mywrite(wd, &bh, sizeof(BHead));
	mywrite(wd, adr, len);
}

/* use this to force writing of lists in same order as reading (using link_list) */
static void writelist_nr(WriteData *wd, int filecode, const int struct_nr, const ListBase *lb)
{
	const Link *link = lb->first;

	while (link) {
		writestruct_nr(wd, filecode, struct_nr, 1, link);
		link = link->next;
	}
}

#if 0
static void writelist_id(WriteData *wd, int filecode, const char *structname, const ListBase *lb)
{
	const Link *link = lb->first;
	if (link) {

		const int struct_nr = DNA_struct_find_nr(wd->sdna, structname);
		if (struct_nr == -1) {
			printf("error: can't find SDNA code <%s>\n", structname);
			return;
		}

		while (link) {
			writestruct_nr(wd, filecode, struct_nr, 1, link);
			link = link->next;
		}
	}
}
#endif

#define writestruct_at_address(wd, filecode, struct_id, nr, adr, data) \
	writestruct_at_address_nr(wd, filecode, SDNA_TYPE_FROM_STRUCT(struct_id), nr, adr, data)

#define writestruct(wd, filecode, struct_id, nr, adr) \
	writestruct_nr(wd, filecode, SDNA_TYPE_FROM_STRUCT(struct_id), nr, adr)

#define writelist(wd, filecode, struct_id, lb) \
	writelist_nr(wd, filecode, SDNA_TYPE_FROM_STRUCT(struct_id), lb)

/** \} */

/* -------------------------------------------------------------------- */
/** \name Typed DNA File Writing
 *
 * These functions are used by blender's .blend system for file saving/loading.
 * \{ */

void IDP_WriteProperty_OnlyData(const IDProperty *prop, void *wd);
void IDP_WriteProperty(const IDProperty *prop, void *wd);

static void IDP_WriteArray(const IDProperty *prop, void *wd)
{
	/*REMEMBER to set totalen to len in the linking code!!*/
	if (prop->data.pointer) {
		writedata(wd, DATA, MEM_allocN_len(prop->data.pointer), prop->data.pointer);

		if (prop->subtype == IDP_GROUP) {
			IDProperty **array = prop->data.pointer;
			int a;

			for (a = 0; a < prop->len; a++) {
				IDP_WriteProperty(array[a], wd);
			}
		}
	}
}

static void IDP_WriteIDPArray(const IDProperty *prop, void *wd)
{
	/*REMEMBER to set totalen to len in the linking code!!*/
	if (prop->data.pointer) {
		const IDProperty *array = prop->data.pointer;
		int a;

		writestruct(wd, DATA, IDProperty, prop->len, array);

		for (a = 0; a < prop->len; a++) {
			IDP_WriteProperty_OnlyData(&array[a], wd);
		}
	}
}

static void IDP_WriteString(const IDProperty *prop, void *wd)
{
	/*REMEMBER to set totalen to len in the linking code!!*/
	writedata(wd, DATA, prop->len, prop->data.pointer);
}

static void IDP_WriteGroup(const IDProperty *prop, void *wd)
{
	IDProperty *loop;

	for (loop = prop->data.group.first; loop; loop = loop->next) {
		IDP_WriteProperty(loop, wd);
	}
}

/* Functions to read/write ID Properties */
void IDP_WriteProperty_OnlyData(const IDProperty *prop, void *wd)
{
	switch (prop->type) {
		case IDP_GROUP:
			IDP_WriteGroup(prop, wd);
			break;
		case IDP_STRING:
			IDP_WriteString(prop, wd);
			break;
		case IDP_ARRAY:
			IDP_WriteArray(prop, wd);
			break;
		case IDP_IDPARRAY:
			IDP_WriteIDPArray(prop, wd);
			break;
	}
}

void IDP_WriteProperty(const IDProperty *prop, void *wd)
{
	writestruct(wd, DATA, IDProperty, 1, prop);
	IDP_WriteProperty_OnlyData(prop, wd);
}

static void write_iddata(void *wd, const ID *id)
{
	/* ID_WM's id->properties are considered runtime only, and never written in .blend file. */
	if (id->properties && !ELEM(GS(id->name), ID_WM)) {
		IDP_WriteProperty(id->properties, wd);
	}
}

static void write_previews(WriteData *wd, const PreviewImage *prv_orig)
{
	/* Note we write previews also for undo steps. It takes up some memory,
	 * but not doing so would causes all previews to be re-rendered after
	 * undo which is too expensive. */
	if (prv_orig) {
		PreviewImage prv = *prv_orig;

		/* don't write out large previews if not requested */
		if (!(U.flag & USER_SAVE_PREVIEWS)) {
			prv.w[1] = 0;
			prv.h[1] = 0;
			prv.rect[1] = NULL;
		}
		writestruct_at_address(wd, DATA, PreviewImage, 1, prv_orig, &prv);
		if (prv.rect[0]) {
			writedata(wd, DATA, prv.w[0] * prv.h[0] * sizeof(uint), prv.rect[0]);
		}
		if (prv.rect[1]) {
			writedata(wd, DATA, prv.w[1] * prv.h[1] * sizeof(uint), prv.rect[1]);
		}
	}
}

static void write_curvemapping_curves(WriteData *wd, CurveMapping *cumap)
{
	for (int a = 0; a < CM_TOT; a++) {
		writestruct(wd, DATA, CurveMapPoint, cumap->cm[a].totpoint, cumap->cm[a].curve);
	}
}

static void write_curvemapping(WriteData *wd, CurveMapping *cumap)
{
	writestruct(wd, DATA, CurveMapping, 1, cumap);

	write_curvemapping_curves(wd, cumap);
}

/**
 * Take care using 'use_active_win', since we wont want the currently active window
 * to change which scene renders (currently only used for undo).
 */
static void current_screen_compat(Main *mainvar, bScreen **r_screen, bool use_active_win)
{
	wmWindowManager *wm;
	wmWindow *window = NULL;

	/* find a global current screen in the first open window, to have
	 * a reasonable default for reading in older versions */
	wm = mainvar->wm.first;

	if (wm) {
		if (use_active_win) {
			/* write the active window into the file, needed for multi-window undo T43424 */
			for (window = wm->windows.first; window; window = window->next) {
				if (window->active) {
					break;
				}
			}

			/* fallback */
			if (window == NULL) {
				window = wm->windows.first;
			}
		}
		else {
			window = wm->windows.first;
		}
	}

	*r_screen = (window) ? window->screen : NULL;
}

static void write_keymapitem(WriteData *wd, const wmKeyMapItem *kmi)
{
	writestruct(wd, DATA, wmKeyMapItem, 1, kmi);
	if (kmi->properties) {
		IDP_WriteProperty(kmi->properties, wd);
	}
}

static void write_userdef(WriteData *wd, const UserDef *userdef)
{
	writestruct(wd, USER, UserDef, 1, userdef);

	for (const bTheme *btheme = userdef->themes.first; btheme; btheme = btheme->next) {
		writestruct(wd, DATA, bTheme, 1, btheme);
	}

	for (const wmKeyMap *keymap = userdef->user_keymaps.first; keymap; keymap = keymap->next) {
		writestruct(wd, DATA, wmKeyMap, 1, keymap);

		for (const wmKeyMapDiffItem *kmdi = keymap->diff_items.first; kmdi; kmdi = kmdi->next) {
			writestruct(wd, DATA, wmKeyMapDiffItem, 1, kmdi);
			if (kmdi->remove_item) {
				write_keymapitem(wd, kmdi->remove_item);
			}
			if (kmdi->add_item) {
				write_keymapitem(wd, kmdi->add_item);
			}
		}

		for (const wmKeyMapItem *kmi = keymap->items.first; kmi; kmi = kmi->next) {
			write_keymapitem(wd, kmi);
		}
	}

	for (const bAddon *bext = userdef->addons.first; bext; bext = bext->next) {
		writestruct(wd, DATA, bAddon, 1, bext);
		if (bext->prop) {
			IDP_WriteProperty(bext->prop, wd);
		}
	}

	for (const bPathCompare *path_cmp = userdef->autoexec_paths.first; path_cmp; path_cmp = path_cmp->next) {
		writestruct(wd, DATA, bPathCompare, 1, path_cmp);
	}

	for (const uiStyle *style = userdef->uistyles.first; style; style = style->next) {
		writestruct(wd, DATA, uiStyle, 1, style);
	}
}

static void write_defgroups(WriteData *wd, ListBase *defbase)
{
	for (bDeformGroup *defgroup = defbase->first; defgroup; defgroup = defgroup->next) {
		writestruct(wd, DATA, bDeformGroup, 1, defgroup);
	}
}

static void write_modifiers(WriteData *wd, ListBase *modbase)
{
	ModifierData *md;

	if (modbase == NULL) {
		return;
	}

	for (md = modbase->first; md; md = md->next) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
		if (mti == NULL) {
			return;
		}

		writestruct_id(wd, DATA, mti->structName, 1, md);

		if (md->type == eModifierType_Hook) {
			HookModifierData *hmd = (HookModifierData *)md;

			if (hmd->curfalloff) {
				write_curvemapping(wd, hmd->curfalloff);
			}

			writedata(wd, DATA, sizeof(int) * hmd->totindex, hmd->indexar);
		}
		else if (md->type == eModifierType_Collision) {

		}
		else if (md->type == eModifierType_MeshDeform) {
			MeshDeformModifierData *mmd = (MeshDeformModifierData *)md;
			int size = mmd->dyngridsize;

			writestruct(wd, DATA, MDefInfluence, mmd->totinfluence, mmd->bindinfluences);
			writedata(wd, DATA, sizeof(int) * (mmd->totvert + 1), mmd->bindoffsets);
			writedata(wd, DATA, sizeof(float) * 3 * mmd->totcagevert,
			          mmd->bindcagecos);
			writestruct(wd, DATA, MDefCell, size * size * size, mmd->dyngrid);
			writestruct(wd, DATA, MDefInfluence, mmd->totinfluence, mmd->dyninfluences);
			writedata(wd, DATA, sizeof(int) * mmd->totvert, mmd->dynverts);
		}
		else if (md->type == eModifierType_Warp) {
			WarpModifierData *tmd = (WarpModifierData *)md;
			if (tmd->curfalloff) {
				write_curvemapping(wd, tmd->curfalloff);
			}
		}
		else if (md->type == eModifierType_WeightVGEdit) {
			WeightVGEditModifierData *wmd = (WeightVGEditModifierData *)md;

			if (wmd->cmap_curve) {
				write_curvemapping(wd, wmd->cmap_curve);
			}
		}
		else if (md->type == eModifierType_LaplacianDeform) {
			LaplacianDeformModifierData *lmd = (LaplacianDeformModifierData *)md;

			writedata(wd, DATA, sizeof(float) * lmd->total_verts * 3, lmd->vertexco);
		}
		else if (md->type == eModifierType_CorrectiveSmooth) {
			CorrectiveSmoothModifierData *csmd = (CorrectiveSmoothModifierData *)md;

			if (csmd->bind_coords) {
				writedata(wd, DATA, sizeof(float[3]) * csmd->bind_coords_num, csmd->bind_coords);
			}
		}
		else if (md->type == eModifierType_SurfaceDeform) {
			SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *)md;

			writestruct(wd, DATA, SDefVert, smd->numverts, smd->verts);

			if (smd->verts) {
				for (int i = 0; i < smd->numverts; i++) {
					writestruct(wd, DATA, SDefBind, smd->verts[i].numbinds, smd->verts[i].binds);

					if (smd->verts[i].binds) {
						for (int j = 0; j < smd->verts[i].numbinds; j++) {
							writedata(wd, DATA, sizeof(int) * smd->verts[i].binds[j].numverts, smd->verts[i].binds[j].vert_inds);

							if (smd->verts[i].binds[j].mode == MOD_SDEF_MODE_CENTROID ||
							    smd->verts[i].binds[j].mode == MOD_SDEF_MODE_LOOPTRI)
							{
								writedata(wd, DATA, sizeof(float) * 3, smd->verts[i].binds[j].vert_weights);
							}
							else {
								writedata(wd, DATA, sizeof(float) * smd->verts[i].binds[j].numverts, smd->verts[i].binds[j].vert_weights);
							}
						}
					}
				}
			}
		}
	}
}

static void write_object(WriteData *wd, Object *ob)
{
	if (ob->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_OB, Object, 1, ob);
		write_iddata(wd, &ob->id);

		/* direct data */
		writedata(wd, DATA, sizeof(void *) * ob->totcol, ob->mat);
		writedata(wd, DATA, sizeof(char) * ob->totcol, ob->matbits);

		write_defgroups(wd, &ob->defbase);


		if (ob->rigidbody_object) {
			/* TODO: if any extra data is added to handle duplis, will need separate function then */
			writestruct(wd, DATA, RigidBodyOb, 1, ob->rigidbody_object);
		}

		if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE) {
			writestruct(wd, DATA, ImageUser, 1, ob->iuser);
		}

		write_modifiers(wd, &ob->modifiers);

		writelist(wd, DATA, LinkData, &ob->pc_ids);
		writelist(wd, DATA, LodLevel, &ob->lodlevels);

		write_previews(wd, ob->preview);
	}
}


static void write_vfont(WriteData *wd, VFont *vf)
{
	if (vf->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_VF, VFont, 1, vf);
		write_iddata(wd, &vf->id);

		/* direct data */
		if (vf->packedfile) {
			PackedFile *pf = vf->packedfile;
			writestruct(wd, DATA, PackedFile, 1, pf);
			writedata(wd, DATA, pf->size, pf->data);
		}
	}
}


static void write_camera(WriteData *wd, Camera *cam)
{
	if (cam->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_CA, Camera, 1, cam);
		write_iddata(wd, &cam->id);

	}
}

static void write_curve(WriteData *wd, Curve *cu)
{
	if (cu->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_CU, Curve, 1, cu);
		write_iddata(wd, &cu->id);

		/* direct data */
		writedata(wd, DATA, sizeof(void *) * cu->totcol, cu->mat);

		if (cu->vfont) {
			writedata(wd, DATA, cu->len + 1, cu->str);
			writestruct(wd, DATA, CharInfo, cu->len_wchar + 1, cu->strinfo);
			writestruct(wd, DATA, TextBox, cu->totbox, cu->tb);
		}
		else {
			/* is also the order of reading */
			for (Nurb *nu = cu->nurb.first; nu; nu = nu->next) {
				writestruct(wd, DATA, Nurb, 1, nu);
			}
			for (Nurb *nu = cu->nurb.first; nu; nu = nu->next) {
				if (nu->type == CU_BEZIER) {
					writestruct(wd, DATA, BezTriple, nu->pntsu, nu->bezt);
				}
				else {
					writestruct(wd, DATA, BPoint, nu->pntsu * nu->pntsv, nu->bp);
					if (nu->knotsu) {
						writedata(wd, DATA, KNOTSU(nu) * sizeof(float), nu->knotsu);
					}
					if (nu->knotsv) {
						writedata(wd, DATA, KNOTSV(nu) * sizeof(float), nu->knotsv);
					}
				}
			}
		}
	}
}

static void write_dverts(WriteData *wd, int count, MDeformVert *dvlist)
{
	if (dvlist) {

		/* Write the dvert list */
		writestruct(wd, DATA, MDeformVert, count, dvlist);

		/* Write deformation data for each dvert */
		for (int i = 0; i < count; i++) {
			if (dvlist[i].dw) {
				writestruct(wd, DATA, MDeformWeight, dvlist[i].totweight, dvlist[i].dw);
			}
		}
	}
}

static void write_mdisps(WriteData *wd, int count, MDisps *mdlist, int external)
{
	if (mdlist) {
		int i;

		writestruct(wd, DATA, MDisps, count, mdlist);
		for (i = 0; i < count; ++i) {
			MDisps *md = &mdlist[i];
			if (md->disps) {
				if (!external) {
					writedata(wd, DATA, sizeof(float) * 3 * md->totdisp, md->disps);
				}
			}

			if (md->hidden) {
				writedata(wd, DATA, BLI_BITMAP_SIZE(md->totdisp), md->hidden);
			}
		}
	}
}

static void write_customdata(
        WriteData *wd, ID *id, int count, CustomData *data, CustomDataLayer *layers,
        int partial_type, int partial_count)
{
	int i;

	/* write external customdata (not for undo) */
	if (data->external && (wd->use_memfile == false)) {
		CustomData_external_write(data, id, CD_MASK_MESH, count, 0);
	}

	writestruct_at_address(wd, DATA, CustomDataLayer, data->totlayer, data->layers, layers);

	for (i = 0; i < data->totlayer; i++) {
		CustomDataLayer *layer = &layers[i];
		const char *structname;
		int structnum, datasize;

		if (layer->type == CD_MDEFORMVERT) {
			/* layer types that allocate own memory need special handling */
			write_dverts(wd, count, layer->data);
		}
		else if (layer->type == CD_MDISPS) {
			write_mdisps(wd, count, layer->data, layer->flag & CD_FLAG_EXTERNAL);
		}
		else if (layer->type == CD_PAINT_MASK) {
			const float *layer_data = layer->data;
			writedata(wd, DATA, sizeof(*layer_data) * count, layer_data);
		}
		else {
			CustomData_file_write_info(layer->type, &structname, &structnum);
			if (structnum) {
				/* when using partial visibility, the MEdge and MFace layers
				 * are smaller than the original, so their type and count is
				 * passed to make this work */
				if (layer->type != partial_type) {
					datasize = structnum * count;
				}
				else {
					datasize = structnum * partial_count;
				}

				writestruct_id(wd, DATA, structname, datasize, layer->data);
			}
			else {
				printf("%s error: layer '%s':%d - can't be written to file\n",
				       __func__, structname, layer->type);
			}
		}
	}

	if (data->external) {
		writestruct(wd, DATA, CustomDataExternal, 1, data->external);
	}
}

static void write_mesh(WriteData *wd, Mesh *mesh)
{
#ifdef USE_BMESH_SAVE_AS_COMPAT
	const bool save_for_old_blender = wd->use_mesh_compat;  /* option to save with older mesh format */
#else
	const bool save_for_old_blender = false;
#endif

	CustomDataLayer *vlayers = NULL, vlayers_buff[CD_TEMP_CHUNK_SIZE];
	CustomDataLayer *elayers = NULL, elayers_buff[CD_TEMP_CHUNK_SIZE];
	CustomDataLayer *flayers = NULL, flayers_buff[CD_TEMP_CHUNK_SIZE];
	CustomDataLayer *llayers = NULL, llayers_buff[CD_TEMP_CHUNK_SIZE];
	CustomDataLayer *players = NULL, players_buff[CD_TEMP_CHUNK_SIZE];

	if (mesh->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		if (!save_for_old_blender) {
			/* write a copy of the mesh, don't modify in place because it is
			 * not thread safe for threaded renders that are reading this */
			Mesh *old_mesh = mesh;
			Mesh copy_mesh = *mesh;
			mesh = &copy_mesh;

#ifdef USE_BMESH_SAVE_WITHOUT_MFACE
			/* cache only - don't write */
			mesh->mface = NULL;
			mesh->totface = 0;
			memset(&mesh->fdata, 0, sizeof(mesh->fdata));
#endif /* USE_BMESH_SAVE_WITHOUT_MFACE */

			/**
			 * Those calls:
			 *   - Reduce mesh->xdata.totlayer to number of layers to write.
			 *   - Fill xlayers with those layers to be written.
			 * Note that mesh->xdata is from now on invalid for Blender, but this is why the whole mesh is
			 * a temp local copy!
			 */
			CustomData_file_write_prepare(&mesh->vdata, &vlayers, vlayers_buff, ARRAY_SIZE(vlayers_buff));
			CustomData_file_write_prepare(&mesh->edata, &elayers, elayers_buff, ARRAY_SIZE(elayers_buff));
#ifndef USE_BMESH_SAVE_WITHOUT_MFACE  /* Do not copy org fdata in this case!!! */
			CustomData_file_write_prepare(&mesh->fdata, &flayers, flayers_buff, ARRAY_SIZE(flayers_buff));
#else
			flayers = flayers_buff;
#endif
			CustomData_file_write_prepare(&mesh->ldata, &llayers, llayers_buff, ARRAY_SIZE(llayers_buff));
			CustomData_file_write_prepare(&mesh->pdata, &players, players_buff, ARRAY_SIZE(players_buff));

			writestruct_at_address(wd, ID_ME, Mesh, 1, old_mesh, mesh);
			write_iddata(wd, &mesh->id);

			/* direct data */

			writedata(wd, DATA, sizeof(void *) * mesh->totcol, mesh->mat);
			writedata(wd, DATA, sizeof(MSelect) * mesh->totselect, mesh->mselect);

			write_customdata(wd, &mesh->id, mesh->totvert, &mesh->vdata, vlayers, -1, 0);
			write_customdata(wd, &mesh->id, mesh->totedge, &mesh->edata, elayers, -1, 0);
			/* fdata is really a dummy - written so slots align */
			write_customdata(wd, &mesh->id, mesh->totface, &mesh->fdata, flayers, -1, 0);
			write_customdata(wd, &mesh->id, mesh->totloop, &mesh->ldata, llayers, -1, 0);
			write_customdata(wd, &mesh->id, mesh->totpoly, &mesh->pdata, players, -1, 0);

			/* restore pointer */
			mesh = old_mesh;
		}
		else {

#ifdef USE_BMESH_SAVE_AS_COMPAT
			/* write a copy of the mesh, don't modify in place because it is
			 * not thread safe for threaded renders that are reading this */
			Mesh *old_mesh = mesh;
			Mesh copy_mesh = *mesh;
			mesh = &copy_mesh;

			mesh->mpoly = NULL;
			mesh->mface = NULL;
			mesh->totface = 0;
			mesh->totpoly = 0;
			mesh->totloop = 0;
			CustomData_reset(&mesh->fdata);
			CustomData_reset(&mesh->pdata);
			CustomData_reset(&mesh->ldata);
			mesh->edit_btmesh = NULL;

			/* now fill in polys to mfaces */
			/* XXX This breaks writing design, by using temp allocated memory, which will likely generate
			 *     duplicates in stored 'old' addresses.
			 *     This is very bad, but do not see easy way to avoid this, aside from generating those data
			 *     outside of save process itself.
			 *     Maybe we can live with this, though?
			 */
			mesh->totface = BKE_mesh_mpoly_to_mface(
			        &mesh->fdata, &old_mesh->ldata, &old_mesh->pdata,
			        mesh->totface, old_mesh->totloop, old_mesh->totpoly);

			BKE_mesh_update_customdata_pointers(mesh, false);

			CustomData_file_write_prepare(&mesh->vdata, &vlayers, vlayers_buff, ARRAY_SIZE(vlayers_buff));
			CustomData_file_write_prepare(&mesh->edata, &elayers, elayers_buff, ARRAY_SIZE(elayers_buff));
			CustomData_file_write_prepare(&mesh->fdata, &flayers, flayers_buff, ARRAY_SIZE(flayers_buff));

			writestruct_at_address(wd, ID_ME, Mesh, 1, old_mesh, mesh);
			write_iddata(wd, &mesh->id);

			/* direct data */

			writedata(wd, DATA, sizeof(void *) * mesh->totcol, mesh->mat);
			/* writedata(wd, DATA, sizeof(MSelect) * mesh->totselect, mesh->mselect); */ /* pre-bmesh NULL's */

			write_customdata(wd, &mesh->id, mesh->totvert, &mesh->vdata, vlayers, -1, 0);
			write_customdata(wd, &mesh->id, mesh->totedge, &mesh->edata, elayers, -1, 0);
			write_customdata(wd, &mesh->id, mesh->totface, &mesh->fdata, flayers, -1, 0);
			/* harmless for older blender versioins but _not_ writing these keeps file size down */

			CustomData_free(&mesh->fdata, mesh->totface);
			flayers = NULL;

			/* restore pointer */
			mesh = old_mesh;
#endif /* USE_BMESH_SAVE_AS_COMPAT */
		}
	}

	if (vlayers && vlayers != vlayers_buff) {
		MEM_freeN(vlayers);
	}
	if (elayers && elayers != elayers_buff) {
		MEM_freeN(elayers);
	}
	if (flayers && flayers != flayers_buff) {
		MEM_freeN(flayers);
	}
	if (llayers && llayers != llayers_buff) {
		MEM_freeN(llayers);
	}
	if (players && players != players_buff) {
		MEM_freeN(players);
	}
}

static void write_image(WriteData *wd, Image *ima)
{
	if (ima->id.us > 0 || wd->use_memfile) {
		ImagePackedFile *imapf;

		/* Some trickery to keep forward compatibility of packed images. */
		BLI_assert(ima->packedfile == NULL);
		if (ima->packedfiles.first != NULL) {
			imapf = ima->packedfiles.first;
			ima->packedfile = imapf->packedfile;
		}

		/* write LibData */
		writestruct(wd, ID_IM, Image, 1, ima);
		write_iddata(wd, &ima->id);

		for (imapf = ima->packedfiles.first; imapf; imapf = imapf->next) {
			writestruct(wd, DATA, ImagePackedFile, 1, imapf);
			if (imapf->packedfile) {
				PackedFile *pf = imapf->packedfile;
				writestruct(wd, DATA, PackedFile, 1, pf);
				writedata(wd, DATA, pf->size, pf->data);
			}
		}

		write_previews(wd, ima->preview);

		for (ImageView *iv = ima->views.first; iv; iv = iv->next) {
			writestruct(wd, DATA, ImageView, 1, iv);
		}

		ima->packedfile = NULL;
	}
}

static void write_texture(WriteData *wd, Tex *tex)
{
	if (tex->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_TE, Tex, 1, tex);
		write_iddata(wd, &tex->id);

		/* direct data */
		if (tex->coba) {
			writestruct(wd, DATA, ColorBand, 1, tex->coba);
		}
		if (tex->type == TEX_ENVMAP && tex->env) {
			writestruct(wd, DATA, EnvMap, 1, tex->env);
		}
		if (tex->type == TEX_VOXELDATA) {
			writestruct(wd, DATA, VoxelData, 1, tex->vd);
		}

	}
}

static void write_material(WriteData *wd, Material *ma)
{
	if (ma->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_MA, Material, 1, ma);
		write_iddata(wd, &ma->id);

		for (int a = 0; a < MAX_MTEX; a++) {
			if (ma->mtex[a]) {
				writestruct(wd, DATA, MTex, 1, ma->mtex[a]);
			}
		}

		if (ma->ramp_col) {
			writestruct(wd, DATA, ColorBand, 1, ma->ramp_col);
		}
		if (ma->ramp_spec) {
			writestruct(wd, DATA, ColorBand, 1, ma->ramp_spec);
		}

		write_previews(wd, ma->preview);
	}
}

static void write_world(WriteData *wd, World *wrld)
{
	if (wrld->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_WO, World, 1, wrld);
		write_iddata(wd, &wrld->id);

		for (int a = 0; a < MAX_MTEX; a++) {
			if (wrld->mtex[a]) {
				writestruct(wd, DATA, MTex, 1, wrld->mtex[a]);
			}
		}

		write_previews(wd, wrld->preview);
	}
}

static void write_lamp(WriteData *wd, Lamp *la)
{
	if (la->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_LA, Lamp, 1, la);
		write_iddata(wd, &la->id);

		/* direct data */
		for (int a = 0; a < MAX_MTEX; a++) {
			if (la->mtex[a]) {
				writestruct(wd, DATA, MTex, 1, la->mtex[a]);
			}
		}

		if (la->curfalloff) {
			write_curvemapping(wd, la->curfalloff);
		}

		write_previews(wd, la->preview);
	}
}

static void write_view_settings(WriteData *wd, ColorManagedViewSettings *view_settings)
{
	if (view_settings->curve_mapping) {
		write_curvemapping(wd, view_settings->curve_mapping);
	}
}

static void write_scene(WriteData *wd, Scene *sce)
{
	/* write LibData */
	writestruct(wd, ID_SCE, Scene, 1, sce);
	write_iddata(wd, &sce->id);

	/* direct data */
	for (Base *base = sce->base.first; base; base = base->next) {
		writestruct(wd, DATA, Base, 1, base);
	}

	ToolSettings *tos = sce->toolsettings;
	writestruct(wd, DATA, ToolSettings, 1, tos);

	/* writing dynamic list of TransformOrientations to the blend file */
	for (TransformOrientation *ts = sce->transform_spaces.first; ts; ts = ts->next) {
		writestruct(wd, DATA, TransformOrientation, 1, ts);
	}

	write_view_settings(wd, &sce->view_settings);

	/* writing RigidBodyWorld data to the blend file */
	if (sce->rigidbody_world) {
		writestruct(wd, DATA, RigidBodyWorld, 1, sce->rigidbody_world);
	}

	write_previews(wd, sce->preview);
}

static void write_windowmanager(WriteData *wd, wmWindowManager *wm)
{
	writestruct(wd, ID_WM, wmWindowManager, 1, wm);
	write_iddata(wd, &wm->id);

	for (wmWindow *win = wm->windows.first; win; win = win->next) {
		writestruct(wd, DATA, wmWindow, 1, win);
	}
}

static void write_region(WriteData *wd, ARegion *ar, int spacetype)
{
	writestruct(wd, DATA, ARegion, 1, ar);

	if (ar->regiondata) {
		if (ar->flag & RGN_FLAG_TEMP_REGIONDATA) {
			return;
		}

		switch (spacetype) {
			case SPACE_VIEW3D:
				if (ar->regiontype == RGN_TYPE_WINDOW) {
					RegionView3D *rv3d = ar->regiondata;
					writestruct(wd, DATA, RegionView3D, 1, rv3d);

					if (rv3d->localvd) {
						writestruct(wd, DATA, RegionView3D, 1, rv3d->localvd);
					}
					if (rv3d->clipbb) {
						writestruct(wd, DATA, BoundBox, 1, rv3d->clipbb);
					}

				}
				else
					printf("regiondata write missing!\n");
				break;
			default:
				printf("regiondata write missing!\n");
		}
	}
}

static void write_uilist(WriteData *wd, uiList *ui_list)
{
	writestruct(wd, DATA, uiList, 1, ui_list);

	if (ui_list->properties) {
		IDP_WriteProperty(ui_list->properties, wd);
	}
}

static void write_soops(WriteData *wd, SpaceOops *so)
{
	BLI_mempool *ts = so->treestore;

	if (ts) {
		SpaceOops so_flat = *so;

		int elems = BLI_mempool_len(ts);
		/* linearize mempool to array */
		TreeStoreElem *data = elems ? BLI_mempool_as_arrayN(ts, "TreeStoreElem") : NULL;

		if (data) {
			/* In this block we use the memory location of the treestore
			 * but _not_ its data, the addresses in this case are UUID's,
			 * since we can't rely on malloc giving us different values each time.
			 */
			TreeStore ts_flat = {0};

			/* we know the treestore is at least as big as a pointer,
			 * so offsetting works to give us a UUID. */
			void *data_addr = (void *)POINTER_OFFSET(ts, sizeof(void *));

			ts_flat.usedelem = elems;
			ts_flat.data = data_addr;

			writestruct(wd, DATA, SpaceOops, 1, so);

			writestruct_at_address(wd, DATA, TreeStore, 1, ts, &ts_flat);
			writestruct_at_address(wd, DATA, TreeStoreElem, elems, data_addr, data);

			MEM_freeN(data);
		}
		else {
			so_flat.treestore = NULL;
			writestruct_at_address(wd, DATA, SpaceOops, 1, so, &so_flat);
		}
	}
	else {
		writestruct(wd, DATA, SpaceOops, 1, so);
	}
}

static void write_screen(WriteData *wd, bScreen *sc)
{
	/* write LibData */
	/* in 2.50+ files, the file identifier for screens is patched, forward compatibility */
	writestruct(wd, ID_SCRN, bScreen, 1, sc);
	write_iddata(wd, &sc->id);

	/* direct data */
	for (ScrVert *sv = sc->vertbase.first; sv; sv = sv->next) {
		writestruct(wd, DATA, ScrVert, 1, sv);
	}

	for (ScrEdge *se = sc->edgebase.first; se; se = se->next) {
		writestruct(wd, DATA, ScrEdge, 1, se);
	}

	for (ScrArea *sa = sc->areabase.first; sa; sa = sa->next) {
		SpaceLink *sl;
		Panel *pa;
		uiList *ui_list;
		uiPreview *ui_preview;
		PanelCategoryStack *pc_act;
		ARegion *ar;

		writestruct(wd, DATA, ScrArea, 1, sa);

		for (ar = sa->regionbase.first; ar; ar = ar->next) {
			write_region(wd, ar, sa->spacetype);

			for (pa = ar->panels.first; pa; pa = pa->next) {
				writestruct(wd, DATA, Panel, 1, pa);
			}

			for (pc_act = ar->panels_category_active.first; pc_act; pc_act = pc_act->next) {
				writestruct(wd, DATA, PanelCategoryStack, 1, pc_act);
			}

			for (ui_list = ar->ui_lists.first; ui_list; ui_list = ui_list->next) {
				write_uilist(wd, ui_list);
			}

			for (ui_preview = ar->ui_previews.first; ui_preview; ui_preview = ui_preview->next) {
				writestruct(wd, DATA, uiPreview, 1, ui_preview);
			}
		}

		for (sl = sa->spacedata.first; sl; sl = sl->next) {
			for (ar = sl->regionbase.first; ar; ar = ar->next) {
				write_region(wd, ar, sl->spacetype);
			}

			if (sl->spacetype == SPACE_VIEW3D) {
				View3D *v3d = (View3D *)sl;
				BGpic *bgpic;
				writestruct(wd, DATA, View3D, 1, v3d);
				for (bgpic = v3d->bgpicbase.first; bgpic; bgpic = bgpic->next) {
					writestruct(wd, DATA, BGpic, 1, bgpic);
				}
				if (v3d->localvd) {
					writestruct(wd, DATA, View3D, 1, v3d->localvd);
				}

			}
			else if (sl->spacetype == SPACE_BUTS) {
				writestruct(wd, DATA, SpaceButs, 1, sl);
			}
			else if (sl->spacetype == SPACE_FILE) {
				SpaceFile *sfile = (SpaceFile *)sl;

				writestruct(wd, DATA, SpaceFile, 1, sl);
				if (sfile->params) {
					writestruct(wd, DATA, FileSelectParams, 1, sfile->params);
				}
			}
			else if (sl->spacetype == SPACE_OUTLINER) {
				SpaceOops *so = (SpaceOops *)sl;
				write_soops(wd, so);
			}
			else if (sl->spacetype == SPACE_IMAGE) {
				writestruct(wd, DATA, SpaceImage, 1, sl);
			}
			else if (sl->spacetype == SPACE_TEXT) {
				writestruct(wd, DATA, SpaceText, 1, sl);
			}
			else if (sl->spacetype == SPACE_SCRIPT) {
				SpaceScript *scr = (SpaceScript *)sl;
				scr->but_refs = NULL;
				writestruct(wd, DATA, SpaceScript, 1, sl);
			}
			else if (sl->spacetype == SPACE_CONSOLE) {
				SpaceConsole *con = (SpaceConsole *)sl;
				ConsoleLine *cl;

				for (cl = con->history.first; cl; cl = cl->next) {
					/* 'len_alloc' is invalid on write, set from 'len' on read */
					writestruct(wd, DATA, ConsoleLine, 1, cl);
					writedata(wd, DATA, cl->len + 1, cl->line);
				}
				writestruct(wd, DATA, SpaceConsole, 1, sl);

			}
			else if (sl->spacetype == SPACE_USERPREF) {
				writestruct(wd, DATA, SpaceUserPref, 1, sl);
			}
			else if (sl->spacetype == SPACE_INFO) {
				writestruct(wd, DATA, SpaceInfo, 1, sl);
			}
		}
	}
}

static void write_text(WriteData *wd, Text *text)
{
	if ((text->flags & TXT_ISMEM) && (text->flags & TXT_ISEXT)) {
		text->flags &= ~TXT_ISEXT;
	}

	/* write LibData */
	writestruct(wd, ID_TXT, Text, 1, text);
	write_iddata(wd, &text->id);

	if (text->name) {
		writedata(wd, DATA, strlen(text->name) + 1, text->name);
	}

	if (!(text->flags & TXT_ISEXT)) {
		/* now write the text data, in two steps for optimization in the readfunction */
		for (TextLine *tmp = text->lines.first; tmp; tmp = tmp->next) {
			writestruct(wd, DATA, TextLine, 1, tmp);
		}

		for (TextLine *tmp = text->lines.first; tmp; tmp = tmp->next) {
			writedata(wd, DATA, tmp->len + 1, tmp->line);
		}
	}
}

static void write_group(WriteData *wd, Group *group)
{
	if (group->id.us > 0 || wd->use_memfile) {
		/* write LibData */
		writestruct(wd, ID_GR, Group, 1, group);
		write_iddata(wd, &group->id);

		write_previews(wd, group->preview);

		for (GroupObject *go = group->gobject.first; go; go = go->next) {
			writestruct(wd, DATA, GroupObject, 1, go);
		}
	}
}

static void write_cachefile(WriteData *wd, CacheFile *cache_file)
{
	if (cache_file->id.us > 0 || wd->use_memfile) {
		writestruct(wd, ID_CF, CacheFile, 1, cache_file);

	}
}

/* Keep it last of write_foodata functions. */
static void write_libraries(WriteData *wd, Main *main)
{
	ListBase *lbarray[MAX_LIBARRAY];
	ID *id;
	int a, tot;
	bool found_one;

	for (; main; main = main->next) {
		a = tot = set_listbasepointers(main, lbarray);

		/* test: is lib being used */
		if (main->curlib && main->curlib->packedfile) {
			found_one = true;
		}
		else {
			found_one = false;
			while (!found_one && tot--) {
				for (id = lbarray[tot]->first; id; id = id->next) {
					if (id->us > 0 && (id->tag & LIB_TAG_EXTERN)) {
						found_one = true;
						break;
					}
				}
			}
		}

		/* to be able to restore quit.blend and temp saves, the packed blend has to be in undo buffers... */
		/* XXX needs rethink, just like save UI in undo files now - would be nice to append things only for the]
		 * quit.blend and temp saves */
		if (found_one) {
			writestruct(wd, ID_LI, Library, 1, main->curlib);
			write_iddata(wd, &main->curlib->id);

			if (main->curlib->packedfile) {
				PackedFile *pf = main->curlib->packedfile;
				writestruct(wd, DATA, PackedFile, 1, pf);
				writedata(wd, DATA, pf->size, pf->data);
				if (wd->use_memfile == false) {
					printf("write packed .blend: %s\n", main->curlib->name);
				}
			}

			while (a--) {
				for (id = lbarray[a]->first; id; id = id->next) {
					if (id->us > 0 && (id->tag & LIB_TAG_EXTERN)) {
						if (!BKE_idcode_is_linkable(GS(id->name))) {
							printf("ERROR: write file: data-block '%s' from lib '%s' is not linkable "
							       "but is flagged as directly linked", id->name, main->curlib->filepath);
							BLI_assert(0);
						}
						writestruct(wd, ID_ID, ID, 1, id);
					}
				}
			}
		}
	}

	mywrite_flush(wd);
}

/* context is usually defined by WM, two cases where no WM is available:
 * - for forward compatibility, curscreen has to be saved
 * - for undofile, curscene needs to be saved */
static void write_global(WriteData *wd, int fileflags, Main *mainvar)
{
	const bool is_undo = wd->use_memfile;
	FileGlobal fg;
	bScreen *screen;
	char subvstr[8];

	/* prevent mem checkers from complaining */
	memset(fg.pad, 0, sizeof(fg.pad));
	memset(fg.filename, 0, sizeof(fg.filename));
	memset(fg.build_hash, 0, sizeof(fg.build_hash));

	current_screen_compat(mainvar, &screen, is_undo);

	/* XXX still remap G */
	fg.curscreen = screen;
	fg.curscene = screen ? screen->scene : NULL;

	/* prevent to save this, is not good convention, and feature with concerns... */
	fg.fileflags = (fileflags & ~G_FILE_FLAGS_RUNTIME);

	fg.globalf = G.f;
	BLI_strncpy(fg.filename, mainvar->name, sizeof(fg.filename));
	sprintf(subvstr, "%4d", BLENDER_SUBVERSION);
	memcpy(fg.subvstr, subvstr, 4);

	fg.subversion = BLENDER_SUBVERSION;
	fg.minversion = BLENDER_MINVERSION;
	fg.minsubversion = BLENDER_MINSUBVERSION;
#ifdef WITH_BUILDINFO
	{
		extern unsigned long build_commit_timestamp;
		extern char build_hash[];
		/* TODO(sergey): Add branch name to file as well? */
		fg.build_commit_timestamp = build_commit_timestamp;
		BLI_strncpy(fg.build_hash, build_hash, sizeof(fg.build_hash));
	}
#else
	fg.build_commit_timestamp = 0;
	BLI_strncpy(fg.build_hash, "unknown", sizeof(fg.build_hash));
#endif
	writestruct(wd, GLOB, FileGlobal, 1, &fg);
}

/* preview image, first 2 values are width and height
 * second are an RGBA image (uchar)
 * note, this uses 'TEST' since new types will segfault on file load for older blender versions.
 */
static void write_thumb(WriteData *wd, const BlendThumbnail *thumb)
{
	if (thumb) {
		writedata(wd, TEST, BLEN_THUMB_MEMSIZE_FILE(thumb->width, thumb->height), thumb);
	}
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name File Writing (Private)
 * \{ */

/* if MemFile * there's filesave to memory */
static bool write_file_handle(
        Main *mainvar,
        WriteWrap *ww,
        MemFile *compare, MemFile *current,
        int write_flags, const BlendThumbnail *thumb)
{
	BHead bhead;
	ListBase mainlist;
	char buf[16];
	WriteData *wd;

	blo_split_main(&mainlist, mainvar);

	wd = mywrite_begin(ww, compare, current);

#ifdef USE_BMESH_SAVE_AS_COMPAT
	wd->use_mesh_compat = (write_flags & G_FILE_MESH_COMPAT) != 0;
#endif

#ifdef USE_NODE_COMPAT_CUSTOMNODES
	/* don't write compatibility data on undo */
	if (!current) {
		/* deprecated forward compat data is freed again below */
		customnodes_add_deprecated_data(mainvar);
	}
#endif

	sprintf(buf, "BLENDER%c%c%.3d",
	        (sizeof(void *) == 8)      ? '-' : '_',
	        (ENDIAN_ORDER == B_ENDIAN) ? 'V' : 'v',
	        BLENDER_VERSION);

	mywrite(wd, buf, 12);

	write_thumb(wd, thumb);
	write_global(wd, write_flags, mainvar);

	/* The windowmanager and screen often change,
	 * avoid thumbnail detecting changes because of this. */
	mywrite_flush(wd);

	ListBase *lbarray[MAX_LIBARRAY];
	int a = set_listbasepointers(mainvar, lbarray);
	while (a--) {
		ID *id = lbarray[a]->first;

		if (id && GS(id->name) == ID_LI) {
			continue;  /* Libraries are handled separately below. */
		}

		for (; id; id = id->next) {
			/* We should never attempt to write non-regular IDs (i.e. all kind of temp/runtime ones). */
			BLI_assert((id->tag & (LIB_TAG_NO_MAIN | LIB_TAG_NO_USER_REFCOUNT | LIB_TAG_NOT_ALLOCATED)) == 0);

			switch ((ID_Type)GS(id->name)) {
				case ID_WM:
					write_windowmanager(wd, (wmWindowManager *)id);
					break;
				case ID_SCR:
					write_screen(wd, (bScreen *)id);
					break;
				case ID_SCE:
					write_scene(wd, (Scene *)id);
					break;
				case ID_CU:
					write_curve(wd, (Curve *)id);
					break;
				case ID_IM:
					write_image(wd, (Image *)id);
					break;
				case ID_CA:
					write_camera(wd, (Camera *)id);
					break;
				case ID_LA:
					write_lamp(wd, (Lamp *)id);
					break;
				case ID_VF:
					write_vfont(wd, (VFont *)id);
					break;
				case ID_WO:
					write_world(wd, (World *)id);
					break;
				case ID_TXT:
					write_text(wd, (Text *)id);
					break;
				case ID_GR:
					write_group(wd, (Group *)id);
					break;
				case ID_OB:
					write_object(wd, (Object *)id);
					break;
				case ID_MA:
					write_material(wd, (Material *)id);
					break;
				case ID_TE:
					write_texture(wd, (Tex *)id);
					break;
				case ID_ME:
					write_mesh(wd, (Mesh *)id);
					break;
				case ID_CF:
					write_cachefile(wd, (CacheFile *)id);
					break;
				case ID_LI:
					/* Do nothing, handled below - and should never be reached. */
					BLI_assert(0);
					break;
				default:
					/* Should never be reached. */
					BLI_assert(0);
					break;
			}
		}

		mywrite_flush(wd);
	}

	/* Special handling, operating over split Mains... */
	write_libraries(wd,  mainvar->next);

	/* So changes above don't cause a 'DNA1' to be detected as changed on undo. */
	mywrite_flush(wd);

	if (write_flags & G_FILE_USERPREFS) {
		write_userdef(wd, &U);
	}

	/* Write DNA last, because (to be implemented) test for which structs are written.
	 *
	 * Note that we *borrow* the pointer to 'DNAstr',
	 * so writing each time uses the same address and doesn't cause unnecessary undo overhead. */
	writedata(wd, DNA1, wd->sdna->datalen, wd->sdna->data);

#ifdef USE_NODE_COMPAT_CUSTOMNODES
	/* compatibility data not created on undo */
	if (!current) {
		/* Ugly, forward compatibility code generates deprecated data during writing,
		 * this has to be freed again. Can not be done directly after writing, otherwise
		 * the data pointers could be reused and not be mapped correctly.
		 */
		customnodes_free_deprecated_data(mainvar);
	}
#endif

	/* end of file */
	memset(&bhead, 0, sizeof(BHead));
	bhead.code = ENDB;
	mywrite(wd, &bhead, sizeof(BHead));

	blo_join_main(&mainlist);

	return mywrite_end(wd);
}

/* do reverse file history: .blend1 -> .blend2, .blend -> .blend1 */
/* return: success(0), failure(1) */
static bool do_history(const char *name, ReportList *reports)
{
	char tempname1[FILE_MAX], tempname2[FILE_MAX];
	int hisnr = U.versions;

	if (U.versions == 0) {
		return 0;
	}

	if (strlen(name) < 2) {
		BKE_report(reports, RPT_ERROR, "Unable to make version backup: filename too short");
		return 1;
	}

	while (hisnr > 1) {
		BLI_snprintf(tempname1, sizeof(tempname1), "%s%d", name, hisnr - 1);
		if (BLI_exists(tempname1)) {
			BLI_snprintf(tempname2, sizeof(tempname2), "%s%d", name, hisnr);

			if (BLI_rename(tempname1, tempname2)) {
				BKE_report(reports, RPT_ERROR, "Unable to make version backup");
				return true;
			}
		}
		hisnr--;
	}

	/* is needed when hisnr==1 */
	if (BLI_exists(name)) {
		BLI_snprintf(tempname1, sizeof(tempname1), "%s%d", name, hisnr);

		if (BLI_rename(name, tempname1)) {
			BKE_report(reports, RPT_ERROR, "Unable to make version backup");
			return true;
		}
	}

	return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name File Writing (Public)
 * \{ */

/**
 * \return Success.
 */
bool BLO_write_file(
        Main *mainvar, const char *filepath, int write_flags,
        ReportList *reports, const BlendThumbnail *thumb)
{
	char tempname[FILE_MAX + 1];
	eWriteWrapType ww_type;
	WriteWrap ww;

	/* path backup/restore */
	void     *path_list_backup = NULL;
	const int path_list_flag = (BKE_BPATH_TRAVERSE_SKIP_LIBRARY | BKE_BPATH_TRAVERSE_SKIP_MULTIFILE);

	if (G.debug & G_DEBUG_IO && mainvar->lock != NULL) {
		BKE_report(reports, RPT_INFO, "Checking sanity of current .blend file *BEFORE* save to disk.");
		BLO_main_validate_libraries(mainvar, reports);
	}

	/* open temporary file, so we preserve the original in case we crash */
	BLI_snprintf(tempname, sizeof(tempname), "%s@", filepath);

	if (write_flags & G_FILE_COMPRESS) {
		ww_type = WW_WRAP_ZLIB;
	}
	else {
		ww_type = WW_WRAP_NONE;
	}

	ww_handle_init(ww_type, &ww);

	if (ww.open(&ww, tempname) == false) {
		BKE_reportf(reports, RPT_ERROR, "Cannot open file %s for writing: %s", tempname, strerror(errno));
		return 0;
	}

	/* check if we need to backup and restore paths */
	if (UNLIKELY((write_flags & G_FILE_RELATIVE_REMAP) && (G_FILE_SAVE_COPY & write_flags))) {
		path_list_backup = BKE_bpath_list_backup(mainvar, path_list_flag);
	}

	/* remapping of relative paths to new file location */
	if (write_flags & G_FILE_RELATIVE_REMAP) {
		char dir1[FILE_MAX];
		char dir2[FILE_MAX];
		BLI_split_dir_part(filepath, dir1, sizeof(dir1));
		BLI_split_dir_part(mainvar->name, dir2, sizeof(dir2));

		/* just in case there is some subtle difference */
		BLI_cleanup_dir(mainvar->name, dir1);
		BLI_cleanup_dir(mainvar->name, dir2);

		if (G.relbase_valid && (BLI_path_cmp(dir1, dir2) == 0)) {
			write_flags &= ~G_FILE_RELATIVE_REMAP;
		}
		else {
			if (G.relbase_valid) {
				/* blend may not have been saved before. Tn this case
				 * we should not have any relative paths, but if there
				 * is somehow, an invalid or empty G_MAIN->name it will
				 * print an error, don't try make the absolute in this case. */
				BKE_bpath_absolute_convert(mainvar, BKE_main_blendfile_path_from_global(), NULL);
			}
		}
	}

	if (write_flags & G_FILE_RELATIVE_REMAP) {
		/* note, making relative to something OTHER then G_MAIN->name */
		BKE_bpath_relative_convert(mainvar, filepath, NULL);
	}

	/* actual file writing */
	const bool err = write_file_handle(mainvar, &ww, NULL, NULL, write_flags, thumb);

	ww.close(&ww);

	if (UNLIKELY(path_list_backup)) {
		BKE_bpath_list_restore(mainvar, path_list_flag, path_list_backup);
		BKE_bpath_list_free(path_list_backup);
	}

	if (err) {
		BKE_report(reports, RPT_ERROR, strerror(errno));
		remove(tempname);

		return 0;
	}

	/* file save to temporary file was successful */
	/* now do reverse file history (move .blend1 -> .blend2, .blend -> .blend1) */
	if (write_flags & G_FILE_HISTORY) {
		const bool err_hist = do_history(filepath, reports);
		if (err_hist) {
			BKE_report(reports, RPT_ERROR, "Version backup failed (file saved with @)");
			return 0;
		}
	}

	if (BLI_rename(tempname, filepath) != 0) {
		BKE_report(reports, RPT_ERROR, "Cannot change old file (file saved with @)");
		return 0;
	}

	if (G.debug & G_DEBUG_IO && mainvar->lock != NULL) {
		BKE_report(reports, RPT_INFO, "Checking sanity of current .blend file *AFTER* save to disk.");
		BLO_main_validate_libraries(mainvar, reports);
	}

	return 1;
}

/**
 * \return Success.
 */
bool BLO_write_file_mem(Main *mainvar, MemFile *compare, MemFile *current, int write_flags)
{
	write_flags &= ~G_FILE_USERPREFS;

	const bool err = write_file_handle(mainvar, NULL, compare, current, write_flags, NULL);

	return (err == 0);
}

/** \} */
