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

/** \file blender/editors/space_image/image_buttons.c
 *  \ingroup spimage
 */

#include <string.h>
#include <stdio.h>

#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_screen.h"
#include "BKE_scene.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "ED_screen.h"
#include "ED_image.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "image_intern.h"

#define B_NOP -1
#define MAX_IMAGE_INFO_LEN  128

/* proto */

static void image_info(Scene *scene, ImageUser *iuser, Image *ima, ImBuf *ibuf, char *str, size_t len)
{
	size_t ofs = 0;

	str[0] = 0;
	if (ima == NULL)
		return;

	if (ibuf == NULL) {
		ofs += BLI_strncpy_rlen(str + ofs, IFACE_("Can't Load Image"), len - ofs);
	}
	else {
		ofs += BLI_strncpy_rlen(str, IFACE_("Image"), len - ofs);

		ofs += BLI_snprintf(str + ofs, len - ofs, IFACE_(": size %d x %d,"), ibuf->x, ibuf->y);

		if (ibuf->rect_float) {
			if (ibuf->channels != 4) {
				ofs += BLI_snprintf(str + ofs, len - ofs, IFACE_("%d float channel(s)"), ibuf->channels);
			}
			else if (ibuf->planes == R_IMF_PLANES_RGBA)
				ofs += BLI_strncpy_rlen(str + ofs, IFACE_(" RGBA float"), len - ofs);
			else
				ofs += BLI_strncpy_rlen(str + ofs, IFACE_(" RGB float"), len - ofs);
		}
		else {
			if (ibuf->planes == R_IMF_PLANES_RGBA)
				ofs += BLI_strncpy_rlen(str + ofs, IFACE_(" RGBA byte"), len - ofs);
			else
				ofs += BLI_strncpy_rlen(str + ofs, IFACE_(" RGB byte"), len - ofs);
		}
		if (ibuf->zbuf || ibuf->zbuf_float)
			ofs += BLI_strncpy_rlen(str + ofs, IFACE_(" + Z"), len - ofs);

	}

}

/* ************ panel stuff ************* */

#if 0
/* 0: disable preview
 * otherwise refresh preview
 *
 * XXX if you put this back, also check XXX in image_main_region_draw() */
 * /
void image_preview_event(int event)
{
	int exec = 0;

	if (event == 0) {
		G.scene->r.scemode &= ~R_COMP_CROP;
		exec = 1;
	}
	else {
		if (image_preview_active(curarea, NULL, NULL)) {
			G.scene->r.scemode |= R_COMP_CROP;
			exec = 1;
		}
		else
			G.scene->r.scemode &= ~R_COMP_CROP;
	}

}


/* nothing drawn here, we use it to store values */
static void preview_cb(ScrArea *sa, struct uiBlock *block)
{
	SpaceImage *sima = sa->spacedata.first;
	rctf dispf;
	rcti *disprect = &G.scene->r.disprect;
	int winx = (G.scene->r.size * G.scene->r.xsch) / 100;
	int winy = (G.scene->r.size * G.scene->r.ysch) / 100;
	int mval[2];

	if (G.scene->r.mode & R_BORDER) {
		winx *= BLI_rcti_size_x(&G.scene->r.border);
		winy *= BLI_rctf_size_y(&G.scene->r.border);
	}

	/* while dragging we need to update the rects, otherwise it doesn't end with correct one */

	BLI_rctf_init(&dispf, 15.0f, BLI_rcti_size_x(&block->rect) - 15.0f, 15.0f, (BLI_rctf_size_y(&block->rect)) - 15.0f);
	ui_graphics_to_window_rct(sa->win, &dispf, disprect);

	/* correction for gla draw */
	BLI_rcti_translate(disprect, -curarea->winrct.xmin, -curarea->winrct.ymin);

	calc_image_view(sima, 'p');
//	printf("winrct %d %d %d %d\n", disprect->xmin, disprect->ymin, disprect->xmax, disprect->ymax);
	/* map to image space coordinates */
	mval[0] = disprect->xmin; mval[1] = disprect->ymin;
	areamouseco_to_ipoco(v2d, mval, &dispf.xmin, &dispf.ymin);
	mval[0] = disprect->xmax; mval[1] = disprect->ymax;
	areamouseco_to_ipoco(v2d, mval, &dispf.xmax, &dispf.ymax);

	/* map to render coordinates */
	disprect->xmin = dispf.xmin;
	disprect->xmax = dispf.xmax;
	disprect->ymin = dispf.ymin;
	disprect->ymax = dispf.ymax;

	CLAMP(disprect->xmin, 0, winx);
	CLAMP(disprect->xmax, 0, winx);
	CLAMP(disprect->ymin, 0, winy);
	CLAMP(disprect->ymax, 0, winy);
//	printf("drawrct %d %d %d %d\n", disprect->xmin, disprect->ymin, disprect->xmax, disprect->ymax);

}

static bool is_preview_allowed(ScrArea *cur)
{
	SpaceImage *sima = cur->spacedata.first;
	ScrArea *sa;

	/* check if another areawindow has preview set */
	for (sa = G.curscreen->areabase.first; sa; sa = sa->next) {
		if (sa != cur && sa->spacetype == SPACE_IMAGE) {
			if (image_preview_active(sa, NULL, NULL))
				return 0;
		}
	}
	/* check image type */
	if (sima->image == NULL || sima->image->type != IMA_TYPE_COMPOSITE)
		return 0;

	return 1;
}


static void image_panel_preview(ScrArea *sa, short cntrl)   // IMAGE_HANDLER_PREVIEW
{
	uiBlock *block;
	SpaceImage *sima = sa->spacedata.first;
	int ofsx, ofsy;

	if (is_preview_allowed(sa) == 0) {
		rem_blockhandler(sa, IMAGE_HANDLER_PREVIEW);
		G.scene->r.scemode &= ~R_COMP_CROP; /* quite weak */
		return;
	}

	block = UI_block_begin(C, ar, __func__, UI_EMBOSS);
	uiPanelControl(UI_PNL_SOLID | UI_PNL_CLOSE | UI_PNL_SCALE | cntrl);
	uiSetPanelHandler(IMAGE_HANDLER_PREVIEW);  // for close and esc

	ofsx = -150 + (sa->winx / 2) / sima->blockscale;
	ofsy = -100 + (sa->winy / 2) / sima->blockscale;
	if (uiNewPanel(C, ar, block, "Preview", "Image", ofsx, ofsy, 300, 200) == 0) return;

	UI_but_func_drawextra_set(block, preview_cb);

}
#endif


/* ********************* callbacks for standard image buttons *************** */

/* workaround for passing many args */
struct ImageUI_Data {
	Image *image;
	ImageUser *iuser;
	int rpass_index;
};

/**************************** view menus *****************************/
// XXX HACK!
// static int packdummy=0;

typedef struct RNAUpdateCb {
	PointerRNA ptr;
	PropertyRNA *prop;
	ImageUser *iuser;
} RNAUpdateCb;

static void rna_update_cb(bContext *C, void *arg_cb, void *UNUSED(arg))
{
	RNAUpdateCb *cb = (RNAUpdateCb *)arg_cb;

	/* ideally this would be done by RNA itself, but there we have
	 * no image user available, so we just update this flag here */
	cb->iuser->ok = 1;

	/* we call update here on the pointer property, this way the
	 * owner of the image pointer can still define it's own update
	 * and notifier */
	RNA_property_update(C, &cb->ptr, cb->prop);
}

void uiTemplateImage(uiLayout *layout, bContext *C, PointerRNA *ptr, const char *propname, PointerRNA *userptr, bool compact, bool multiview)
{
	PropertyRNA *prop;
	PointerRNA imaptr;
	RNAUpdateCb *cb;
	Image *ima;
	ImageUser *iuser;
	Scene *scene = CTX_data_scene(C);
	uiLayout *row, *split, *col;
	uiBlock *block;
	char str[MAX_IMAGE_INFO_LEN];

	void *lock;

	if (!ptr->data)
		return;

	prop = RNA_struct_find_property(ptr, propname);
	if (!prop) {
		printf("%s: property not found: %s.%s\n",
		       __func__, RNA_struct_identifier(ptr->type), propname);
		return;
	}

	if (RNA_property_type(prop) != PROP_POINTER) {
		printf("%s: expected pointer property for %s.%s\n",
		       __func__, RNA_struct_identifier(ptr->type), propname);
		return;
	}

	block = uiLayoutGetBlock(layout);

	imaptr = RNA_property_pointer_get(ptr, prop);
	ima = imaptr.data;
	iuser = userptr->data;

	cb = MEM_callocN(sizeof(RNAUpdateCb), "RNAUpdateCb");
	cb->ptr = *ptr;
	cb->prop = prop;
	cb->iuser = iuser;

	uiLayoutSetContextPointer(layout, "edit_image", &imaptr);
	uiLayoutSetContextPointer(layout, "edit_image_user", userptr);

	if (!compact) {
		uiTemplateID(
		        layout, C, ptr, propname,
		        ima ? NULL : "IMAGE_OT_new", "IMAGE_OT_open", NULL, UI_TEMPLATE_ID_FILTER_ALL);
	}

	if (ima) {
		UI_block_funcN_set(block, rna_update_cb, MEM_dupallocN(cb), NULL);

		if (ima->source == IMA_SRC_VIEWER) {
			ImBuf *ibuf = BKE_image_acquire_ibuf(ima, iuser, &lock);
			image_info(scene, iuser, ima, ibuf, str, MAX_IMAGE_INFO_LEN);
			BKE_image_release_ibuf(ima, ibuf, lock);

			uiItemL(layout, ima->id.name + 2, ICON_NONE);
			uiItemL(layout, str, ICON_NONE);

		}
		else {
			uiItemR(layout, &imaptr, "source", 0, NULL, ICON_NONE);

			if (ima->source != IMA_SRC_GENERATED) {
				row = uiLayoutRow(layout, true);
				if (BKE_image_has_packedfile(ima))
					uiItemO(row, "", ICON_PACKAGE, "image.unpack");
				else
					uiItemO(row, "", ICON_UGLYPACKAGE, "image.pack");

				row = uiLayoutRow(row, true);
				uiLayoutSetEnabled(row, BKE_image_has_packedfile(ima) == false);
				uiItemR(row, &imaptr, "filepath", 0, "", ICON_NONE);
				uiItemO(row, "", ICON_FILE_REFRESH, "image.reload");
			}

			if (ima->source != IMA_SRC_GENERATED) {
				if (compact == 0) {
					uiTemplateImageInfo(layout, C, ima, iuser);
				}
			}

			col = uiLayoutColumn(layout, false);
			uiTemplateColorspaceSettings(col, &imaptr, "colorspace_settings");
			uiItemR(col, &imaptr, "use_view_as_render", 0, NULL, ICON_NONE);

			if (ima->source != IMA_SRC_GENERATED) {
				if (compact == 0) { /* background image view doesn't need these */
					ImBuf *ibuf = BKE_image_acquire_ibuf(ima, iuser, NULL);
					bool has_alpha = true;

					if (ibuf) {
						int imtype = BKE_image_ftype_to_imtype(ibuf->ftype, &ibuf->foptions);
						char valid_channels = BKE_imtype_valid_channels(imtype, false);

						has_alpha = (valid_channels & IMA_CHAN_FLAG_ALPHA) != 0;

						BKE_image_release_ibuf(ima, ibuf, NULL);
					}

					if (has_alpha) {
						col = uiLayoutColumn(layout, false);
						uiItemR(col, &imaptr, "use_alpha", 0, NULL, ICON_NONE);
						row = uiLayoutRow(col, false);
						uiLayoutSetActive(row, RNA_boolean_get(&imaptr, "use_alpha"));
						uiItemR(row, &imaptr, "alpha_mode", 0, IFACE_("Alpha"), ICON_NONE);
					}

					split = uiLayoutSplit(layout, 0.0f, false);

					col = uiLayoutColumn(split, false);
					/* XXX Why only display fields_per_frame only for video image types?
					 *     And why allow fields for non-video image types at all??? */
					uiItemR(col, &imaptr, "use_fields", 0, NULL, ICON_NONE);

					row = uiLayoutRow(col, false);
					uiLayoutSetActive(row, RNA_boolean_get(&imaptr, "use_fields"));
					uiItemR(row, &imaptr, "field_order", UI_ITEM_R_EXPAND, NULL, ICON_NONE);
				}
			}

			if (ima->source == IMA_SRC_GENERATED) {
				split = uiLayoutSplit(layout, 0.0f, false);

				col = uiLayoutColumn(split, true);
				uiItemR(col, &imaptr, "generated_width", 0, "X", ICON_NONE);
				uiItemR(col, &imaptr, "generated_height", 0, "Y", ICON_NONE);

				uiItemR(col, &imaptr, "use_generated_float", 0, NULL, ICON_NONE);

				uiItemR(split, &imaptr, "generated_type", UI_ITEM_R_EXPAND, NULL, ICON_NONE);

				if (ima->gen_type == IMA_GENTYPE_BLANK) {
					uiItemR(layout, &imaptr, "generated_color", 0, NULL, ICON_NONE);
				}
			}

		}

		UI_block_funcN_set(block, NULL, NULL, NULL);
	}

	MEM_freeN(cb);
}

void uiTemplateImageSettings(uiLayout *layout, PointerRNA *imfptr, bool color_management)
{
	ImageFormatData *imf = imfptr->data;
	ID *id = imfptr->id.data;
	PointerRNA display_settings_ptr;
	PropertyRNA *prop;
	const int depth_ok = BKE_imtype_valid_depths(imf->imtype);
	/* some settings depend on this being a scene thats rendered */
	const bool is_render_out = (id && GS(id->name) == ID_SCE);

	uiLayout *col, *row, *split, *sub;
	bool show_preview = false;

	col = uiLayoutColumn(layout, false);

	split = uiLayoutSplit(col, 0.5f, false);

	uiItemR(split, imfptr, "file_format", 0, "", ICON_NONE);
	sub = uiLayoutRow(split, false);
	uiItemR(sub, imfptr, "color_mode", UI_ITEM_R_EXPAND, IFACE_("Color"), ICON_NONE);

	/* only display depth setting if multiple depths can be used */
	if ((ELEM(depth_ok,
	          R_IMF_CHAN_DEPTH_1,
	          R_IMF_CHAN_DEPTH_8,
	          R_IMF_CHAN_DEPTH_10,
	          R_IMF_CHAN_DEPTH_12,
	          R_IMF_CHAN_DEPTH_16,
	          R_IMF_CHAN_DEPTH_24,
	          R_IMF_CHAN_DEPTH_32)) == 0)
	{
		row = uiLayoutRow(col, false);

		uiItemL(row, IFACE_("Color Depth:"), ICON_NONE);
		uiItemR(row, imfptr, "color_depth", UI_ITEM_R_EXPAND, NULL, ICON_NONE);
	}

	if (BKE_imtype_supports_quality(imf->imtype)) {
		uiItemR(col, imfptr, "quality", 0, NULL, ICON_NONE);
	}

	if (BKE_imtype_supports_compress(imf->imtype)) {
		uiItemR(col, imfptr, "compression", 0, NULL, ICON_NONE);
	}

	row = uiLayoutRow(col, false);
	if (BKE_imtype_supports_zbuf(imf->imtype)) {
		uiItemR(row, imfptr, "use_zbuffer", 0, NULL, ICON_NONE);
	}

	if (is_render_out && ELEM(imf->imtype, R_IMF_IMTYPE_OPENEXR)) {
		show_preview = true;
		uiItemR(row, imfptr, "use_preview", 0, NULL, ICON_NONE);
	}

	if (imf->imtype == R_IMF_IMTYPE_JP2) {
		uiItemR(col, imfptr, "jpeg2k_codec", 0, NULL, ICON_NONE);

		row = uiLayoutRow(col, false);
		uiItemR(row, imfptr, "use_jpeg2k_cinema_preset", 0, NULL, ICON_NONE);
		uiItemR(row, imfptr, "use_jpeg2k_cinema_48", 0, NULL, ICON_NONE);

		uiItemR(col, imfptr, "use_jpeg2k_ycc", 0, NULL, ICON_NONE);
	}

	if (imf->imtype == R_IMF_IMTYPE_DPX) {
		uiItemR(col, imfptr, "use_cineon_log", 0, NULL, ICON_NONE);
	}

	if (imf->imtype == R_IMF_IMTYPE_CINEON) {
#if 1
		uiItemL(col, IFACE_("Hard coded Non-Linear, Gamma:1.7"), ICON_NONE);
#else
		uiItemR(col, imfptr, "use_cineon_log", 0, NULL, ICON_NONE);
		uiItemR(col, imfptr, "cineon_black", 0, NULL, ICON_NONE);
		uiItemR(col, imfptr, "cineon_white", 0, NULL, ICON_NONE);
		uiItemR(col, imfptr, "cineon_gamma", 0, NULL, ICON_NONE);
#endif
	}

	if (imf->imtype == R_IMF_IMTYPE_TIFF) {
		uiItemR(col, imfptr, "tiff_codec", 0, NULL, ICON_NONE);
	}

	/* color management */
	if (color_management &&
	    (!BKE_imtype_requires_linear_float(imf->imtype) ||
	     (show_preview && imf->flag & R_IMF_FLAG_PREVIEW_JPG)))
	{
		prop = RNA_struct_find_property(imfptr, "display_settings");
		display_settings_ptr = RNA_property_pointer_get(imfptr, prop);

		col = uiLayoutColumn(layout, false);
		uiItemL(col, IFACE_("Color Management"), ICON_NONE);

		uiItemR(col, &display_settings_ptr, "display_device", 0, NULL, ICON_NONE);

		uiTemplateColormanagedViewSettings(col, NULL, imfptr, "view_settings");
	}
}

static void uiTemplateViewsFormat(uiLayout *layout, PointerRNA *ptr, PointerRNA *stereo3d_format_ptr)
{
	uiLayout *col;

	col = uiLayoutColumn(layout, false);

	uiItemL(col, IFACE_("Views Format:"), ICON_NONE);
	uiItemR(uiLayoutRow(col, false), ptr, "views_format", UI_ITEM_R_EXPAND, NULL, ICON_NONE);
}

void uiTemplateImageViews(uiLayout *layout, PointerRNA *imaptr)
{
	Image *ima = imaptr->data;

	if (ima->type != IMA_TYPE_MULTILAYER) {
		PropertyRNA *prop;
		PointerRNA stereo3d_format_ptr;

		prop = RNA_struct_find_property(imaptr, "stereo_3d_format");
		stereo3d_format_ptr = RNA_property_pointer_get(imaptr, prop);

		uiTemplateViewsFormat(layout, imaptr, &stereo3d_format_ptr);
	}
	else {
		uiTemplateViewsFormat(layout, imaptr, NULL);
	}
}

void uiTemplateImageFormatViews(uiLayout *layout, PointerRNA *imfptr, PointerRNA *ptr)
{

}

void uiTemplateImageInfo(uiLayout *layout, bContext *C, Image *ima, ImageUser *iuser)
{
	ImBuf *ibuf;
	char str[MAX_IMAGE_INFO_LEN];
	void *lock;

	if (!ima || !iuser)
		return;

	ibuf = BKE_image_acquire_ibuf(ima, iuser, &lock);

	image_info(CTX_data_scene(C), iuser, ima, ibuf, str, MAX_IMAGE_INFO_LEN);
	BKE_image_release_ibuf(ima, ibuf, lock);
	uiItemL(layout, str, ICON_NONE);
}

#undef MAX_IMAGE_INFO_LEN

void image_buttons_register(ARegionType *UNUSED(art))
{

}

static int image_properties_toggle_exec(bContext *C, wmOperator *UNUSED(op))
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = image_has_buttons_region(sa);

	if (ar)
		ED_region_toggle_hidden(C, ar);

	return OPERATOR_FINISHED;
}

void IMAGE_OT_properties(wmOperatorType *ot)
{
	ot->name = "Properties";
	ot->idname = "IMAGE_OT_properties";
	ot->description = "Toggle the properties region visibility";

	ot->exec = image_properties_toggle_exec;
	ot->poll = ED_operator_image_active;

	/* flags */
	ot->flag = 0;
}

static int image_scopes_toggle_exec(bContext *C, wmOperator *UNUSED(op))
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = image_has_tools_region(sa);

	if (ar)
		ED_region_toggle_hidden(C, ar);

	return OPERATOR_FINISHED;
}

void IMAGE_OT_toolshelf(wmOperatorType *ot)
{
	ot->name = "Tool Shelf";
	ot->idname = "IMAGE_OT_toolshelf";
	ot->description = "Toggles tool shelf display";

	ot->exec = image_scopes_toggle_exec;
	ot->poll = ED_operator_image_active;

	/* flags */
	ot->flag = 0;
}
