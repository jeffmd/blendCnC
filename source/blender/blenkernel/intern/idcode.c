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
 * return info about ID types
 */

/** \file blender/blenkernel/intern/idcode.c
 *  \ingroup bke
 */

#include <stdlib.h>
#include <string.h>

#include "DNA_ID.h"

#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_library.h"
#include "BKE_idcode.h"

typedef struct {
	unsigned short code;
	const char *name, *plural;

	const char *i18n_context;

	int flags;
#define IDTYPE_FLAGS_ISLINKABLE (1 << 0)
} IDType;

/* plural need to match rna_main.c's MainCollectionDef */
/* WARNING! Keep it in sync with i18n contexts in BLT_translation.h */
static IDType idtypes[] = {
	/** ID's directly below must all be in #Main, and be kept in sync with #MAX_LIBARRAY (membership, not order) */
	{ ID_CA,   "Camera",             "cameras",         BLT_I18NCONTEXT_ID_CAMERA,             IDTYPE_FLAGS_ISLINKABLE },
	{ ID_CF,   "CacheFile",          "cache_files",     BLT_I18NCONTEXT_ID_CACHEFILE,          IDTYPE_FLAGS_ISLINKABLE },
	{ ID_CU,   "Curve",              "curves",          BLT_I18NCONTEXT_ID_CURVE,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_GR,   "Group",              "groups",          BLT_I18NCONTEXT_ID_GROUP,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_IM,   "Image",              "images",          BLT_I18NCONTEXT_ID_IMAGE,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_LA,   "Lamp",               "lamps",           BLT_I18NCONTEXT_ID_LAMP,               IDTYPE_FLAGS_ISLINKABLE },
	{ ID_LI,   "Library",            "libraries",       BLT_I18NCONTEXT_ID_LIBRARY,            0                       },
	{ ID_MA,   "Material",           "materials",       BLT_I18NCONTEXT_ID_MATERIAL,           IDTYPE_FLAGS_ISLINKABLE },
	{ ID_ME,   "Mesh",               "meshes",          BLT_I18NCONTEXT_ID_MESH,               IDTYPE_FLAGS_ISLINKABLE },
	{ ID_OB,   "Object",             "objects",         BLT_I18NCONTEXT_ID_OBJECT,             IDTYPE_FLAGS_ISLINKABLE },
	{ ID_SCE,  "Scene",              "scenes",          BLT_I18NCONTEXT_ID_SCENE,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_SCR,  "Screen",             "screens",         BLT_I18NCONTEXT_ID_SCREEN,             0                       },
	{ ID_TE,   "Texture",            "textures",        BLT_I18NCONTEXT_ID_TEXTURE,            IDTYPE_FLAGS_ISLINKABLE },
	{ ID_TXT,  "Text",               "texts",           BLT_I18NCONTEXT_ID_TEXT,               IDTYPE_FLAGS_ISLINKABLE },
	{ ID_VF,   "VFont",              "fonts",           BLT_I18NCONTEXT_ID_VFONT,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_WO,   "World",              "worlds",          BLT_I18NCONTEXT_ID_WORLD,              IDTYPE_FLAGS_ISLINKABLE },
	{ ID_WM,   "WindowManager",      "window_managers", BLT_I18NCONTEXT_ID_WINDOWMANAGER,      0                       },

	/** Keep last, not an ID exactly, only include for completeness */
	{ ID_ID,   "ID",                 "ids",             BLT_I18NCONTEXT_ID_ID,                 0                       }, /* plural is fake */
};

/* -1 for ID_ID */
BLI_STATIC_ASSERT((ARRAY_SIZE(idtypes) == MAX_LIBARRAY), "Missing IDType");

static IDType *idtype_from_name(const char *str)
{
	int i = ARRAY_SIZE(idtypes);

	while (i--) {
		if (STREQ(str, idtypes[i].name)) {
			return &idtypes[i];
		}
	}

	return NULL;
}

static IDType *idtype_from_code(short idcode)
{
	int i = ARRAY_SIZE(idtypes);

	while (i--)
		if (idcode == idtypes[i].code)
			return &idtypes[i];

	return NULL;
}

/**
 * Return if the ID code is a valid ID code.
 *
 * \param idcode: The code to check.
 * \return Boolean, 0 when invalid.
 */
bool BKE_idcode_is_valid(short idcode)
{
	return idtype_from_code(idcode) ? true : false;
}

/**
 * Return non-zero when an ID type is linkable.
 *
 * \param idcode: The code to check.
 * \return Boolean, 0 when non linkable.
 */
bool BKE_idcode_is_linkable(short idcode)
{
	IDType *idt = idtype_from_code(idcode);
	BLI_assert(idt);
	return idt ? ((idt->flags & IDTYPE_FLAGS_ISLINKABLE) != 0) : false;
}

/**
 * Convert an idcode into a name.
 *
 * \param idcode: The code to convert.
 * \return A static string representing the name of
 * the code.
 */
const char *BKE_idcode_to_name(short idcode)
{
	IDType *idt = idtype_from_code(idcode);
	BLI_assert(idt);
	return idt ? idt->name : NULL;
}

/**
 * Convert a name into an idcode (ie. ID_SCE)
 *
 * \param name: The name to convert.
 * \return The code for the name, or 0 if invalid.
 */
short BKE_idcode_from_name(const char *name)
{
	IDType *idt = idtype_from_name(name);
	BLI_assert(idt);
	return idt ? idt->code : 0;
}

/**
 * Convert an idcode into an idfilter (e.g. ID_OB -> FILTER_ID_OB).
 */
int BKE_idcode_to_idfilter(const short idcode)
{
#define CASE_IDFILTER(_id) case ID_##_id: return FILTER_ID_##_id

	switch (idcode) {
		CASE_IDFILTER(CA);
		CASE_IDFILTER(CF);
		CASE_IDFILTER(CU);
		CASE_IDFILTER(GR);
		CASE_IDFILTER(IM);
		CASE_IDFILTER(LA);
		CASE_IDFILTER(MA);
		CASE_IDFILTER(ME);
		CASE_IDFILTER(OB);
		CASE_IDFILTER(SCE);
		CASE_IDFILTER(TE);
		CASE_IDFILTER(TXT);
		CASE_IDFILTER(VF);
		CASE_IDFILTER(WO);
		default:
			return 0;
	}

#undef CASE_IDFILTER
}

/**
 * Convert an idfilter into an idcode (e.g. FILTER_ID_OB -> ID_OB).
 */
short BKE_idcode_from_idfilter(const int idfilter)
{
#define CASE_IDFILTER(_id) case FILTER_ID_##_id: return ID_##_id

	switch (idfilter) {
		CASE_IDFILTER(CA);
		CASE_IDFILTER(CF);
		CASE_IDFILTER(CU);
		CASE_IDFILTER(GR);
		CASE_IDFILTER(IM);
		CASE_IDFILTER(LA);
		CASE_IDFILTER(MA);
		CASE_IDFILTER(ME);
		CASE_IDFILTER(OB);
		CASE_IDFILTER(SCE);
		CASE_IDFILTER(TE);
		CASE_IDFILTER(TXT);
		CASE_IDFILTER(VF);
		CASE_IDFILTER(WO);
		default:
			return 0;
	}

#undef CASE_IDFILTER
}

/**
 * Convert an idcode into an index (e.g. ID_OB -> INDEX_ID_OB).
 */
int BKE_idcode_to_index(const short idcode)
{
#define CASE_IDINDEX(_id) case ID_##_id: return INDEX_ID_##_id

	switch ((ID_Type)idcode) {
		CASE_IDINDEX(CA);
		CASE_IDINDEX(CF);
		CASE_IDINDEX(CU);
		CASE_IDINDEX(GR);
		CASE_IDINDEX(IM);
		CASE_IDINDEX(LA);
		CASE_IDINDEX(LI);
		CASE_IDINDEX(MA);
		CASE_IDINDEX(ME);
		CASE_IDINDEX(OB);
		CASE_IDINDEX(SCE);
		CASE_IDINDEX(SCR);
		CASE_IDINDEX(TE);
		CASE_IDINDEX(TXT);
		CASE_IDINDEX(VF);
		CASE_IDINDEX(WM);
		CASE_IDINDEX(WO);
	}

	BLI_assert(0);
	return -1;

#undef CASE_IDINDEX
}

/**
 * Convert an idcode into a name (plural).
 *
 * \param idcode: The code to convert.
 * \return A static string representing the name of
 * the code.
 */
const char *BKE_idcode_to_name_plural(short idcode)
{
	IDType *idt = idtype_from_code(idcode);
	BLI_assert(idt);
	return idt ? idt->plural : NULL;
}

/**
 * Convert an idcode into its translations' context.
 *
 * \param idcode: The code to convert.
 * \return A static string representing the i18n context of the code.
 */
const char *BKE_idcode_to_translation_context(short idcode)
{
	IDType *idt = idtype_from_code(idcode);
	BLI_assert(idt);
	return idt ? idt->i18n_context : BLT_I18NCONTEXT_DEFAULT;
}

/**
 * Return an ID code and steps the index forward 1.
 *
 * \param index: start as 0.
 * \return the code, 0 when all codes have been returned.
 */
short BKE_idcode_iter_step(int *index)
{
	return (*index < ARRAY_SIZE(idtypes)) ? idtypes[(*index)++].code : 0;
}
