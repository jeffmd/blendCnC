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

/** \file blender/blenkernel/intern/image.c
 *  \ingroup bke
 */


#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>
#ifndef WIN32
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include <time.h>

#include "MEM_guardedalloc.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf_types.h"
#include "IMB_imbuf.h"
#include "IMB_metadata.h"
#include "IMB_moviecache.h"

#ifdef WITH_OPENEXR
#  include "intern/openexr/openexr_multi.h"
#endif

#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_camera_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_blenlib.h"
#include "BLI_math_vector.h"
#include "BLI_mempool.h"
#include "BLI_threads.h"
#include "BLI_timecode.h"  /* for stamp timecode format */
#include "BLI_utildefines.h"

#include "BKE_bmfont.h"
#include "BKE_colortools.h"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_image.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_packedFile.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "BLF_api.h"

#include "PIL_time.h"

#include "GPU_draw.h"

#include "BLI_sys_types.h" // for intptr_t support

/* for image user iteration */
#include "DNA_space_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

static SpinLock image_spin;

/* prototypes */
static int image_num_files(struct Image *ima);
static ImBuf *image_acquire_ibuf(Image *ima, ImageUser *iuser, void **r_lock);
static void image_update_views_format(Image *ima, ImageUser *iuser);

/* max int, to indicate we don't store sequences in ibuf */
#define IMA_NO_INDEX    0x7FEFEFEF

/* quick lookup: supports 1 million frames, thousand passes */
#define IMA_MAKE_INDEX(frame, index)    (((frame) << 10) + (index))
#define IMA_INDEX_FRAME(index)           ((index) >> 10)
#if 0
#define IMA_INDEX_PASS(index)           (index & ~1023)
#endif

/* ******** IMAGE CACHE ************* */

typedef struct ImageCacheKey {
	int index;
} ImageCacheKey;

static unsigned int imagecache_hashhash(const void *key_v)
{
	const ImageCacheKey *key = key_v;
	return key->index;
}

static bool imagecache_hashcmp(const void *a_v, const void *b_v)
{
	const ImageCacheKey *a = a_v;
	const ImageCacheKey *b = b_v;

	return (a->index != b->index);
}

static void imagecache_keydata(void *userkey, int *framenr, int *proxy, int *render_flags)
{
	ImageCacheKey *key = userkey;

	*framenr = IMA_INDEX_FRAME(key->index);
	*proxy = IMB_PROXY_NONE;
	*render_flags = 0;
}

static void imagecache_put(Image *image, int index, ImBuf *ibuf)
{
	ImageCacheKey key;

	if (image->cache == NULL) {
		// char cache_name[64];
		// SNPRINTF(cache_name, "Image Datablock %s", image->id.name);

		image->cache = IMB_moviecache_create("Image Datablock Cache", sizeof(ImageCacheKey),
		                                     imagecache_hashhash, imagecache_hashcmp);
		IMB_moviecache_set_getdata_callback(image->cache, imagecache_keydata);
	}

	key.index = index;

	IMB_moviecache_put(image->cache, &key, ibuf);
}

static struct ImBuf *imagecache_get(Image *image, int index)
{
	if (image->cache) {
		ImageCacheKey key;
		key.index = index;
		return IMB_moviecache_get(image->cache, &key);
	}

	return NULL;
}

void BKE_images_init(void)
{
	BLI_spin_init(&image_spin);
}

void BKE_images_exit(void)
{
	BLI_spin_end(&image_spin);
}

/* ******** IMAGE PROCESSING ************* */

static void de_interlace_ng(struct ImBuf *ibuf) /* neogeo fields */
{
	struct ImBuf *tbuf1, *tbuf2;

	if (ibuf == NULL) return;
	if (ibuf->flags & IB_fields) return;
	ibuf->flags |= IB_fields;

	if (ibuf->rect) {
		/* make copies */
		tbuf1 = IMB_allocImBuf(ibuf->x, (ibuf->y >> 1), (unsigned char)32, (int)IB_rect);
		tbuf2 = IMB_allocImBuf(ibuf->x, (ibuf->y >> 1), (unsigned char)32, (int)IB_rect);

		ibuf->x *= 2;

		IMB_rectcpy(tbuf1, ibuf, 0, 0, 0, 0, ibuf->x, ibuf->y);
		IMB_rectcpy(tbuf2, ibuf, 0, 0, tbuf2->x, 0, ibuf->x, ibuf->y);

		ibuf->x /= 2;
		IMB_rectcpy(ibuf, tbuf1, 0, 0, 0, 0, tbuf1->x, tbuf1->y);
		IMB_rectcpy(ibuf, tbuf2, 0, tbuf2->y, 0, 0, tbuf2->x, tbuf2->y);

		IMB_freeImBuf(tbuf1);
		IMB_freeImBuf(tbuf2);
	}
	ibuf->y /= 2;
}

static void de_interlace_st(struct ImBuf *ibuf) /* standard fields */
{
	struct ImBuf *tbuf1, *tbuf2;

	if (ibuf == NULL) return;
	if (ibuf->flags & IB_fields) return;
	ibuf->flags |= IB_fields;

	if (ibuf->rect) {
		/* make copies */
		tbuf1 = IMB_allocImBuf(ibuf->x, (ibuf->y >> 1), (unsigned char)32, IB_rect);
		tbuf2 = IMB_allocImBuf(ibuf->x, (ibuf->y >> 1), (unsigned char)32, IB_rect);

		ibuf->x *= 2;

		IMB_rectcpy(tbuf1, ibuf, 0, 0, 0, 0, ibuf->x, ibuf->y);
		IMB_rectcpy(tbuf2, ibuf, 0, 0, tbuf2->x, 0, ibuf->x, ibuf->y);

		ibuf->x /= 2;
		IMB_rectcpy(ibuf, tbuf2, 0, 0, 0, 0, tbuf2->x, tbuf2->y);
		IMB_rectcpy(ibuf, tbuf1, 0, tbuf2->y, 0, 0, tbuf1->x, tbuf1->y);

		IMB_freeImBuf(tbuf1);
		IMB_freeImBuf(tbuf2);
	}
	ibuf->y /= 2;
}

void BKE_image_de_interlace(Image *ima, int odd)
{
	ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);
	if (ibuf) {
		if (odd)
			de_interlace_st(ibuf);
		else
			de_interlace_ng(ibuf);
	}
	BKE_image_release_ibuf(ima, ibuf, NULL);
}

/* ***************** ALLOC & FREE, DATA MANAGING *************** */

static void image_free_cached_frames(Image *image)
{
	if (image->cache) {
		IMB_moviecache_free(image->cache);
		image->cache = NULL;
	}
}

static void image_free_packedfiles(Image *ima)
{
	while (ima->packedfiles.last) {
		ImagePackedFile *imapf = ima->packedfiles.last;
		if (imapf->packedfile) {
			freePackedFile(imapf->packedfile);
		}
		BLI_remlink(&ima->packedfiles, imapf);
		MEM_freeN(imapf);
	}
}

void BKE_image_free_packedfiles(Image *ima)
{
	image_free_packedfiles(ima);
}

void BKE_image_free_views(Image *image)
{
	BLI_freelistN(&image->views);
}

/**
 * Simply free the image data from memory,
 * on display the image can load again (except for render buffers).
 */
void BKE_image_free_buffers_ex(Image *ima, bool do_lock)
{
	if (do_lock) {
		BLI_spin_lock(&image_spin);
	}
	image_free_cached_frames(ima);

	if (ima->rr) {
		ima->rr = NULL;
	}

	GPU_free_image(ima);

	ima->ok = IMA_OK;

	if (do_lock) {
		BLI_spin_unlock(&image_spin);
	}
}

void BKE_image_free_buffers(Image *ima)
{
	BKE_image_free_buffers_ex(ima, false);
}

/** Free (or release) any data used by this image (does not free the image itself). */
void BKE_image_free(Image *ima)
{
	BKE_image_free_buffers(ima);

	image_free_packedfiles(ima);

	BKE_image_free_views(ima);

	BKE_icon_id_delete(&ima->id);
	BKE_previewimg_free(&ima->preview);
}

/* only image block itself */
static void image_init(Image *ima, short source, short type)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(ima, id));

	ima->ok = IMA_OK;

	ima->xrep = ima->yrep = 1;
	ima->aspx = ima->aspy = 1.0;
	ima->gen_x = 1024; ima->gen_y = 1024;
	ima->gen_type = IMA_GENTYPE_GRID;

	ima->source = source;
	ima->type = type;

	if (source == IMA_SRC_VIEWER)
		ima->flag |= IMA_VIEW_AS_RENDER;

	BKE_color_managed_colorspace_settings_init(&ima->colorspace_settings);
}

void BKE_image_init(struct Image *image)
{
	if (image) {
		image_init(image, IMA_SRC_GENERATED, IMA_TYPE_UV_TEST);
	}
}

static Image *image_alloc(Main *bmain, const char *name, short source, short type)
{
	Image *ima;

	ima = BKE_libblock_alloc(bmain, ID_IM, name, 0);
	if (ima) {
		image_init(ima, source, type);
	}

	return ima;
}

/* Get the ibuf from an image cache by it's index and frame.
 * Local use here only.
 *
 * Returns referenced image buffer if it exists, callee is to
 * call IMB_freeImBuf to de-reference the image buffer after
 * it's done handling it.
 */
static ImBuf *image_get_cached_ibuf_for_index_frame(Image *ima, int index, int frame)
{
	if (index != IMA_NO_INDEX) {
		index = IMA_MAKE_INDEX(frame, index);
	}

	return imagecache_get(ima, index);
}

/* no ima->ibuf anymore, but listbase */
static void image_assign_ibuf(Image *ima, ImBuf *ibuf, int index, int frame)
{
	if (ibuf) {
		if (index != IMA_NO_INDEX)
			index = IMA_MAKE_INDEX(frame, index);

		imagecache_put(ima, index, ibuf);
	}
}

static void copy_image_packedfiles(ListBase *lb_dst, const ListBase *lb_src)
{
	const ImagePackedFile *imapf_src;

	BLI_listbase_clear(lb_dst);
	for (imapf_src = lb_src->first; imapf_src; imapf_src = imapf_src->next) {
		ImagePackedFile *imapf_dst = MEM_mallocN(sizeof(ImagePackedFile), "Image Packed Files (copy)");
		STRNCPY(imapf_dst->filepath, imapf_src->filepath);

		if (imapf_src->packedfile)
			imapf_dst->packedfile = dupPackedFile(imapf_src->packedfile);

		BLI_addtail(lb_dst, imapf_dst);
	}
}

/**
 * Only copy internal data of Image ID from source to already allocated/initialized destination.
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_image_copy_data(Main *UNUSED(bmain), Image *ima_dst, const Image *ima_src, const int flag)
{
	BKE_color_managed_colorspace_settings_copy(&ima_dst->colorspace_settings, &ima_src->colorspace_settings);

	copy_image_packedfiles(&ima_dst->packedfiles, &ima_src->packedfiles);

	BLI_duplicatelist(&ima_dst->views, &ima_src->views);

	/* Cleanup stuff that cannot be copied. */
	ima_dst->cache = NULL;
	ima_dst->rr = NULL;

	BLI_listbase_clear(&ima_dst->anims);

	ima_dst->totbind = 0;
	for (int i = 0; i < TEXTARGET_COUNT; i++) {
		ima_dst->bindcode[i] = 0;
		ima_dst->gputexture[i] = NULL;
	}
	ima_dst->repbind = NULL;

	if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
		BKE_previewimg_id_copy(&ima_dst->id, &ima_src->id);
	}
	else {
		ima_dst->preview = NULL;
	}
}

/* empty image block, of similar type and filename */
Image *BKE_image_copy(Main *bmain, const Image *ima)
{
	Image *ima_copy;
	BKE_id_copy_ex(bmain, &ima->id, (ID **)&ima_copy, 0, false);
	return ima_copy;
}

void BKE_image_make_local(Main *bmain, Image *ima, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &ima->id, true, lib_local);
}

void BKE_image_merge(Main *bmain, Image *dest, Image *source)
{
	/* sanity check */
	if (dest && source && dest != source) {
		BLI_spin_lock(&image_spin);
		if (source->cache != NULL) {
			struct MovieCacheIter *iter;
			iter = IMB_moviecacheIter_new(source->cache);
			while (!IMB_moviecacheIter_done(iter)) {
				ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
				ImageCacheKey *key = IMB_moviecacheIter_getUserKey(iter);
				imagecache_put(dest, key->index, ibuf);
				IMB_moviecacheIter_step(iter);
			}
			IMB_moviecacheIter_free(iter);
		}
		BLI_spin_unlock(&image_spin);

		BKE_libblock_free(bmain, source);
	}
}

/* note, we could be clever and scale all imbuf's but since some are mipmaps its not so simple */
bool BKE_image_scale(Image *image, int width, int height)
{
	ImBuf *ibuf;
	void *lock;

	ibuf = BKE_image_acquire_ibuf(image, NULL, &lock);

	if (ibuf) {
		IMB_scaleImBuf(ibuf, width, height);
		ibuf->userflags |= IB_BITMAPDIRTY;
	}

	BKE_image_release_ibuf(image, ibuf, lock);

	return (ibuf != NULL);
}

bool BKE_image_has_bindcode(Image *ima)
{
	bool has_bindcode = false;
	for (int i = 0; i < TEXTARGET_COUNT; i++) {
		if (ima->bindcode[i]) {
			has_bindcode = true;
			break;
		}
	}
	return has_bindcode;
}

static void image_init_color_management(Image *ima)
{
	ImBuf *ibuf;
	char name[FILE_MAX];

	BKE_image_user_file_path(NULL, ima, name);

	/* will set input color space to image format default's */
	ibuf = IMB_loadiffname(name, IB_test | IB_alphamode_detect, ima->colorspace_settings.name);

	if (ibuf) {
		if (ibuf->flags & IB_alphamode_premul)
			ima->alpha_mode = IMA_ALPHA_PREMUL;
		else
			ima->alpha_mode = IMA_ALPHA_STRAIGHT;

		IMB_freeImBuf(ibuf);
	}
}

char BKE_image_alpha_mode_from_extension_ex(const char *filepath)
{
	if (BLI_path_extension_check_n(filepath, ".exr", ".cin", ".dpx", ".hdr", NULL)) {
		return IMA_ALPHA_PREMUL;
	}
	else {
		return IMA_ALPHA_STRAIGHT;
	}
}

void BKE_image_alpha_mode_from_extension(Image *image)
{
	image->alpha_mode = BKE_image_alpha_mode_from_extension_ex(image->name);
}

Image *BKE_image_load(Main *bmain, const char *filepath)
{
	Image *ima;
	int file;
	char str[FILE_MAX];

	STRNCPY(str, filepath);
	BLI_path_abs(str, BKE_main_blendfile_path(bmain));

	/* exists? */
	file = BLI_open(str, O_BINARY | O_RDONLY, 0);
	if (file == -1)
		return NULL;
	close(file);

	ima = image_alloc(bmain, BLI_path_basename(filepath), IMA_SRC_FILE, IMA_TYPE_IMAGE);
	STRNCPY(ima->name, filepath);

	image_init_color_management(ima);

	return ima;
}

/* checks if image was already loaded, then returns same image */
/* otherwise creates new. */
/* does not load ibuf itself */
/* pass on optional frame for #name images */
Image *BKE_image_load_exists_ex(Main *bmain, const char *filepath, bool *r_exists)
{
	Image *ima;
	char str[FILE_MAX], strtest[FILE_MAX];

	STRNCPY(str, filepath);
	BLI_path_abs(str, BKE_main_blendfile_path_from_global());

	/* first search an identical filepath */
	for (ima = bmain->image.first; ima; ima = ima->id.next) {
		if (ima->source != IMA_SRC_VIEWER && ima->source != IMA_SRC_GENERATED) {
			STRNCPY(strtest, ima->name);
			BLI_path_abs(strtest, ID_BLEND_PATH(bmain, &ima->id));

			if (BLI_path_cmp(strtest, str) == 0) {
				if ((ima->id.us == 0))
				{
					id_us_plus(&ima->id);  /* officially should not, it doesn't link here! */
					if (ima->ok == 0)
						ima->ok = IMA_OK;
					if (r_exists)
						*r_exists = true;
					return ima;
				}
			}
		}
	}

	if (r_exists)
		*r_exists = false;
	return BKE_image_load(bmain, filepath);
}

Image *BKE_image_load_exists(Main *bmain, const char *filepath)
{
	return BKE_image_load_exists_ex(bmain, filepath, NULL);
}

static ImBuf *add_ibuf_size(unsigned int width, unsigned int height, const char *name, int depth, int floatbuf, short gen_type,
                            const float color[4], ColorManagedColorspaceSettings *colorspace_settings)
{
	ImBuf *ibuf;
	unsigned char *rect = NULL;
	float *rect_float = NULL;

	if (floatbuf) {
		ibuf = IMB_allocImBuf(width, height, depth, IB_rectfloat);

		if (colorspace_settings->name[0] == '\0') {
			const char *colorspace = IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_FLOAT);

			STRNCPY(colorspace_settings->name, colorspace);
		}

		if (ibuf != NULL) {
			rect_float = ibuf->rect_float;
			IMB_colormanagement_check_is_data(ibuf, colorspace_settings->name);
		}
	}
	else {
		ibuf = IMB_allocImBuf(width, height, depth, IB_rect);

		if (colorspace_settings->name[0] == '\0') {
			const char *colorspace = IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_BYTE);

			STRNCPY(colorspace_settings->name, colorspace);
		}

		if (ibuf != NULL) {
			rect = (unsigned char *)ibuf->rect;
			IMB_colormanagement_assign_rect_colorspace(ibuf, colorspace_settings->name);
		}
	}

	if (!ibuf) {
		return NULL;
	}

	STRNCPY(ibuf->name, name);
	ibuf->userflags |= IB_BITMAPDIRTY;

	switch (gen_type) {
		case IMA_GENTYPE_GRID:
			BKE_image_buf_fill_checker(rect, rect_float, width, height);
			break;
		case IMA_GENTYPE_GRID_COLOR:
			BKE_image_buf_fill_checker_color(rect, rect_float, width, height);
			break;
		default:
			BKE_image_buf_fill_color(rect, rect_float, width, height, color);
			break;
	}

	return ibuf;
}

/* adds new image block, creates ImBuf and initializes color */
Image *BKE_image_add_generated(
        Main *bmain, unsigned int width, unsigned int height, const char *name,
        int depth, int floatbuf, short gen_type, const float color[4])
{
	/* on save, type is changed to FILE in editsima.c */
	Image *ima = image_alloc(bmain, name, IMA_SRC_GENERATED, IMA_TYPE_UV_TEST);

	if (ima) {
		ima->gen_x = width;
		ima->gen_y = height;
		ima->gen_type = gen_type;
		ima->gen_flag |= (floatbuf ? IMA_GEN_FLOAT : 0);
		ima->gen_depth = depth;
		copy_v4_v4(ima->gen_color, color);

		ImBuf *ibuf;
		ibuf = add_ibuf_size(width, height, ima->name, depth, floatbuf, gen_type, color, &ima->colorspace_settings);
		image_assign_ibuf(ima, ibuf, IMA_NO_INDEX, 0);

		/* image_assign_ibuf puts buffer to the cache, which increments user counter. */
		IMB_freeImBuf(ibuf);

		ima->ok = IMA_OK_LOADED;
	}

	return ima;
}

/* Create an image image from ibuf. The refcount of ibuf is increased,
 * caller should take care to drop its reference by calling
 * IMB_freeImBuf if needed. */
Image *BKE_image_add_from_imbuf(Main *bmain, ImBuf *ibuf, const char *name)
{
	/* on save, type is changed to FILE in editsima.c */
	Image *ima;

	if (name == NULL) {
		name = BLI_path_basename(ibuf->name);
	}

	ima = image_alloc(bmain, name, IMA_SRC_FILE, IMA_TYPE_IMAGE);

	if (ima) {
		STRNCPY(ima->name, ibuf->name);
		image_assign_ibuf(ima, ibuf, IMA_NO_INDEX, 0);
		ima->ok = IMA_OK_LOADED;
	}

	return ima;
}

/* packs rect from memory as PNG */
void BKE_image_memorypack(Image *ima)
{
	ImBuf *ibuf;

	ibuf = image_get_cached_ibuf_for_index_frame(ima, IMA_NO_INDEX, 0);

	if (ibuf == NULL)
		return;

	image_free_packedfiles(ima);

	ibuf->ftype = IMB_FTYPE_PNG;
	ibuf->planes = R_IMF_PLANES_RGBA;

	IMB_saveiff(ibuf, ibuf->name, IB_rect | IB_mem);
	if (ibuf->encodedbuffer == NULL) {
		printf("memory save for pack error\n");
	}
	else {
		ImagePackedFile *imapf;
		PackedFile *pf = MEM_callocN(sizeof(*pf), "PackedFile");

		pf->data = ibuf->encodedbuffer;
		pf->size = ibuf->encodedsize;

		imapf = MEM_mallocN(sizeof(ImagePackedFile), "Image PackedFile");
		STRNCPY(imapf->filepath, ima->name);
		imapf->packedfile = pf;
		BLI_addtail(&ima->packedfiles, imapf);

		ibuf->encodedbuffer = NULL;
		ibuf->encodedsize = 0;
		ibuf->userflags &= ~IB_BITMAPDIRTY;

		if (ima->source == IMA_SRC_GENERATED) {
			ima->source = IMA_SRC_FILE;
			ima->type = IMA_TYPE_IMAGE;
		}
	}

	IMB_freeImBuf(ibuf);
}

void BKE_image_packfiles(ReportList *reports, Image *ima, const char *basepath)
{
	const int totfiles = image_num_files(ima);

	if (totfiles == 1) {
		ImagePackedFile *imapf = MEM_mallocN(sizeof(ImagePackedFile), "Image packed file");
		BLI_addtail(&ima->packedfiles, imapf);
		imapf->packedfile = newPackedFile(reports, ima->name, basepath);
		if (imapf->packedfile) {
			STRNCPY(imapf->filepath, ima->name);
		}
		else {
			BLI_freelinkN(&ima->packedfiles, imapf);
		}
	}
	else {
		ImageView *iv;
		for (iv = ima->views.first; iv; iv = iv->next) {
			ImagePackedFile *imapf = MEM_mallocN(sizeof(ImagePackedFile), "Image packed file");
			BLI_addtail(&ima->packedfiles, imapf);

			imapf->packedfile = newPackedFile(reports, iv->filepath, basepath);
			if (imapf->packedfile) {
				STRNCPY(imapf->filepath, iv->filepath);
			}
			else {
				BLI_freelinkN(&ima->packedfiles, imapf);
			}
		}
	}
}

void BKE_image_packfiles_from_mem(ReportList *reports, Image *ima, char *data, const size_t data_len)
{
	const int totfiles = image_num_files(ima);

	if (totfiles != 1) {
		BKE_report(reports, RPT_ERROR, "Cannot pack multiview images from raw data currently...");
	}
	else {
		ImagePackedFile *imapf = MEM_mallocN(sizeof(ImagePackedFile), __func__);
		BLI_addtail(&ima->packedfiles, imapf);
		imapf->packedfile = newPackedFileMemory(data, data_len);
		STRNCPY(imapf->filepath, ima->name);
	}
}

void BKE_image_tag_time(Image *ima)
{
	ima->lastused = PIL_check_seconds_timer_i();
}

#if 0
static void tag_all_images_time(Main *bmain)
{
	Image *ima;
	int ctime = PIL_check_seconds_timer_i();

	ima = bmain->image.first;
	while (ima) {
		if (ima->bindcode || ima->repbind || ima->ibufs.first) {
			ima->lastused = ctime;
		}
	}
}
#endif

static uintptr_t image_mem_size(Image *image)
{
	uintptr_t size = 0;

	/* viewers have memory depending on other rules, has no valid rect pointer */
	if (image->source == IMA_SRC_VIEWER)
		return 0;

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
			ImBuf *ibufm;
			int level;

			if (ibuf->rect) {
				size += MEM_allocN_len(ibuf->rect);
			}
			if (ibuf->rect_float) {
				size += MEM_allocN_len(ibuf->rect_float);
			}

			for (level = 0; level < IMB_MIPMAP_LEVELS; level++) {
				ibufm = ibuf->mipmap[level];
				if (ibufm) {
					if (ibufm->rect) {
						size += MEM_allocN_len(ibufm->rect);
					}
					if (ibufm->rect_float) {
						size += MEM_allocN_len(ibufm->rect_float);
					}
				}
			}

			IMB_moviecacheIter_step(iter);
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);

	return size;
}

void BKE_image_print_memlist(Main *bmain)
{
	Image *ima;
	uintptr_t size, totsize = 0;

	for (ima = bmain->image.first; ima; ima = ima->id.next)
		totsize += image_mem_size(ima);

	printf("\ntotal image memory len: %.3f MB\n", (double)totsize / (double)(1024 * 1024));

	for (ima = bmain->image.first; ima; ima = ima->id.next) {
		size = image_mem_size(ima);

		if (size)
			printf("%s len: %.3f MB\n", ima->id.name + 2, (double)size / (double)(1024 * 1024));
	}
}

static bool imagecache_check_dirty(ImBuf *ibuf, void *UNUSED(userkey), void *UNUSED(userdata))
{
	return (ibuf->userflags & IB_BITMAPDIRTY) == 0;
}

void BKE_image_free_all_textures(Main *bmain)
{
#undef CHECK_FREED_SIZE

	Tex *tex;
	Image *ima;
#ifdef CHECK_FREED_SIZE
	uintptr_t tot_freed_size = 0;
#endif

	for (ima = bmain->image.first; ima; ima = ima->id.next)
		ima->id.tag &= ~LIB_TAG_DOIT;

	for (tex = bmain->tex.first; tex; tex = tex->id.next)
		if (tex->ima)
			tex->ima->id.tag |= LIB_TAG_DOIT;

	for (ima = bmain->image.first; ima; ima = ima->id.next) {
		if (ima->cache && (ima->id.tag & LIB_TAG_DOIT)) {
#ifdef CHECK_FREED_SIZE
			uintptr_t old_size = image_mem_size(ima);
#endif

			IMB_moviecache_cleanup(ima->cache, imagecache_check_dirty, NULL);

#ifdef CHECK_FREED_SIZE
			tot_freed_size += old_size - image_mem_size(ima);
#endif
		}
	}
#ifdef CHECK_FREED_SIZE
	printf("%s: freed total %lu MB\n", __func__, tot_freed_size / (1024 * 1024));
#endif
}

static bool imagecache_check_free_anim(ImBuf *ibuf, void *UNUSED(userkey), void *userdata)
{
	int except_frame = *(int *)userdata;
	return (ibuf->userflags & IB_BITMAPDIRTY) == 0 &&
	       (ibuf->index != IMA_NO_INDEX) &&
	       (except_frame != IMA_INDEX_FRAME(ibuf->index));
}

/* except_frame is weak, only works for seqs without offset... */
void BKE_image_free_anim_ibufs(Image *ima, int except_frame)
{
	BLI_spin_lock(&image_spin);
	if (ima->cache != NULL) {
		IMB_moviecache_cleanup(ima->cache, imagecache_check_free_anim, &except_frame);
	}
	BLI_spin_unlock(&image_spin);
}


/* *********** READ AND WRITE ************** */

int BKE_image_imtype_to_ftype(const char imtype, ImbFormatOptions *r_options)
{
	memset(r_options, 0, sizeof(*r_options));

	if (imtype == R_IMF_IMTYPE_TARGA) {
		return IMB_FTYPE_TGA;
	}
	else if (imtype == R_IMF_IMTYPE_RAWTGA) {
		r_options->flag = RAWTGA;
		return IMB_FTYPE_TGA;
	}
	else if (imtype == R_IMF_IMTYPE_IRIS) {
		return IMB_FTYPE_IMAGIC;
	}
#ifdef WITH_HDR
	else if (imtype == R_IMF_IMTYPE_RADHDR) {
		return IMB_FTYPE_RADHDR;
	}
#endif
	else if (imtype == R_IMF_IMTYPE_PNG) {
		r_options->quality = 15;
		return IMB_FTYPE_PNG;
	}
#ifdef WITH_DDS
	else if (imtype == R_IMF_IMTYPE_DDS) {
		return IMB_FTYPE_DDS;
	}
#endif
	else if (imtype == R_IMF_IMTYPE_BMP) {
		return IMB_FTYPE_BMP;
	}
#ifdef WITH_TIFF
	else if (imtype == R_IMF_IMTYPE_TIFF) {
		return IMB_FTYPE_TIF;
	}
#endif
	else if (imtype == R_IMF_IMTYPE_OPENEXR || imtype == R_IMF_IMTYPE_MULTILAYER) {
		return IMB_FTYPE_OPENEXR;
	}
#ifdef WITH_CINEON
	else if (imtype == R_IMF_IMTYPE_CINEON) {
		return IMB_FTYPE_CINEON;
	}
	else if (imtype == R_IMF_IMTYPE_DPX) {
		return IMB_FTYPE_DPX;
	}
#endif
#ifdef WITH_OPENJPEG
	else if (imtype == R_IMF_IMTYPE_JP2) {
		r_options->flag |= JP2_JP2;
		r_options->quality = 90;
		return IMB_FTYPE_JP2;
	}
#endif
	else {
		r_options->quality = 90;
		return IMB_FTYPE_JPG;
	}
}

char BKE_image_ftype_to_imtype(const int ftype, const ImbFormatOptions *options)
{
	if (ftype == 0) {
		return R_IMF_IMTYPE_TARGA;
	}
	else if (ftype == IMB_FTYPE_IMAGIC) {
		return R_IMF_IMTYPE_IRIS;
	}
#ifdef WITH_HDR
	else if (ftype == IMB_FTYPE_RADHDR) {
		return R_IMF_IMTYPE_RADHDR;
	}
#endif
	else if (ftype == IMB_FTYPE_PNG) {
		return R_IMF_IMTYPE_PNG;
	}
#ifdef WITH_DDS
	else if (ftype == IMB_FTYPE_DDS) {
		return R_IMF_IMTYPE_DDS;
	}
#endif
	else if (ftype == IMB_FTYPE_BMP) {
		return R_IMF_IMTYPE_BMP;
	}
#ifdef WITH_TIFF
	else if (ftype == IMB_FTYPE_TIF) {
		return R_IMF_IMTYPE_TIFF;
	}
#endif
	else if (ftype == IMB_FTYPE_OPENEXR) {
		return R_IMF_IMTYPE_OPENEXR;
	}
#ifdef WITH_CINEON
	else if (ftype == IMB_FTYPE_CINEON) {
		return R_IMF_IMTYPE_CINEON;
	}
	else if (ftype == IMB_FTYPE_DPX) {
		return R_IMF_IMTYPE_DPX;
	}
#endif
	else if (ftype == IMB_FTYPE_TGA) {
		if (options && (options->flag & RAWTGA)) {
			return R_IMF_IMTYPE_RAWTGA;
		}
		else {
			return R_IMF_IMTYPE_TARGA;
		}
	}
#ifdef WITH_OPENJPEG
	else if (ftype == IMB_FTYPE_JP2) {
		return R_IMF_IMTYPE_JP2;
	}
#endif
	else {
		return R_IMF_IMTYPE_JPEG90;
	}
}


int BKE_imtype_supports_zbuf(const char imtype)
{
	switch (imtype) {
		case R_IMF_IMTYPE_IRIZ:
		case R_IMF_IMTYPE_OPENEXR: /* but not R_IMF_IMTYPE_MULTILAYER */
			return 1;
	}
	return 0;
}

int BKE_imtype_supports_compress(const char imtype)
{
	switch (imtype) {
		case R_IMF_IMTYPE_PNG:
			return 1;
	}
	return 0;
}

int BKE_imtype_supports_quality(const char imtype)
{
	switch (imtype) {
		case R_IMF_IMTYPE_JPEG90:
		case R_IMF_IMTYPE_JP2:
		case R_IMF_IMTYPE_AVIJPEG:
			return 1;
	}
	return 0;
}

int BKE_imtype_requires_linear_float(const char imtype)
{
	switch (imtype) {
		case R_IMF_IMTYPE_CINEON:
		case R_IMF_IMTYPE_DPX:
		case R_IMF_IMTYPE_RADHDR:
		case R_IMF_IMTYPE_OPENEXR:
		case R_IMF_IMTYPE_MULTILAYER:
			return true;
	}
	return 0;
}

char BKE_imtype_valid_channels(const char imtype, bool write_file)
{
	char chan_flag = IMA_CHAN_FLAG_RGB; /* assume all support rgb */

	/* alpha */
	switch (imtype) {
		case R_IMF_IMTYPE_BMP:
			if (write_file) break;
			ATTR_FALLTHROUGH;
		case R_IMF_IMTYPE_TARGA:
		case R_IMF_IMTYPE_RAWTGA:
		case R_IMF_IMTYPE_IRIS:
		case R_IMF_IMTYPE_PNG:
		case R_IMF_IMTYPE_TIFF:
		case R_IMF_IMTYPE_OPENEXR:
		case R_IMF_IMTYPE_MULTILAYER:
		case R_IMF_IMTYPE_DDS:
		case R_IMF_IMTYPE_JP2:
		case R_IMF_IMTYPE_DPX:
			chan_flag |= IMA_CHAN_FLAG_ALPHA;
			break;
	}

	/* bw */
	switch (imtype) {
		case R_IMF_IMTYPE_PNG:
		case R_IMF_IMTYPE_JPEG90:
		case R_IMF_IMTYPE_TARGA:
		case R_IMF_IMTYPE_RAWTGA:
		case R_IMF_IMTYPE_TIFF:
		case R_IMF_IMTYPE_IRIS:
			chan_flag |= IMA_CHAN_FLAG_BW;
			break;
	}

	return chan_flag;
}

char BKE_imtype_valid_depths(const char imtype)
{
	switch (imtype) {
		case R_IMF_IMTYPE_RADHDR:
			return R_IMF_CHAN_DEPTH_32;
		case R_IMF_IMTYPE_TIFF:
			return R_IMF_CHAN_DEPTH_8 | R_IMF_CHAN_DEPTH_16;
		case R_IMF_IMTYPE_OPENEXR:
			return R_IMF_CHAN_DEPTH_16 | R_IMF_CHAN_DEPTH_32;
		case R_IMF_IMTYPE_MULTILAYER:
			return R_IMF_CHAN_DEPTH_16 | R_IMF_CHAN_DEPTH_32;
		/* eeh, cineon does some strange 10bits per channel */
		case R_IMF_IMTYPE_DPX:
			return R_IMF_CHAN_DEPTH_8 | R_IMF_CHAN_DEPTH_10 | R_IMF_CHAN_DEPTH_12 | R_IMF_CHAN_DEPTH_16;
		case R_IMF_IMTYPE_CINEON:
			return R_IMF_CHAN_DEPTH_10;
		case R_IMF_IMTYPE_JP2:
			return R_IMF_CHAN_DEPTH_8 | R_IMF_CHAN_DEPTH_12 | R_IMF_CHAN_DEPTH_16;
		case R_IMF_IMTYPE_PNG:
			return R_IMF_CHAN_DEPTH_8 | R_IMF_CHAN_DEPTH_16;
		/* most formats are 8bit only */
		default:
			return R_IMF_CHAN_DEPTH_8;
	}
}


/* string is from command line --render-format arg, keep in sync with
 * creator_args.c help info */
char BKE_imtype_from_arg(const char *imtype_arg)
{
	if      (STREQ(imtype_arg, "TGA")) return R_IMF_IMTYPE_TARGA;
	else if (STREQ(imtype_arg, "IRIS")) return R_IMF_IMTYPE_IRIS;
#ifdef WITH_DDS
	else if (STREQ(imtype_arg, "DDS")) return R_IMF_IMTYPE_DDS;
#endif
	else if (STREQ(imtype_arg, "JPEG")) return R_IMF_IMTYPE_JPEG90;
	else if (STREQ(imtype_arg, "IRIZ")) return R_IMF_IMTYPE_IRIZ;
	else if (STREQ(imtype_arg, "RAWTGA")) return R_IMF_IMTYPE_RAWTGA;
	else if (STREQ(imtype_arg, "AVIRAW")) return R_IMF_IMTYPE_AVIRAW;
	else if (STREQ(imtype_arg, "AVIJPEG")) return R_IMF_IMTYPE_AVIJPEG;
	else if (STREQ(imtype_arg, "PNG")) return R_IMF_IMTYPE_PNG;
	else if (STREQ(imtype_arg, "BMP")) return R_IMF_IMTYPE_BMP;
#ifdef WITH_HDR
	else if (STREQ(imtype_arg, "HDR")) return R_IMF_IMTYPE_RADHDR;
#endif
#ifdef WITH_TIFF
	else if (STREQ(imtype_arg, "TIFF")) return R_IMF_IMTYPE_TIFF;
#endif
#ifdef WITH_OPENEXR
	else if (STREQ(imtype_arg, "EXR")) return R_IMF_IMTYPE_OPENEXR;
	else if (STREQ(imtype_arg, "MULTILAYER")) return R_IMF_IMTYPE_MULTILAYER;
#endif
	else if (STREQ(imtype_arg, "FFMPEG")) return R_IMF_IMTYPE_FFMPEG;
	else if (STREQ(imtype_arg, "FRAMESERVER")) return R_IMF_IMTYPE_FRAMESERVER;
#ifdef WITH_CINEON
	else if (STREQ(imtype_arg, "CINEON")) return R_IMF_IMTYPE_CINEON;
	else if (STREQ(imtype_arg, "DPX")) return R_IMF_IMTYPE_DPX;
#endif
#ifdef WITH_OPENJPEG
	else if (STREQ(imtype_arg, "JP2")) return R_IMF_IMTYPE_JP2;
#endif
	else return R_IMF_IMTYPE_INVALID;
}

static bool do_add_image_extension(char *string, const char imtype, const ImageFormatData *im_format)
{
	const char *extension = NULL;
	const char *extension_test;
	(void)im_format;  /* may be unused, depends on build options */

	if (imtype == R_IMF_IMTYPE_IRIS) {
		if (!BLI_path_extension_check(string, extension_test = ".rgb"))
			extension = extension_test;
	}
	else if (imtype == R_IMF_IMTYPE_IRIZ) {
		if (!BLI_path_extension_check(string, extension_test = ".rgb"))
			extension = extension_test;
	}
#ifdef WITH_HDR
	else if (imtype == R_IMF_IMTYPE_RADHDR) {
		if (!BLI_path_extension_check(string, extension_test = ".hdr"))
			extension = extension_test;
	}
#endif
	else if (ELEM(imtype, R_IMF_IMTYPE_PNG, R_IMF_IMTYPE_FFMPEG, R_IMF_IMTYPE_H264, R_IMF_IMTYPE_THEORA, R_IMF_IMTYPE_XVID)) {
		if (!BLI_path_extension_check(string, extension_test = ".png"))
			extension = extension_test;
	}
#ifdef WITH_DDS
	else if (imtype == R_IMF_IMTYPE_DDS) {
		if (!BLI_path_extension_check(string, extension_test = ".dds"))
			extension = extension_test;
	}
#endif
	else if (ELEM(imtype, R_IMF_IMTYPE_TARGA, R_IMF_IMTYPE_RAWTGA)) {
		if (!BLI_path_extension_check(string, extension_test = ".tga"))
			extension = extension_test;
	}
	else if (imtype == R_IMF_IMTYPE_BMP) {
		if (!BLI_path_extension_check(string, extension_test = ".bmp"))
			extension = extension_test;
	}
#ifdef WITH_TIFF
	else if (imtype == R_IMF_IMTYPE_TIFF) {
		if (!BLI_path_extension_check_n(string, extension_test = ".tif", ".tiff", NULL)) {
			extension = extension_test;
		}
	}
#endif
#ifdef WITH_OPENIMAGEIO
	else if (imtype == R_IMF_IMTYPE_PSD) {
		if (!BLI_path_extension_check(string, extension_test = ".psd"))
			extension = extension_test;
	}
#endif
#ifdef WITH_OPENEXR
	else if (imtype == R_IMF_IMTYPE_OPENEXR || imtype == R_IMF_IMTYPE_MULTILAYER) {
		if (!BLI_path_extension_check(string, extension_test = ".exr"))
			extension = extension_test;
	}
#endif
#ifdef WITH_CINEON
	else if (imtype == R_IMF_IMTYPE_CINEON) {
		if (!BLI_path_extension_check(string, extension_test = ".cin"))
			extension = extension_test;
	}
	else if (imtype == R_IMF_IMTYPE_DPX) {
		if (!BLI_path_extension_check(string, extension_test = ".dpx"))
			extension = extension_test;
	}
#endif
#ifdef WITH_OPENJPEG
	else if (imtype == R_IMF_IMTYPE_JP2) {
		if (im_format) {
			if (im_format->jp2_codec == R_IMF_JP2_CODEC_JP2) {
				if (!BLI_path_extension_check(string, extension_test = ".jp2"))
					extension = extension_test;
			}
			else if (im_format->jp2_codec == R_IMF_JP2_CODEC_J2K) {
				if (!BLI_path_extension_check(string, extension_test = ".j2c"))
					extension = extension_test;
			}
			else
				BLI_assert(!"Unsupported jp2 codec was specified in im_format->jp2_codec");
		}
		else {
			if (!BLI_path_extension_check(string, extension_test = ".jp2"))
				extension = extension_test;
		}
	}
#endif
	else { //   R_IMF_IMTYPE_AVIRAW, R_IMF_IMTYPE_AVIJPEG, R_IMF_IMTYPE_JPEG90 etc
		if (!(BLI_path_extension_check_n(string, extension_test = ".jpg", ".jpeg", NULL)))
			extension = extension_test;
	}

	if (extension) {
		/* prefer this in many cases to avoid .png.tga, but in certain cases it breaks */
		/* remove any other known image extension */
		if (BLI_path_extension_check_array(string, imb_ext_image)) {
			return BLI_path_extension_replace(string, FILE_MAX, extension);
		}
		else {
			return BLI_path_extension_ensure(string, FILE_MAX, extension);
		}

	}
	else {
		return false;
	}
}

int BKE_image_path_ensure_ext_from_imformat(char *string, const ImageFormatData *im_format)
{
	return do_add_image_extension(string, im_format->imtype, im_format);
}

int BKE_image_path_ensure_ext_from_imtype(char *string, const char imtype)
{
	return do_add_image_extension(string, imtype, NULL);
}

void BKE_imformat_defaults(ImageFormatData *im_format)
{
	memset(im_format, 0, sizeof(*im_format));
	im_format->planes = R_IMF_PLANES_RGBA;
	im_format->imtype = R_IMF_IMTYPE_PNG;
	im_format->depth = R_IMF_CHAN_DEPTH_8;
	im_format->quality = 90;
	im_format->compress = 15;

	BKE_color_managed_display_settings_init(&im_format->display_settings);
	BKE_color_managed_view_settings_init(&im_format->view_settings,
	                                     &im_format->display_settings);
}

void BKE_imbuf_to_image_format(struct ImageFormatData *im_format, const ImBuf *imbuf)
{
	int ftype        = imbuf->ftype;
	int custom_flags = imbuf->foptions.flag;
	char quality     = imbuf->foptions.quality;

	BKE_imformat_defaults(im_format);

	/* file type */

	if (ftype == IMB_FTYPE_IMAGIC)
		im_format->imtype = R_IMF_IMTYPE_IRIS;

#ifdef WITH_HDR
	else if (ftype == IMB_FTYPE_RADHDR)
		im_format->imtype = R_IMF_IMTYPE_RADHDR;
#endif

	else if (ftype == IMB_FTYPE_PNG) {
		im_format->imtype = R_IMF_IMTYPE_PNG;

		if (custom_flags & PNG_16BIT)
			im_format->depth = R_IMF_CHAN_DEPTH_16;

		im_format->compress = quality;
	}

#ifdef WITH_DDS
	else if (ftype == IMB_FTYPE_DDS)
		im_format->imtype = R_IMF_IMTYPE_DDS;
#endif

	else if (ftype == IMB_FTYPE_BMP)
		im_format->imtype = R_IMF_IMTYPE_BMP;

#ifdef WITH_TIFF
	else if (ftype == IMB_FTYPE_TIF) {
		im_format->imtype = R_IMF_IMTYPE_TIFF;
		if (custom_flags & TIF_16BIT)
			im_format->depth = R_IMF_CHAN_DEPTH_16;
		if (custom_flags & TIF_COMPRESS_NONE)
			im_format->tiff_codec = R_IMF_TIFF_CODEC_NONE;
		if (custom_flags & TIF_COMPRESS_DEFLATE)
			im_format->tiff_codec = R_IMF_TIFF_CODEC_DEFLATE;
		if (custom_flags & TIF_COMPRESS_LZW)
			im_format->tiff_codec = R_IMF_TIFF_CODEC_LZW;
		if (custom_flags & TIF_COMPRESS_PACKBITS)
			im_format->tiff_codec = R_IMF_TIFF_CODEC_PACKBITS;
	}
#endif

#ifdef WITH_OPENEXR
	else if (ftype == IMB_FTYPE_OPENEXR) {
		im_format->imtype = R_IMF_IMTYPE_OPENEXR;
		if (custom_flags & OPENEXR_HALF)
			im_format->depth = R_IMF_CHAN_DEPTH_16;
		if (custom_flags & OPENEXR_COMPRESS)
			im_format->exr_codec = R_IMF_EXR_CODEC_ZIP;  // Can't determine compression
		if (imbuf->zbuf_float)
			im_format->flag |= R_IMF_FLAG_ZBUF;
	}
#endif

#ifdef WITH_CINEON
	else if (ftype == IMB_FTYPE_CINEON)
		im_format->imtype = R_IMF_IMTYPE_CINEON;
	else if (ftype == IMB_FTYPE_DPX)
		im_format->imtype = R_IMF_IMTYPE_DPX;
#endif

	else if (ftype == IMB_FTYPE_TGA) {
		if (custom_flags & RAWTGA)
			im_format->imtype = R_IMF_IMTYPE_RAWTGA;
		else
			im_format->imtype = R_IMF_IMTYPE_TARGA;
	}
#ifdef WITH_OPENJPEG
	else if (ftype == IMB_FTYPE_JP2) {
		im_format->imtype = R_IMF_IMTYPE_JP2;
		im_format->quality = quality;

		if (custom_flags & JP2_16BIT)
			im_format->depth = R_IMF_CHAN_DEPTH_16;
		else if (custom_flags & JP2_12BIT)
			im_format->depth = R_IMF_CHAN_DEPTH_12;

		if (custom_flags & JP2_YCC)
			im_format->jp2_flag |= R_IMF_JP2_FLAG_YCC;

		if (custom_flags & JP2_CINE) {
			im_format->jp2_flag |= R_IMF_JP2_FLAG_CINE_PRESET;
			if (custom_flags & JP2_CINE_48FPS)
				im_format->jp2_flag |= R_IMF_JP2_FLAG_CINE_48;
		}

		if (custom_flags & JP2_JP2)
			im_format->jp2_codec = R_IMF_JP2_CODEC_JP2;
		else if (custom_flags & JP2_J2K)
			im_format->jp2_codec = R_IMF_JP2_CODEC_J2K;
		else
			BLI_assert(!"Unsupported jp2 codec was specified in file type");
	}
#endif

	else {
		im_format->imtype = R_IMF_IMTYPE_JPEG90;
		im_format->quality = quality;
	}

	/* planes */
	im_format->planes = imbuf->planes;
}

bool BKE_imbuf_alpha_test(ImBuf *ibuf)
{
	int tot;
	if (ibuf->rect_float) {
		const float *buf = ibuf->rect_float;
		for (tot = ibuf->x * ibuf->y; tot--; buf += 4) {
			if (buf[3] < 1.0f) {
				return true;
			}
		}
	}
	else if (ibuf->rect) {
		unsigned char *buf = (unsigned char *)ibuf->rect;
		for (tot = ibuf->x * ibuf->y; tot--; buf += 4) {
			if (buf[3] != 255) {
				return true;
			}
		}
	}

	return false;
}

/* note: imf->planes is ignored here, its assumed the image channels
 * are already set */
void BKE_imbuf_write_prepare(ImBuf *ibuf, const ImageFormatData *imf)
{
	char imtype = imf->imtype;
	char compress = imf->compress;
	char quality = imf->quality;

	/* initialize all from image format */
	ibuf->foptions.flag = 0;

	if (imtype == R_IMF_IMTYPE_IRIS) {
		ibuf->ftype = IMB_FTYPE_IMAGIC;
	}
#ifdef WITH_HDR
	else if (imtype == R_IMF_IMTYPE_RADHDR) {
		ibuf->ftype = IMB_FTYPE_RADHDR;
	}
#endif
	else if (ELEM(imtype, R_IMF_IMTYPE_PNG, R_IMF_IMTYPE_FFMPEG, R_IMF_IMTYPE_H264, R_IMF_IMTYPE_THEORA, R_IMF_IMTYPE_XVID)) {
		ibuf->ftype = IMB_FTYPE_PNG;

		if (imtype == R_IMF_IMTYPE_PNG) {
			if (imf->depth == R_IMF_CHAN_DEPTH_16)
				ibuf->foptions.flag |= PNG_16BIT;

			ibuf->foptions.quality = compress;
		}

	}
#ifdef WITH_DDS
	else if (imtype == R_IMF_IMTYPE_DDS) {
		ibuf->ftype = IMB_FTYPE_DDS;
	}
#endif
	else if (imtype == R_IMF_IMTYPE_BMP) {
		ibuf->ftype = IMB_FTYPE_BMP;
	}
#ifdef WITH_TIFF
	else if (imtype == R_IMF_IMTYPE_TIFF) {
		ibuf->ftype = IMB_FTYPE_TIF;

		if (imf->depth == R_IMF_CHAN_DEPTH_16) {
			ibuf->foptions.flag |= TIF_16BIT;
		}
		if (imf->tiff_codec == R_IMF_TIFF_CODEC_NONE) {
			ibuf->foptions.flag |= TIF_COMPRESS_NONE;
		}
		else if (imf->tiff_codec == R_IMF_TIFF_CODEC_DEFLATE) {
			ibuf->foptions.flag |= TIF_COMPRESS_DEFLATE;
		}
		else if (imf->tiff_codec == R_IMF_TIFF_CODEC_LZW) {
			ibuf->foptions.flag |= TIF_COMPRESS_LZW;
		}
		else if (imf->tiff_codec == R_IMF_TIFF_CODEC_PACKBITS) {
			ibuf->foptions.flag |= TIF_COMPRESS_PACKBITS;
		}
	}
#endif
#ifdef WITH_OPENEXR
	else if (ELEM(imtype, R_IMF_IMTYPE_OPENEXR, R_IMF_IMTYPE_MULTILAYER)) {
		ibuf->ftype = IMB_FTYPE_OPENEXR;
		if (imf->depth == R_IMF_CHAN_DEPTH_16)
			ibuf->foptions.flag |= OPENEXR_HALF;
		ibuf->foptions.flag |= (imf->exr_codec & OPENEXR_COMPRESS);

		if (!(imf->flag & R_IMF_FLAG_ZBUF)) {
			/* Signal for exr saving. */
			IMB_freezbuffloatImBuf(ibuf);
		}

	}
#endif
#ifdef WITH_CINEON
	else if (imtype == R_IMF_IMTYPE_CINEON) {
		ibuf->ftype = IMB_FTYPE_CINEON;
		if (imf->cineon_flag & R_IMF_CINEON_FLAG_LOG) {
			ibuf->foptions.flag |= CINEON_LOG;
		}
		if (imf->depth == R_IMF_CHAN_DEPTH_16) {
			ibuf->foptions.flag |= CINEON_16BIT;
		}
		else if (imf->depth == R_IMF_CHAN_DEPTH_12) {
			ibuf->foptions.flag |= CINEON_12BIT;
		}
		else if (imf->depth == R_IMF_CHAN_DEPTH_10) {
			ibuf->foptions.flag |= CINEON_10BIT;
		}
	}
	else if (imtype == R_IMF_IMTYPE_DPX) {
		ibuf->ftype = IMB_FTYPE_DPX;
		if (imf->cineon_flag & R_IMF_CINEON_FLAG_LOG) {
			ibuf->foptions.flag |= CINEON_LOG;
		}
		if (imf->depth == R_IMF_CHAN_DEPTH_16) {
			ibuf->foptions.flag |= CINEON_16BIT;
		}
		else if (imf->depth == R_IMF_CHAN_DEPTH_12) {
			ibuf->foptions.flag |= CINEON_12BIT;
		}
		else if (imf->depth == R_IMF_CHAN_DEPTH_10) {
			ibuf->foptions.flag |= CINEON_10BIT;
		}
	}
#endif
	else if (imtype == R_IMF_IMTYPE_TARGA) {
		ibuf->ftype = IMB_FTYPE_TGA;
	}
	else if (imtype == R_IMF_IMTYPE_RAWTGA) {
		ibuf->ftype = IMB_FTYPE_TGA;
		ibuf->foptions.flag = RAWTGA;
	}
#ifdef WITH_OPENJPEG
	else if (imtype == R_IMF_IMTYPE_JP2) {
		if (quality < 10) quality = 90;
		ibuf->ftype = IMB_FTYPE_JP2;
		ibuf->foptions.quality = quality;

		if (imf->depth == R_IMF_CHAN_DEPTH_16) {
			ibuf->foptions.flag |= JP2_16BIT;
		}
		else if (imf->depth == R_IMF_CHAN_DEPTH_12) {
			ibuf->foptions.flag |= JP2_12BIT;
		}

		if (imf->jp2_flag & R_IMF_JP2_FLAG_YCC) {
			ibuf->foptions.flag |= JP2_YCC;
		}

		if (imf->jp2_flag & R_IMF_JP2_FLAG_CINE_PRESET) {
			ibuf->foptions.flag |= JP2_CINE;
			if (imf->jp2_flag & R_IMF_JP2_FLAG_CINE_48)
				ibuf->foptions.flag |= JP2_CINE_48FPS;
		}

		if (imf->jp2_codec == R_IMF_JP2_CODEC_JP2)
			ibuf->foptions.flag |= JP2_JP2;
		else if (imf->jp2_codec == R_IMF_JP2_CODEC_J2K)
			ibuf->foptions.flag |= JP2_J2K;
		else
			BLI_assert(!"Unsupported jp2 codec was specified in im_format->jp2_codec");
	}
#endif
	else {
		/* R_IMF_IMTYPE_JPEG90, etc. default we save jpegs */
		if (quality < 10) quality = 90;
		ibuf->ftype = IMB_FTYPE_JPG;
		ibuf->foptions.quality = quality;
	}
}

int BKE_imbuf_write(ImBuf *ibuf, const char *name, const ImageFormatData *imf)
{
	int ok;

	BKE_imbuf_write_prepare(ibuf, imf);

	BLI_make_existing_file(name);

	ok = IMB_saveiff(ibuf, name, IB_rect | IB_zbuf | IB_zbuffloat);
	if (ok == 0) {
		perror(name);
	}

	return(ok);
}

/* same as BKE_imbuf_write() but crappy workaround not to permanently modify
 * _some_, values in the imbuf */
int BKE_imbuf_write_as(ImBuf *ibuf, const char *name, ImageFormatData *imf,
                       const bool save_copy)
{
	ImBuf ibuf_back = *ibuf;
	int ok;

	/* all data is rgba anyway,
	 * this just controls how to save for some formats */
	ibuf->planes = imf->planes;

	ok = BKE_imbuf_write(ibuf, name, imf);

	if (save_copy) {
		/* note that we are not restoring _all_ settings */
		ibuf->planes = ibuf_back.planes;
		ibuf->ftype =  ibuf_back.ftype;
		ibuf->foptions =  ibuf_back.foptions;
	}

	return ok;
}

static void do_makepicstring(
        char *string, const char *base, const char *relbase, int frame, const char imtype,
        const ImageFormatData *im_format, const short use_ext, const short use_frames,
        const char *suffix)
{
	if (string == NULL) return;
	BLI_strncpy(string, base, FILE_MAX - 10);   /* weak assumption */
	BLI_path_abs(string, relbase);

	if (use_frames)
		BLI_path_frame(string, frame, 4);

	if (suffix)
		BLI_path_suffix(string, FILE_MAX, suffix, "");

	if (use_ext)
		do_add_image_extension(string, imtype, im_format);
}

void BKE_image_path_from_imformat(
        char *string, const char *base, const char *relbase, int frame,
        const ImageFormatData *im_format, const bool use_ext, const bool use_frames, const char *suffix)
{
	do_makepicstring(string, base, relbase, frame, im_format->imtype, im_format, use_ext, use_frames, suffix);
}

void BKE_image_path_from_imtype(
        char *string, const char *base, const char *relbase, int frame,
        const char imtype, const bool use_ext, const bool use_frames, const char *view)
{
	do_makepicstring(string, base, relbase, frame, imtype, NULL, use_ext, use_frames, view);
}

/* ************************* New Image API *************** */


/* Notes about Image storage
 * - packedfile
 *   -> written in .blend
 * - filename
 *   -> written in .blend
 * - listbase
 *   -> ibufs from exrhandle
 * - ibuf
 *   -> comes from packedfile or filename or generated
 */


/* forces existence of 1 Image for renderout or nodes, returns Image */
/* name is only for default, when making new one */
Image *BKE_image_verify_viewer(Main *bmain, int type, const char *name)
{
	Image *ima;

	for (ima = bmain->image.first; ima; ima = ima->id.next)
		if (ima->source == IMA_SRC_VIEWER)
			if (ima->type == type)
				break;

	if (ima == NULL)
		ima = image_alloc(bmain, name, IMA_SRC_VIEWER, type);

	/* happens on reload, imagewindow cannot be image user when hidden*/
	if (ima->id.us == 0)
		id_us_plus(&ima->id);

	return ima;
}

void BKE_image_walk_all_users(const Main *mainp, void *customdata,
                              void callback(Image *ima, ImageUser *iuser, void *customdata))
{
	wmWindowManager *wm;
	wmWindow *win;
	Tex *tex;

	/* texture users */
	for (tex = mainp->tex.first; tex; tex = tex->id.next) {
		if (tex->type == TEX_IMAGE && tex->ima) {
			callback(tex->ima, &tex->iuser, customdata);
		}

	}

	/* image window, compo node users */
	for (wm = mainp->wm.first; wm; wm = wm->id.next) { /* only 1 wm */
		for (win = wm->windows.first; win; win = win->next) {
			ScrArea *sa;
			for (sa = win->screen->areabase.first; sa; sa = sa->next) {
				if (sa->spacetype == SPACE_VIEW3D) {
					View3D *v3d = sa->spacedata.first;
					BGpic *bgpic;
					for (bgpic = v3d->bgpicbase.first; bgpic; bgpic = bgpic->next) {
						callback(bgpic->ima, &bgpic->iuser, customdata);
					}
				}
				else if (sa->spacetype == SPACE_IMAGE) {
					SpaceImage *sima = sa->spacedata.first;
					callback(sima->image, &sima->iuser, customdata);
				}
			}
		}
	}
}

static void image_init_imageuser(Image *ima, ImageUser *iuser)
{
	iuser->multi_index = 0;
	iuser->layer = iuser->pass = iuser->view = 0;

}

void BKE_image_init_imageuser(Image *ima, ImageUser *iuser)
{
	image_init_imageuser(ima, iuser);
}

void BKE_image_signal(Main *bmain, Image *ima, ImageUser *iuser, int signal)
{
	if (ima == NULL)
		return;

	BLI_spin_lock(&image_spin);

	switch (signal) {
		case IMA_SIGNAL_FREE:
			BKE_image_free_buffers(ima);

			if (iuser) {
				iuser->ok = 1;
				if (iuser->scene) {
					image_update_views_format(ima, iuser);
				}
			}
			break;
		case IMA_SIGNAL_SRC_CHANGE:
			if (ima->type == IMA_TYPE_UV_TEST)
				if (ima->source != IMA_SRC_GENERATED)
					ima->type = IMA_TYPE_IMAGE;

			if (ima->source == IMA_SRC_GENERATED) {
				if (ima->gen_x == 0 || ima->gen_y == 0) {
					ImBuf *ibuf = image_get_cached_ibuf_for_index_frame(ima, IMA_NO_INDEX, 0);
					if (ibuf) {
						ima->gen_x = ibuf->x;
						ima->gen_y = ibuf->y;
						IMB_freeImBuf(ibuf);
					}
				}

				/* Changing source type to generated will likely change file format
				 * used by generated image buffer. Saving different file format to
				 * the old name might confuse other applications.
				 *
				 * Here we ensure original image path wouldn't be used when saving
				 * generated image.
				 */
				ima->name[0] = '\0';
			}

			/* image buffers for non-sequence multilayer will share buffers with RenderResult,
			 * however sequence multilayer will own buffers. Such logic makes switching from
			 * single multilayer file to sequence completely unstable
			 * since changes in nodes seems this workaround isn't needed anymore, all sockets
			 * are nicely detecting anyway, but freeing buffers always here makes multilayer
			 * sequences behave stable
			 */
			BKE_image_free_buffers(ima);

			ima->ok = 1;
			if (iuser)
				iuser->ok = 1;

			break;

		case IMA_SIGNAL_RELOAD:
			/* try to repack file */
			if (BKE_image_has_packedfile(ima)) {
				const int totfiles = image_num_files(ima);

				if (totfiles != BLI_listbase_count_at_most(&ima->packedfiles, totfiles + 1)) {
					/* in case there are new available files to be loaded */
					image_free_packedfiles(ima);
					BKE_image_packfiles(NULL, ima, ID_BLEND_PATH(bmain, &ima->id));
				}
				else {
					ImagePackedFile *imapf;
					for (imapf = ima->packedfiles.first; imapf; imapf = imapf->next) {
						PackedFile *pf;
						pf = newPackedFile(NULL, imapf->filepath, ID_BLEND_PATH(bmain, &ima->id));
						if (pf) {
							freePackedFile(imapf->packedfile);
							imapf->packedfile = pf;
						}
						else {
							printf("ERROR: Image \"%s\" not available. Keeping packed image\n", imapf->filepath);
						}
					}
				}

				if (BKE_image_has_packedfile(ima))
					BKE_image_free_buffers(ima);
			}
			else
				BKE_image_free_buffers(ima);

			if (iuser) {
				iuser->ok = 1;
				if (iuser->scene) {
					image_update_views_format(ima, iuser);
				}
			}

			break;
		case IMA_SIGNAL_USER_NEW_IMAGE:
			if (iuser) {
				iuser->ok = 1;
				if (ima->source == IMA_SRC_FILE) {
					if (ima->type == IMA_TYPE_MULTILAYER) {
						image_init_imageuser(ima, iuser);
					}
				}
			}
			break;
		case IMA_SIGNAL_COLORMANAGE:
			BKE_image_free_buffers(ima);

			ima->ok = 1;

			if (iuser)
				iuser->ok = 1;

			break;
	}

	BLI_spin_unlock(&image_spin);

}

bool BKE_image_is_openexr(struct Image *ima)
{
#ifdef WITH_OPENEXR
	if (ELEM(ima->source, IMA_SRC_FILE)) {
		return BLI_path_extension_check(ima->name, ".exr");
	}
#else
	UNUSED_VARS(ima);
#endif
	return false;
}

/**************************** multiview load openexr *********************************/

/* common stuff to do with images after loading */
static void image_initialize_after_load(Image *ima, ImBuf *ibuf)
{
	/* Preview is NULL when it has never been used as an icon before.
	 * Never handle previews/icons outside of main thread. */
	if (ima->preview == NULL && BLI_thread_is_main()) {
		BKE_icon_changed(BKE_icon_id_ensure(&ima->id));
	}

	/* fields */
	if (ima->flag & IMA_FIELDS) {
		if (ima->flag & IMA_STD_FIELD) de_interlace_st(ibuf);
		else de_interlace_ng(ibuf);
	}
	/* timer */
	BKE_image_tag_time(ima);

	ima->ok = IMA_OK_LOADED;

}

static int imbuf_alpha_flags_for_image(Image *ima)
{
	int flag = 0;

	if (ima->flag & IMA_IGNORE_ALPHA)
		flag |= IB_ignore_alpha;
	else if (ima->alpha_mode == IMA_ALPHA_PREMUL)
		flag |= IB_alphamode_premul;

	return flag;
}

/* the number of files will vary according to the stereo format */
static int image_num_files(Image *ima)
{
	return 1;
}

static ImBuf *load_image_single(
        Image *ima, ImageUser *iuser, int cfra,
        const int view_id,
        const bool has_packed,
        bool *r_assign)
{
	char filepath[FILE_MAX];
	struct ImBuf *ibuf = NULL;
	int flag;

	/* is there a PackedFile with this image ? */
	if (has_packed) {
		ImagePackedFile *imapf;

		flag = IB_rect | IB_multilayer;
		flag |= imbuf_alpha_flags_for_image(ima);

		imapf = BLI_findlink(&ima->packedfiles, view_id);
		if (imapf->packedfile) {
			ibuf = IMB_ibImageFromMemory(
			       (unsigned char *)imapf->packedfile->data, imapf->packedfile->size, flag,
			       ima->colorspace_settings.name, "<packed data>");
		}
	}
	else {
		ImageUser iuser_t;

		flag = IB_rect | IB_multilayer | IB_metadata;
		flag |= imbuf_alpha_flags_for_image(ima);

		if (iuser)
			iuser_t = *iuser;
		else
			iuser_t.framenr = ima->lastframe;

		iuser_t.view = view_id;

		BKE_image_user_file_path(&iuser_t, ima, filepath);

		/* read ibuf */
		ibuf = IMB_loadiffname(filepath, flag, ima->colorspace_settings.name);
	}

	if (ibuf) {
		{
			image_initialize_after_load(ima, ibuf);
			*r_assign = true;

			/* check if the image is a font image... */
			detectBitmapFont(ibuf);

			/* make packed file for autopack */
			if ((has_packed == false) && (G.fileflags & G_AUTOPACK)) {
				ImagePackedFile *imapf = MEM_mallocN(sizeof(ImagePackedFile), "Image Packefile");
				BLI_addtail(&ima->packedfiles, imapf);

				STRNCPY(imapf->filepath, filepath);
				imapf->packedfile = newPackedFile(NULL, filepath, ID_BLEND_PATH_FROM_GLOBAL(&ima->id));
			}
		}
	}
	else {
		ima->ok = 0;
	}

	return ibuf;
}

/* warning, 'iuser' can be NULL
 * note: Image->views was already populated (in image_update_views_format)
 */
static ImBuf *image_load_image_file(Image *ima, ImageUser *iuser, int cfra)
{
	struct ImBuf *ibuf = NULL;
	bool assign = false;
	const bool is_multiview = false;
	const int totfiles = image_num_files(ima);
	bool has_packed = BKE_image_has_packedfile(ima);

	/* always ensure clean ima */
	BKE_image_free_buffers(ima);

	/* this should never happen, but just playing safe */
	if (has_packed) {
		if (totfiles != BLI_listbase_count_at_most(&ima->packedfiles, totfiles + 1)) {
			image_free_packedfiles(ima);
			has_packed = false;
		}
	}

	if (!is_multiview) {
		ibuf = load_image_single(ima, iuser, cfra, 0, has_packed, &assign);
		if (assign) {
			image_assign_ibuf(ima, ibuf, IMA_NO_INDEX, 0);
		}
	}
	else {
		struct ImBuf **ibuf_arr;
		const int totviews = BLI_listbase_count(&ima->views);
		int i;
		BLI_assert(totviews > 0);

		ibuf_arr = MEM_callocN(sizeof(ImBuf *) * totviews, "Image Views Imbufs");

		for (i = 0; i < totfiles; i++)
			ibuf_arr[i] = load_image_single(ima, iuser, cfra, i, has_packed, &assign);

		/* return the original requested ImBuf */
		i = (iuser && iuser->multi_index < totviews) ? iuser->multi_index : 0;
		ibuf = ibuf_arr[i];

		if (assign) {
			for (i = 0; i < totviews; i++) {
				image_assign_ibuf(ima, ibuf_arr[i], i, 0);
			}
		}

		/* "remove" the others (decrease their refcount) */
		for (i = 0; i < totviews; i++) {
			if (ibuf_arr[i] != ibuf) {
				IMB_freeImBuf(ibuf_arr[i]);
			}
		}

		/* cleanup */
		MEM_freeN(ibuf_arr);
	}

	if (iuser)
		iuser->ok = ima->ok;

	return ibuf;
}


/* Get the ibuf from an image cache for a given image user.
 *
 * Returns referenced image buffer if it exists, callee is to
 * call IMB_freeImBuf to de-reference the image buffer after
 * it's done handling it.
 */
static ImBuf *image_get_cached_ibuf(Image *ima, ImageUser *iuser, int *r_frame, int *r_index)
{
	ImBuf *ibuf = NULL;
	int frame = 0, index = 0;

	/* see if we already have an appropriate ibuf, with image source and type */
	if (ima->source == IMA_SRC_FILE) {
		if (ima->type == IMA_TYPE_IMAGE)
			ibuf = image_get_cached_ibuf_for_index_frame(ima, index, 0);
		else if (ima->type == IMA_TYPE_MULTILAYER)
			ibuf = image_get_cached_ibuf_for_index_frame(ima, index, 0);
	}
	else if (ima->source == IMA_SRC_GENERATED) {
		ibuf = image_get_cached_ibuf_for_index_frame(ima, index, 0);
	}
	else if (ima->source == IMA_SRC_VIEWER) {
		/* always verify entirely, not that this shouldn't happen
		 * as part of texture sampling in rendering anyway, so not
		 * a big bottleneck */
	}

	if (r_frame)
		*r_frame = frame;

	if (r_index)
		*r_index = index;

	return ibuf;
}

BLI_INLINE bool image_quick_test(Image *ima, ImageUser *iuser)
{
	if (ima == NULL)
		return false;

	if (iuser) {
		if (iuser->ok == 0)
			return false;
	}
	else if (ima->ok == 0)
		return false;

	return true;
}

/* Checks optional ImageUser and verifies/creates ImBuf.
 *
 * not thread-safe, so callee should worry about thread locks
 */
static ImBuf *image_acquire_ibuf(Image *ima, ImageUser *iuser, void **r_lock)
{
	ImBuf *ibuf = NULL;
	int frame = 0, index = 0;

	if (r_lock)
		*r_lock = NULL;

	/* quick reject tests */
	if (!image_quick_test(ima, iuser))
		return NULL;

	ibuf = image_get_cached_ibuf(ima, iuser, &frame, &index);

	if (ibuf == NULL) {
		/* we are sure we have to load the ibuf, using source and type */
		if (ima->source == IMA_SRC_FILE) {

			if (ima->type == IMA_TYPE_IMAGE)
				ibuf = image_load_image_file(ima, iuser, frame);  /* cfra only for '#', this global is OK */
			/* no else; on load the ima type can change */
		}
		else if (ima->source == IMA_SRC_GENERATED) {
			/* generated is: ibuf is allocated dynamically */
			/* UV testgrid or black or solid etc */
			if (ima->gen_x == 0) ima->gen_x = 1024;
			if (ima->gen_y == 0) ima->gen_y = 1024;
			if (ima->gen_depth == 0) ima->gen_depth = 24;
			ibuf = add_ibuf_size(ima->gen_x, ima->gen_y, ima->name, ima->gen_depth, (ima->gen_flag & IMA_GEN_FLOAT) != 0, ima->gen_type,
			                     ima->gen_color, &ima->colorspace_settings);
			image_assign_ibuf(ima, ibuf, index, 0);
			ima->ok = IMA_OK_LOADED;
		}

		ibuf->userflags |= IB_PERSISTENT;
	}

	BKE_image_tag_time(ima);

	return ibuf;
}

/* return image buffer for given image and user
 *
 * - will lock render result if image type is render result and lock is not NULL
 * - will return NULL if image type if render or composite result and lock is NULL
 *
 * references the result, BKE_image_release_ibuf should be used to de-reference
 */
ImBuf *BKE_image_acquire_ibuf(Image *ima, ImageUser *iuser, void **r_lock)
{
	ImBuf *ibuf;

	BLI_spin_lock(&image_spin);

	ibuf = image_acquire_ibuf(ima, iuser, r_lock);

	BLI_spin_unlock(&image_spin);

	return ibuf;
}

void BKE_image_release_ibuf(Image *ima, ImBuf *ibuf, void *lock)
{
	if (lock != NULL) {
		/* for getting image during threaded render / compositing, need to release */
		if (lock == ima) {
			BLI_thread_unlock(LOCK_VIEWER); /* viewer image */
		}
		else {
			BLI_thread_unlock(LOCK_VIEWER); /* view image imbuf */
		}
	}

	if (ibuf) {
		BLI_spin_lock(&image_spin);
		IMB_freeImBuf(ibuf);
		BLI_spin_unlock(&image_spin);
	}
}

/* checks whether there's an image buffer for given image and user */
bool BKE_image_has_ibuf(Image *ima, ImageUser *iuser)
{
	ImBuf *ibuf;

	/* quick reject tests */
	if (!image_quick_test(ima, iuser))
		return false;

	BLI_spin_lock(&image_spin);

	ibuf = image_get_cached_ibuf(ima, iuser, NULL, NULL);

	if (!ibuf)
		ibuf = image_acquire_ibuf(ima, iuser, NULL);

	BLI_spin_unlock(&image_spin);

	IMB_freeImBuf(ibuf);

	return ibuf != NULL;
}

/* ******** Pool for image buffers ********  */

typedef struct ImagePoolEntry {
	struct ImagePoolEntry *next, *prev;
	Image *image;
	ImBuf *ibuf;
	int index;
	int frame;
} ImagePoolEntry;

typedef struct ImagePool {
	ListBase image_buffers;
	BLI_mempool *memory_pool;
} ImagePool;

ImagePool *BKE_image_pool_new(void)
{
	ImagePool *pool = MEM_callocN(sizeof(ImagePool), "Image Pool");
	pool->memory_pool = BLI_mempool_create(sizeof(ImagePoolEntry), 0, 128, BLI_MEMPOOL_NOP);

	return pool;
}

void BKE_image_pool_free(ImagePool *pool)
{
	/* Use single lock to dereference all the image buffers. */
	BLI_spin_lock(&image_spin);
	for (ImagePoolEntry *entry = pool->image_buffers.first;
	     entry != NULL;
	     entry = entry->next)
	{
		if (entry->ibuf) {
			IMB_freeImBuf(entry->ibuf);
		}
	}
	BLI_spin_unlock(&image_spin);

	BLI_mempool_destroy(pool->memory_pool);
	MEM_freeN(pool);
}

BLI_INLINE ImBuf *image_pool_find_entry(ImagePool *pool, Image *image, int frame, int index, bool *found)
{
	ImagePoolEntry *entry;

	*found = false;

	for (entry = pool->image_buffers.first; entry; entry = entry->next) {
		if (entry->image == image && entry->frame == frame && entry->index == index) {
			*found = true;
			return entry->ibuf;
		}
	}

	return NULL;
}

ImBuf *BKE_image_pool_acquire_ibuf(Image *ima, ImageUser *iuser, ImagePool *pool)
{
	ImBuf *ibuf;
	int index = 0, frame = 0;
	bool found;

	if (!image_quick_test(ima, iuser))
		return NULL;

	if (pool == NULL) {
		/* pool could be NULL, in this case use general acquire function */
		return BKE_image_acquire_ibuf(ima, iuser, NULL);
	}

	ibuf = image_pool_find_entry(pool, ima, frame, index, &found);
	if (found)
		return ibuf;

	BLI_spin_lock(&image_spin);

	ibuf = image_pool_find_entry(pool, ima, frame, index, &found);

	/* will also create entry even in cases image buffer failed to load,
	 * prevents trying to load the same buggy file multiple times
	 */
	if (!found) {
		ImagePoolEntry *entry;

		ibuf = image_acquire_ibuf(ima, iuser, NULL);

		entry = BLI_mempool_alloc(pool->memory_pool);
		entry->image = ima;
		entry->frame = frame;
		entry->index = index;
		entry->ibuf = ibuf;

		BLI_addtail(&pool->image_buffers, entry);
	}

	BLI_spin_unlock(&image_spin);

	return ibuf;
}

void BKE_image_pool_release_ibuf(Image *ima, ImBuf *ibuf, ImagePool *pool)
{
	/* if pool wasn't actually used, use general release stuff,
	 * for pools image buffers will be dereferenced on pool free
	 */
	if (pool == NULL) {
		BKE_image_release_ibuf(ima, ibuf, NULL);
	}
}

void BKE_image_user_file_path(ImageUser *iuser, Image *ima, char *filepath)
{
	BLI_strncpy(filepath, ima->name, FILE_MAX);

	BLI_path_abs(filepath, ID_BLEND_PATH_FROM_GLOBAL(&ima->id));
}

bool BKE_image_has_alpha(struct Image *image)
{
	ImBuf *ibuf;
	void *lock;
	int planes;

	ibuf = BKE_image_acquire_ibuf(image, NULL, &lock);
	planes = (ibuf ? ibuf->planes : 0);
	BKE_image_release_ibuf(image, ibuf, lock);

	if (planes == 32)
		return true;
	else
		return false;
}

void BKE_image_get_size(Image *image, ImageUser *iuser, int *width, int *height)
{
	ImBuf *ibuf = NULL;
	void *lock;

	if (image != NULL) {
		ibuf = BKE_image_acquire_ibuf(image, iuser, &lock);
	}

	if (ibuf && ibuf->x > 0 && ibuf->y > 0) {
		*width = ibuf->x;
		*height = ibuf->y;
	}
	else {
		*width  = IMG_SIZE_FALLBACK;
		*height = IMG_SIZE_FALLBACK;
	}

	if (image != NULL) {
		BKE_image_release_ibuf(image, ibuf, lock);
	}
}

void BKE_image_get_size_fl(Image *image, ImageUser *iuser, float size[2])
{
	int width, height;
	BKE_image_get_size(image, iuser, &width, &height);

	size[0] = (float)width;
	size[1] = (float)height;

}

void BKE_image_get_aspect(Image *image, float *aspx, float *aspy)
{
	*aspx = 1.0;

	/* x is always 1 */
	if (image)
		*aspy = image->aspy / image->aspx;
	else
		*aspy = 1.0f;
}

unsigned char *BKE_image_get_pixels_for_frame(struct Image *image, int frame)
{
	ImageUser iuser = {NULL};
	void *lock;
	ImBuf *ibuf;
	unsigned char *pixels = NULL;

	iuser.framenr = frame;
	iuser.ok = true;

	ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

	if (ibuf) {
		pixels = (unsigned char *) ibuf->rect;

		if (pixels)
			pixels = MEM_dupallocN(pixels);

		BKE_image_release_ibuf(image, ibuf, lock);
	}

	if (!pixels)
		return NULL;

	return pixels;
}

float *BKE_image_get_float_pixels_for_frame(struct Image *image, int frame)
{
	ImageUser iuser = {NULL};
	void *lock;
	ImBuf *ibuf;
	float *pixels = NULL;

	iuser.framenr = frame;
	iuser.ok = true;

	ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

	if (ibuf) {
		pixels = ibuf->rect_float;

		if (pixels)
			pixels = MEM_dupallocN(pixels);

		BKE_image_release_ibuf(image, ibuf, lock);
	}

	if (!pixels)
		return NULL;

	return pixels;
}

int BKE_image_sequence_guess_offset(Image *image)
{
	return BLI_stringdec(image->name, NULL, NULL, NULL);
}

bool BKE_image_has_anim(Image *ima)
{
	return (BLI_listbase_is_empty(&ima->anims) == false);
}

bool BKE_image_has_packedfile(Image *ima)
{
	return (BLI_listbase_is_empty(&ima->packedfiles) == false);
}

bool BKE_image_is_dirty(Image *image)
{
	bool is_dirty = false;

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
			if (ibuf->userflags & IB_BITMAPDIRTY) {
				is_dirty = true;
				break;
			}
			IMB_moviecacheIter_step(iter);
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);

	return is_dirty;
}

void BKE_image_file_format_set(Image *image, int ftype, const ImbFormatOptions *options)
{
#if 0
	ImBuf *ibuf = BKE_image_acquire_ibuf(image, NULL, NULL);
	if (ibuf) {
		ibuf->ftype = ftype;
		ibuf->foptions = options;
	}
	BKE_image_release_ibuf(image, ibuf, NULL);
#endif

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			ImBuf *ibuf = IMB_moviecacheIter_getImBuf(iter);
			ibuf->ftype = ftype;
			ibuf->foptions = *options;
			IMB_moviecacheIter_step(iter);
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);
}

bool BKE_image_has_loaded_ibuf(Image *image)
{
	bool has_loaded_ibuf = false;

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			has_loaded_ibuf = true;
			break;
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);

	return has_loaded_ibuf;
}

/* References the result, BKE_image_release_ibuf is to be called to de-reference.
 * Use lock=NULL when calling BKE_image_release_ibuf().
 */
ImBuf *BKE_image_get_ibuf_with_name(Image *image, const char *name)
{
	ImBuf *ibuf = NULL;

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			ImBuf *current_ibuf = IMB_moviecacheIter_getImBuf(iter);
			if (STREQ(current_ibuf->name, name)) {
				ibuf = current_ibuf;
				IMB_refImBuf(ibuf);
				break;
			}
			IMB_moviecacheIter_step(iter);
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);

	return ibuf;
}

/* References the result, BKE_image_release_ibuf is to be called to de-reference.
 * Use lock=NULL when calling BKE_image_release_ibuf().
 *
 * TODO(sergey): This is actually "get first entry from the cache", which is
 *               not so much predictable. But using first loaded image buffer
 *               was also malicious logic and all the areas which uses this
 *               function are to be re-considered.
 */
ImBuf *BKE_image_get_first_ibuf(Image *image)
{
	ImBuf *ibuf = NULL;

	BLI_spin_lock(&image_spin);
	if (image->cache != NULL) {
		struct MovieCacheIter *iter = IMB_moviecacheIter_new(image->cache);

		while (!IMB_moviecacheIter_done(iter)) {
			ibuf = IMB_moviecacheIter_getImBuf(iter);
			IMB_refImBuf(ibuf);
			break;
		}
		IMB_moviecacheIter_free(iter);
	}
	BLI_spin_unlock(&image_spin);

	return ibuf;
}

static void image_update_views_format(Image *ima, ImageUser *iuser)
{

}
