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
 * util.c
 */

/** \file blender/imbuf/intern/util.c
 *  \ingroup imbuf
 */


#ifdef _WIN32
#  include <io.h>
#endif

#include <stdlib.h>

#include "BLI_utildefines.h"
#include "BLI_path_util.h"
#include "BLI_fileops.h"
#include "BLI_string.h"

#include "imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_imbuf.h"
#include "IMB_filetype.h"


#define UTIL_DEBUG 0

const char *imb_ext_image[] = {
	".png",
	".tga",
	".bmp",
	".jpg", ".jpeg",
	".sgi", ".rgb", ".rgba",
#ifdef WITH_TIFF
	".tif", ".tiff", ".tx",
#endif
#ifdef WITH_OPENJPEG
	".jp2",
	".j2c",
#endif
#ifdef WITH_HDR
	".hdr",
#endif
#ifdef WITH_DDS
	".dds",
#endif
#ifdef WITH_CINEON
	".dpx",
	".cin",
#endif
#ifdef WITH_OPENEXR
	".exr",
#endif
#ifdef WITH_OPENIMAGEIO
	".psd", ".pdd", ".psb",
#endif
	NULL
};

const char *imb_ext_image_filepath_only[] = {
#ifdef WITH_OPENIMAGEIO
	".psd", ".pdd", ".psb",
#endif
	NULL
};

int IMB_ispic_type(const char *name)
{
	/* increased from 32 to 64 because of the bitmaps header size */
#define HEADER_SIZE 64

	unsigned char buf[HEADER_SIZE];
	const ImFileType *type;
	BLI_stat_t st;
	int fp;

	BLI_assert(!BLI_path_is_rel(name));

	if (UTIL_DEBUG) printf("%s: loading %s\n", __func__, name);

	if (BLI_stat(name, &st) == -1)
		return false;
	if (((st.st_mode) & S_IFMT) != S_IFREG)
		return false;

	if ((fp = BLI_open(name, O_BINARY | O_RDONLY, 0)) == -1)
		return false;

	memset(buf, 0, sizeof(buf));
	if (read(fp, buf, HEADER_SIZE) <= 0) {
		close(fp);
		return false;
	}

	close(fp);

	/* XXX move this exception */
	if ((BIG_LONG(((int *)buf)[0]) & 0xfffffff0) == 0xffd8ffe0)
		return IMB_FTYPE_JPG;

	for (type = IMB_FILE_TYPES; type < IMB_FILE_TYPES_LAST; type++) {
		if (type->is_a) {
			if (type->is_a(buf)) {
				return type->filetype;
			}
		}
		else if (type->is_a_filepath) {
			if (type->is_a_filepath(name)) {
				return type->filetype;
			}
		}
	}

	return 0;

#undef HEADER_SIZE
}

bool IMB_ispic(const char *name)
{
	return (IMB_ispic_type(name) != 0);
}

bool IMB_isfloat(ImBuf *ibuf)
{
	const ImFileType *type;

	for (type = IMB_FILE_TYPES; type < IMB_FILE_TYPES_LAST; type++) {
		if (type->ftype(type, ibuf)) {
			return (type->flag & IM_FTYPE_FLOAT) != 0;
		}
	}
	return false;
}
