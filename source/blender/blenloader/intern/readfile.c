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

/** \file blender/blenloader/intern/readfile.c
 *  \ingroup blenloader
 */


#include "zlib.h"

#include <limits.h>
#include <stdio.h> // for printf fopen fwrite fclose sprintf FILE
#include <stdlib.h> // for getenv atoi
#include <stddef.h> // for offsetof
#include <fcntl.h> // for open
#include <string.h> // for strrchr strncmp strstr
#include <math.h> // for fabs
#include <stdarg.h> /* for va_start/end */
#include <time.h> /* for gmtime */
#include <ctype.h> /* for isdigit */

#include "BLI_utildefines.h"
#include <unistd.h> // for read close

#include "DNA_camera_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_curve_types.h"
#include "DNA_fileglobal_types.h"
#include "DNA_genfile.h"
#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_property_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_text_types.h"
#include "DNA_view3d_types.h"
#include "DNA_screen_types.h"
#include "DNA_sdna_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"
#include "DNA_vfont_types.h"
#include "DNA_world_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_endian_switch.h"
#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_threads.h"
#include "BLI_mempool.h"
#include "BLI_ghash.h"

#include "BLT_translation.h"

#include "BKE_cachefile.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h" // for G
#include "BKE_group.h"
#include "BKE_library.h" // for which_libbase
#include "BKE_library_idmap.h"
#include "BKE_library_query.h"
#include "BKE_idcode.h"
#include "BKE_idprop.h"
#include "BKE_material.h"
#include "BKE_main.h" // for Main
#include "BKE_mesh.h" // for ME_ defines (patching)
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_outliner_treehash.h"
#include "BKE_colortools.h"

#include "BLO_readfile.h"
#include "BLO_undofile.h"
#include "BLO_blend_defs.h"

#include "readfile.h"


#include <errno.h>

/**
 * READ
 * ====
 *
 * - Existing Library (#Main) push or free
 * - allocate new #Main
 * - load file
 * - read #SDNA
 * - for each LibBlock
 *   - read LibBlock
 *   - if a Library
 *     - make a new #Main
 *     - attach ID's to it
 *   - else
 *     - read associated 'direct data'
 *     - link direct data (internal and to LibBlock)
 * - read #FileGlobal
 * - read #USER data, only when indicated (file is ``~/X.XX/startup.blend``)
 * - free file
 * - per Library (per #Main)
 *   - read file
 *   - read #SDNA
 *   - find LibBlocks and attach #ID's to #Main
 *     - if external LibBlock
 *       - search all #Main's
 *         - or it's already read,
 *         - or not read yet
 *         - or make new #Main
 *   - per LibBlock
 *     - read recursive
 *     - read associated direct data
 *     - link direct data (internal and to LibBlock)
 *   - free file
 * - per Library with unread LibBlocks
 *   - read file
 *   - read #SDNA
 *   - per LibBlock
 *     - read recursive
 *     - read associated direct data
 *     - link direct data (internal and to LibBlock)
 *   - free file
 * - join all #Main's
 * - link all LibBlocks and indirect pointers to libblocks
 * - initialize #FileGlobal and copy pointers to #Global
 *
 * \note Still a weak point is the new-address function, that doesnt solve reading from
 * multiple files at the same time.
 * (added remark: oh, i thought that was solved? will look at that... (ton).
 */

/* use GHash for BHead name-based lookups (speeds up linking) */
#define USE_GHASH_BHEAD

/* Use GHash for restoring pointers by name */
#define USE_GHASH_RESTORE_POINTER

/* Define this to have verbose debug prints. */
#define USE_DEBUG_PRINT

#ifdef USE_DEBUG_PRINT
#  define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#  define DEBUG_PRINTF(...)
#endif

/***/

typedef struct OldNew {
	const void *old;
	void *newp;
	int nr;
} OldNew;

typedef struct OldNewMap {
	OldNew *entries;
	int nentries, entriessize;
	bool sorted;
	int lasthit;
} OldNewMap;


/* local prototypes */
static void *read_struct(FileData *fd, BHead *bh, const char *blockname);
static void direct_link_modifiers(FileData *fd, ListBase *lb);
static void convert_tface_mt(FileData *fd, Main *main);
static BHead *find_bhead_from_code_name(FileData *fd, const short idcode, const char *name);
static BHead *find_bhead_from_idname(FileData *fd, const char *idname);

/* this function ensures that reports are printed,
 * in the case of libraray linking errors this is important!
 *
 * bit kludge but better then doubling up on prints,
 * we could alternatively have a versions of a report function which forces printing - campbell
 */

void blo_reportf_wrap(ReportList *reports, ReportType type, const char *format, ...)
{
	char fixed_buf[1024]; /* should be long enough */

	va_list args;

	va_start(args, format);
	vsnprintf(fixed_buf, sizeof(fixed_buf), format, args);
	va_end(args);

	fixed_buf[sizeof(fixed_buf) - 1] = '\0';

	BKE_report(reports, type, fixed_buf);

	printf("%s: %s\n", BKE_report_type_str(type), fixed_buf);
}

/* for reporting linking messages */
static const char *library_parent_filepath(Library *lib)
{
	return lib->parent ? lib->parent->filepath : "<direct>";
}

static OldNewMap *oldnewmap_new(void)
{
	OldNewMap *onm = MEM_callocN(sizeof(*onm), "OldNewMap");

	onm->entriessize = 1024;
	onm->entries = MEM_malloc_arrayN(onm->entriessize, sizeof(*onm->entries), "OldNewMap.entries");

	return onm;
}

static int verg_oldnewmap(const void *v1, const void *v2)
{
	const struct OldNew *x1 = v1, *x2 = v2;

	if (x1->old > x2->old) return 1;
	else if (x1->old < x2->old) return -1;
	return 0;
}


static void oldnewmap_sort(FileData *fd)
{
	BLI_assert(fd->libmap->sorted == false);
	qsort(fd->libmap->entries, fd->libmap->nentries, sizeof(OldNew), verg_oldnewmap);
	fd->libmap->sorted = 1;
}

/* nr is zero for data, and ID code for libdata */
static void oldnewmap_insert(OldNewMap *onm, const void *oldaddr, void *newaddr, int nr)
{
	OldNew *entry;

	if (oldaddr == NULL || newaddr == NULL) return;

	if (UNLIKELY(onm->nentries == onm->entriessize)) {
		onm->entriessize *= 2;
		onm->entries = MEM_reallocN(onm->entries, sizeof(*onm->entries) * onm->entriessize);
	}

	entry = &onm->entries[onm->nentries++];
	entry->old = oldaddr;
	entry->newp = newaddr;
	entry->nr = nr;
}

void blo_do_versions_oldnewmap_insert(OldNewMap *onm, const void *oldaddr, void *newaddr, int nr)
{
	oldnewmap_insert(onm, oldaddr, newaddr, nr);
}

/**
 * Do a full search (no state).
 *
 * \param lasthit: Use as a reference position to avoid a full search
 * from either end of the array, giving more efficient lookups.
 *
 * \note This would seem an ideal case for hash or btree lookups.
 * However the data is written in-order, using the \a lasthit will normally avoid calling this function.
 * Creating a btree/hash structure adds overhead for the common-case to optimize the corner-case
 * (since most entries will never be retrieved).
 * So just keep full lookups as a fall-back.
 */
static int oldnewmap_lookup_entry_full(const OldNewMap *onm, const void *addr, int lasthit)
{
	const int nentries = onm->nentries;
	const OldNew *entries = onm->entries;
	int i;

	/* search relative to lasthit where possible */
	if (lasthit >= 0 && lasthit < nentries) {

		/* search forwards */
		i = lasthit;
		while (++i != nentries) {
			if (entries[i].old == addr) {
				return i;
			}
		}

		/* search backwards */
		i = lasthit + 1;
		while (i--) {
			if (entries[i].old == addr) {
				return i;
			}
		}
	}
	else {
		/* search backwards (full) */
		i = nentries;
		while (i--) {
			if (entries[i].old == addr) {
				return i;
			}
		}
	}

	return -1;
}

static void *oldnewmap_lookup_and_inc(OldNewMap *onm, const void *addr, bool increase_users)
{
	int i;

	if (addr == NULL) return NULL;

	if (onm->lasthit < onm->nentries - 1) {
		OldNew *entry = &onm->entries[++onm->lasthit];

		if (entry->old == addr) {
			if (increase_users)
				entry->nr++;
			return entry->newp;
		}
	}

	i = oldnewmap_lookup_entry_full(onm, addr, onm->lasthit);
	if (i != -1) {
		OldNew *entry = &onm->entries[i];
		BLI_assert(entry->old == addr);
		onm->lasthit = i;
		if (increase_users)
			entry->nr++;
		return entry->newp;
	}

	return NULL;
}

/* for libdata, nr has ID code, no increment */
static void *oldnewmap_liblookup(OldNewMap *onm, const void *addr, const void *lib)
{
	if (addr == NULL) {
		return NULL;
	}

	/* lasthit works fine for non-libdata, linking there is done in same sequence as writing */
	if (onm->sorted) {
		const OldNew entry_s = {.old = addr};
		OldNew *entry = bsearch(&entry_s, onm->entries, onm->nentries, sizeof(OldNew), verg_oldnewmap);
		if (entry) {
			ID *id = entry->newp;

			if (id && (!lib || id->lib)) {
				return id;
			}
		}
	}
	else {
		/* note, this can be a bottle neck when loading some files */
		const int i = oldnewmap_lookup_entry_full(onm, addr, -1);
		if (i != -1) {
			OldNew *entry = &onm->entries[i];
			ID *id = entry->newp;
			BLI_assert(entry->old == addr);
			if (id && (!lib || id->lib)) {
				return id;
			}
		}
	}

	return NULL;
}

static void oldnewmap_free_unused(OldNewMap *onm)
{
	int i;

	for (i = 0; i < onm->nentries; i++) {
		OldNew *entry = &onm->entries[i];
		if (entry->nr == 0) {
			MEM_freeN(entry->newp);
			entry->newp = NULL;
		}
	}
}

static void oldnewmap_clear(OldNewMap *onm)
{
	onm->nentries = 0;
	onm->lasthit = 0;
}

static void oldnewmap_free(OldNewMap *onm)
{
	MEM_freeN(onm->entries);
	MEM_freeN(onm);
}

/***/

static void read_libraries(FileData *basefd, ListBase *mainlist);

/* ************ help functions ***************** */

static void add_main_to_main(Main *mainvar, Main *from)
{
	ListBase *lbarray[MAX_LIBARRAY], *fromarray[MAX_LIBARRAY];
	int a;

	set_listbasepointers(mainvar, lbarray);
	a = set_listbasepointers(from, fromarray);
	while (a--) {
		BLI_movelisttolist(lbarray[a], fromarray[a]);
	}
}

void blo_join_main(ListBase *mainlist)
{
	Main *tojoin, *mainl;

	mainl = mainlist->first;
	while ((tojoin = mainl->next)) {
		add_main_to_main(mainl, tojoin);
		BLI_remlink(mainlist, tojoin);
		BKE_main_free(tojoin);
	}
}

static void split_libdata(ListBase *lb_src, Main **lib_main_array, const uint lib_main_array_len)
{
	for (ID *id = lb_src->first, *idnext; id; id = idnext) {
		idnext = id->next;

		if (id->lib) {
			if (((uint)id->lib->temp_index < lib_main_array_len) &&
			    /* this check should never fail, just incase 'id->lib' is a dangling pointer. */
			    (lib_main_array[id->lib->temp_index]->curlib == id->lib))
			{
				Main *mainvar = lib_main_array[id->lib->temp_index];
				ListBase *lb_dst = which_libbase(mainvar, GS(id->name));
				BLI_remlink(lb_src, id);
				BLI_addtail(lb_dst, id);
			}
			else {
				printf("%s: invalid library for '%s'\n", __func__, id->name);
				BLI_assert(0);
			}
		}
	}
}

void blo_split_main(ListBase *mainlist, Main *main)
{
	mainlist->first = mainlist->last = main;
	main->next = NULL;

	if (BLI_listbase_is_empty(&main->library))
		return;

	/* (Library.temp_index -> Main), lookup table */
	const uint lib_main_array_len = BLI_listbase_count(&main->library);
	Main     **lib_main_array     = MEM_malloc_arrayN(lib_main_array_len, sizeof(*lib_main_array), __func__);

	int i = 0;
	for (Library *lib = main->library.first; lib; lib = lib->id.next, i++) {
		Main *libmain = BKE_main_new();
		libmain->curlib = lib;
		libmain->versionfile = lib->versionfile;
		libmain->subversionfile = lib->subversionfile;
		BLI_addtail(mainlist, libmain);
		lib->temp_index = i;
		lib_main_array[i] = libmain;
	}

	ListBase *lbarray[MAX_LIBARRAY];
	i = set_listbasepointers(main, lbarray);
	while (i--) {
		ID *id = lbarray[i]->first;
		if (id == NULL || GS(id->name) == ID_LI) {
			continue;  /* no ID_LI datablock should ever be linked anyway, but just in case, better be explicit. */
		}
		split_libdata(lbarray[i], lib_main_array, lib_main_array_len);
	}

	MEM_freeN(lib_main_array);
}

static void read_file_version(FileData *fd, Main *main)
{
	BHead *bhead;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (bhead->code == GLOB) {
			FileGlobal *fg = read_struct(fd, bhead, "Global");
			if (fg) {
				main->subversionfile = fg->subversion;
				main->minversionfile = fg->minversion;
				main->minsubversionfile = fg->minsubversion;
				MEM_freeN(fg);
			}
			else if (bhead->code == ENDB)
				break;
		}
	}
	if (main->curlib) {
		main->curlib->versionfile = main->versionfile;
		main->curlib->subversionfile = main->subversionfile;
	}
}

#ifdef USE_GHASH_BHEAD
static void read_file_bhead_idname_map_create(FileData *fd)
{
	BHead *bhead;

	/* dummy values */
	bool is_link = false;
	int code_prev = ENDB;
	uint reserve = 0;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (code_prev != bhead->code) {
			code_prev = bhead->code;
			is_link = BKE_idcode_is_valid(code_prev) ? BKE_idcode_is_linkable(code_prev) : false;
		}

		if (is_link) {
			reserve += 1;
		}
	}

	BLI_assert(fd->bhead_idname_hash == NULL);

	fd->bhead_idname_hash = BLI_ghash_str_new_ex(__func__, reserve);

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (code_prev != bhead->code) {
			code_prev = bhead->code;
			is_link = BKE_idcode_is_valid(code_prev) ? BKE_idcode_is_linkable(code_prev) : false;
		}

		if (is_link) {
			BLI_ghash_insert(fd->bhead_idname_hash, (void *)bhead_id_name(fd, bhead), bhead);
		}
	}
}
#endif


static Main *blo_find_main(FileData *fd, const char *filepath, const char *relabase)
{
	ListBase *mainlist = fd->mainlist;
	Main *m;
	Library *lib;
	char name1[FILE_MAX];

	BLI_strncpy(name1, filepath, sizeof(name1));
	BLI_cleanup_path(relabase, name1);

//	printf("blo_find_main: relabase  %s\n", relabase);
//	printf("blo_find_main: original in  %s\n", filepath);
//	printf("blo_find_main: converted to %s\n", name1);

	for (m = mainlist->first; m; m = m->next) {
		const char *libname = (m->curlib) ? m->curlib->filepath : m->name;

		if (BLI_path_cmp(name1, libname) == 0) {
			if (G.debug & G_DEBUG) printf("blo_find_main: found library %s\n", libname);
			return m;
		}
	}

	m = BKE_main_new();
	BLI_addtail(mainlist, m);

	/* Add library datablock itself to 'main' Main, since libraries are **never** linked data.
	 * Fixes bug where you could end with all ID_LI datablocks having the same name... */
	lib = BKE_libblock_alloc(mainlist->first, ID_LI, "Lib", 0);
	lib->id.us = ID_FAKE_USERS(lib);  /* Important, consistency with main ID reading code from read_libblock(). */
	BLI_strncpy(lib->name, filepath, sizeof(lib->name));
	BLI_strncpy(lib->filepath, name1, sizeof(lib->filepath));

	m->curlib = lib;

	read_file_version(fd, m);

	if (G.debug & G_DEBUG) printf("blo_find_main: added new lib %s\n", filepath);
	return m;
}


/* ************ FILE PARSING ****************** */

static void switch_endian_bh4(BHead4 *bhead)
{
	/* the ID_.. codes */
	if ((bhead->code & 0xFFFF) == 0) bhead->code >>= 16;

	if (bhead->code != ENDB) {
		BLI_endian_switch_int32(&bhead->len);
		BLI_endian_switch_int32(&bhead->SDNAnr);
		BLI_endian_switch_int32(&bhead->nr);
	}
}

static void switch_endian_bh8(BHead8 *bhead)
{
	/* the ID_.. codes */
	if ((bhead->code & 0xFFFF) == 0) bhead->code >>= 16;

	if (bhead->code != ENDB) {
		BLI_endian_switch_int32(&bhead->len);
		BLI_endian_switch_int32(&bhead->SDNAnr);
		BLI_endian_switch_int32(&bhead->nr);
	}
}

static void bh4_from_bh8(BHead *bhead, BHead8 *bhead8, int do_endian_swap)
{
	BHead4 *bhead4 = (BHead4 *)bhead;
	int64_t old;

	bhead4->code = bhead8->code;
	bhead4->len = bhead8->len;

	if (bhead4->code != ENDB) {
		/* perform a endian swap on 64bit pointers, otherwise the pointer might map to zero
		 * 0x0000000000000000000012345678 would become 0x12345678000000000000000000000000
		 */
		if (do_endian_swap) {
			BLI_endian_switch_int64(&bhead8->old);
		}

		/* this patch is to avoid a long long being read from not-eight aligned positions
		 * is necessary on any modern 64bit architecture) */
		memcpy(&old, &bhead8->old, 8);
		bhead4->old = (int)(old >> 3);

		bhead4->SDNAnr = bhead8->SDNAnr;
		bhead4->nr = bhead8->nr;
	}
}

static void bh8_from_bh4(BHead *bhead, BHead4 *bhead4)
{
	BHead8 *bhead8 = (BHead8 *)bhead;

	bhead8->code = bhead4->code;
	bhead8->len = bhead4->len;

	if (bhead8->code != ENDB) {
		bhead8->old = bhead4->old;
		bhead8->SDNAnr = bhead4->SDNAnr;
		bhead8->nr = bhead4->nr;
	}
}

static BHeadN *get_bhead(FileData *fd)
{
	BHeadN *new_bhead = NULL;
	int readsize;

	if (fd) {
		if (!fd->eof) {
			/* initializing to zero isn't strictly needed but shuts valgrind up
			 * since uninitialized memory gets compared */
			BHead8 bhead8 = {0};
			BHead4 bhead4 = {0};
			BHead bhead = {0};

			/* First read the bhead structure.
			 * Depending on the platform the file was written on this can
			 * be a big or little endian BHead4 or BHead8 structure.
			 *
			 * As usual 'ENDB' (the last *partial* bhead of the file)
			 * needs some special handling. We don't want to EOF just yet.
			 */
			if (fd->flags & FD_FLAGS_FILE_POINTSIZE_IS_4) {
				bhead4.code = DATA;
				readsize = fd->read(fd, &bhead4, sizeof(bhead4));

				if (readsize == sizeof(bhead4) || bhead4.code == ENDB) {
					if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
						switch_endian_bh4(&bhead4);
					}

					if (fd->flags & FD_FLAGS_POINTSIZE_DIFFERS) {
						bh8_from_bh4(&bhead, &bhead4);
					}
					else {
						/* MIN2 is only to quiet '-Warray-bounds' compiler warning. */
						BLI_assert(sizeof(bhead) == sizeof(bhead4));
						memcpy(&bhead, &bhead4, MIN2(sizeof(bhead), sizeof(bhead4)));
					}
				}
				else {
					fd->eof = 1;
					bhead.len = 0;
				}
			}
			else {
				bhead8.code = DATA;
				readsize = fd->read(fd, &bhead8, sizeof(bhead8));

				if (readsize == sizeof(bhead8) || bhead8.code == ENDB) {
					if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
						switch_endian_bh8(&bhead8);
					}

					if (fd->flags & FD_FLAGS_POINTSIZE_DIFFERS) {
						bh4_from_bh8(&bhead, &bhead8, (fd->flags & FD_FLAGS_SWITCH_ENDIAN));
					}
					else {
						/* MIN2 is only to quiet '-Warray-bounds' compiler warning. */
						BLI_assert(sizeof(bhead) == sizeof(bhead8));
						memcpy(&bhead, &bhead8, MIN2(sizeof(bhead), sizeof(bhead8)));
					}
				}
				else {
					fd->eof = 1;
					bhead.len = 0;
				}
			}

			/* make sure people are not trying to pass bad blend files */
			if (bhead.len < 0) fd->eof = 1;

			/* bhead now contains the (converted) bhead structure. Now read
			 * the associated data and put everything in a BHeadN (creative naming !)
			 */
			if (!fd->eof) {
				new_bhead = MEM_mallocN(sizeof(BHeadN) + bhead.len, "new_bhead");
				if (new_bhead) {
					new_bhead->next = new_bhead->prev = NULL;
					new_bhead->bhead = bhead;

					readsize = fd->read(fd, new_bhead + 1, bhead.len);

					if (readsize != bhead.len) {
						fd->eof = 1;
						MEM_freeN(new_bhead);
						new_bhead = NULL;
					}
				}
				else {
					fd->eof = 1;
				}
			}
		}
	}

	/* We've read a new block. Now add it to the list
	 * of blocks.
	 */
	if (new_bhead) {
		BLI_addtail(&fd->listbase, new_bhead);
	}

	return(new_bhead);
}

BHead *blo_firstbhead(FileData *fd)
{
	BHeadN *new_bhead;
	BHead *bhead = NULL;

	/* Rewind the file
	 * Read in a new block if necessary
	 */
	new_bhead = fd->listbase.first;
	if (new_bhead == NULL) {
		new_bhead = get_bhead(fd);
	}

	if (new_bhead) {
		bhead = &new_bhead->bhead;
	}

	return(bhead);
}

BHead *blo_prevbhead(FileData *UNUSED(fd), BHead *thisblock)
{
	BHeadN *bheadn = (BHeadN *)POINTER_OFFSET(thisblock, -offsetof(BHeadN, bhead));
	BHeadN *prev = bheadn->prev;

	return (prev) ? &prev->bhead : NULL;
}

BHead *blo_nextbhead(FileData *fd, BHead *thisblock)
{
	BHeadN *new_bhead = NULL;
	BHead *bhead = NULL;

	if (thisblock) {
		/* bhead is actually a sub part of BHeadN
		 * We calculate the BHeadN pointer from the BHead pointer below */
		new_bhead = (BHeadN *)POINTER_OFFSET(thisblock, -offsetof(BHeadN, bhead));

		/* get the next BHeadN. If it doesn't exist we read in the next one */
		new_bhead = new_bhead->next;
		if (new_bhead == NULL) {
			new_bhead = get_bhead(fd);
		}
	}

	if (new_bhead) {
		/* here we do the reverse:
		 * go from the BHeadN pointer to the BHead pointer */
		bhead = &new_bhead->bhead;
	}

	return(bhead);
}

/* Warning! Caller's responsibility to ensure given bhead **is** and ID one! */
const char *bhead_id_name(const FileData *fd, const BHead *bhead)
{
	return (const char *)POINTER_OFFSET(bhead, sizeof(*bhead) + fd->id_name_offs);
}

static void decode_blender_header(FileData *fd)
{
	char header[SIZEOFBLENDERHEADER], num[4];
	int readsize;

	/* read in the header data */
	readsize = fd->read(fd, header, sizeof(header));

	if (readsize == sizeof(header) &&
	    STREQLEN(header, "BLENDER", 7) &&
	    ELEM(header[7], '_', '-') &&
	    ELEM(header[8], 'v', 'V') &&
	    (isdigit(header[9]) && isdigit(header[10]) && isdigit(header[11])))
	{
		fd->flags |= FD_FLAGS_FILE_OK;

		/* what size are pointers in the file ? */
		if (header[7] == '_') {
			fd->flags |= FD_FLAGS_FILE_POINTSIZE_IS_4;
			if (sizeof(void *) != 4) {
				fd->flags |= FD_FLAGS_POINTSIZE_DIFFERS;
			}
		}
		else {
			if (sizeof(void *) != 8) {
				fd->flags |= FD_FLAGS_POINTSIZE_DIFFERS;
			}
		}

		/* is the file saved in a different endian
		 * than we need ?
		 */
		if (((header[8] == 'v') ? L_ENDIAN : B_ENDIAN) != ENDIAN_ORDER) {
			fd->flags |= FD_FLAGS_SWITCH_ENDIAN;
		}

		/* get the version number */
		memcpy(num, header + 9, 3);
		num[3] = 0;
		fd->fileversion = atoi(num);
	}
}

/**
 * \return Success if the file is read correctly, else set \a r_error_message.
 */
static bool read_file_dna(FileData *fd, const char **r_error_message)
{
	BHead *bhead;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (bhead->code == DNA1) {
			const bool do_endian_swap = (fd->flags & FD_FLAGS_SWITCH_ENDIAN) != 0;

			fd->filesdna = DNA_sdna_from_data(&bhead[1], bhead->len, do_endian_swap, true, r_error_message);
			if (fd->filesdna) {
				fd->compflags = DNA_struct_get_compareflags(fd->filesdna, fd->memsdna);
				/* used to retrieve ID names from (bhead+1) */
				fd->id_name_offs = DNA_elem_offset(fd->filesdna, "ID", "char", "name[]");

				return true;
			}
			else {
				return false;
			}

		}
		else if (bhead->code == ENDB)
			break;
	}

	*r_error_message = "Missing DNA block";
	return false;
}

static int *read_file_thumbnail(FileData *fd)
{
	BHead *bhead;
	int *blend_thumb = NULL;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (bhead->code == TEST) {
			const bool do_endian_swap = (fd->flags & FD_FLAGS_SWITCH_ENDIAN) != 0;
			int *data = (int *)(bhead + 1);

			if (bhead->len < (2 * sizeof(int))) {
				break;
			}

			if (do_endian_swap) {
				BLI_endian_switch_int32(&data[0]);
				BLI_endian_switch_int32(&data[1]);
			}

			int width = data[0];
			int height = data[1];

			if (!BLEN_THUMB_SAFE_MEMSIZE(width, height)) {
				break;
			}
			if (bhead->len < BLEN_THUMB_MEMSIZE_FILE(width, height)) {
				break;
			}

			blend_thumb = data;
			break;
		}
		else if (bhead->code != REND) {
			/* Thumbnail is stored in TEST immediately after first REND... */
			break;
		}
	}

	return blend_thumb;
}

static int fd_read_from_file(FileData *filedata, void *buffer, uint size)
{
	int readsize = read(filedata->filedes, buffer, size);

	if (readsize < 0) {
		readsize = EOF;
	}
	else {
		filedata->seek += readsize;
	}

	return readsize;
}

static int fd_read_gzip_from_file(FileData *filedata, void *buffer, uint size)
{
	int readsize = gzread(filedata->gzfiledes, buffer, size);

	if (readsize < 0) {
		readsize = EOF;
	}
	else {
		filedata->seek += readsize;
	}

	return (readsize);
}

static int fd_read_from_memory(FileData *filedata, void *buffer, uint size)
{
	/* don't read more bytes then there are available in the buffer */
	int readsize = (int)MIN2(size, (uint)(filedata->buffersize - filedata->seek));

	memcpy(buffer, filedata->buffer + filedata->seek, readsize);
	filedata->seek += readsize;

	return (readsize);
}

static int fd_read_from_memfile(FileData *filedata, void *buffer, uint size)
{
	static uint seek = (1 << 30); /* the current position */
	static uint offset = 0;     /* size of previous chunks */
	static MemFileChunk *chunk = NULL;
	uint chunkoffset, readsize, totread;

	if (size == 0) return 0;

	if (seek != (uint)filedata->seek) {
		chunk = filedata->memfile->chunks.first;
		seek = 0;

		while (chunk) {
			if (seek + chunk->size > (uint)filedata->seek) {
				break;
			}
			seek += chunk->size;
			chunk = chunk->next;
		}
		offset = seek;
		seek = filedata->seek;
	}

	if (chunk) {
		totread = 0;

		do {
			/* first check if it's on the end if current chunk */
			if (seek - offset == chunk->size) {
				offset += chunk->size;
				chunk = chunk->next;
			}

			/* debug, should never happen */
			if (chunk == NULL) {
				printf("illegal read, chunk zero\n");
				return 0;
			}

			chunkoffset = seek - offset;
			readsize = size - totread;

			/* data can be spread over multiple chunks, so clamp size
			 * to within this chunk, and then it will read further in
			 * the next chunk */
			if (chunkoffset + readsize > chunk->size)
				readsize = chunk->size - chunkoffset;

			memcpy(POINTER_OFFSET(buffer, totread), chunk->buf + chunkoffset, readsize);
			totread += readsize;
			filedata->seek += readsize;
			seek += readsize;
		} while (totread < size);

		return totread;
	}

	return 0;
}

static FileData *filedata_new(void)
{
	FileData *fd = MEM_callocN(sizeof(FileData), "FileData");

	fd->filedes = -1;
	fd->gzfiledes = NULL;

	fd->memsdna = DNA_sdna_current_get();

	fd->datamap = oldnewmap_new();
	fd->globmap = oldnewmap_new();
	fd->libmap = oldnewmap_new();

	return fd;
}

static FileData *blo_decode_and_check(FileData *fd, ReportList *reports)
{
	decode_blender_header(fd);

	if (fd->flags & FD_FLAGS_FILE_OK) {
		const char *error_message = NULL;
		if (read_file_dna(fd, &error_message) == false) {
			BKE_reportf(reports, RPT_ERROR,
			            "Failed to read blend file '%s': %s",
			            fd->relabase, error_message);
			blo_freefiledata(fd);
			fd = NULL;
		}
	}
	else {
		BKE_reportf(reports, RPT_ERROR, "Failed to read blend file '%s', not a blend file", fd->relabase);
		blo_freefiledata(fd);
		fd = NULL;
	}

	return fd;
}

/* cannot be called with relative paths anymore! */
/* on each new library added, it now checks for the current FileData and expands relativeness */
FileData *blo_openblenderfile(const char *filepath, ReportList *reports)
{
	gzFile gzfile;
	errno = 0;
	gzfile = BLI_gzopen(filepath, "rb");

	if (gzfile == (gzFile)Z_NULL) {
		BKE_reportf(reports, RPT_WARNING, "Unable to open '%s': %s",
		            filepath, errno ? strerror(errno) : TIP_("unknown error reading file"));
		return NULL;
	}
	else {
		FileData *fd = filedata_new();
		fd->gzfiledes = gzfile;
		fd->read = fd_read_gzip_from_file;

		/* needed for library_append and read_libraries */
		BLI_strncpy(fd->relabase, filepath, sizeof(fd->relabase));

		return blo_decode_and_check(fd, reports);
	}
}

/**
 * Same as blo_openblenderfile(), but does not reads DNA data, only header. Use it for light access
 * (e.g. thumbnail reading).
 */
static FileData *blo_openblenderfile_minimal(const char *filepath)
{
	gzFile gzfile;
	errno = 0;
	gzfile = BLI_gzopen(filepath, "rb");

	if (gzfile != (gzFile)Z_NULL) {
		FileData *fd = filedata_new();
		fd->gzfiledes = gzfile;
		fd->read = fd_read_gzip_from_file;

		decode_blender_header(fd);

		if (fd->flags & FD_FLAGS_FILE_OK) {
			return fd;
		}

		blo_freefiledata(fd);
	}

	return NULL;
}

static int fd_read_gzip_from_memory(FileData *filedata, void *buffer, uint size)
{
	int err;

	filedata->strm.next_out = (Bytef *)buffer;
	filedata->strm.avail_out = size;

	// Inflate another chunk.
	err = inflate(&filedata->strm, Z_SYNC_FLUSH);

	if (err == Z_STREAM_END) {
		return 0;
	}
	else if (err != Z_OK) {
		printf("fd_read_gzip_from_memory: zlib error\n");
		return 0;
	}

	filedata->seek += size;

	return (size);
}

static int fd_read_gzip_from_memory_init(FileData *fd)
{

	fd->strm.next_in = (Bytef *)fd->buffer;
	fd->strm.avail_in = fd->buffersize;
	fd->strm.total_out = 0;
	fd->strm.zalloc = Z_NULL;
	fd->strm.zfree = Z_NULL;

	if (inflateInit2(&fd->strm, (16 + MAX_WBITS)) != Z_OK)
		return 0;

	fd->read = fd_read_gzip_from_memory;

	return 1;
}

FileData *blo_openblendermemory(const void *mem, int memsize, ReportList *reports)
{
	if (!mem || memsize < SIZEOFBLENDERHEADER) {
		BKE_report(reports, RPT_WARNING, (mem) ? TIP_("Unable to read") : TIP_("Unable to open"));
		return NULL;
	}
	else {
		FileData *fd = filedata_new();
		const char *cp = mem;

		fd->buffer = mem;
		fd->buffersize = memsize;

		/* test if gzip */
		if (cp[0] == 0x1f && cp[1] == 0x8b) {
			if (0 == fd_read_gzip_from_memory_init(fd)) {
				blo_freefiledata(fd);
				return NULL;
			}
		}
		else
			fd->read = fd_read_from_memory;

		fd->flags |= FD_FLAGS_NOT_MY_BUFFER;

		return blo_decode_and_check(fd, reports);
	}
}

FileData *blo_openblendermemfile(MemFile *memfile, ReportList *reports)
{
	if (!memfile) {
		BKE_report(reports, RPT_WARNING, "Unable to open blend <memory>");
		return NULL;
	}
	else {
		FileData *fd = filedata_new();
		fd->memfile = memfile;

		fd->read = fd_read_from_memfile;
		fd->flags |= FD_FLAGS_NOT_MY_BUFFER;

		return blo_decode_and_check(fd, reports);
	}
}


void blo_freefiledata(FileData *fd)
{
	if (fd) {
		if (fd->filedes != -1) {
			close(fd->filedes);
		}

		if (fd->gzfiledes != NULL) {
			gzclose(fd->gzfiledes);
		}

		if (fd->strm.next_in) {
			if (inflateEnd(&fd->strm) != Z_OK) {
				printf("close gzip stream error\n");
			}
		}

		if (fd->buffer && !(fd->flags & FD_FLAGS_NOT_MY_BUFFER)) {
			MEM_freeN((void *)fd->buffer);
			fd->buffer = NULL;
		}

		// Free all BHeadN data blocks
		BLI_freelistN(&fd->listbase);

		if (fd->filesdna)
			DNA_sdna_free(fd->filesdna);
		if (fd->compflags)
			MEM_freeN((void *)fd->compflags);

		if (fd->datamap)
			oldnewmap_free(fd->datamap);
		if (fd->globmap)
			oldnewmap_free(fd->globmap);
		if (fd->imamap)
			oldnewmap_free(fd->imamap);
		if (fd->packedmap)
			oldnewmap_free(fd->packedmap);
		if (fd->libmap && !(fd->flags & FD_FLAGS_NOT_MY_LIBMAP))
			oldnewmap_free(fd->libmap);
		if (fd->bheadmap)
			MEM_freeN(fd->bheadmap);

#ifdef USE_GHASH_BHEAD
		if (fd->bhead_idname_hash) {
			BLI_ghash_free(fd->bhead_idname_hash, NULL, NULL);
		}
#endif

		MEM_freeN(fd);
	}
}

/* ************ DIV ****************** */

/**
 * Check whether given path ends with a blend file compatible extension (.blend, .ble or .blend.gz).
 *
 * \param str: The path to check.
 * \return true is this path ends with a blender file extension.
 */
bool BLO_has_bfile_extension(const char *str)
{
	const char *ext_test[4] = {".blend", ".ble", ".blend.gz", NULL};
	return BLI_path_extension_check_array(str, ext_test);
}

/**
 * Try to explode given path into its 'library components' (i.e. a .blend file, id type/group, and datablock itself).
 *
 * \param path: the full path to explode.
 * \param r_dir: the string that'll contain path up to blend file itself ('library' path).
 *              WARNING! Must be FILE_MAX_LIBEXTRA long (it also stores group and name strings)!
 * \param r_group: the string that'll contain 'group' part of the path, if any. May be NULL.
 * \param r_name: the string that'll contain data's name part of the path, if any. May be NULL.
 * \return true if path contains a blend file.
 */
bool BLO_library_path_explode(const char *path, char *r_dir, char **r_group, char **r_name)
{
	/* We might get some data names with slashes, so we have to go up in path until we find blend file itself,
	 * then we now next path item is group, and everything else is data name. */
	char *slash = NULL, *prev_slash = NULL, c = '\0';

	r_dir[0] = '\0';
	if (r_group) {
		*r_group = NULL;
	}
	if (r_name) {
		*r_name = NULL;
	}

	/* if path leads to an existing directory, we can be sure we're not (in) a library */
	if (BLI_is_dir(path)) {
		return false;
	}

	strcpy(r_dir, path);

	while ((slash = (char *)BLI_last_slash(r_dir))) {
		char tc = *slash;
		*slash = '\0';
		if (BLO_has_bfile_extension(r_dir) && BLI_is_file(r_dir)) {
			break;
		}

		if (prev_slash) {
			*prev_slash = c;
		}
		prev_slash = slash;
		c = tc;
	}

	if (!slash) {
		return false;
	}

	if (slash[1] != '\0') {
		BLI_assert(strlen(slash + 1) < BLO_GROUP_MAX);
		if (r_group) {
			*r_group = slash + 1;
		}
	}

	if (prev_slash && (prev_slash[1] != '\0')) {
		BLI_assert(strlen(prev_slash + 1) < MAX_ID_NAME - 2);
		if (r_name) {
			*r_name = prev_slash + 1;
		}
	}

	return true;
}

/**
 * Does a very light reading of given .blend file to extract its stored thumbnail.
 *
 * \param filepath: The path of the file to extract thumbnail from.
 * \return The raw thumbnail
 *         (MEM-allocated, as stored in file, use BKE_main_thumbnail_to_imbuf() to convert it to ImBuf image).
 */
BlendThumbnail *BLO_thumbnail_from_file(const char *filepath)
{
	FileData *fd;
	BlendThumbnail *data = NULL;
	int *fd_data;

	fd = blo_openblenderfile_minimal(filepath);
	fd_data = fd ? read_file_thumbnail(fd) : NULL;

	if (fd_data) {
		int width = fd_data[0];
		int height = fd_data[1];

		/* Protect against buffer overflow vulnerability. */
		if (BLEN_THUMB_SAFE_MEMSIZE(width, height)) {
			const size_t sz = BLEN_THUMB_MEMSIZE(width, height);
			data = MEM_mallocN(sz, __func__);

			if (data) {
				BLI_assert((sz - sizeof(*data)) == (BLEN_THUMB_MEMSIZE_FILE(width, height) - (sizeof(*fd_data) * 2)));
				data->width = width;
				data->height = height;
				memcpy(data->rect, &fd_data[2], sz - sizeof(*data));
			}
		}
	}

	blo_freefiledata(fd);

	return data;
}

/* ************** OLD POINTERS ******************* */

static void *newdataadr(FileData *fd, const void *adr)      /* only direct databocks */
{
	return oldnewmap_lookup_and_inc(fd->datamap, adr, true);
}

static void *newdataadr_no_us(FileData *fd, const void *adr)        /* only direct databocks */
{
	return oldnewmap_lookup_and_inc(fd->datamap, adr, false);
}

static void *newimaadr(FileData *fd, const void *adr)           /* used to restore image data after undo */
{
	if (fd->imamap && adr)
		return oldnewmap_lookup_and_inc(fd->imamap, adr, true);
	return NULL;
}


static void *newpackedadr(FileData *fd, const void *adr)      /* used to restore packed data after undo */
{
	if (fd->packedmap && adr)
		return oldnewmap_lookup_and_inc(fd->packedmap, adr, true);

	return oldnewmap_lookup_and_inc(fd->datamap, adr, true);
}


static void *newlibadr(FileData *fd, const void *lib, const void *adr)      /* only lib data */
{
	return oldnewmap_liblookup(fd->libmap, adr, lib);
}

void *blo_do_versions_newlibadr(FileData *fd, const void *lib, const void *adr)     /* only lib data */
{
	return newlibadr(fd, lib, adr);
}

static void *newlibadr_us(FileData *fd, const void *lib, const void *adr)   /* increases user number */
{
	ID *id = newlibadr(fd, lib, adr);

	id_us_plus_no_lib(id);

	return id;
}

void *blo_do_versions_newlibadr_us(FileData *fd, const void *lib, const void *adr)  /* increases user number */
{
	return newlibadr_us(fd, lib, adr);
}

static void *newlibadr_real_us(FileData *fd, const void *lib, const void *adr)  /* ensures real user */
{
	ID *id = newlibadr(fd, lib, adr);

	id_us_ensure_real(id);

	return id;
}

static void change_idid_adr_fd(FileData *fd, const void *old, void *new)
{
	int i;

	/* use a binary search if we have a sorted libmap, for now it's not needed. */
	BLI_assert(fd->libmap->sorted == false);

	for (i = 0; i < fd->libmap->nentries; i++) {
		OldNew *entry = &fd->libmap->entries[i];

		if (old == entry->newp && entry->nr == ID_ID) {
			entry->newp = new;
			if (new) entry->nr = GS( ((ID *)new)->name);
		}
	}
}

static void change_idid_adr(ListBase *mainlist, FileData *basefd, void *old, void *new)
{
	Main *mainptr;

	for (mainptr = mainlist->first; mainptr; mainptr = mainptr->next) {
		FileData *fd;

		if (mainptr->curlib)
			fd = mainptr->curlib->filedata;
		else
			fd = basefd;

		if (fd) {
			change_idid_adr_fd(fd, old, new);
		}
	}
}

/* lib linked proxy objects point to our local data, we need
 * to clear that pointer before reading the undo memfile since
 * the object might be removed, it is set again in reading
 * if the local object still exists */
void blo_clear_proxy_pointers_from_lib(Main *oldmain)
{
	Object *ob = oldmain->object.first;

	for (; ob; ob = ob->id.next) {
		if (ob->id.lib)
			ob->proxy_from = NULL;
	}
}

void blo_make_image_pointer_map(FileData *fd, Main *oldmain)
{
	Image *ima = oldmain->image.first;
	int a;

	fd->imamap = oldnewmap_new();

	for (; ima; ima = ima->id.next) {
		if (ima->cache)
			oldnewmap_insert(fd->imamap, ima->cache, ima->cache, 0);
		for (a = 0; a < TEXTARGET_COUNT; a++)
			if (ima->gputexture[a])
				oldnewmap_insert(fd->imamap, ima->gputexture[a], ima->gputexture[a], 0);
		if (ima->rr)
			oldnewmap_insert(fd->imamap, ima->rr, ima->rr, 0);
		for (a = 0; a < IMA_MAX_RENDER_SLOT; a++)
			if (ima->renders[a])
				oldnewmap_insert(fd->imamap, ima->renders[a], ima->renders[a], 0);
	}
}

/* set old main image ibufs to zero if it has been restored */
/* this works because freeing old main only happens after this call */
void blo_end_image_pointer_map(FileData *fd, Main *oldmain)
{
	OldNew *entry = fd->imamap->entries;
	Image *ima = oldmain->image.first;
	int i;

	/* used entries were restored, so we put them to zero */
	for (i = 0; i < fd->imamap->nentries; i++, entry++) {
		if (entry->nr > 0)
			entry->newp = NULL;
	}

	for (; ima; ima = ima->id.next) {
		ima->cache = newimaadr(fd, ima->cache);
		if (ima->cache == NULL) {
			ima->tpageflag &= ~IMA_GLBIND_IS_DATA;
			for (i = 0; i < TEXTARGET_COUNT; i++) {
				ima->bindcode[i] = 0;
				ima->gputexture[i] = NULL;
			}
			ima->rr = NULL;
		}
		for (i = 0; i < IMA_MAX_RENDER_SLOT; i++)
			ima->renders[i] = newimaadr(fd, ima->renders[i]);

		for (i = 0; i < TEXTARGET_COUNT; i++)
			ima->gputexture[i] = newimaadr(fd, ima->gputexture[i]);
		ima->rr = newimaadr(fd, ima->rr);
	}
}

/* XXX disabled this feature - packed files also belong in temp saves and quit.blend, to make restore work */

static void insert_packedmap(FileData *fd, PackedFile *pf)
{
	oldnewmap_insert(fd->packedmap, pf, pf, 0);
	oldnewmap_insert(fd->packedmap, pf->data, pf->data, 0);
}

void blo_make_packed_pointer_map(FileData *fd, Main *oldmain)
{
	Image *ima;
	VFont *vfont;
	Library *lib;

	fd->packedmap = oldnewmap_new();

	for (ima = oldmain->image.first; ima; ima = ima->id.next) {
		ImagePackedFile *imapf;

		if (ima->packedfile)
			insert_packedmap(fd, ima->packedfile);

		for (imapf = ima->packedfiles.first; imapf; imapf = imapf->next)
			if (imapf->packedfile)
				insert_packedmap(fd, imapf->packedfile);
	}

	for (vfont = oldmain->vfont.first; vfont; vfont = vfont->id.next)
		if (vfont->packedfile)
			insert_packedmap(fd, vfont->packedfile);

	for (lib = oldmain->library.first; lib; lib = lib->id.next)
		if (lib->packedfile)
			insert_packedmap(fd, lib->packedfile);

}

/* set old main packed data to zero if it has been restored */
/* this works because freeing old main only happens after this call */
void blo_end_packed_pointer_map(FileData *fd, Main *oldmain)
{
	Image *ima;
	VFont *vfont;
	Library *lib;
	OldNew *entry = fd->packedmap->entries;
	int i;

	/* used entries were restored, so we put them to zero */
	for (i = 0; i < fd->packedmap->nentries; i++, entry++) {
		if (entry->nr > 0)
			entry->newp = NULL;
	}

	for (ima = oldmain->image.first; ima; ima = ima->id.next) {
		ImagePackedFile *imapf;

		ima->packedfile = newpackedadr(fd, ima->packedfile);

		for (imapf = ima->packedfiles.first; imapf; imapf = imapf->next)
			imapf->packedfile = newpackedadr(fd, imapf->packedfile);
	}

	for (vfont = oldmain->vfont.first; vfont; vfont = vfont->id.next)
		vfont->packedfile = newpackedadr(fd, vfont->packedfile);

	for (lib = oldmain->library.first; lib; lib = lib->id.next)
		lib->packedfile = newpackedadr(fd, lib->packedfile);
}


/* undo file support: add all library pointers in lookup */
void blo_add_library_pointer_map(ListBase *old_mainlist, FileData *fd)
{
	Main *ptr = old_mainlist->first;
	ListBase *lbarray[MAX_LIBARRAY];

	for (ptr = ptr->next; ptr; ptr = ptr->next) {
		int i = set_listbasepointers(ptr, lbarray);
		while (i--) {
			ID *id;
			for (id = lbarray[i]->first; id; id = id->next)
				oldnewmap_insert(fd->libmap, id, id, GS(id->name));
		}
	}

	fd->old_mainlist = old_mainlist;
}


/* ********** END OLD POINTERS ****************** */
/* ********** READ FILE ****************** */

static void switch_endian_structs(const struct SDNA *filesdna, BHead *bhead)
{
	int blocksize, nblocks;
	char *data;

	data = (char *)(bhead + 1);
	blocksize = filesdna->typelens[filesdna->structs[bhead->SDNAnr][0]];

	nblocks = bhead->nr;
	while (nblocks--) {
		DNA_struct_switch_endian(filesdna, bhead->SDNAnr, data);

		data += blocksize;
	}
}

static void *read_struct(FileData *fd, BHead *bh, const char *blockname)
{
	void *temp = NULL;

	if (bh->len) {
		/* switch is based on file dna */
		if (bh->SDNAnr && (fd->flags & FD_FLAGS_SWITCH_ENDIAN))
			switch_endian_structs(fd->filesdna, bh);

		if (fd->compflags[bh->SDNAnr] != SDNA_CMP_REMOVED) {
			if (fd->compflags[bh->SDNAnr] == SDNA_CMP_NOT_EQUAL) {
				temp = DNA_struct_reconstruct(fd->memsdna, fd->filesdna, fd->compflags, bh->SDNAnr, bh->nr, (bh + 1));
			}
			else {
				/* SDNA_CMP_EQUAL */
				temp = MEM_mallocN(bh->len, blockname);
				memcpy(temp, (bh + 1), bh->len);
			}
		}
	}

	return temp;
}

typedef void (*link_list_cb)(FileData *fd, void *data);

static void link_list_ex(FileData *fd, ListBase *lb, link_list_cb callback)     /* only direct data */
{
	Link *ln, *prev;

	if (BLI_listbase_is_empty(lb)) return;

	lb->first = newdataadr(fd, lb->first);
	if (callback != NULL) {
		callback(fd, lb->first);
	}
	ln = lb->first;
	prev = NULL;
	while (ln) {
		ln->next = newdataadr(fd, ln->next);
		if (ln->next != NULL && callback != NULL) {
			callback(fd, ln->next);
		}
		ln->prev = prev;
		prev = ln;
		ln = ln->next;
	}
	lb->last = prev;
}

static void link_list(FileData *fd, ListBase *lb)       /* only direct data */
{
	link_list_ex(fd, lb, NULL);
}

static void test_pointer_array(FileData *fd, void **mat)
{
	int64_t *lpoin, *lmat;
	int *ipoin, *imat;
	size_t len;

	/* manually convert the pointer array in
	 * the old dna format to a pointer array in
	 * the new dna format.
	 */
	if (*mat) {
		len = MEM_allocN_len(*mat) / fd->filesdna->pointerlen;

		if (fd->filesdna->pointerlen == 8 && fd->memsdna->pointerlen == 4) {
			ipoin = imat = MEM_malloc_arrayN(len, 4, "newmatar");
			lpoin = *mat;

			while (len-- > 0) {
				if ((fd->flags & FD_FLAGS_SWITCH_ENDIAN))
					BLI_endian_switch_int64(lpoin);
				*ipoin = (int)((*lpoin) >> 3);
				ipoin++;
				lpoin++;
			}
			MEM_freeN(*mat);
			*mat = imat;
		}

		if (fd->filesdna->pointerlen == 4 && fd->memsdna->pointerlen == 8) {
			lpoin = lmat = MEM_malloc_arrayN(len, 8, "newmatar");
			ipoin = *mat;

			while (len-- > 0) {
				*lpoin = *ipoin;
				ipoin++;
				lpoin++;
			}
			MEM_freeN(*mat);
			*mat = lmat;
		}
	}
}

/* ************ READ ID Properties *************** */

static void IDP_DirectLinkProperty(IDProperty *prop, int switch_endian, FileData *fd);
static void IDP_LibLinkProperty(IDProperty *prop, FileData *fd);

static void IDP_DirectLinkIDPArray(IDProperty *prop, int switch_endian, FileData *fd)
{
	IDProperty *array;
	int i;

	/* since we didn't save the extra buffer, set totallen to len */
	prop->totallen = prop->len;
	prop->data.pointer = newdataadr(fd, prop->data.pointer);

	array = (IDProperty *)prop->data.pointer;

	/* note!, idp-arrays didn't exist in 2.4x, so the pointer will be cleared
	 * theres not really anything we can do to correct this, at least don't crash */
	if (array == NULL) {
		prop->len = 0;
		prop->totallen = 0;
	}


	for (i = 0; i < prop->len; i++)
		IDP_DirectLinkProperty(&array[i], switch_endian, fd);
}

static void IDP_DirectLinkArray(IDProperty *prop, int switch_endian, FileData *fd)
{
	IDProperty **array;
	int i;

	/* since we didn't save the extra buffer, set totallen to len */
	prop->totallen = prop->len;
	prop->data.pointer = newdataadr(fd, prop->data.pointer);

	if (prop->subtype == IDP_GROUP) {
		test_pointer_array(fd, prop->data.pointer);
		array = prop->data.pointer;

		for (i = 0; i < prop->len; i++)
			IDP_DirectLinkProperty(array[i], switch_endian, fd);
	}
	else if (prop->subtype == IDP_DOUBLE) {
		if (switch_endian) {
			BLI_endian_switch_double_array(prop->data.pointer, prop->len);
		}
	}
	else {
		if (switch_endian) {
			/* also used for floats */
			BLI_endian_switch_int32_array(prop->data.pointer, prop->len);
		}
	}
}

static void IDP_DirectLinkString(IDProperty *prop, FileData *fd)
{
	/*since we didn't save the extra string buffer, set totallen to len.*/
	prop->totallen = prop->len;
	prop->data.pointer = newdataadr(fd, prop->data.pointer);
}

static void IDP_DirectLinkGroup(IDProperty *prop, int switch_endian, FileData *fd)
{
	ListBase *lb = &prop->data.group;
	IDProperty *loop;

	link_list(fd, lb);

	/*Link child id properties now*/
	for (loop = prop->data.group.first; loop; loop = loop->next) {
		IDP_DirectLinkProperty(loop, switch_endian, fd);
	}
}

static void IDP_DirectLinkProperty(IDProperty *prop, int switch_endian, FileData *fd)
{
	switch (prop->type) {
		case IDP_GROUP:
			IDP_DirectLinkGroup(prop, switch_endian, fd);
			break;
		case IDP_STRING:
			IDP_DirectLinkString(prop, fd);
			break;
		case IDP_ARRAY:
			IDP_DirectLinkArray(prop, switch_endian, fd);
			break;
		case IDP_IDPARRAY:
			IDP_DirectLinkIDPArray(prop, switch_endian, fd);
			break;
		case IDP_DOUBLE:
			/* erg, stupid doubles.  since I'm storing them
			 * in the same field as int val; val2 in the
			 * IDPropertyData struct, they have to deal with
			 * endianness specifically
			 *
			 * in theory, val and val2 would've already been swapped
			 * if switch_endian is true, so we have to first unswap
			 * them then reswap them as a single 64-bit entity.
			 */

			if (switch_endian) {
				BLI_endian_switch_int32(&prop->data.val);
				BLI_endian_switch_int32(&prop->data.val2);
				BLI_endian_switch_int64((int64_t *)&prop->data.val);
			}
			break;
		case IDP_INT:
		case IDP_FLOAT:
		case IDP_ID:
			break;  /* Nothing special to do here. */
		default:
			/* Unknown IDP type, nuke it (we cannot handle unknown types everywhere in code,
			 * IDP are way too polymorphic to do it safely. */
			printf("%s: found unknown IDProperty type %d, reset to Integer one !\n", __func__, prop->type);
			/* Note: we do not attempt to free unknown prop, we have no way to know how to do that! */
			prop->type = IDP_INT;
			prop->subtype = 0;
			IDP_Int(prop) = 0;
	}
}

#define IDP_DirectLinkGroup_OrFree(prop, switch_endian, fd) \
       _IDP_DirectLinkGroup_OrFree(prop, switch_endian, fd, __func__)

static void _IDP_DirectLinkGroup_OrFree(IDProperty **prop, int switch_endian, FileData *fd,
                                        const char *caller_func_id)
{
	if (*prop) {
		if ((*prop)->type == IDP_GROUP) {
			IDP_DirectLinkGroup(*prop, switch_endian, fd);
		}
		else {
			/* corrupt file! */
			printf("%s: found non group data, freeing type %d!\n",
			       caller_func_id, (*prop)->type);
			/* don't risk id, data's likely corrupt. */
			// IDP_FreeProperty(*prop);
			*prop = NULL;
		}
	}
}

static void IDP_LibLinkProperty(IDProperty *prop, FileData *fd)
{
	if (!prop)
		return;

	switch (prop->type) {
		case IDP_ID: /* PointerProperty */
		{
			void *newaddr = newlibadr_us(fd, NULL, IDP_Id(prop));
			if (IDP_Id(prop) && !newaddr && G.debug) {
				printf("Error while loading \"%s\". Data not found in file!\n", prop->name);
			}
			prop->data.pointer = newaddr;
			break;
		}
		case IDP_IDPARRAY: /* CollectionProperty */
		{
			IDProperty *idp_array = IDP_IDPArray(prop);
			for (int i = 0; i < prop->len; i++) {
				IDP_LibLinkProperty(&(idp_array[i]), fd);
			}
			break;
		}
		case IDP_GROUP: /* PointerProperty */
		{
			for (IDProperty *loop = prop->data.group.first; loop; loop = loop->next) {
				IDP_LibLinkProperty(loop, fd);
			}
			break;
		}
		default:
			break;  /* Nothing to do for other IDProps. */
	}
}

/* ************ READ IMAGE PREVIEW *************** */

static PreviewImage *direct_link_preview_image(FileData *fd, PreviewImage *old_prv)
{
	PreviewImage *prv = newdataadr(fd, old_prv);

	if (prv) {
		int i;
		for (i = 0; i < NUM_ICON_SIZES; ++i) {
			if (prv->rect[i]) {
				prv->rect[i] = newdataadr(fd, prv->rect[i]);
			}
			prv->gputexture[i] = NULL;
		}
		prv->icon_id = 0;
		prv->tag = 0;
	}

	return prv;
}

/* ************ READ ID *************** */

static void direct_link_id(FileData *fd, ID *id)
{
	/*link direct data of ID properties*/
	if (id->properties) {
		id->properties = newdataadr(fd, id->properties);
		/* this case means the data was written incorrectly, it should not happen */
		IDP_DirectLinkGroup_OrFree(&id->properties, (fd->flags & FD_FLAGS_SWITCH_ENDIAN), fd);
	}
	id->py_instance = NULL;

	/* That way datablock reading not going through main read_libblock() function are still in a clear tag state.
	 * (glowering at certain nodetree fake datablock here...). */
	id->tag = 0;
}

/* ************ READ CurveMapping *************** */

/* cuma itself has been read! */
static void direct_link_curvemapping(FileData *fd, CurveMapping *cumap)
{
	int a;

	/* flag seems to be able to hang? Maybe old files... not bad to clear anyway */
	cumap->flag &= ~CUMA_PREMULLED;

	for (a = 0; a < CM_TOT; a++) {
		cumap->cm[a].curve = newdataadr(fd, cumap->cm[a].curve);
		cumap->cm[a].table = NULL;
		cumap->cm[a].premultable = NULL;
	}
}

/* ************ READ PACKEDFILE *************** */

static PackedFile *direct_link_packedfile(FileData *fd, PackedFile *oldpf)
{
	PackedFile *pf = newpackedadr(fd, oldpf);

	if (pf) {
		pf->data = newpackedadr(fd, pf->data);
	}

	return pf;
}

/* Data Linking ----------------------------- */

/* ------- */
/* ************ READ CACHEFILES *************** */

static void lib_link_cachefiles(FileData *fd, Main *bmain)
{
	/* only link ID pointers */
	for (CacheFile *cache_file = bmain->cachefiles.first; cache_file; cache_file = cache_file->id.next) {
		if (cache_file->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(cache_file->id.properties, fd);

			cache_file->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}


/* ************ READ CAMERA ***************** */

static void lib_link_camera(FileData *fd, Main *main)
{
	for (Camera *ca = main->camera.first; ca; ca = ca->id.next) {
		if (ca->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(ca->id.properties, fd);

			ca->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_camera(FileData *fd, Camera *ca)
{
}


/* ************ READ LAMP ***************** */

static void lib_link_lamp(FileData *fd, Main *main)
{
	for (Lamp *la = main->lamp.first; la; la = la->id.next) {
		if (la->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(la->id.properties, fd);

			for (int a = 0; a < MAX_MTEX; a++) {
				MTex *mtex = la->mtex[a];
				if (mtex) {
					mtex->tex = newlibadr_us(fd, la->id.lib, mtex->tex);
					mtex->object = newlibadr(fd, la->id.lib, mtex->object);
				}
			}

			la->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_lamp(FileData *fd, Lamp *la)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		la->mtex[a] = newdataadr(fd, la->mtex[a]);
	}

	la->curfalloff = newdataadr(fd, la->curfalloff);
	if (la->curfalloff)
		direct_link_curvemapping(fd, la->curfalloff);

	la->preview = direct_link_preview_image(fd, la->preview);
}

/* ************ READ WORLD ***************** */

static void lib_link_world(FileData *fd, Main *main)
{
	for (World *wrld = main->world.first; wrld; wrld = wrld->id.next) {
		if (wrld->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(wrld->id.properties, fd);

			for (int a = 0; a < MAX_MTEX; a++) {
				MTex *mtex = wrld->mtex[a];
				if (mtex) {
					mtex->tex = newlibadr_us(fd, wrld->id.lib, mtex->tex);
					mtex->object = newlibadr(fd, wrld->id.lib, mtex->object);
				}
			}

			wrld->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_world(FileData *fd, World *wrld)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		wrld->mtex[a] = newdataadr(fd, wrld->mtex[a]);
	}

	wrld->preview = direct_link_preview_image(fd, wrld->preview);
	BLI_listbase_clear(&wrld->gpumaterial);
}


/* ************ READ VFONT ***************** */

static void lib_link_vfont(FileData *fd, Main *main)
{
	for (VFont *vf = main->vfont.first; vf; vf = vf->id.next) {
		if (vf->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(vf->id.properties, fd);

			vf->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_vfont(FileData *fd, VFont *vf)
{
	vf->data = NULL;
	vf->temp_pf = NULL;
	vf->packedfile = direct_link_packedfile(fd, vf->packedfile);
}

/* ************ READ TEXT ****************** */

static void lib_link_text(FileData *fd, Main *main)
{
	for (Text *text = main->text.first; text; text = text->id.next) {
		if (text->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(text->id.properties, fd);

			text->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_text(FileData *fd, Text *text)
{
	TextLine *ln;

	text->name = newdataadr(fd, text->name);

	text->compiled = NULL;

#if 0
	if (text->flags & TXT_ISEXT) {
		BKE_text_reload(text);
	}
	/* else { */
#endif

	link_list(fd, &text->lines);

	text->curl = newdataadr(fd, text->curl);
	text->sell = newdataadr(fd, text->sell);

	for (ln = text->lines.first; ln; ln = ln->next) {
		ln->line = newdataadr(fd, ln->line);
		ln->format = NULL;

		if (ln->len != (int)strlen(ln->line)) {
			printf("Error loading text, line lengths differ\n");
			ln->len = strlen(ln->line);
		}
	}

	text->flags = (text->flags) & ~TXT_ISEXT;

	id_us_ensure_real(&text->id);
}

/* ************ READ IMAGE ***************** */

static void lib_link_image(FileData *fd, Main *main)
{
	for (Image *ima = main->image.first; ima; ima = ima->id.next) {
		if (ima->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(ima->id.properties, fd);

			ima->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_image(FileData *fd, Image *ima)
{
	ImagePackedFile *imapf;

	/* for undo system, pointers could be restored */
	if (fd->imamap)
		ima->cache = newimaadr(fd, ima->cache);
	else
		ima->cache = NULL;

	/* if not restored, we keep the binded opengl index */
	if (!ima->cache) {
		ima->tpageflag &= ~IMA_GLBIND_IS_DATA;
		for (int i = 0; i < TEXTARGET_COUNT; i++) {
			ima->bindcode[i] = 0;
			ima->gputexture[i] = NULL;
		}
		ima->rr = NULL;
	}

	ima->repbind = NULL;

	/* undo system, try to restore render buffers */
	if (fd->imamap) {
		int a;

		for (a = 0; a < IMA_MAX_RENDER_SLOT; a++)
			ima->renders[a] = newimaadr(fd, ima->renders[a]);
	}
	else {
		memset(ima->renders, 0, sizeof(ima->renders));
		ima->last_render_slot = ima->render_slot;
	}

	link_list(fd, &(ima->views));
	link_list(fd, &(ima->packedfiles));

	if (ima->packedfiles.first) {
		for (imapf = ima->packedfiles.first; imapf; imapf = imapf->next) {
			imapf->packedfile = direct_link_packedfile(fd, imapf->packedfile);
		}
		ima->packedfile = NULL;
	}
	else {
		ima->packedfile = direct_link_packedfile(fd, ima->packedfile);
	}

	BLI_listbase_clear(&ima->anims);
	ima->preview = direct_link_preview_image(fd, ima->preview);
	ima->ok = 1;
}


/* ************ READ CURVE ***************** */

static void lib_link_curve(FileData *fd, Main *main)
{
	for (Curve *cu = main->curve.first; cu; cu = cu->id.next) {
		if (cu->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(cu->id.properties, fd);

			for (int a = 0; a < cu->totcol; a++) {
				cu->mat[a] = newlibadr_us(fd, cu->id.lib, cu->mat[a]);
			}

			cu->bevobj = newlibadr(fd, cu->id.lib, cu->bevobj);
			cu->taperobj = newlibadr(fd, cu->id.lib, cu->taperobj);
			cu->textoncurve = newlibadr(fd, cu->id.lib, cu->textoncurve);
			cu->vfont = newlibadr_us(fd, cu->id.lib, cu->vfont);
			cu->vfontb = newlibadr_us(fd, cu->id.lib, cu->vfontb);
			cu->vfonti = newlibadr_us(fd, cu->id.lib, cu->vfonti);
			cu->vfontbi = newlibadr_us(fd, cu->id.lib, cu->vfontbi);

			cu->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}


static void switch_endian_knots(Nurb *nu)
{
	if (nu->knotsu) {
		BLI_endian_switch_float_array(nu->knotsu, KNOTSU(nu));
	}
	if (nu->knotsv) {
		BLI_endian_switch_float_array(nu->knotsv, KNOTSV(nu));
	}
}

static void direct_link_curve(FileData *fd, Curve *cu)
{
	Nurb *nu;
	TextBox *tb;

	/* Protect against integer overflow vulnerability. */
	CLAMP(cu->len_wchar, 0, INT_MAX - 4);

	cu->mat = newdataadr(fd, cu->mat);
	test_pointer_array(fd, (void **)&cu->mat);
	cu->str = newdataadr(fd, cu->str);
	cu->strinfo = newdataadr(fd, cu->strinfo);
	cu->tb = newdataadr(fd, cu->tb);

	if (cu->vfont == NULL) {
		link_list(fd, &(cu->nurb));
	}
	else {
		cu->nurb.first = cu->nurb.last = NULL;

		tb = MEM_calloc_arrayN(MAXTEXTBOX, sizeof(TextBox), "TextBoxread");
		if (cu->tb) {
			memcpy(tb, cu->tb, cu->totbox * sizeof(TextBox));
			MEM_freeN(cu->tb);
			cu->tb = tb;
		}
		else {
			cu->totbox = 1;
			cu->actbox = 1;
			cu->tb = tb;
			cu->tb[0].w = cu->linewidth;
		}
		if (cu->wordspace == 0.0f) cu->wordspace = 1.0f;
	}

	cu->editnurb = NULL;
	cu->editfont = NULL;

	for (nu = cu->nurb.first; nu; nu = nu->next) {
		nu->bezt = newdataadr(fd, nu->bezt);
		nu->bp = newdataadr(fd, nu->bp);
		nu->knotsu = newdataadr(fd, nu->knotsu);
		nu->knotsv = newdataadr(fd, nu->knotsv);
		if (cu->vfont == NULL) nu->charidx = 0;

		if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
			switch_endian_knots(nu);
		}
	}
	cu->bb = NULL;
}

/* ************ READ TEX ***************** */

static void lib_link_texture(FileData *fd, Main *main)
{
	for (Tex *tex = main->tex.first; tex; tex = tex->id.next) {
		if (tex->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(tex->id.properties, fd);

			tex->ima = newlibadr_us(fd, tex->id.lib, tex->ima);
			if (tex->env)
				tex->env->object = newlibadr(fd, tex->id.lib, tex->env->object);

			if (tex->vd)
				tex->vd->object = newlibadr(fd, tex->id.lib, tex->vd->object);

			tex->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_texture(FileData *fd, Tex *tex)
{
	tex->coba = newdataadr(fd, tex->coba);
	tex->env = newdataadr(fd, tex->env);
	if (tex->env) {
		tex->env->ima = NULL;
		memset(tex->env->cube, 0, 6 * sizeof(void *));
		tex->env->ok = 0;
	}

	tex->vd = newdataadr(fd, tex->vd);
	if (tex->vd) {
		tex->vd->dataset = NULL;
		tex->vd->ok = 0;
	}
	else {
		if (tex->type == TEX_VOXELDATA)
			tex->vd = MEM_callocN(sizeof(VoxelData), "direct_link_texture VoxelData");
	}

	tex->preview = direct_link_preview_image(fd, tex->preview);

	tex->iuser.ok = 1;
}



/* ************ READ MATERIAL ***************** */

static void lib_link_material(FileData *fd, Main *main)
{
	for (Material *ma = main->mat.first; ma; ma = ma->id.next) {
		if (ma->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(ma->id.properties, fd);

			ma->group = newlibadr_us(fd, ma->id.lib, ma->group);

			for (int a = 0; a < MAX_MTEX; a++) {
				MTex *mtex = ma->mtex[a];
				if (mtex) {
					mtex->tex = newlibadr_us(fd, ma->id.lib, mtex->tex);
					mtex->object = newlibadr(fd, ma->id.lib, mtex->object);
				}
			}

			ma->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_material(FileData *fd, Material *ma)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		ma->mtex[a] = newdataadr(fd, ma->mtex[a]);
	}
	ma->texpaintslot = NULL;

	ma->ramp_col = newdataadr(fd, ma->ramp_col);
	ma->ramp_spec = newdataadr(fd, ma->ramp_spec);

	ma->preview = direct_link_preview_image(fd, ma->preview);
	BLI_listbase_clear(&ma->gpumaterial);
}

/* ************ READ MESH ***************** */

static void lib_link_mtface(FileData *fd, Mesh *me, MTFace *mtface, int totface)
{
	MTFace *tf = mtface;
	int i;

	/* Add pseudo-references (not fake users!) to images used by texface. A
	 * little bogus; it would be better if each mesh consistently added one ref
	 * to each image it used. - z0r */
	for (i = 0; i < totface; i++, tf++) {
		tf->tpage = newlibadr_real_us(fd, me->id.lib, tf->tpage);
	}
}

static void lib_link_customdata_mtface(FileData *fd, Mesh *me, CustomData *fdata, int totface)
{
	int i;
	for (i = 0; i < fdata->totlayer; i++) {
		CustomDataLayer *layer = &fdata->layers[i];

		if (layer->type == CD_MTFACE)
			lib_link_mtface(fd, me, layer->data, totface);
	}

}

static void lib_link_customdata_mtpoly(FileData *fd, Mesh *me, CustomData *pdata, int totface)
{
	int i;

	for (i = 0; i < pdata->totlayer; i++) {
		CustomDataLayer *layer = &pdata->layers[i];

		if (layer->type == CD_MTEXPOLY) {
			MTexPoly *tf = layer->data;
			int j;

			for (j = 0; j < totface; j++, tf++) {
				tf->tpage = newlibadr_real_us(fd, me->id.lib, tf->tpage);
			}
		}
	}
}

static void lib_link_mesh(FileData *fd, Main *main)
{
	Mesh *me;

	for (me = main->mesh.first; me; me = me->id.next) {
		if (me->id.tag & LIB_TAG_NEED_LINK) {
			int i;

			/* Link ID Properties -- and copy this comment EXACTLY for easy finding
			 * of library blocks that implement this.*/
			IDP_LibLinkProperty(me->id.properties, fd);

			/* this check added for python created meshes */
			if (me->mat) {
				for (i = 0; i < me->totcol; i++) {
					me->mat[i] = newlibadr_us(fd, me->id.lib, me->mat[i]);
				}
			}
			else {
				me->totcol = 0;
			}

			me->texcomesh = newlibadr_us(fd, me->id.lib, me->texcomesh);

			lib_link_customdata_mtface(fd, me, &me->fdata, me->totface);
			lib_link_customdata_mtpoly(fd, me, &me->pdata, me->totpoly);
		}
	}

	/* convert texface options to material */
	convert_tface_mt(fd, main);

	for (me = main->mesh.first; me; me = me->id.next) {
		if (me->id.tag & LIB_TAG_NEED_LINK) {
			/*check if we need to convert mfaces to mpolys*/
			if (me->totface && !me->totpoly) {
				/* temporarily switch main so that reading from
				 * external CustomData works */
				Main *gmain = G_MAIN;
				G_MAIN = main;

				BKE_mesh_do_versions_convert_mfaces_to_mpolys(me);

				G_MAIN = gmain;
			}

			/*
			 * Re-tessellate, even if the polys were just created from tessfaces, this
			 * is important because it:
			 * - fill the CD_ORIGINDEX layer
			 * - gives consistency of tessface between loading from a file and
			 *   converting an edited BMesh back into a mesh (i.e. it replaces
			 *   quad tessfaces in a loaded mesh immediately, instead of lazily
			 *   waiting until edit mode has been entered/exited, making it easier
			 *   to recognize problems that would otherwise only show up after edits).
			 */
#ifdef USE_TESSFACE_DEFAULT
			BKE_mesh_tessface_calc(me);
#else
			BKE_mesh_tessface_clear(me);
#endif

			me->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

static void direct_link_dverts(FileData *fd, int count, MDeformVert *mdverts)
{
	int i;

	if (mdverts == NULL) {
		return;
	}

	for (i = count; i > 0; i--, mdverts++) {
		/*convert to vgroup allocation system*/
		MDeformWeight *dw;
		if (mdverts->dw && (dw = newdataadr(fd, mdverts->dw))) {
			const ssize_t dw_len = mdverts->totweight * sizeof(MDeformWeight);
			void *dw_tmp = MEM_mallocN(dw_len, "direct_link_dverts");
			memcpy(dw_tmp, dw, dw_len);
			mdverts->dw = dw_tmp;
			MEM_freeN(dw);
		}
		else {
			mdverts->dw = NULL;
			mdverts->totweight = 0;
		}
	}
}

static void direct_link_mdisps(FileData *fd, int count, MDisps *mdisps, int external)
{
	if (mdisps) {
		int i;

		for (i = 0; i < count; ++i) {
			mdisps[i].disps = newdataadr(fd, mdisps[i].disps);
			mdisps[i].hidden = newdataadr(fd, mdisps[i].hidden);

			if (mdisps[i].totdisp && !mdisps[i].level) {
				/* this calculation is only correct for loop mdisps;
				 * if loading pre-BMesh face mdisps this will be
				 * overwritten with the correct value in
				 * bm_corners_to_loops() */
				float gridsize = sqrtf(mdisps[i].totdisp);
				mdisps[i].level = (int)(logf(gridsize - 1.0f) / (float)M_LN2) + 1;
			}

			if ((fd->flags & FD_FLAGS_SWITCH_ENDIAN) && (mdisps[i].disps)) {
				/* DNA_struct_switch_endian doesn't do endian swap for (*disps)[] */
				/* this does swap for data written at write_mdisps() - readfile.c */
				BLI_endian_switch_float_array(*mdisps[i].disps, mdisps[i].totdisp * 3);
			}
			if (!external && !mdisps[i].disps)
				mdisps[i].totdisp = 0;
		}
	}
}

/*this isn't really a public api function, so prototyped here*/
static void direct_link_customdata(FileData *fd, CustomData *data, int count)
{
	int i = 0;

	data->layers = newdataadr(fd, data->layers);

	/* annoying workaround for bug [#31079] loading legacy files with
	 * no polygons _but_ have stale customdata */
	if (UNLIKELY(count == 0 && data->layers == NULL && data->totlayer != 0)) {
		CustomData_reset(data);
		return;
	}

	data->external = newdataadr(fd, data->external);

	while (i < data->totlayer) {
		CustomDataLayer *layer = &data->layers[i];

		if (layer->flag & CD_FLAG_EXTERNAL)
			layer->flag &= ~CD_FLAG_IN_MEMORY;

		layer->flag &= ~CD_FLAG_NOFREE;

		if (CustomData_verify_versions(data, i)) {
			layer->data = newdataadr(fd, layer->data);
			if (layer->type == CD_MDISPS)
				direct_link_mdisps(fd, count, layer->data, layer->flag & CD_FLAG_EXTERNAL);
			i++;
		}
	}

	CustomData_update_typemap(data);
}

static void direct_link_mesh(FileData *fd, Mesh *mesh)
{
	mesh->mat = newdataadr(fd, mesh->mat);
	test_pointer_array(fd, (void **)&mesh->mat);

	mesh->mvert = newdataadr(fd, mesh->mvert);
	mesh->medge = newdataadr(fd, mesh->medge);
	mesh->mface = newdataadr(fd, mesh->mface);
	mesh->mloop = newdataadr(fd, mesh->mloop);
	mesh->mpoly = newdataadr(fd, mesh->mpoly);
	mesh->mtface = newdataadr(fd, mesh->mtface);
	mesh->mcol = newdataadr(fd, mesh->mcol);
	mesh->dvert = newdataadr(fd, mesh->dvert);
	mesh->mloopcol = newdataadr(fd, mesh->mloopcol);
	mesh->mloopuv = newdataadr(fd, mesh->mloopuv);
	mesh->mtpoly = newdataadr(fd, mesh->mtpoly);
	mesh->mselect = newdataadr(fd, mesh->mselect);

	/* normally direct_link_dverts should be called in direct_link_customdata,
	 * but for backwards compat in do_versions to work we do it here */
	direct_link_dverts(fd, mesh->totvert, mesh->dvert);

	direct_link_customdata(fd, &mesh->vdata, mesh->totvert);
	direct_link_customdata(fd, &mesh->edata, mesh->totedge);
	direct_link_customdata(fd, &mesh->fdata, mesh->totface);
	direct_link_customdata(fd, &mesh->ldata, mesh->totloop);
	direct_link_customdata(fd, &mesh->pdata, mesh->totpoly);

	mesh->bb = NULL;
	mesh->edit_btmesh = NULL;

	/* happens with old files */
	if (mesh->mselect == NULL) {
		mesh->totselect = 0;
	}

	if (mesh->mloopuv || mesh->mtpoly) {
		/* for now we have to ensure texpoly and mloopuv layers are aligned
		 * in the future we may allow non-aligned layers */
		BKE_mesh_cd_validate(mesh);
	}

}

/* ************ READ OBJECT ***************** */

static void lib_link_modifiers__linkModifiers(
	void *userData, Object *ob, ID **idpoin, int cb_flag)
{
	FileData *fd = userData;

	*idpoin = newlibadr(fd, ob->id.lib, *idpoin);
	if (*idpoin != NULL && (cb_flag & IDWALK_CB_USER) != 0) {
		id_us_plus_no_lib(*idpoin);
	}
}
static void lib_link_modifiers(FileData *fd, Object *ob)
{
	modifiers_foreachIDLink(ob, lib_link_modifiers__linkModifiers, fd);
}

static void lib_link_object(FileData *fd, Main *main)
{
	bool warn = false;

	for (Object *ob = main->object.first; ob; ob = ob->id.next) {
		if (ob->id.tag & LIB_TAG_NEED_LINK) {
			int a;

			IDP_LibLinkProperty(ob->id.properties, fd);

			ob->parent = newlibadr(fd, ob->id.lib, ob->parent);
			ob->dup_group = newlibadr_us(fd, ob->id.lib, ob->dup_group);

			ob->proxy = newlibadr_us(fd, ob->id.lib, ob->proxy);
			if (ob->proxy) {
				/* paranoia check, actually a proxy_from pointer should never be written... */
				if (ob->proxy->id.lib == NULL) {
					ob->proxy->proxy_from = NULL;
					ob->proxy = NULL;

					if (ob->id.lib)
						printf("Proxy lost from  object %s lib %s\n", ob->id.name + 2, ob->id.lib->name);
					else
						printf("Proxy lost from  object %s lib <NONE>\n", ob->id.name + 2);
				}
				else {
					/* this triggers object_update to always use a copy */
					ob->proxy->proxy_from = ob;
				}
			}
			ob->proxy_group = newlibadr(fd, ob->id.lib, ob->proxy_group);

			void *poin = ob->data;
			ob->data = newlibadr_us(fd, ob->id.lib, ob->data);

			if (ob->data == NULL && poin != NULL) {
				if (ob->id.lib)
					printf("Can't find obdata of %s lib %s\n", ob->id.name + 2, ob->id.lib->name);
				else
					printf("Object %s lost data.\n", ob->id.name + 2);

				ob->type = OB_EMPTY;
				warn = true;

			}
			for (a = 0; a < ob->totcol; a++)
				ob->mat[a] = newlibadr_us(fd, ob->id.lib, ob->mat[a]);

			/* When the object is local and the data is library its possible
			 * the material list size gets out of sync. [#22663] */
			if (ob->data && ob->id.lib != ((ID *)ob->data)->lib) {
				const short *totcol_data = give_totcolp(ob);
				/* Only expand so as not to loose any object materials that might be set. */
				if (totcol_data && (*totcol_data > ob->totcol)) {
					/* printf("'%s' %d -> %d\n", ob->id.name, ob->totcol, *totcol_data); */
					BKE_material_resize_object(main, ob, *totcol_data, false);
				}
			}

			ob->id.tag &= ~LIB_TAG_NEED_LINK;
			/* if id.us==0 a new base will be created later on */

			lib_link_modifiers(fd, ob);

			if (ob->rigidbody_constraint) {
				ob->rigidbody_constraint->ob1 = newlibadr(fd, ob->id.lib, ob->rigidbody_constraint->ob1);
				ob->rigidbody_constraint->ob2 = newlibadr(fd, ob->id.lib, ob->rigidbody_constraint->ob2);
			}

			{
				LodLevel *level;
				for (level = ob->lodlevels.first; level; level = level->next) {
					level->source = newlibadr(fd, ob->id.lib, level->source);

					if (!level->source && level == ob->lodlevels.first)
						level->source = ob;
				}
			}
		}
	}

	if (warn) {
		BKE_report(fd->reports, RPT_WARNING, "Warning in console");
	}
}


static void direct_link_modifiers(FileData *fd, ListBase *lb)
{
	ModifierData *md;

	link_list(fd, lb);

	for (md = lb->first; md; md = md->next) {
		md->error = NULL;
		md->scene = NULL;

		/* if modifiers disappear, or for upward compatibility */
		if (NULL == modifierType_getInfo(md->type))
			md->type = eModifierType_None;

		if (md->type == eModifierType_Subsurf) {
			SubsurfModifierData *smd = (SubsurfModifierData *)md;

			smd->emCache = smd->mCache = NULL;
		}
		else if (md->type == eModifierType_Collision) {
			CollisionModifierData *collmd = (CollisionModifierData *)md;

			collmd->x = NULL;
			collmd->xnew = NULL;
			collmd->current_x = NULL;
			collmd->current_xnew = NULL;
			collmd->current_v = NULL;
			collmd->time_x = collmd->time_xnew = -1000;
			collmd->mvert_num = 0;
			collmd->tri_num = 0;
			collmd->is_static = false;
			collmd->bvhtree = NULL;
			collmd->tri = NULL;

		}
		else if (md->type == eModifierType_Surface) {
			SurfaceModifierData *surmd = (SurfaceModifierData *)md;

			surmd->dm = NULL;
			surmd->bvhtree = NULL;
			surmd->x = NULL;
			surmd->v = NULL;
			surmd->numverts = 0;
		}
		else if (md->type == eModifierType_Hook) {
			HookModifierData *hmd = (HookModifierData *)md;

			hmd->indexar = newdataadr(fd, hmd->indexar);
			if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
				BLI_endian_switch_int32_array(hmd->indexar, hmd->totindex);
			}

			hmd->curfalloff = newdataadr(fd, hmd->curfalloff);
			if (hmd->curfalloff) {
				direct_link_curvemapping(fd, hmd->curfalloff);
			}
		}
		else if (md->type == eModifierType_MeshDeform) {
			MeshDeformModifierData *mmd = (MeshDeformModifierData *)md;

			mmd->bindinfluences = newdataadr(fd, mmd->bindinfluences);
			mmd->bindoffsets = newdataadr(fd, mmd->bindoffsets);
			mmd->bindcagecos = newdataadr(fd, mmd->bindcagecos);
			mmd->dyngrid = newdataadr(fd, mmd->dyngrid);
			mmd->dyninfluences = newdataadr(fd, mmd->dyninfluences);
			mmd->dynverts = newdataadr(fd, mmd->dynverts);

			mmd->bindweights = newdataadr(fd, mmd->bindweights);
			mmd->bindcos = newdataadr(fd, mmd->bindcos);

			if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
				if (mmd->bindoffsets)  BLI_endian_switch_int32_array(mmd->bindoffsets, mmd->totvert + 1);
				if (mmd->bindcagecos)  BLI_endian_switch_float_array(mmd->bindcagecos, mmd->totcagevert * 3);
				if (mmd->dynverts)     BLI_endian_switch_int32_array(mmd->dynverts, mmd->totvert);
				if (mmd->bindweights)  BLI_endian_switch_float_array(mmd->bindweights, mmd->totvert);
				if (mmd->bindcos)      BLI_endian_switch_float_array(mmd->bindcos, mmd->totcagevert * 3);
			}
		}
		else if (md->type == eModifierType_Warp) {
			WarpModifierData *tmd = (WarpModifierData *)md;

			tmd->curfalloff = newdataadr(fd, tmd->curfalloff);
			if (tmd->curfalloff)
				direct_link_curvemapping(fd, tmd->curfalloff);
		}
		else if (md->type == eModifierType_WeightVGEdit) {
			WeightVGEditModifierData *wmd = (WeightVGEditModifierData *)md;

			wmd->cmap_curve = newdataadr(fd, wmd->cmap_curve);
			if (wmd->cmap_curve)
				direct_link_curvemapping(fd, wmd->cmap_curve);
		}
		else if (md->type == eModifierType_LaplacianDeform) {
			LaplacianDeformModifierData *lmd = (LaplacianDeformModifierData *)md;

			lmd->vertexco = newdataadr(fd, lmd->vertexco);
			if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
				BLI_endian_switch_float_array(lmd->vertexco, lmd->total_verts * 3);
			}
			lmd->cache_system = NULL;
		}
		else if (md->type == eModifierType_CorrectiveSmooth) {
			CorrectiveSmoothModifierData *csmd = (CorrectiveSmoothModifierData *)md;

			if (csmd->bind_coords) {
				csmd->bind_coords = newdataadr(fd, csmd->bind_coords);
				if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
					BLI_endian_switch_float_array((float *)csmd->bind_coords, csmd->bind_coords_num * 3);
				}
			}

			/* runtime only */
			csmd->delta_cache = NULL;
			csmd->delta_cache_num = 0;
		}
		else if (md->type == eModifierType_MeshSequenceCache) {
			MeshSeqCacheModifierData *msmcd = (MeshSeqCacheModifierData *)md;
			msmcd->reader = NULL;
		}
		else if (md->type == eModifierType_SurfaceDeform) {
			SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *)md;

			smd->verts = newdataadr(fd, smd->verts);

			if (smd->verts) {
				for (int i = 0; i < smd->numverts; i++) {
					smd->verts[i].binds = newdataadr(fd, smd->verts[i].binds);

					if (smd->verts[i].binds) {
						for (int j = 0; j < smd->verts[i].numbinds; j++) {
							smd->verts[i].binds[j].vert_inds = newdataadr(fd, smd->verts[i].binds[j].vert_inds);
							smd->verts[i].binds[j].vert_weights = newdataadr(fd, smd->verts[i].binds[j].vert_weights);

							if (fd->flags & FD_FLAGS_SWITCH_ENDIAN) {
								if (smd->verts[i].binds[j].vert_inds)
									BLI_endian_switch_uint32_array(
									        smd->verts[i].binds[j].vert_inds, smd->verts[i].binds[j].numverts);

								if (smd->verts[i].binds[j].vert_weights) {
									if (smd->verts[i].binds[j].mode == MOD_SDEF_MODE_CENTROID ||
									    smd->verts[i].binds[j].mode == MOD_SDEF_MODE_LOOPTRI)
									{
										BLI_endian_switch_float_array(
										        smd->verts[i].binds[j].vert_weights, 3);
									}
									else {
										BLI_endian_switch_float_array(
										        smd->verts[i].binds[j].vert_weights, smd->verts[i].binds[j].numverts);
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

static void direct_link_object(FileData *fd, Object *ob)
{
	/* weak weak... this was only meant as draw flag, now is used in give_base_to_objects too */
	ob->flag &= ~OB_FROMGROUP;

	/* This is a transient flag; clear in order to avoid unneeded object update pending from
	 * time when file was saved.
	 */
	ob->id.recalc = 0;

	/* XXX This should not be needed - but seems like it can happen in some cases, so for now play safe... */
	ob->proxy_from = NULL;

	/* loading saved files with editmode enabled works, but for undo we like
	 * to stay in object mode during undo presses so keep editmode disabled.
	 *
	 * Also when linking in a file don't allow edit and pose modes.
	 * See [#34776, #42780] for more information.
	 */
	if (fd->memfile || (ob->id.tag & (LIB_TAG_EXTERN | LIB_TAG_INDIRECT))) {
		ob->mode &= ~(OB_MODE_EDIT);
	}

	link_list(fd, &ob->defbase);

	ob->mat = newdataadr(fd, ob->mat);
	test_pointer_array(fd, (void **)&ob->mat);
	ob->matbits = newdataadr(fd, ob->matbits);

	/* do it here, below old data gets converted */
	direct_link_modifiers(fd, &ob->modifiers);


	ob->rigidbody_object = newdataadr(fd, ob->rigidbody_object);
	if (ob->rigidbody_object) {
		RigidBodyOb *rbo = ob->rigidbody_object;

		/* must nullify the references to physics sim objects, since they no-longer exist
		 * (and will need to be recalculated)
		 */
		rbo->physics_object = NULL;
		rbo->physics_shape = NULL;
	}
	ob->rigidbody_constraint = newdataadr(fd, ob->rigidbody_constraint);
	if (ob->rigidbody_constraint)
		ob->rigidbody_constraint->physics_constraint = NULL;

	ob->iuser = newdataadr(fd, ob->iuser);
	if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE && !ob->iuser) {
		BKE_object_empty_draw_type_set(ob, ob->empty_drawtype);
	}

	ob->customdata_mask = 0;
	ob->bb = NULL;
	ob->derivedDeform = NULL;
	ob->derivedFinal = NULL;
	BLI_listbase_clear(&ob->gpulamp);
	link_list(fd, &ob->pc_ids);

	/* Runtime curve data  */
	ob->curve_cache = NULL;

	/* in case this value changes in future, clamp else we get undefined behavior */
	CLAMP(ob->rotmode, ROT_MODE_MIN, ROT_MODE_MAX);

	link_list(fd, &ob->lodlevels);
	ob->currentlod = ob->lodlevels.first;

	ob->preview = direct_link_preview_image(fd, ob->preview);
}

/* ************ READ SCENE ***************** */

/* check for cyclic set-scene,
 * libs can cause this case which is normally prevented, see (T#####) */
#define USE_SETSCENE_CHECK

#ifdef USE_SETSCENE_CHECK
/**
 * A version of #BKE_scene_validate_setscene with special checks for linked libs.
 */
static bool scene_validate_setscene__liblink(Scene *sce, const int totscene)
{
	Scene *sce_iter;
	int a;

	if (sce->set == NULL) return 1;

	for (a = 0, sce_iter = sce; sce_iter->set; sce_iter = sce_iter->set, a++) {
		if (sce_iter->id.tag & LIB_TAG_NEED_LINK) {
			return 1;
		}

		if (a > totscene) {
			sce->set = NULL;
			return 0;
		}
	}

	return 1;
}
#endif

static void lib_link_scene(FileData *fd, Main *main)
{
#ifdef USE_SETSCENE_CHECK
	bool need_check_set = false;
	int totscene = 0;
#endif

	for (Scene *sce = main->scene.first; sce; sce = sce->id.next) {
		if (sce->id.tag & LIB_TAG_NEED_LINK) {
			/* Link ID Properties -- and copy this comment EXACTLY for easy finding
			 * of library blocks that implement this.*/
			IDP_LibLinkProperty(sce->id.properties, fd);

			sce->camera = newlibadr(fd, sce->id.lib, sce->camera);
			sce->world = newlibadr_us(fd, sce->id.lib, sce->world);
			sce->set = newlibadr(fd, sce->id.lib, sce->set);

			for (Base *next, *base = sce->base.first; base; base = next) {
				next = base->next;

				base->object = newlibadr_us(fd, sce->id.lib, base->object);

				if (base->object == NULL) {
					blo_reportf_wrap(fd->reports, RPT_WARNING, TIP_("LIB: object lost from scene: '%s'"),
					                 sce->id.name + 2);
					BLI_remlink(&sce->base, base);
					if (base == sce->basact) sce->basact = NULL;
					MEM_freeN(base);
				}
			}


			/* rigidbody world relies on it's linked groups */
			if (sce->rigidbody_world) {
				RigidBodyWorld *rbw = sce->rigidbody_world;
				if (rbw->group)
					rbw->group = newlibadr(fd, sce->id.lib, rbw->group);
			}

#ifdef USE_SETSCENE_CHECK
			if (sce->set != NULL) {
				/* link flag for scenes with set would be reset later,
				 * so this way we only check cyclic for newly linked scenes.
				 */
				need_check_set = true;
			}
			else {
				/* postpone un-setting the flag until we've checked the set-scene */
				sce->id.tag &= ~LIB_TAG_NEED_LINK;
			}
#else
			sce->id.tag &= ~LIB_TAG_NEED_LINK;
#endif
		}

#ifdef USE_SETSCENE_CHECK
		totscene++;
#endif
	}

#ifdef USE_SETSCENE_CHECK
	if (need_check_set) {
		for (Scene *sce = main->scene.first; sce; sce = sce->id.next) {
			if (sce->id.tag & LIB_TAG_NEED_LINK) {
				sce->id.tag &= ~LIB_TAG_NEED_LINK;
				if (!scene_validate_setscene__liblink(sce, totscene)) {
					printf("Found cyclic background scene when linking %s\n", sce->id.name + 2);
				}
			}
		}
	}
#endif
}

#undef USE_SETSCENE_CHECK


static void direct_link_view_settings(FileData *fd, ColorManagedViewSettings *view_settings)
{
	view_settings->curve_mapping = newdataadr(fd, view_settings->curve_mapping);

	if (view_settings->curve_mapping)
		direct_link_curvemapping(fd, view_settings->curve_mapping);
}

static void direct_link_scene(FileData *fd, Scene *sce)
{
	RigidBodyWorld *rbw;

	sce->obedit = NULL;
	sce->stats = NULL;
	sce->fps_info = NULL;
	sce->customdata_mask_modal = 0;
	sce->lay_updated = 0;

	/* set users to one by default, not in lib-link, this will increase it for compo nodes */
	id_us_ensure_real(&sce->id);

	link_list(fd, &(sce->base));

	sce->basact = newdataadr(fd, sce->basact);

	sce->toolsettings = newdataadr(fd, sce->toolsettings);


	link_list(fd, &(sce->transform_spaces));


	direct_link_view_settings(fd, &sce->view_settings);

	sce->rigidbody_world = newdataadr(fd, sce->rigidbody_world);
	rbw = sce->rigidbody_world;
	if (rbw) {
		/* must nullify the reference to physics sim object, since it no-longer exist
		 * (and will need to be recalculated)
		 */
		rbw->physics_world = NULL;
		rbw->objects = NULL;
		rbw->numbodies = 0;

	}

	sce->preview = direct_link_preview_image(fd, sce->preview);

}

/* ************ READ WM ***************** */

static void direct_link_windowmanager(FileData *fd, wmWindowManager *wm)
{
	wmWindow *win;

	id_us_ensure_real(&wm->id);
	link_list(fd, &wm->windows);

	for (win = wm->windows.first; win; win = win->next) {
		win->ghostwin = NULL;
		win->eventstate = NULL;
		win->curswin = NULL;
		win->tweak = NULL;
#ifdef WIN32
		win->ime_data = NULL;
#endif

		BLI_listbase_clear(&win->queue);
		BLI_listbase_clear(&win->handlers);
		BLI_listbase_clear(&win->modalhandlers);
		BLI_listbase_clear(&win->subwindows);
		BLI_listbase_clear(&win->gesture);
		BLI_listbase_clear(&win->drawdata);

		win->drawmethod = -1;
		win->drawfail = 0;
		win->active = 0;

		win->cursor       = 0;
		win->lastcursor   = 0;
		win->modalcursor  = 0;
		win->grabcursor   = 0;
		win->addmousemove = true;
		win->multisamples = 0;

	}

	BLI_listbase_clear(&wm->timers);
	BLI_listbase_clear(&wm->operators);
	BLI_listbase_clear(&wm->paintcursors);
	BLI_listbase_clear(&wm->queue);
	BKE_reports_init(&wm->reports, RPT_STORE);

	BLI_listbase_clear(&wm->keyconfigs);
	wm->defaultconf = NULL;
	wm->addonconf = NULL;
	wm->userconf = NULL;
	wm->undo_stack = NULL;

	BLI_listbase_clear(&wm->jobs);
	BLI_listbase_clear(&wm->drags);

	wm->windrawable = NULL;
	wm->winactive = NULL;
	wm->initialized = 0;
	wm->op_undo_depth = 0;
	wm->is_interface_locked = 0;
}

static void lib_link_windowmanager(FileData *fd, Main *main)
{
	wmWindowManager *wm;
	wmWindow *win;

	for (wm = main->wm.first; wm; wm = wm->id.next) {
		if (wm->id.tag & LIB_TAG_NEED_LINK) {
			/* Note: WM IDProperties are never written to file, hence no need to read/link them here. */
			for (win = wm->windows.first; win; win = win->next) {
				win->screen = newlibadr(fd, NULL, win->screen);
			}

			wm->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

/* ****************** READ SCREEN ***************** */

/* note: file read without screens option G_FILE_NO_UI;
 * check lib pointers in call below */
static void lib_link_screen(FileData *fd, Main *main)
{
	for (bScreen *sc = main->screen.first; sc; sc = sc->id.next) {
		if (sc->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(sc->id.properties, fd);
			id_us_ensure_real(&sc->id);

			sc->scene = newlibadr(fd, sc->id.lib, sc->scene);

			/* this should not happen, but apparently it does somehow. Until we figure out the cause,
			 * just assign first available scene */
			if (!sc->scene)
				sc->scene = main->scene.first;

			sc->animtimer = NULL; /* saved in rare cases */
			sc->tool_tip = NULL;
			sc->scrubbing = false;

			for (ScrArea *sa = sc->areabase.first; sa; sa = sa->next) {
				sa->full = newlibadr(fd, sc->id.lib, sa->full);

				for (SpaceLink *sl = sa->spacedata.first; sl; sl = sl->next) {
					switch (sl->spacetype) {
						case SPACE_VIEW3D:
						{
							View3D *v3d = (View3D *)sl;

							v3d->camera = newlibadr(fd, sc->id.lib, v3d->camera);
							v3d->ob_centre = newlibadr(fd, sc->id.lib, v3d->ob_centre);

							if (v3d->localvd) {
								v3d->localvd->camera = newlibadr(fd, sc->id.lib, v3d->localvd->camera);
							}
							break;
						}
						case SPACE_BUTS:
						{
							SpaceButs *sbuts = (SpaceButs *)sl;
							sbuts->pinid = newlibadr(fd, sc->id.lib, sbuts->pinid);
							if (sbuts->pinid == NULL) {
								sbuts->flag &= ~SB_PIN_CONTEXT;
							}
							break;
						}
						case SPACE_FILE:
							break;
						case SPACE_IMAGE:
						{
							SpaceImage *sima = (SpaceImage *)sl;

							sima->image = newlibadr_real_us(fd, sc->id.lib, sima->image);
							break;
						}
						case SPACE_TEXT:
						{
							SpaceText *st = (SpaceText *)sl;

							st->text = newlibadr(fd, sc->id.lib, st->text);
							break;
						}
						case SPACE_SCRIPT:
						{
							SpaceScript *scpt = (SpaceScript *)sl;
							/*scpt->script = NULL; - 2.45 set to null, better re-run the script */
							if (scpt->script) {
								scpt->script = newlibadr(fd, sc->id.lib, scpt->script);
								if (scpt->script) {
									SCRIPT_SET_NULL(scpt->script);
								}
							}
							break;
						}
						case SPACE_OUTLINER:
						{
							SpaceOops *so = (SpaceOops *)sl;
							so->search_tse.id = newlibadr(fd, NULL, so->search_tse.id);

							if (so->treestore) {
								TreeStoreElem *tselem;
								BLI_mempool_iter iter;

								BLI_mempool_iternew(so->treestore, &iter);
								while ((tselem = BLI_mempool_iterstep(&iter))) {
									tselem->id = newlibadr(fd, NULL, tselem->id);
								}
								if (so->treehash) {
									/* rebuild hash table, because it depends on ids too */
									so->storeflag |= SO_TREESTORE_REBUILD;
								}
							}
							break;
						}

						default:
							break;
					}
				}
			}
			sc->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

/* how to handle user count on pointer restore */
typedef enum ePointerUserMode {
	USER_IGNORE = 0,  /* ignore user count */
	USER_REAL   = 1,  /* ensure at least one real user (fake user ignored) */
} ePointerUserMode;

static void restore_pointer_user(ID *id, ID *newid, ePointerUserMode user)
{
	BLI_assert(STREQ(newid->name + 2, id->name + 2));
	BLI_assert(newid->lib == id->lib);
	UNUSED_VARS_NDEBUG(id);

	if (user == USER_REAL) {
		id_us_ensure_real(newid);
	}
}

#ifndef USE_GHASH_RESTORE_POINTER
/**
 * A version of #restore_pointer_by_name that performs a full search (slow!).
 * Use only for limited lookups, when the overhead of
 * creating a #IDNameLib_Map for a single lookup isn't worthwhile.
 */
static void *restore_pointer_by_name_main(Main *mainp, ID *id, ePointerUserMode user)
{
	if (id) {
		ListBase *lb = which_libbase(mainp, GS(id->name));
		if (lb) {  /* there's still risk of checking corrupt mem (freed Ids in oops) */
			ID *idn = lb->first;
			for (; idn; idn = idn->next) {
				if (STREQ(idn->name + 2, id->name + 2)) {
					if (idn->lib == id->lib) {
						restore_pointer_user(id, idn, user);
						break;
					}
				}
			}
			return idn;
		}
	}
	return NULL;
}
#endif

/**
 * Only for undo files, or to restore a screen after reading without UI...
 *
 * \param user:
 * - USER_IGNORE: no usercount change
 * - USER_REAL: ensure a real user (even if a fake one is set)
 * \param id_map: lookup table, use when performing many lookups.
 * this could be made an optional argument (falling back to a full lookup),
 * however at the moment it's always available.
 */
static void *restore_pointer_by_name(struct IDNameLib_Map *id_map, ID *id, ePointerUserMode user)
{
#ifdef USE_GHASH_RESTORE_POINTER
	if (id) {
		/* use fast lookup when available */
		ID *idn = BKE_main_idmap_lookup_id(id_map, id);
		if (idn) {
			restore_pointer_user(id, idn, user);
		}
		return idn;
	}
	return NULL;
#else
	Main *mainp = BKE_main_idmap_main_get(id_map);
	return restore_pointer_by_name_main(mainp, id, user);
#endif
}

static void lib_link_clipboard_restore(struct IDNameLib_Map *id_map)
{
	/* update IDs stored in sequencer clipboard */
}

/* called from kernel/blender.c */
/* used to link a file (without UI) to the current UI */
/* note that it assumes the old pointers in UI are still valid, so old Main is not freed */
void blo_lib_link_screen_restore(Main *newmain, bScreen *curscreen, Scene *curscene)
{
	wmWindow *win;
	wmWindowManager *wm;
	bScreen *sc;
	ScrArea *sa;

	struct IDNameLib_Map *id_map = BKE_main_idmap_create(newmain);

	/* first windowmanager */
	for (wm = newmain->wm.first; wm; wm = wm->id.next) {
		for (win = wm->windows.first; win; win = win->next) {
			win->screen = restore_pointer_by_name(id_map, (ID *)win->screen, USER_REAL);

			if (win->screen == NULL)
				win->screen = curscreen;

			win->screen->winid = win->winid;
		}
	}


	for (sc = newmain->screen.first; sc; sc = sc->id.next) {
		Scene *oldscene = sc->scene;

		sc->scene = restore_pointer_by_name(id_map, (ID *)sc->scene, USER_REAL);
		if (sc->scene == NULL)
			sc->scene = curscene;

		/* keep cursor location through undo */
		copy_v3_v3(sc->scene->cursor, oldscene->cursor);

		for (sa = sc->areabase.first; sa; sa = sa->next) {
			SpaceLink *sl;

			for (sl = sa->spacedata.first; sl; sl = sl->next) {
				if (sl->spacetype == SPACE_VIEW3D) {
					View3D *v3d = (View3D *)sl;
					BGpic *bgpic;

					if (v3d->scenelock)
						v3d->camera = NULL; /* always get from scene */
					else
						v3d->camera = restore_pointer_by_name(id_map, (ID *)v3d->camera, USER_REAL);
					if (v3d->camera == NULL)
						v3d->camera = sc->scene->camera;
					v3d->ob_centre = restore_pointer_by_name(id_map, (ID *)v3d->ob_centre, USER_REAL);

					for (bgpic = v3d->bgpicbase.first; bgpic; bgpic = bgpic->next) {
						if ((bgpic->ima = restore_pointer_by_name(id_map, (ID *)bgpic->ima, USER_IGNORE))) {
							id_us_plus((ID *)bgpic->ima);
						}
					}
					if (v3d->localvd) {
						/*Base *base;*/

						v3d->localvd->camera = sc->scene->camera;

						/* localview can become invalid during undo/redo steps, so we exit it when no could be found */
					}
					else if (v3d->scenelock) {
						v3d->lay = sc->scene->lay;
					}

					/* not very nice, but could help */
					if ((v3d->layact & v3d->lay) == 0) v3d->layact = v3d->lay;

				}
				else if (sl->spacetype == SPACE_BUTS) {
					SpaceButs *sbuts = (SpaceButs *)sl;
					sbuts->pinid = restore_pointer_by_name(id_map, sbuts->pinid, USER_IGNORE);
					if (sbuts->pinid == NULL) {
						sbuts->flag &= ~SB_PIN_CONTEXT;
					}

					/* TODO: restore path pointers: T40046
					 * (complicated because this contains data pointers too, not just ID)*/
					MEM_SAFE_FREE(sbuts->path);
				}
				else if (sl->spacetype == SPACE_FILE) {
					SpaceFile *sfile = (SpaceFile *)sl;
					sfile->op = NULL;
					sfile->previews_timer = NULL;
				}
				else if (sl->spacetype == SPACE_IMAGE) {
					SpaceImage *sima = (SpaceImage *)sl;

					sima->image = restore_pointer_by_name(id_map, (ID *)sima->image, USER_REAL);

					/* this will be freed, not worth attempting to find same scene,
					 * since it gets initialized later */
					sima->iuser.scene = NULL;

					sima->scopes.ok = 0;

				}
				else if (sl->spacetype == SPACE_TEXT) {
					SpaceText *st = (SpaceText *)sl;

					st->text = restore_pointer_by_name(id_map, (ID *)st->text, USER_REAL);
					if (st->text == NULL) st->text = newmain->text.first;
				}
				else if (sl->spacetype == SPACE_SCRIPT) {
					SpaceScript *scpt = (SpaceScript *)sl;

					scpt->script = restore_pointer_by_name(id_map, (ID *)scpt->script, USER_REAL);

					/*sc->script = NULL; - 2.45 set to null, better re-run the script */
					if (scpt->script) {
						SCRIPT_SET_NULL(scpt->script);
					}
				}
				else if (sl->spacetype == SPACE_OUTLINER) {
					SpaceOops *so = (SpaceOops *)sl;

					so->search_tse.id = restore_pointer_by_name(id_map, so->search_tse.id, USER_IGNORE);

					if (so->treestore) {
						TreeStoreElem *tselem;
						BLI_mempool_iter iter;

						BLI_mempool_iternew(so->treestore, &iter);
						while ((tselem = BLI_mempool_iterstep(&iter))) {
							/* Do not try to restore pointers to drivers/sequence/etc., can crash in undo case! */
							if (TSE_IS_REAL_ID(tselem)) {
								tselem->id = restore_pointer_by_name(id_map, tselem->id, USER_IGNORE);
							}
							else {
								tselem->id = NULL;
							}
						}
						if (so->treehash) {
							/* rebuild hash table, because it depends on ids too */
							so->storeflag |= SO_TREESTORE_REBUILD;
						}
					}
				}
			}
		}
	}

	/* update IDs stored in all possible clipboards */
	lib_link_clipboard_restore(id_map);

	BKE_main_idmap_destroy(id_map);
}

static void direct_link_region(FileData *fd, ARegion *ar, int spacetype)
{
	Panel *pa;
	uiList *ui_list;

	link_list(fd, &ar->panels);

	for (pa = ar->panels.first; pa; pa = pa->next) {
		pa->paneltab = newdataadr(fd, pa->paneltab);
		pa->runtime_flag = 0;
		pa->activedata = NULL;
		pa->type = NULL;
	}

	link_list(fd, &ar->panels_category_active);

	link_list(fd, &ar->ui_lists);

	for (ui_list = ar->ui_lists.first; ui_list; ui_list = ui_list->next) {
		ui_list->type = NULL;
		ui_list->dyn_data = NULL;
		ui_list->properties = newdataadr(fd, ui_list->properties);
		IDP_DirectLinkGroup_OrFree(&ui_list->properties, (fd->flags & FD_FLAGS_SWITCH_ENDIAN), fd);
	}

	link_list(fd, &ar->ui_previews);

	if (spacetype == SPACE_EMPTY) {
		/* unkown space type, don't leak regiondata */
		ar->regiondata = NULL;
	}
	else if (ar->flag & RGN_FLAG_TEMP_REGIONDATA) {
		/* Runtime data, don't use. */
		ar->regiondata = NULL;
	}
	else {
		ar->regiondata = newdataadr(fd, ar->regiondata);
		if (ar->regiondata) {
			if (spacetype == SPACE_VIEW3D) {
				RegionView3D *rv3d = ar->regiondata;

				rv3d->localvd = newdataadr(fd, rv3d->localvd);
				rv3d->clipbb = newdataadr(fd, rv3d->clipbb);

				rv3d->depths = NULL;
				rv3d->gpuoffscreen = NULL;
				rv3d->sms = NULL;
				rv3d->smooth_timer = NULL;
			}
		}
	}

	ar->v2d.tab_offset = NULL;
	ar->v2d.tab_num = 0;
	ar->v2d.tab_cur = 0;
	ar->v2d.sms = NULL;
	BLI_listbase_clear(&ar->panels_category);
	BLI_listbase_clear(&ar->handlers);
	BLI_listbase_clear(&ar->uiblocks);
	ar->headerstr = NULL;
	ar->swinid = 0;
	ar->type = NULL;
	ar->swap = 0;
	ar->do_draw = 0;
	ar->regiontimer = NULL;
	memset(&ar->drawrct, 0, sizeof(ar->drawrct));
}

/* for the saved 2.50 files without regiondata */
/* and as patch for 2.48 and older */
void blo_do_versions_view3d_split_250(View3D *v3d, ListBase *regions)
{
	ARegion *ar;

	for (ar = regions->first; ar; ar = ar->next) {
		if (ar->regiontype == RGN_TYPE_WINDOW && ar->regiondata == NULL) {
			ar->regiondata = MEM_callocN(sizeof(RegionView3D), "region v3d patch");

		}
	}

	/* this was not initialized correct always */
	if (v3d->twtype == 0)
		v3d->twtype = V3D_MANIP_TRANSLATE;
	if (v3d->gridsubdiv == 0)
		v3d->gridsubdiv = 10;
}

static bool direct_link_screen(FileData *fd, bScreen *sc)
{
	ScrArea *sa;
	ScrVert *sv;
	ScrEdge *se;
	bool wrong_id = false;

	link_list(fd, &(sc->vertbase));
	link_list(fd, &(sc->edgebase));
	link_list(fd, &(sc->areabase));
	sc->regionbase.first = sc->regionbase.last = NULL;
	sc->context = NULL;

	sc->mainwin = sc->subwinactive = 0;  /* indices */
	sc->swap = 0;

	/* edges */
	for (se = sc->edgebase.first; se; se = se->next) {
		se->v1 = newdataadr(fd, se->v1);
		se->v2 = newdataadr(fd, se->v2);
		if ((intptr_t)se->v1 > (intptr_t)se->v2) {
			sv = se->v1;
			se->v1 = se->v2;
			se->v2 = sv;
		}

		if (se->v1 == NULL) {
			printf("Error reading Screen %s... removing it.\n", sc->id.name + 2);
			BLI_remlink(&sc->edgebase, se);
			wrong_id = true;
		}
	}

	/* areas */
	for (sa = sc->areabase.first; sa; sa = sa->next) {
		SpaceLink *sl;
		ARegion *ar;

		link_list(fd, &(sa->spacedata));
		link_list(fd, &(sa->regionbase));

		BLI_listbase_clear(&sa->handlers);
		sa->type = NULL;    /* spacetype callbacks */
		sa->region_active_win = -1;

		/* if we do not have the spacetype registered (game player), we cannot
		 * free it, so don't allocate any new memory for such spacetypes. */
		if (!BKE_spacetype_exists(sa->spacetype))
			sa->spacetype = SPACE_EMPTY;

		for (ar = sa->regionbase.first; ar; ar = ar->next)
			direct_link_region(fd, ar, sa->spacetype);

		/* accident can happen when read/save new file with older version */
		/* 2.50: we now always add spacedata for info */
		if (sa->spacedata.first == NULL) {
			SpaceInfo *sinfo = MEM_callocN(sizeof(SpaceInfo), "spaceinfo");
			sa->spacetype = sinfo->spacetype = SPACE_INFO;
			BLI_addtail(&sa->spacedata, sinfo);
		}
		/* add local view3d too */
		else if (sa->spacetype == SPACE_VIEW3D)
			blo_do_versions_view3d_split_250(sa->spacedata.first, &sa->regionbase);

		/* incase we set above */
		sa->butspacetype = sa->spacetype;

		for (sl = sa->spacedata.first; sl; sl = sl->next) {
			link_list(fd, &(sl->regionbase));

			/* if we do not have the spacetype registered (game player), we cannot
			 * free it, so don't allocate any new memory for such spacetypes. */
			if (!BKE_spacetype_exists(sl->spacetype))
				sl->spacetype = SPACE_EMPTY;

			for (ar = sl->regionbase.first; ar; ar = ar->next)
				direct_link_region(fd, ar, sl->spacetype);

			if (sl->spacetype == SPACE_VIEW3D) {
				View3D *v3d = (View3D *)sl;

				v3d->flag |= V3D_INVALID_BACKBUF;

				link_list(fd, &v3d->bgpicbase);

				v3d->localvd = newdataadr(fd, v3d->localvd);
				BLI_listbase_clear(&v3d->afterdraw_transp);
				BLI_listbase_clear(&v3d->afterdraw_xray);
				BLI_listbase_clear(&v3d->afterdraw_xraytransp);
				v3d->properties_storage = NULL;
				v3d->defmaterial = NULL;

				/* render can be quite heavy, set to solid on load */
				if (v3d->drawtype == OB_RENDER)
					v3d->drawtype = OB_SOLID;
				v3d->prev_drawtype = OB_SOLID;

				blo_do_versions_view3d_split_250(v3d, &sl->regionbase);
			}
			else if (sl->spacetype == SPACE_OUTLINER) {
				SpaceOops *soops = (SpaceOops *)sl;

				/* use newdataadr_no_us and do not free old memory avoiding double
				 * frees and use of freed memory. this could happen because of a
				 * bug fixed in revision 58959 where the treestore memory address
				 * was not unique */
				TreeStore *ts = newdataadr_no_us(fd, soops->treestore);
				soops->treestore = NULL;
				if (ts) {
					TreeStoreElem *elems = newdataadr_no_us(fd, ts->data);

					soops->treestore = BLI_mempool_create(sizeof(TreeStoreElem), ts->usedelem,
					                                      512, BLI_MEMPOOL_ALLOW_ITER);
					if (ts->usedelem && elems) {
						int i;
						for (i = 0; i < ts->usedelem; i++) {
							TreeStoreElem *new_elem = BLI_mempool_alloc(soops->treestore);
							*new_elem = elems[i];
						}
					}
					/* we only saved what was used */
					soops->storeflag |= SO_TREESTORE_CLEANUP;   // at first draw
				}
				soops->treehash = NULL;
				soops->tree.first = soops->tree.last = NULL;
			}
			else if (sl->spacetype == SPACE_IMAGE) {
				SpaceImage *sima = (SpaceImage *)sl;

				sima->iuser.scene = NULL;
				sima->iuser.ok = 1;
				sima->scopes.waveform_1 = NULL;
				sima->scopes.waveform_2 = NULL;
				sima->scopes.waveform_3 = NULL;
				sima->scopes.vecscope = NULL;
				sima->scopes.ok = 0;

				/* WARNING: gpencil data is no longer stored directly in sima after 2.5
				 * so sacrifice a few old files for now to avoid crashes with new files!
				 * committed: r28002 */
			}
			else if (sl->spacetype == SPACE_TEXT) {
				SpaceText *st = (SpaceText *)sl;

				st->drawcache = NULL;
				st->scroll_accum[0] = 0.0f;
				st->scroll_accum[1] = 0.0f;
			}
			else if (sl->spacetype == SPACE_BUTS) {
				SpaceButs *sbuts = (SpaceButs *)sl;

				sbuts->path = NULL;
				sbuts->texuser = NULL;
				sbuts->mainbo = sbuts->mainb;
				sbuts->mainbuser = sbuts->mainb;
			}
			else if (sl->spacetype == SPACE_CONSOLE) {
				SpaceConsole *sconsole = (SpaceConsole *)sl;
				ConsoleLine *cl, *cl_next;

				link_list(fd, &sconsole->scrollback);
				link_list(fd, &sconsole->history);

				//for (cl= sconsole->scrollback.first; cl; cl= cl->next)
				//	cl->line= newdataadr(fd, cl->line);

				/* comma expressions, (e.g. expr1, expr2, expr3) evaluate each expression,
				 * from left to right.  the right-most expression sets the result of the comma
				 * expression as a whole*/
				for (cl = sconsole->history.first; cl; cl = cl_next) {
					cl_next = cl->next;
					cl->line = newdataadr(fd, cl->line);
					if (cl->line) {
						/* the allocted length is not written, so reset here */
						cl->len_alloc = cl->len + 1;
					}
					else {
						BLI_remlink(&sconsole->history, cl);
						MEM_freeN(cl);
					}
				}
			}
			else if (sl->spacetype == SPACE_FILE) {
				SpaceFile *sfile = (SpaceFile *)sl;

				/* this sort of info is probably irrelevant for reloading...
				 * plus, it isn't saved to files yet!
				 */
				sfile->folders_prev = sfile->folders_next = NULL;
				sfile->files = NULL;
				sfile->layout = NULL;
				sfile->op = NULL;
				sfile->previews_timer = NULL;
				sfile->params = newdataadr(fd, sfile->params);
			}
		}

		BLI_listbase_clear(&sa->actionzones);

		sa->v1 = newdataadr(fd, sa->v1);
		sa->v2 = newdataadr(fd, sa->v2);
		sa->v3 = newdataadr(fd, sa->v3);
		sa->v4 = newdataadr(fd, sa->v4);
	}

	return wrong_id;
}

/* ********** READ LIBRARY *************** */


static void direct_link_library(FileData *fd, Library *lib, Main *main)
{
	Main *newmain;

	/* check if the library was already read */
	for (newmain = fd->mainlist->first; newmain; newmain = newmain->next) {
		if (newmain->curlib) {
			if (BLI_path_cmp(newmain->curlib->filepath, lib->filepath) == 0) {
				blo_reportf_wrap(fd->reports, RPT_WARNING,
				                 TIP_("Library '%s', '%s' had multiple instances, save and reload!"),
				                 lib->name, lib->filepath);

				change_idid_adr(fd->mainlist, fd, lib, newmain->curlib);
/*				change_idid_adr_fd(fd, lib, newmain->curlib); */

				BLI_remlink(&main->library, lib);
				MEM_freeN(lib);

				/* Now, since Blender always expect **latest** Main pointer from fd->mainlist to be the active library
				 * Main pointer, where to add all non-library data-blocks found in file next, we have to switch that
				 * 'dupli' found Main to latest position in the list!
				 * Otherwise, you get weird disappearing linked data on a rather unconsistant basis.
				 * See also T53977 for reproducible case. */
				BLI_remlink(fd->mainlist, newmain);
				BLI_addtail(fd->mainlist, newmain);

				return;
			}
		}
	}

	/* make sure we have full path in lib->filepath */
	BLI_strncpy(lib->filepath, lib->name, sizeof(lib->name));
	BLI_cleanup_path(fd->relabase, lib->filepath);

//	printf("direct_link_library: name %s\n", lib->name);
//	printf("direct_link_library: filepath %s\n", lib->filepath);

	lib->packedfile = direct_link_packedfile(fd, lib->packedfile);

	/* new main */
	newmain = BKE_main_new();
	BLI_addtail(fd->mainlist, newmain);
	newmain->curlib = lib;

	lib->parent = NULL;
}

static void lib_link_library(FileData *UNUSED(fd), Main *main)
{
	Library *lib;
	for (lib = main->library.first; lib; lib = lib->id.next) {
		id_us_ensure_real(&lib->id);
	}
}

/* Always call this once you have loaded new library data to set the relative paths correctly in relation to the blend file */
static void fix_relpaths_library(const char *basepath, Main *main)
{
	Library *lib;
	/* BLO_read_from_memory uses a blank filename */
	if (basepath == NULL || basepath[0] == '\0') {
		for (lib = main->library.first; lib; lib = lib->id.next) {
			/* when loading a linked lib into a file which has not been saved,
			 * there is nothing we can be relative to, so instead we need to make
			 * it absolute. This can happen when appending an object with a relative
			 * link into an unsaved blend file. See [#27405].
			 * The remap relative option will make it relative again on save - campbell */
			if (BLI_path_is_rel(lib->name)) {
				BLI_strncpy(lib->name, lib->filepath, sizeof(lib->name));
			}
		}
	}
	else {
		for (lib = main->library.first; lib; lib = lib->id.next) {
			/* Libraries store both relative and abs paths, recreate relative paths,
			 * relative to the blend file since indirectly linked libs will be relative to their direct linked library */
			if (BLI_path_is_rel(lib->name)) {  /* if this is relative to begin with? */
				BLI_strncpy(lib->name, lib->filepath, sizeof(lib->name));
				BLI_path_rel(lib->name, basepath);
			}
		}
	}
}

/* ***************** READ GROUP *************** */

static void direct_link_group(FileData *fd, Group *group)
{
	link_list(fd, &group->gobject);

	group->preview = direct_link_preview_image(fd, group->preview);
}

static void lib_link_group(FileData *fd, Main *bmain)
{
	for (Group *group = bmain->group.first; group; group = group->id.next) {
		if (group->id.tag & LIB_TAG_NEED_LINK) {
			IDP_LibLinkProperty(group->id.properties, fd);

			bool add_us = false;

			for (GroupObject *go = group->gobject.first; go; go = go->next) {
				go->ob = newlibadr_real_us(fd, group->id.lib, go->ob);
				if (go->ob) {
					go->ob->flag |= OB_FROMGROUP;
					/* if group has an object, it increments user... */
					add_us = true;
				}
			}
			if (add_us) {
				id_us_ensure_real(&group->id);
			}
			BKE_group_object_unlink(bmain, group, NULL, NULL, NULL);    /* removes NULL entries */

			group->id.tag &= ~LIB_TAG_NEED_LINK;
		}
	}
}

/* ************** GENERAL & MAIN ******************** */


static const char *dataname(short id_code)
{
	switch (id_code) {
		case ID_OB: return "Data from OB";
		case ID_ME: return "Data from ME";
		case ID_SCE: return "Data from SCE";
		case ID_MA: return "Data from MA";
		case ID_TE: return "Data from TE";
		case ID_CU: return "Data from CU";
		case ID_GR: return "Data from GR";
		case ID_LI: return "Data from LI";
		case ID_IM: return "Data from IM";
		case ID_LA: return "Data from LA";
		case ID_CA: return "Data from CA";
		case ID_WO: return "Data from WO";
		case ID_SCR: return "Data from SCR";
		case ID_VF: return "Data from VF";
		case ID_TXT: return "Data from TXT";
		case ID_WM: return "Data from WM";
		case ID_CF: return "Data from CF";
	}
	return "Data from Lib Block";

}

static BHead *read_data_into_oldnewmap(FileData *fd, BHead *bhead, const char *allocname)
{
	bhead = blo_nextbhead(fd, bhead);

	while (bhead && bhead->code == DATA) {
		void *data;
#if 0
		/* XXX DUMB DEBUGGING OPTION TO GIVE NAMES for guarded malloc errors */
		short *sp = fd->filesdna->structs[bhead->SDNAnr];
		char *tmp = malloc(100);
		allocname = fd->filesdna->types[sp[0]];
		strcpy(tmp, allocname);
		data = read_struct(fd, bhead, tmp);
#else
		data = read_struct(fd, bhead, allocname);
#endif

		if (data) {
			oldnewmap_insert(fd->datamap, bhead->old, data, 0);
		}

		bhead = blo_nextbhead(fd, bhead);
	}

	return bhead;
}

static BHead *read_libblock(FileData *fd, Main *main, BHead *bhead, const int tag, ID **r_id)
{
	/* this routine reads a libblock and its direct data. Use link functions to connect it all
	 */
	ID *id;
	ListBase *lb;
	const char *allocname;
	bool wrong_id = false;

	/* In undo case, most libs and linked data should be kept as is from previous state (see BLO_read_from_memfile).
	 * However, some needed by the snapshot being read may have been removed in previous one, and would go missing.
	 * This leads e.g. to desappearing objects in some undo/redo case, see T34446.
	 * That means we have to carefully check whether current lib or libdata already exits in old main, if it does
	 * we merely copy it over into new main area, otherwise we have to do a full read of that bhead... */
	if (fd->memfile && ELEM(bhead->code, ID_LI, ID_ID)) {
		const char *idname = bhead_id_name(fd, bhead);

		DEBUG_PRINTF("Checking %s...\n", idname);

		if (bhead->code == ID_LI) {
			Main *libmain = fd->old_mainlist->first;
			/* Skip oldmain itself... */
			for (libmain = libmain->next; libmain; libmain = libmain->next) {
				DEBUG_PRINTF("... against %s: ", libmain->curlib ? libmain->curlib->id.name : "<NULL>");
				if (libmain->curlib && STREQ(idname, libmain->curlib->id.name)) {
					Main *oldmain = fd->old_mainlist->first;
					DEBUG_PRINTF("FOUND!\n");
					/* In case of a library, we need to re-add its main to fd->mainlist, because if we have later
					 * a missing ID_ID, we need to get the correct lib it is linked to!
					 * Order is crucial, we cannot bulk-add it in BLO_read_from_memfile() like it used to be... */
					BLI_remlink(fd->old_mainlist, libmain);
					BLI_remlink_safe(&oldmain->library, libmain->curlib);
					BLI_addtail(fd->mainlist, libmain);
					BLI_addtail(&main->library, libmain->curlib);

					if (r_id) {
						*r_id = NULL;  /* Just in case... */
					}
					return blo_nextbhead(fd, bhead);
				}
				DEBUG_PRINTF("nothing...\n");
			}
		}
		else {
			DEBUG_PRINTF("... in %s (%s): ", main->curlib ? main->curlib->id.name : "<NULL>", main->curlib ? main->curlib->name : "<NULL>");
			if ((id = BKE_libblock_find_name(main, GS(idname), idname + 2))) {
				DEBUG_PRINTF("FOUND!\n");
				/* Even though we found our linked ID, there is no guarantee its address is still the same... */
				if (id != bhead->old) {
					oldnewmap_insert(fd->libmap, bhead->old, id, GS(id->name));
				}

				/* No need to do anything else for ID_ID, it's assumed already present in its lib's main... */
				if (r_id) {
					*r_id = NULL;  /* Just in case... */
				}
				return blo_nextbhead(fd, bhead);
			}
			DEBUG_PRINTF("nothing...\n");
		}
	}

	/* read libblock */
	id = read_struct(fd, bhead, "lib block");

	if (id) {
		const short idcode = GS(id->name);
		/* do after read_struct, for dna reconstruct */
		lb = which_libbase(main, idcode);
		if (lb) {
			oldnewmap_insert(fd->libmap, bhead->old, id, bhead->code);  /* for ID_ID check */
			BLI_addtail(lb, id);
		}
		else {
			/* unknown ID type */
			printf("%s: unknown id code '%c%c'\n", __func__, (idcode & 0xff), (idcode >> 8));
			MEM_freeN(id);
			id = NULL;
		}
	}

	if (r_id)
		*r_id = id;
	if (!id)
		return blo_nextbhead(fd, bhead);

	id->lib = main->curlib;
	id->us = ID_FAKE_USERS(id);
	id->icon_id = 0;
	id->newid = NULL;  /* Needed because .blend may have been saved with crap value here... */
	id->recalc = 0;

	/* this case cannot be direct_linked: it's just the ID part */
	if (bhead->code == ID_ID) {
		/* That way, we know which datablock needs do_versions (required currently for linking). */
		id->tag = tag | LIB_TAG_NEED_LINK | LIB_TAG_NEW;

		return blo_nextbhead(fd, bhead);
	}

	/* need a name for the mallocN, just for debugging and sane prints on leaks */
	allocname = dataname(GS(id->name));

	/* read all data into fd->datamap */
	bhead = read_data_into_oldnewmap(fd, bhead, allocname);

	/* init pointers direct data */
	direct_link_id(fd, id);

	/* That way, we know which datablock needs do_versions (required currently for linking). */
	/* Note: doing this after driect_link_id(), which resets that field. */
	id->tag = tag | LIB_TAG_NEED_LINK | LIB_TAG_NEW;

	switch (GS(id->name)) {
		case ID_WM:
			direct_link_windowmanager(fd, (wmWindowManager *)id);
			break;
		case ID_SCR:
			wrong_id = direct_link_screen(fd, (bScreen *)id);
			break;
		case ID_SCE:
			direct_link_scene(fd, (Scene *)id);
			break;
		case ID_OB:
			direct_link_object(fd, (Object *)id);
			break;
		case ID_ME:
			direct_link_mesh(fd, (Mesh *)id);
			break;
		case ID_CU:
			direct_link_curve(fd, (Curve *)id);
			break;
		case ID_MA:
			direct_link_material(fd, (Material *)id);
			break;
		case ID_TE:
			direct_link_texture(fd, (Tex *)id);
			break;
		case ID_IM:
			direct_link_image(fd, (Image *)id);
			break;
		case ID_LA:
			direct_link_lamp(fd, (Lamp *)id);
			break;
		case ID_VF:
			direct_link_vfont(fd, (VFont *)id);
			break;
		case ID_TXT:
			direct_link_text(fd, (Text *)id);
			break;
		case ID_WO:
			direct_link_world(fd, (World *)id);
			break;
		case ID_LI:
			direct_link_library(fd, (Library *)id, main);
			break;
		case ID_CA:
			direct_link_camera(fd, (Camera *)id);
			break;
		case ID_GR:
			direct_link_group(fd, (Group *)id);
			break;
		default:
			break;
	}

	oldnewmap_free_unused(fd->datamap);
	oldnewmap_clear(fd->datamap);

	if (wrong_id) {
		BKE_libblock_free(main, id);
	}

	return (bhead);
}

/* note, this has to be kept for reading older files... */
/* also version info is written here */
static BHead *read_global(BlendFileData *bfd, FileData *fd, BHead *bhead)
{
	FileGlobal *fg = read_struct(fd, bhead, "Global");

	/* copy to bfd handle */
	bfd->main->subversionfile = fg->subversion;
	bfd->main->minversionfile = fg->minversion;
	bfd->main->minsubversionfile = fg->minsubversion;
	bfd->main->build_commit_timestamp = fg->build_commit_timestamp;
	BLI_strncpy(bfd->main->build_hash, fg->build_hash, sizeof(bfd->main->build_hash));

	bfd->fileflags = fg->fileflags;
	bfd->globalf = fg->globalf;
	BLI_strncpy(bfd->filename, fg->filename, sizeof(bfd->filename));

	/* error in 2.65 and older: main->name was not set if you save from startup (not after loading file) */
	if (bfd->filename[0] == 0) {
		if (fd->fileversion < 265 || (fd->fileversion == 265 && fg->subversion < 1))
			if ((G.fileflags & G_FILE_RECOVER) == 0)
				BLI_strncpy(bfd->filename, BKE_main_blendfile_path(bfd->main), sizeof(bfd->filename));

		/* early 2.50 version patch - filename not in FileGlobal struct at all */
		if (fd->fileversion <= 250)
			BLI_strncpy(bfd->filename, BKE_main_blendfile_path(bfd->main), sizeof(bfd->filename));
	}

	if (G.fileflags & G_FILE_RECOVER)
		BLI_strncpy(fd->relabase, fg->filename, sizeof(fd->relabase));

	bfd->curscreen = fg->curscreen;
	bfd->curscene = fg->curscene;

	MEM_freeN(fg);

	fd->globalf = bfd->globalf;
	fd->fileflags = bfd->fileflags;

	return blo_nextbhead(fd, bhead);
}

/* note, this has to be kept for reading older files... */
static void link_global(FileData *fd, BlendFileData *bfd)
{
	bfd->curscreen = newlibadr(fd, NULL, bfd->curscreen);
	bfd->curscene = newlibadr(fd, NULL, bfd->curscene);
	// this happens in files older than 2.35
	if (bfd->curscene == NULL) {
		if (bfd->curscreen) bfd->curscene = bfd->curscreen->scene;
	}
}

static void convert_tface_mt(FileData *fd, Main *main)
{
	Main *gmain;

	/* this is a delayed do_version (so it can create new materials) */
	if (main->versionfile < 259 || (main->versionfile == 259 && main->subversionfile < 3)) {
		//XXX hack, material.c uses G_MAIN all over the place, instead of main
		/* XXX NOTE: this hack should not beneeded anymore... but will check/remove this in 2.8 code rather */
		// temporarily set G_MAIN to the current main
		gmain = G_MAIN;
		G_MAIN = main;

		if (!(do_version_tface(main))) {
			BKE_report(fd->reports, RPT_WARNING, "Texface conversion problem (see error in console)");
		}

		//XXX hack, material.c uses G_MAIN allover the place, instead of main
		G_MAIN = gmain;
	}
}

/* initialize userdef with non-UI dependency stuff */
/* other initializers (such as theme color defaults) go to resources.c */
static void do_versions_userdef(FileData *fd, BlendFileData *bfd)
{
	UserDef *user = bfd->user;

	if (user == NULL) return;

	if (!DNA_struct_elem_find(fd->filesdna, "UserDef", "WalkNavigation", "walk_navigation")) {
		user->walk_navigation.mouse_speed = 1.0f;
		user->walk_navigation.walk_speed = 2.5f;       /* m/s */
		user->walk_navigation.walk_speed_factor = 5.0f;
		user->walk_navigation.view_height =  1.6f;   /* m */
		user->walk_navigation.jump_height = 0.4f;      /* m */
		user->walk_navigation.teleport_time = 0.2f; /* s */
	}
}

static void do_versions(FileData *fd, Library *lib, Main *main)
{
	/* WATCH IT!!!: pointers from libdata have not been converted */

	if (G.debug & G_DEBUG) {
		char build_commit_datetime[32];
		time_t temp_time = main->build_commit_timestamp;
		struct tm *tm = (temp_time) ? gmtime(&temp_time) : NULL;
		if (LIKELY(tm)) {
			strftime(build_commit_datetime, sizeof(build_commit_datetime), "%Y-%m-%d %H:%M", tm);
		}
		else {
			BLI_strncpy(build_commit_datetime, "unknown", sizeof(build_commit_datetime));
		}

		printf("read file %s\n  Version %d sub %d date %s hash %s\n",
		       fd->relabase, main->versionfile, main->subversionfile,
		       build_commit_datetime, main->build_hash);
	}


	/* WATCH IT!!!: pointers from libdata have not been converted yet here! */
	/* WATCH IT 2!: Userdef struct init see do_versions_userdef() above! */

	/* don't forget to set version number in BKE_blender_version.h! */
}

static void do_versions_after_linking(Main *main)
{
//	printf("%s for %s (%s), %d.%d\n", __func__, main->curlib ? main->curlib->name : main->name,
//	       main->curlib ? "LIB" : "MAIN", main->versionfile, main->subversionfile);

}

static void lib_link_all(FileData *fd, Main *main)
{
	oldnewmap_sort(fd);

	/* No load UI for undo memfiles */
	if (fd->memfile == NULL) {
		lib_link_windowmanager(fd, main);
	}
	/* DO NOT skip screens here, 3Dview may contains pointers to other ID data (like bgpic)! See T41411. */
	lib_link_screen(fd, main);
	lib_link_scene(fd, main);
	lib_link_object(fd, main);
	lib_link_mesh(fd, main);
	lib_link_curve(fd, main);
	lib_link_material(fd, main);
	lib_link_texture(fd, main);
	lib_link_image(fd, main);
	lib_link_world(fd, main);
	lib_link_lamp(fd, main);
	lib_link_text(fd, main);
	lib_link_camera(fd, main);
	lib_link_group(fd, main);
	lib_link_vfont(fd, main);
	lib_link_cachefiles(fd, main);

	lib_link_library(fd, main);    /* only init users */
}

static void direct_link_keymapitem(FileData *fd, wmKeyMapItem *kmi)
{
	kmi->properties = newdataadr(fd, kmi->properties);
	IDP_DirectLinkGroup_OrFree(&kmi->properties, (fd->flags & FD_FLAGS_SWITCH_ENDIAN), fd);
	kmi->ptr = NULL;
	kmi->flag &= ~KMI_UPDATE;
}

static BHead *read_userdef(BlendFileData *bfd, FileData *fd, BHead *bhead)
{
	UserDef *user;
	wmKeyMap *keymap;
	wmKeyMapItem *kmi;
	wmKeyMapDiffItem *kmdi;
	bAddon *addon;

	bfd->user = user = read_struct(fd, bhead, "user def");

	/* User struct has separate do-version handling */
	user->versionfile = bfd->main->versionfile;
	user->subversionfile = bfd->main->subversionfile;

	/* read all data into fd->datamap */
	bhead = read_data_into_oldnewmap(fd, bhead, "user def");

	link_list(fd, &user->themes);
	link_list(fd, &user->user_keymaps);
	link_list(fd, &user->addons);
	link_list(fd, &user->autoexec_paths);

	for (keymap = user->user_keymaps.first; keymap; keymap = keymap->next) {
		keymap->modal_items = NULL;
		keymap->poll = NULL;
		keymap->flag &= ~KEYMAP_UPDATE;

		link_list(fd, &keymap->diff_items);
		link_list(fd, &keymap->items);

		for (kmdi = keymap->diff_items.first; kmdi; kmdi = kmdi->next) {
			kmdi->remove_item = newdataadr(fd, kmdi->remove_item);
			kmdi->add_item = newdataadr(fd, kmdi->add_item);

			if (kmdi->remove_item)
				direct_link_keymapitem(fd, kmdi->remove_item);
			if (kmdi->add_item)
				direct_link_keymapitem(fd, kmdi->add_item);
		}

		for (kmi = keymap->items.first; kmi; kmi = kmi->next)
			direct_link_keymapitem(fd, kmi);
	}

	for (addon = user->addons.first; addon; addon = addon->next) {
		addon->prop = newdataadr(fd, addon->prop);
		IDP_DirectLinkGroup_OrFree(&addon->prop, (fd->flags & FD_FLAGS_SWITCH_ENDIAN), fd);
	}

	// XXX
	user->uifonts.first = user->uifonts.last = NULL;

	link_list(fd, &user->uistyles);

	/* free fd->datamap again */
	oldnewmap_free_unused(fd->datamap);
	oldnewmap_clear(fd->datamap);

	return bhead;
}

BlendFileData *blo_read_file_internal(FileData *fd, const char *filepath)
{
	BHead *bhead = blo_firstbhead(fd);
	BlendFileData *bfd;
	ListBase mainlist = {NULL, NULL};

	bfd = MEM_callocN(sizeof(BlendFileData), "blendfiledata");
	bfd->main = BKE_main_new();
	BLI_addtail(&mainlist, bfd->main);
	fd->mainlist = &mainlist;

	bfd->main->versionfile = fd->fileversion;

	bfd->type = BLENFILETYPE_BLEND;
	BLI_strncpy(bfd->main->name, filepath, sizeof(bfd->main->name));

	while (bhead) {
		switch (bhead->code) {
			case DATA:
			case DNA1:
			case TEST: /* used as preview since 2.5x */
			case REND:
				bhead = blo_nextbhead(fd, bhead);
				break;
			case GLOB:
				bhead = read_global(bfd, fd, bhead);
				break;
			case USER:
				if (fd->skip_flags & BLO_READ_SKIP_USERDEF) {
					bhead = blo_nextbhead(fd, bhead);
				}
				else {
					bhead = read_userdef(bfd, fd, bhead);
				}
				break;
			case ENDB:
				bhead = NULL;
				break;

			case ID_ID:
				/* Always adds to the most recently loaded ID_LI block, see direct_link_library.
				 * This is part of the file format definition. */
				if (fd->skip_flags & BLO_READ_SKIP_DATA) {
					bhead = blo_nextbhead(fd, bhead);
				}
				else {
					bhead = read_libblock(fd, mainlist.last, bhead, LIB_TAG_ID_ID | LIB_TAG_EXTERN, NULL);
				}
				break;
				/* in 2.50+ files, the file identifier for screens is patched, forward compatibility */
			case ID_SCRN:
				bhead->code = ID_SCR;
				/* pass on to default */
				ATTR_FALLTHROUGH;
			default:
				if (fd->skip_flags & BLO_READ_SKIP_DATA) {
					bhead = blo_nextbhead(fd, bhead);
				}
				else {
					bhead = read_libblock(fd, bfd->main, bhead, LIB_TAG_LOCAL, NULL);
				}
		}
	}

	/* do before read_libraries, but skip undo case */
	if (fd->memfile == NULL) {
		do_versions(fd, NULL, bfd->main);
		do_versions_userdef(fd, bfd);
	}

	read_libraries(fd, &mainlist);

	blo_join_main(&mainlist);

	lib_link_all(fd, bfd->main);

	/* Skip in undo case. */
	if (fd->memfile == NULL) {
		/* Yep, second splitting... but this is a very cheap operation, so no big deal. */
		blo_split_main(&mainlist, bfd->main);
		for (Main *mainvar = mainlist.first; mainvar; mainvar = mainvar->next) {
			BLI_assert(mainvar->versionfile != 0);
			do_versions_after_linking(mainvar);
		}
		blo_join_main(&mainlist);
	}

	BKE_main_id_tag_all(bfd->main, LIB_TAG_NEW, false);

	if (fd->memfile != NULL) {
		/* In undo/redo case, we do a whole lot of magic tricks to avoid having to re-read linked datablocks from
		 * libraries (since those are not supposed to change).
		 * Unfortunately, that means that we do not reset their user count, however we do increase that one when
		 * doing lib_link on local IDs using linked ones.
		 * There is no real way to predict amount of changes here, so we have to fully redo refcounting . */
		BLE_main_id_refcount_recompute(bfd->main, true);
	}

	fix_relpaths_library(fd->relabase, bfd->main); /* make all relative paths, relative to the open blend file */

	link_global(fd, bfd);   /* as last */

	fd->mainlist = NULL;  /* Safety, this is local variable, shall not be used afterward. */

	return bfd;
}

/* ************* APPEND LIBRARY ************** */

struct BHeadSort {
	BHead *bhead;
	const void *old;
};

static int verg_bheadsort(const void *v1, const void *v2)
{
	const struct BHeadSort *x1 = v1, *x2 = v2;

	if (x1->old > x2->old) return 1;
	else if (x1->old < x2->old) return -1;
	return 0;
}

static void sort_bhead_old_map(FileData *fd)
{
	BHead *bhead;
	struct BHeadSort *bhs;
	int tot = 0;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead))
		tot++;

	fd->tot_bheadmap = tot;
	if (tot == 0) return;

	bhs = fd->bheadmap = MEM_malloc_arrayN(tot, sizeof(struct BHeadSort), "BHeadSort");

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead), bhs++) {
		bhs->bhead = bhead;
		bhs->old = bhead->old;
	}

	qsort(fd->bheadmap, tot, sizeof(struct BHeadSort), verg_bheadsort);
}

static BHead *find_previous_lib(FileData *fd, BHead *bhead)
{
	/* skip library datablocks in undo, see comment in read_libblock */
	if (fd->memfile)
		return NULL;

	for (; bhead; bhead = blo_prevbhead(fd, bhead)) {
		if (bhead->code == ID_LI)
			break;
	}

	return bhead;
}

static BHead *find_bhead(FileData *fd, void *old)
{
#if 0
	BHead *bhead;
#endif
	struct BHeadSort *bhs, bhs_s;

	if (!old)
		return NULL;

	if (fd->bheadmap == NULL)
		sort_bhead_old_map(fd);

	bhs_s.old = old;
	bhs = bsearch(&bhs_s, fd->bheadmap, fd->tot_bheadmap, sizeof(struct BHeadSort), verg_bheadsort);

	if (bhs)
		return bhs->bhead;

#if 0
	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (bhead->old == old)
			return bhead;
	}
#endif

	return NULL;
}

static BHead *find_bhead_from_code_name(FileData *fd, const short idcode, const char *name)
{
#ifdef USE_GHASH_BHEAD

	char idname_full[MAX_ID_NAME];

	*((short *)idname_full) = idcode;
	BLI_strncpy(idname_full + 2, name, sizeof(idname_full) - 2);

	return BLI_ghash_lookup(fd->bhead_idname_hash, idname_full);

#else
	BHead *bhead;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		if (bhead->code == idcode) {
			const char *idname_test = bhead_id_name(fd, bhead);
			if (STREQ(idname_test + 2, name)) {
				return bhead;
			}
		}
		else if (bhead->code == ENDB) {
			break;
		}
	}

	return NULL;
#endif
}

static BHead *find_bhead_from_idname(FileData *fd, const char *idname)
{
#ifdef USE_GHASH_BHEAD
	return BLI_ghash_lookup(fd->bhead_idname_hash, idname);
#else
	return find_bhead_from_code_name(fd, GS(idname), idname + 2);
#endif
}

static ID *is_yet_read(FileData *fd, Main *mainvar, BHead *bhead)
{
	const char *idname = bhead_id_name(fd, bhead);
	/* which_libbase can be NULL, intentionally not using idname+2 */
	return BLI_findstring(which_libbase(mainvar, GS(idname)), idname, offsetof(ID, name));
}

static void expand_doit_library(void *fdhandle, Main *mainvar, void *old)
{
	BHead *bhead;
	FileData *fd = fdhandle;
	ID *id;

	bhead = find_bhead(fd, old);
	if (bhead) {
		/* from another library? */
		if (bhead->code == ID_ID) {
			BHead *bheadlib = find_previous_lib(fd, bhead);

			if (bheadlib) {
				Library *lib = read_struct(fd, bheadlib, "Library");
				Main *ptr = blo_find_main(fd, lib->name, fd->relabase);

				if (ptr->curlib == NULL) {
					const char *idname = bhead_id_name(fd, bhead);

					blo_reportf_wrap(fd->reports, RPT_WARNING, TIP_("LIB: Data refers to main .blend file: '%s' from %s"),
					                 idname, mainvar->curlib->filepath);
					return;
				}
				else
					id = is_yet_read(fd, ptr, bhead);

				if (id == NULL) {
					read_libblock(fd, ptr, bhead, LIB_TAG_ID_ID | LIB_TAG_INDIRECT, NULL);
					// commented because this can print way too much
					// if (G.debug & G_DEBUG) printf("expand_doit: other lib %s\n", lib->name);

					/* for outliner dependency only */
					ptr->curlib->parent = mainvar->curlib;
				}
				else {
					/* The line below was commented by Ton (I assume), when Hos did the merge from the orange branch. rev 6568
					 * This line is NEEDED, the case is that you have 3 blend files...
					 * user.blend, lib.blend and lib_indirect.blend - if user.blend already references a "tree" from
					 * lib_indirect.blend but lib.blend does too, linking in a Scene or Group from lib.blend can result in an
					 * empty without the dupli group referenced. Once you save and reload the group would appear. - Campbell */
					/* This crashes files, must look further into it */

					/* Update: the issue is that in file reading, the oldnewmap is OK, but for existing data, it has to be
					 * inserted in the map to be found! */

					/* Update: previously it was checking for id->tag & LIB_TAG_PRE_EXISTING, however that
					 * does not affect file reading. For file reading we may need to insert it into the libmap as well,
					 * because you might have two files indirectly linking the same datablock, and in that case
					 * we need this in the libmap for the fd of both those files.
					 *
					 * The crash that this check avoided earlier was because bhead->code wasn't properly passed in, making
					 * change_idid_adr not detect the mapping was for an ID_ID datablock. */
					oldnewmap_insert(fd->libmap, bhead->old, id, bhead->code);
					change_idid_adr_fd(fd, bhead->old, id);

					// commented because this can print way too much
					// if (G.debug & G_DEBUG) printf("expand_doit: already linked: %s lib: %s\n", id->name, lib->name);
				}

				MEM_freeN(lib);
			}
		}
		else {
			id = is_yet_read(fd, mainvar, bhead);
			if (id == NULL) {
				read_libblock(fd, mainvar, bhead, LIB_TAG_NEED_EXPAND | LIB_TAG_INDIRECT, NULL);
			}
			else {
				/* this is actually only needed on UI call? when ID was already read before, and another append
				 * happens which invokes same ID... in that case the lookup table needs this entry */
				oldnewmap_insert(fd->libmap, bhead->old, id, bhead->code);
				// commented because this can print way too much
				// if (G.debug & G_DEBUG) printf("expand: already read %s\n", id->name);
			}
		}
	}
}

static BLOExpandDoitCallback expand_doit;

static void expand_idprops(FileData *fd, Main *mainvar, IDProperty *prop)
{
	if (!prop)
		return;

	switch (prop->type) {
		case IDP_ID:
			expand_doit(fd, mainvar, IDP_Id(prop));
			break;
		case IDP_IDPARRAY:
		{
			IDProperty *idp_array = IDP_IDPArray(prop);
			for (int i = 0; i < prop->len; i++) {
				expand_idprops(fd, mainvar, &idp_array[i]);
			}
			break;
		}
		case IDP_GROUP:
			for (IDProperty *loop = prop->data.group.first; loop; loop = loop->next) {
				expand_idprops(fd, mainvar, loop);
			}
			break;
	}
}

static void expand_group(FileData *fd, Main *mainvar, Group *group)
{
	GroupObject *go;

	for (go = group->gobject.first; go; go = go->next) {
		expand_doit(fd, mainvar, go->ob);
	}
}

static void expand_texture(FileData *fd, Main *mainvar, Tex *tex)
{
	expand_doit(fd, mainvar, tex->ima);

}

static void expand_material(FileData *fd, Main *mainvar, Material *ma)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		if (ma->mtex[a]) {
			expand_doit(fd, mainvar, ma->mtex[a]->tex);
			expand_doit(fd, mainvar, ma->mtex[a]->object);
		}
	}

	if (ma->group)
		expand_doit(fd, mainvar, ma->group);
}

static void expand_lamp(FileData *fd, Main *mainvar, Lamp *la)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		if (la->mtex[a]) {
			expand_doit(fd, mainvar, la->mtex[a]->tex);
			expand_doit(fd, mainvar, la->mtex[a]->object);
		}
	}

}

static void expand_world(FileData *fd, Main *mainvar, World *wrld)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		if (wrld->mtex[a]) {
			expand_doit(fd, mainvar, wrld->mtex[a]->tex);
			expand_doit(fd, mainvar, wrld->mtex[a]->object);
		}
	}

}

static void expand_curve(FileData *fd, Main *mainvar, Curve *cu)
{
	int a;

	for (a = 0; a < cu->totcol; a++) {
		expand_doit(fd, mainvar, cu->mat[a]);
	}

	expand_doit(fd, mainvar, cu->vfont);
	expand_doit(fd, mainvar, cu->vfontb);
	expand_doit(fd, mainvar, cu->vfonti);
	expand_doit(fd, mainvar, cu->vfontbi);
	expand_doit(fd, mainvar, cu->bevobj);
	expand_doit(fd, mainvar, cu->taperobj);
	expand_doit(fd, mainvar, cu->textoncurve);

}

static void expand_mesh(FileData *fd, Main *mainvar, Mesh *me)
{
	CustomDataLayer *layer;
	int a, i;

	for (a = 0; a < me->totcol; a++) {
		expand_doit(fd, mainvar, me->mat[a]);
	}

	expand_doit(fd, mainvar, me->texcomesh);

	if (me->mface && !me->mpoly) {
		MTFace *mtf;

		for (a = 0; a < me->fdata.totlayer; a++) {
			layer = &me->fdata.layers[a];

			if (layer->type == CD_MTFACE) {
				mtf = (MTFace *)layer->data;
				for (i = 0; i < me->totface; i++, mtf++) {
					if (mtf->tpage)
						expand_doit(fd, mainvar, mtf->tpage);
				}
			}
		}
	}
	else {
		MTexPoly *mtp;

		for (a = 0; a < me->pdata.totlayer; a++) {
			layer = &me->pdata.layers[a];

			if (layer->type == CD_MTEXPOLY) {
				mtp = (MTexPoly *)layer->data;

				for (i = 0; i < me->totpoly; i++, mtp++) {
					if (mtp->tpage)
						expand_doit(fd, mainvar, mtp->tpage);
				}
			}
		}
	}
}

static void expand_object_expandModifiers(
        void *userData, Object *UNUSED(ob), ID **idpoin, int UNUSED(cb_flag))
{
	struct { FileData *fd; Main *mainvar; } *data = userData;

	FileData *fd = data->fd;
	Main *mainvar = data->mainvar;

	expand_doit(fd, mainvar, *idpoin);
}

static void expand_object(FileData *fd, Main *mainvar, Object *ob)
{
	int a;

	expand_doit(fd, mainvar, ob->data);

	/* expand_object_expandModifier() */
	if (ob->modifiers.first) {
		struct { FileData *fd; Main *mainvar; } data;
		data.fd = fd;
		data.mainvar = mainvar;

		modifiers_foreachIDLink(ob, expand_object_expandModifiers, (void *)&data);
	}

	for (a = 0; a < ob->totcol; a++) {
		expand_doit(fd, mainvar, ob->mat[a]);
	}

	if (ob->dup_group)
		expand_doit(fd, mainvar, ob->dup_group);

	if (ob->proxy)
		expand_doit(fd, mainvar, ob->proxy);
	if (ob->proxy_group)
		expand_doit(fd, mainvar, ob->proxy_group);

	if (ob->rigidbody_constraint) {
		expand_doit(fd, mainvar, ob->rigidbody_constraint->ob1);
		expand_doit(fd, mainvar, ob->rigidbody_constraint->ob2);
	}

	if (ob->currentlod) {
		LodLevel *level;
		for (level = ob->lodlevels.first; level; level = level->next) {
			expand_doit(fd, mainvar, level->source);
		}
	}
}

static void expand_scene(FileData *fd, Main *mainvar, Scene *sce)
{
	Base *base;

	for (base = sce->base.first; base; base = base->next) {
		expand_doit(fd, mainvar, base->object);
	}
	expand_doit(fd, mainvar, sce->camera);
	expand_doit(fd, mainvar, sce->world);

	if (sce->set)
		expand_doit(fd, mainvar, sce->set);

	if (sce->rigidbody_world) {
		expand_doit(fd, mainvar, sce->rigidbody_world->group);
		expand_doit(fd, mainvar, sce->rigidbody_world->constraints);
	}

}

static void expand_camera(FileData *fd, Main *mainvar, Camera *ca)
{

}

static void expand_cachefile(FileData *fd, Main *mainvar, CacheFile *cache_file)
{

}


/**
 * Set the callback func used over all ID data found by \a BLO_expand_main func.
 *
 * \param expand_doit_func: Called for each ID block it finds.
 */
void BLO_main_expander(BLOExpandDoitCallback expand_doit_func)
{
	expand_doit = expand_doit_func;
}

/**
 * Loop over all ID data in Main to mark relations.
 * Set (id->tag & LIB_TAG_NEED_EXPAND) to mark expanding. Flags get cleared after expanding.
 *
 * \param fdhandle: usually filedata, or own handle.
 * \param mainvar: the Main database to expand.
 */
void BLO_expand_main(void *fdhandle, Main *mainvar)
{
	ListBase *lbarray[MAX_LIBARRAY];
	FileData *fd = fdhandle;
	ID *id;
	int a;
	bool do_it = true;

	while (do_it) {
		do_it = false;

		a = set_listbasepointers(mainvar, lbarray);
		while (a--) {
			id = lbarray[a]->first;
			while (id) {
				if (id->tag & LIB_TAG_NEED_EXPAND) {
					expand_idprops(fd, mainvar, id->properties);

					switch (GS(id->name)) {
						case ID_OB:
							expand_object(fd, mainvar, (Object *)id);
							break;
						case ID_ME:
							expand_mesh(fd, mainvar, (Mesh *)id);
							break;
						case ID_CU:
							expand_curve(fd, mainvar, (Curve *)id);
							break;
						case ID_SCE:
							expand_scene(fd, mainvar, (Scene *)id);
							break;
						case ID_MA:
							expand_material(fd, mainvar, (Material *)id);
							break;
						case ID_TE:
							expand_texture(fd, mainvar, (Tex *)id);
							break;
						case ID_WO:
							expand_world(fd, mainvar, (World *)id);
							break;
						case ID_LA:
							expand_lamp(fd, mainvar, (Lamp *)id);
							break;
						case ID_CA:
							expand_camera(fd, mainvar, (Camera *)id);
							break;
						case ID_GR:
							expand_group(fd, mainvar, (Group *)id);
							break;
						case ID_CF:
							expand_cachefile(fd, mainvar, (CacheFile *)id);
							break;
						default:
							break;
					}

					do_it = true;
					id->tag &= ~LIB_TAG_NEED_EXPAND;

				}
				id = id->next;
			}
		}
	}
}


/* ***************************** */

static bool object_in_any_scene(Main *mainvar, Object *ob)
{
	Scene *sce;

	for (sce = mainvar->scene.first; sce; sce = sce->id.next) {
		if (BKE_scene_base_find(sce, ob)) {
			return true;
		}
	}

	return false;
}

static void give_base_to_objects(Main *mainvar, Scene *scene, View3D *v3d, Library *lib, const short flag)
{
	Object *ob;
	Base *base;
	const uint active_lay = (flag & FILE_ACTIVELAY) ? BKE_screen_view3d_layer_active(v3d, scene) : 0;
	const bool is_link = (flag & FILE_LINK) != 0;

	BLI_assert(scene);

	/* give all objects which are LIB_TAG_INDIRECT a base, or for a group when *lib has been set */
	for (ob = mainvar->object.first; ob; ob = ob->id.next) {
		if ((ob->id.tag & LIB_TAG_INDIRECT) && (ob->id.tag & LIB_TAG_PRE_EXISTING) == 0) {
			bool do_it = false;

			if (ob->id.us == 0) {
				do_it = true;
			}
			else if (!is_link && (ob->id.lib == lib) && (object_in_any_scene(mainvar, ob) == 0)) {
				/* When appending, make sure any indirectly loaded objects get a base, else they cant be accessed at all
				 * (see T27437). */
				do_it = true;
			}

			if (do_it) {
				base = MEM_callocN(sizeof(Base), __func__);
				BLI_addtail(&scene->base, base);

				if (active_lay) {
					ob->lay = active_lay;
				}
				if (flag & FILE_AUTOSELECT) {
					/* Note that link_object_postprocess() already checks for FILE_AUTOSELECT flag,
					 * but it will miss objects from non-instantiated groups... */
					ob->flag |= SELECT;
					/* do NOT make base active here! screws up GUI stuff, if you want it do it on src/ level */
				}

				base->object = ob;
				base->lay = ob->lay;
				base->flag = ob->flag;

				CLAMP_MIN(ob->id.us, 0);
				id_us_plus_no_lib((ID *)ob);

				ob->id.tag &= ~LIB_TAG_INDIRECT;
				ob->id.tag |= LIB_TAG_EXTERN;
			}
		}
	}
}

static void give_base_to_groups(
        Main *mainvar, Scene *scene, View3D *v3d, Library *UNUSED(lib), const short UNUSED(flag))
{
	Group *group;
	Base *base;
	Object *ob;
	const uint active_lay = BKE_screen_view3d_layer_active(v3d, scene);

	/* give all objects which are tagged a base */
	for (group = mainvar->group.first; group; group = group->id.next) {
		if (group->id.tag & LIB_TAG_DOIT) {
			/* any indirect group should not have been tagged */
			BLI_assert((group->id.tag & LIB_TAG_INDIRECT) == 0);

			/* BKE_object_add(...) messes with the selection */
			ob = BKE_object_add_only_object(mainvar, OB_EMPTY, group->id.name + 2);
			ob->type = OB_EMPTY;
			ob->lay = active_lay;

			/* assign the base */
			base = BKE_scene_base_add(scene, ob);
			base->flag |= SELECT;
			base->object->flag = base->flag;
			scene->basact = base;

			/* assign the group */
			ob->dup_group = group;
			copy_v3_v3(ob->loc, scene->cursor);
		}
	}
}

static ID *create_placeholder(Main *mainvar, const short idcode, const char *idname, const int tag)
{
	ListBase *lb = which_libbase(mainvar, idcode);
	ID *ph_id = BKE_libblock_alloc_notest(idcode);

	*((short *)ph_id->name) = idcode;
	BLI_strncpy(ph_id->name + 2, idname, sizeof(ph_id->name) - 2);
	BKE_libblock_init_empty(ph_id);
	ph_id->lib = mainvar->curlib;
	ph_id->tag = tag | LIB_TAG_MISSING;
	ph_id->us = ID_FAKE_USERS(ph_id);
	ph_id->icon_id = 0;

	BLI_addtail(lb, ph_id);
	id_sort_by_name(lb, ph_id);

	return ph_id;
}

/* returns true if the item was found
 * but it may already have already been appended/linked */
static ID *link_named_part(
        Main *mainl, FileData *fd, const short idcode, const char *name, const int flag)
{
	BHead *bhead = find_bhead_from_code_name(fd, idcode, name);
	ID *id;

	const bool use_placeholders = (flag & BLO_LIBLINK_USE_PLACEHOLDERS) != 0;
	const bool force_indirect = (flag & BLO_LIBLINK_FORCE_INDIRECT) != 0;

	BLI_assert(BKE_idcode_is_linkable(idcode) && BKE_idcode_is_valid(idcode));

	if (bhead) {
		id = is_yet_read(fd, mainl, bhead);
		if (id == NULL) {
			/* not read yet */
			const int tag = force_indirect ? LIB_TAG_INDIRECT : LIB_TAG_EXTERN;
			read_libblock(fd, mainl, bhead, tag | LIB_TAG_NEED_EXPAND , &id);

			if (id) {
				/* sort by name in list */
				ListBase *lb = which_libbase(mainl, idcode);
				id_sort_by_name(lb, id);
			}
		}
		else {
			/* already linked */
			if (G.debug)
				printf("append: already linked\n");
			oldnewmap_insert(fd->libmap, bhead->old, id, bhead->code);
			if (!force_indirect && (id->tag & LIB_TAG_INDIRECT)) {
				id->tag &= ~LIB_TAG_INDIRECT;
				id->tag |= LIB_TAG_EXTERN;
			}
		}
	}
	else if (use_placeholders) {
		/* XXX flag part is weak! */
		id = create_placeholder(mainl, idcode, name, force_indirect ? LIB_TAG_INDIRECT : LIB_TAG_EXTERN);
	}
	else {
		id = NULL;
	}

	/* if we found the id but the id is NULL, this is really bad */
	BLI_assert(!((bhead != NULL) && (id == NULL)));

	return id;
}

static void link_object_postprocess(ID *id, Scene *scene, View3D *v3d, const int flag)
{
	if (scene) {
		Base *base;
		Object *ob;

		base = MEM_callocN(sizeof(Base), "app_nam_part");
		BLI_addtail(&scene->base, base);

		ob = (Object *)id;

		/* link at active layer (view3d if available in context, else scene one */
		if (flag & FILE_ACTIVELAY) {
			ob->lay = BKE_screen_view3d_layer_active(v3d, scene);
		}

		ob->mode = OB_MODE_OBJECT;
		base->lay = ob->lay;
		base->object = ob;
		base->flag = ob->flag;
		id_us_plus_no_lib((ID *)ob);

		if (flag & FILE_AUTOSELECT) {
			base->flag |= SELECT;
			base->object->flag = base->flag;
			/* do NOT make base active here! screws up GUI stuff, if you want it do it on src/ level */
		}
	}
}

/**
 * Simple reader for copy/paste buffers.
 */
void BLO_library_link_copypaste(Main *mainl, BlendHandle *bh)
{
	FileData *fd = (FileData *)(bh);
	BHead *bhead;

	for (bhead = blo_firstbhead(fd); bhead; bhead = blo_nextbhead(fd, bhead)) {
		ID *id = NULL;

		if (bhead->code == ENDB)
			break;
		if (ELEM(bhead->code, ID_OB, ID_GR)) {
			read_libblock(fd, mainl, bhead, LIB_TAG_NEED_EXPAND | LIB_TAG_INDIRECT, &id);
		}


		if (id) {
			/* sort by name in list */
			ListBase *lb = which_libbase(mainl, GS(id->name));
			id_sort_by_name(lb, id);

			if (bhead->code == ID_OB) {
				/* Instead of instancing Base's directly, postpone until after groups are loaded
				 * otherwise the base's flag is set incorrectly when groups are used */
				Object *ob = (Object *)id;
				ob->mode = OB_MODE_OBJECT;
				/* ensure give_base_to_objects runs on this object */
				BLI_assert(id->us == 0);
			}
		}
	}
}

static ID *link_named_part_ex(
        Main *mainl, FileData *fd, const short idcode, const char *name, const int flag,
        Scene *scene, View3D *v3d)
{
	ID *id = link_named_part(mainl, fd, idcode, name, flag);

	if (id && (GS(id->name) == ID_OB)) {    /* loose object: give a base */
		link_object_postprocess(id, scene, v3d, flag);
	}
	else if (id && (GS(id->name) == ID_GR)) {
		/* tag as needing to be instantiated */
		if (flag & FILE_GROUP_INSTANCE)
			id->tag |= LIB_TAG_DOIT;
	}

	return id;
}

/**
 * Link a named datablock from an external blend file.
 *
 * \param mainl: The main database to link from (not the active one).
 * \param bh: The blender file handle.
 * \param idcode: The kind of datablock to link.
 * \param name: The name of the datablock (without the 2 char ID prefix).
 * \return the linked ID when found.
 */
ID *BLO_library_link_named_part(Main *mainl, BlendHandle **bh, const short idcode, const char *name)
{
	FileData *fd = (FileData *)(*bh);
	return link_named_part(mainl, fd, idcode, name, 0);
}

/**
 * Link a named datablock from an external blend file.
 * Optionally instantiate the object/group in the scene when the flags are set.
 *
 * \param mainl: The main database to link from (not the active one).
 * \param bh: The blender file handle.
 * \param idcode: The kind of datablock to link.
 * \param name: The name of the datablock (without the 2 char ID prefix).
 * \param flag: Options for linking, used for instantiating.
 * \param scene: The scene in which to instantiate objects/groups (if NULL, no instantiation is done).
 * \param v3d: The active View3D (only to define active layers for instantiated objects & groups, can be NULL).
 * \return the linked ID when found.
 */
ID *BLO_library_link_named_part_ex(
        Main *mainl, BlendHandle **bh,
        const short idcode, const char *name, const int flag,
        Scene *scene, View3D *v3d)
{
	FileData *fd = (FileData *)(*bh);
	return link_named_part_ex(mainl, fd, idcode, name, flag, scene, v3d);
}

static void link_id_part(ReportList *reports, FileData *fd, Main *mainvar, ID *id, ID **r_id)
{
	BHead *bhead = NULL;
	const bool is_valid = BKE_idcode_is_linkable(GS(id->name)) || ((id->tag & LIB_TAG_EXTERN) == 0);

	if (fd) {
		bhead = find_bhead_from_idname(fd, id->name);
	}

	id->tag &= ~LIB_TAG_ID_ID;

	if (!is_valid) {
		blo_reportf_wrap(
		        reports, RPT_ERROR,
		        TIP_("LIB: %s: '%s' is directly linked from '%s' (parent '%s'), but is a non-linkable data type"),
		        BKE_idcode_to_name(GS(id->name)),
		        id->name + 2,
		        mainvar->curlib->filepath,
		        library_parent_filepath(mainvar->curlib));
	}

	if (bhead) {
		id->tag |= LIB_TAG_NEED_EXPAND;
		// printf("read lib block %s\n", id->name);
		read_libblock(fd, mainvar, bhead, id->tag, r_id);
	}
	else {
		blo_reportf_wrap(
		        reports, RPT_WARNING,
		        TIP_("LIB: %s: '%s' missing from '%s', parent '%s'"),
		        BKE_idcode_to_name(GS(id->name)),
		        id->name + 2,
		        mainvar->curlib->filepath,
		        library_parent_filepath(mainvar->curlib));

		/* Generate a placeholder for this ID (simplified version of read_libblock actually...). */
		if (r_id) {
			*r_id = is_valid ? create_placeholder(mainvar, GS(id->name), id->name + 2, id->tag) : NULL;
		}
	}
}

/* common routine to append/link something from a library */

static Main *library_link_begin(Main *mainvar, FileData **fd, const char *filepath)
{
	Main *mainl;

	(*fd)->mainlist = MEM_callocN(sizeof(ListBase), "FileData.mainlist");

	/* clear for group instantiating tag */
	BKE_main_id_tag_listbase(&(mainvar->group), LIB_TAG_DOIT, false);

	/* make mains */
	blo_split_main((*fd)->mainlist, mainvar);

	/* which one do we need? */
	mainl = blo_find_main(*fd, filepath, BKE_main_blendfile_path(mainvar));

	/* needed for do_version */
	mainl->versionfile = (*fd)->fileversion;
	read_file_version(*fd, mainl);
#ifdef USE_GHASH_BHEAD
	read_file_bhead_idname_map_create(*fd);
#endif

	return mainl;
}

/**
 * Initialize the BlendHandle for linking library data.
 *
 * \param mainvar: The current main database, e.g. G_MAIN or CTX_data_main(C).
 * \param bh: A blender file handle as returned by \a BLO_blendhandle_from_file or \a BLO_blendhandle_from_memory.
 * \param filepath: Used for relative linking, copied to the \a lib->name.
 * \return the library Main, to be passed to \a BLO_library_append_named_part as \a mainl.
 */
Main *BLO_library_link_begin(Main *mainvar, BlendHandle **bh, const char *filepath)
{
	FileData *fd = (FileData *)(*bh);
	return library_link_begin(mainvar, &fd, filepath);
}

static void split_main_newid(Main *mainptr, Main *main_newid)
{
	/* We only copy the necessary subset of data in this temp main. */
	main_newid->versionfile = mainptr->versionfile;
	main_newid->subversionfile = mainptr->subversionfile;
	BLI_strncpy(main_newid->name, mainptr->name, sizeof(main_newid->name));
	main_newid->curlib = mainptr->curlib;

	ListBase *lbarray[MAX_LIBARRAY];
	ListBase *lbarray_newid[MAX_LIBARRAY];
	int i = set_listbasepointers(mainptr, lbarray);
	set_listbasepointers(main_newid, lbarray_newid);
	while (i--) {
		BLI_listbase_clear(lbarray_newid[i]);

		for (ID *id = lbarray[i]->first, *idnext; id; id = idnext) {
			idnext = id->next;

			if (id->tag & LIB_TAG_NEW) {
				BLI_remlink(lbarray[i], id);
				BLI_addtail(lbarray_newid[i], id);
			}
		}
	}
}

/* scene and v3d may be NULL. */
static void library_link_end(Main *mainl, FileData **fd, const short flag, Scene *scene, View3D *v3d)
{
	Main *mainvar;
	Library *curlib;

	/* expander now is callback function */
	BLO_main_expander(expand_doit_library);

	/* make main consistent */
	BLO_expand_main(*fd, mainl);

	/* do this when expand found other libs */
	read_libraries(*fd, (*fd)->mainlist);

	curlib = mainl->curlib;

	/* make the lib path relative if required */
	if (flag & FILE_RELPATH) {
		/* use the full path, this could have been read by other library even */
		BLI_strncpy(curlib->name, curlib->filepath, sizeof(curlib->name));

		/* uses current .blend file as reference */
		BLI_path_rel(curlib->name, BKE_main_blendfile_path_from_global());
	}

	blo_join_main((*fd)->mainlist);
	mainvar = (*fd)->mainlist->first;
	mainl = NULL; /* blo_join_main free's mainl, cant use anymore */

	lib_link_all(*fd, mainvar);

	/* Yep, second splitting... but this is a very cheap operation, so no big deal. */
	blo_split_main((*fd)->mainlist, mainvar);
	Main main_newid = {0};
	for (mainvar = ((Main *)(*fd)->mainlist->first)->next; mainvar; mainvar = mainvar->next) {
		BLI_assert(mainvar->versionfile != 0);
		/* We need to split out IDs already existing, or they will go again through do_versions - bad, very bad! */
		split_main_newid(mainvar, &main_newid);

		do_versions_after_linking(&main_newid);

		add_main_to_main(mainvar, &main_newid);
	}
	blo_join_main((*fd)->mainlist);
	mainvar = (*fd)->mainlist->first;
	MEM_freeN((*fd)->mainlist);

	BKE_main_id_tag_all(mainvar, LIB_TAG_NEW, false);

	fix_relpaths_library(BKE_main_blendfile_path(mainvar), mainvar); /* make all relative paths, relative to the open blend file */

	/* Give a base to loose objects. If group append, do it for objects too.
	 * Only directly linked objects & groups are instantiated by `BLO_library_link_named_part_ex()` & co,
	 * here we handle indirect ones and other possible edge-cases. */
	if (scene) {
		give_base_to_objects(mainvar, scene, v3d, curlib, flag);

		if (flag & FILE_GROUP_INSTANCE) {
			give_base_to_groups(mainvar, scene, v3d, curlib, flag);
		}
	}
	else {
		/* printf("library_append_end, scene is NULL (objects wont get bases)\n"); */
	}

	/* clear group instantiating tag */
	BKE_main_id_tag_listbase(&(mainvar->group), LIB_TAG_DOIT, false);

	/* patch to prevent switch_endian happens twice */
	if ((*fd)->flags & FD_FLAGS_SWITCH_ENDIAN) {
		blo_freefiledata(*fd);
		*fd = NULL;
	}
}

/**
 * Finalize linking from a given .blend file (library).
 * Optionally instance the indirect object/group in the scene when the flags are set.
 * \note Do not use \a bh after calling this function, it may frees it.
 *
 * \param mainl: The main database to link from (not the active one).
 * \param bh: The blender file handle (WARNING! may be freed by this function!).
 * \param flag: Options for linking, used for instantiating.
 * \param scene: The scene in which to instantiate objects/groups (if NULL, no instantiation is done).
 * \param v3d: The active View3D (only to define active layers for instantiated objects & groups, can be NULL).
 */
void BLO_library_link_end(Main *mainl, BlendHandle **bh, short flag, Scene *scene, View3D *v3d)
{
	FileData *fd = (FileData *)(*bh);
	library_link_end(mainl, &fd, flag, scene, v3d);
	*bh = (BlendHandle *)fd;
}

void *BLO_library_read_struct(FileData *fd, BHead *bh, const char *blockname)
{
	return read_struct(fd, bh, blockname);
}

/* ************* READ LIBRARY ************** */

static int mainvar_id_tag_any_check(Main *mainvar, const int tag)
{
	ListBase *lbarray[MAX_LIBARRAY];
	int a;

	a = set_listbasepointers(mainvar, lbarray);
	while (a--) {
		ID *id;

		for (id = lbarray[a]->first; id; id = id->next) {
			if (id->tag & tag) {
				return true;
			}
		}
	}
	return false;
}

static void read_libraries(FileData *basefd, ListBase *mainlist)
{
	Main *mainl = mainlist->first;
	Main *mainptr;
	ListBase *lbarray[MAX_LIBARRAY];
	GHash *loaded_ids = BLI_ghash_str_new(__func__);
	int a;
	bool do_it = true;

	/* expander now is callback function */
	BLO_main_expander(expand_doit_library);

	while (do_it) {
		do_it = false;

		/* test 1: read libdata */
		mainptr = mainl->next;
		while (mainptr) {
			if (mainvar_id_tag_any_check(mainptr, LIB_TAG_ID_ID)) {
				// printf("found LIB_TAG_ID_ID %s (%s)\n", mainptr->curlib->id.name, mainptr->curlib->name);

				FileData *fd = mainptr->curlib->filedata;

				if (fd == NULL) {

					/* printf and reports for now... its important users know this */

					/* if packed file... */
					if (mainptr->curlib->packedfile) {
						PackedFile *pf = mainptr->curlib->packedfile;

						blo_reportf_wrap(
						        basefd->reports, RPT_INFO, TIP_("Read packed library:  '%s', parent '%s'"),
						        mainptr->curlib->name,
						        library_parent_filepath(mainptr->curlib));
						fd = blo_openblendermemory(pf->data, pf->size, basefd->reports);


						/* needed for library_append and read_libraries */
						BLI_strncpy(fd->relabase, mainptr->curlib->filepath, sizeof(fd->relabase));
					}
					else {
						blo_reportf_wrap(
						        basefd->reports, RPT_INFO, TIP_("Read library:  '%s', '%s', parent '%s'"),
						        mainptr->curlib->filepath,
						        mainptr->curlib->name,
						        library_parent_filepath(mainptr->curlib));
						fd = blo_openblenderfile(mainptr->curlib->filepath, basefd->reports);
					}
					/* allow typing in a new lib path */
					if (G.debug_value == -666) {
						while (fd == NULL) {
							char newlib_path[FILE_MAX] = {0};
							printf("Missing library...'\n");
							printf("	current file: %s\n", BKE_main_blendfile_path_from_global());
							printf("	absolute lib: %s\n", mainptr->curlib->filepath);
							printf("	relative lib: %s\n", mainptr->curlib->name);
							printf("  enter a new path:\n");

							if (scanf("%1023s", newlib_path) > 0) {  /* Warning, keep length in sync with FILE_MAX! */
								BLI_strncpy(mainptr->curlib->name, newlib_path, sizeof(mainptr->curlib->name));
								BLI_strncpy(mainptr->curlib->filepath, newlib_path, sizeof(mainptr->curlib->filepath));
								BLI_cleanup_path(BKE_main_blendfile_path_from_global(), mainptr->curlib->filepath);

								fd = blo_openblenderfile(mainptr->curlib->filepath, basefd->reports);

								if (fd) {
									fd->mainlist = mainlist;
									printf("found: '%s', party on macuno!\n", mainptr->curlib->filepath);
								}
							}
						}
					}

					if (fd) {
						/* share the mainlist, so all libraries are added immediately in a
						 * single list. it used to be that all FileData's had their own list,
						 * but with indirectly linking this meant we didn't catch duplicate
						 * libraries properly */
						fd->mainlist = mainlist;

						fd->reports = basefd->reports;

						if (fd->libmap)
							oldnewmap_free(fd->libmap);

						fd->libmap = oldnewmap_new();

						mainptr->curlib->filedata = fd;
						mainptr->versionfile =  fd->fileversion;

						/* subversion */
						read_file_version(fd, mainptr);
#ifdef USE_GHASH_BHEAD
						read_file_bhead_idname_map_create(fd);
#endif

					}
					else {
						mainptr->curlib->filedata = NULL;
						mainptr->curlib->id.tag |= LIB_TAG_MISSING;
						/* Set lib version to current main one... Makes assert later happy. */
						mainptr->versionfile = mainptr->curlib->versionfile = mainl->versionfile;
						mainptr->subversionfile = mainptr->curlib->subversionfile = mainl->subversionfile;
					}

					if (fd == NULL) {
						blo_reportf_wrap(basefd->reports, RPT_WARNING, TIP_("Cannot find lib '%s'"),
						                 mainptr->curlib->filepath);
					}
				}
				if (fd) {
					do_it = true;
				}
				a = set_listbasepointers(mainptr, lbarray);
				while (a--) {
					ID *id = lbarray[a]->first;
					ListBase pending_free_ids = {NULL};

					while (id) {
						ID *idn = id->next;
						if (id->tag & LIB_TAG_ID_ID) {
							BLI_remlink(lbarray[a], id);

							/* When playing with lib renaming and such, you may end with cases where you have
							 * more than one linked ID of the same data-block from same library.
							 * This is absolutely horrible, hence we use a ghash to ensure we go back to a single
							 * linked data when loading the file... */
							ID **realid = NULL;
							if (!BLI_ghash_ensure_p(loaded_ids, id->name, (void ***)&realid)) {
								link_id_part(basefd->reports, fd, mainptr, id, realid);
							}

							/* realid shall never be NULL - unless some source file/lib is broken
							 * (known case: some directly linked shapekey from a missing lib...). */
							/* BLI_assert(*realid != NULL); */

							change_idid_adr(mainlist, basefd, id, *realid);

							/* We cannot free old lib-ref placeholder ID here anymore, since we use its name
							 * as key in loaded_ids hass. */
							BLI_addtail(&pending_free_ids, id);
						}
						id = idn;
					}

					/* Clear GHash and free all lib-ref placeholders IDs of that type now. */
					BLI_ghash_clear(loaded_ids, NULL, NULL);
					BLI_freelistN(&pending_free_ids);
				}
				BLO_expand_main(fd, mainptr);
			}

			mainptr = mainptr->next;
		}
	}

	BLI_ghash_free(loaded_ids, NULL, NULL);
	loaded_ids = NULL;

	/* test if there are unread libblocks */
	/* XXX This code block is kept for 2.77, until we are sure it never gets reached anymore. Can be removed later. */
	for (mainptr = mainl->next; mainptr; mainptr = mainptr->next) {
		a = set_listbasepointers(mainptr, lbarray);
		while (a--) {
			ID *id, *idn = NULL;

			for (id = lbarray[a]->first; id; id = idn) {
				idn = id->next;
				if (id->tag & LIB_TAG_ID_ID) {
					BLI_assert(0);
					BLI_remlink(lbarray[a], id);
					blo_reportf_wrap(
					        basefd->reports, RPT_ERROR,
					        TIP_("LIB: %s: '%s' unread lib block missing from '%s', parent '%s' - "
					             "Please file a bug report if you see this message"),
					        BKE_idcode_to_name(GS(id->name)),
					        id->name + 2,
					        mainptr->curlib->filepath,
					        library_parent_filepath(mainptr->curlib));
					change_idid_adr(mainlist, basefd, id, NULL);

					MEM_freeN(id);
				}
			}
		}
	}

	/* do versions, link, and free */
	Main main_newid = {0};
	for (mainptr = mainl->next; mainptr; mainptr = mainptr->next) {
		/* some mains still have to be read, then versionfile is still zero! */
		if (mainptr->versionfile) {
			/* We need to split out IDs already existing, or they will go again through do_versions - bad, very bad! */
			split_main_newid(mainptr, &main_newid);

			if (mainptr->curlib->filedata) // can be zero... with shift+f1 append
				do_versions(mainptr->curlib->filedata, mainptr->curlib, &main_newid);
			else
				do_versions(basefd, NULL, &main_newid);

			add_main_to_main(mainptr, &main_newid);
		}

		if (mainptr->curlib->filedata)
			lib_link_all(mainptr->curlib->filedata, mainptr);

		if (mainptr->curlib->filedata) blo_freefiledata(mainptr->curlib->filedata);
		mainptr->curlib->filedata = NULL;
	}
}


/* reading runtime */

BlendFileData *blo_read_blendafterruntime(int file, const char *name, int actualsize, ReportList *reports)
{
	BlendFileData *bfd = NULL;
	FileData *fd = filedata_new();
	fd->filedes = file;
	fd->buffersize = actualsize;
	fd->read = fd_read_from_file;

	/* needed for library_append and read_libraries */
	BLI_strncpy(fd->relabase, name, sizeof(fd->relabase));

	fd = blo_decode_and_check(fd, reports);
	if (!fd)
		return NULL;

	fd->reports = reports;
	bfd = blo_read_file_internal(fd, "");
	blo_freefiledata(fd);

	return bfd;
}
