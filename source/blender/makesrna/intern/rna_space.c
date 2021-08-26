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
 */

/** \file blender/makesrna/intern/rna_space.c
 *  \ingroup RNA
 */

#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLT_translation.h"

#include "BKE_image.h"

#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "rna_internal.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_enum_types.h"


const EnumPropertyItem rna_enum_space_type_items[] = {
	/* empty must be here for python, is skipped for UI */
	{SPACE_EMPTY, "EMPTY", ICON_NONE, "Empty", ""},

	/* General */
	{0, "", ICON_NONE, "General", ""},
	{SPACE_VIEW3D, "VIEW_3D", ICON_VIEW3D, "3D View", "3D viewport"},
	{SPACE_IMAGE, "IMAGE_EDITOR", ICON_IMAGE_COL, "UV/Image Editor", "View and edit images and UV Maps"},

	/* Scripting */
	{0, "", ICON_NONE, "Scripting", ""},
	{SPACE_TEXT, "TEXT_EDITOR", ICON_TEXT, "Text Editor", "Edit scripts and in-file documentation"},
	{SPACE_CONSOLE, "CONSOLE", ICON_CONSOLE, "Python Console", "Interactive programmatic console for "
	                "advanced editing and script development"},
	{SPACE_INFO, "INFO", ICON_INFO, "Info", "Main menu bar and list of error messages "
	             "(drag down to expand and display)"},

	/* Data */
	{0, "", ICON_NONE, "Data", ""},
	{SPACE_OUTLINER, "OUTLINER", ICON_OOPS, "Outliner", "Overview of scene graph and all available data-blocks"},
	{SPACE_BUTS, "PROPERTIES", ICON_BUTS, "Properties", "Edit properties of active object and related data-blocks"},
	{SPACE_FILE, "FILE_BROWSER", ICON_FILESEL, "File Browser", "Browse for files and assets"},
	{SPACE_USERPREF, "USER_PREFERENCES", ICON_PREFERENCES, "User Preferences",
	                 "Edit persistent configuration settings"},
	{0, NULL, 0, NULL, NULL}
};

static const EnumPropertyItem pivot_items_full[] = {
	{V3D_AROUND_CENTER_BOUNDS, "BOUNDING_BOX_CENTER", ICON_ROTATE, "Bounding Box Center",
	             "Pivot around bounding box center of selected object(s)"},
	{V3D_AROUND_CURSOR, "CURSOR", ICON_CURSOR, "3D Cursor", "Pivot around the 3D cursor"},
	{V3D_AROUND_LOCAL_ORIGINS, "INDIVIDUAL_ORIGINS", ICON_ROTATECOLLECTION,
	            "Individual Origins", "Pivot around each object's own origin"},
	{V3D_AROUND_CENTER_MEDIAN, "MEDIAN_POINT", ICON_ROTATECENTER, "Median Point",
	               "Pivot around the median point of selected objects"},
	{V3D_AROUND_ACTIVE, "ACTIVE_ELEMENT", ICON_ROTACTIVE, "Active Element", "Pivot around active object"},
	{0, NULL, 0, NULL, NULL}
};

static const EnumPropertyItem draw_channels_items[] = {
	{SI_USE_ALPHA, "COLOR_ALPHA", ICON_IMAGE_RGB_ALPHA, "Color and Alpha",
	               "Draw image with RGB colors and alpha transparency"},
	{0, "COLOR", ICON_IMAGE_RGB, "Color", "Draw image with RGB colors"},
	{SI_SHOW_ALPHA, "ALPHA", ICON_IMAGE_ALPHA, "Alpha", "Draw alpha transparency channel"},
	{SI_SHOW_ZBUF, "Z_BUFFER", ICON_IMAGE_ZDEPTH, "Z-Buffer",
	               "Draw Z-buffer associated with image (mapped from camera clip start to end)"},
	{SI_SHOW_R, "RED",   ICON_COLOR_RED, "Red", ""},
	{SI_SHOW_G, "GREEN", ICON_COLOR_GREEN, "Green", ""},
	{SI_SHOW_B, "BLUE",  ICON_COLOR_BLUE, "Blue", ""},
	{0, NULL, 0, NULL, NULL}
};

static const EnumPropertyItem transform_orientation_items[] = {
	{V3D_MANIP_GLOBAL, "GLOBAL", 0, "Global", "Align the transformation axes to world space"},
	{V3D_MANIP_LOCAL, "LOCAL", 0, "Local", "Align the transformation axes to the selected objects' local space"},
	{V3D_MANIP_NORMAL, "NORMAL", 0, "Normal",
	                   "Align the transformation axes to average normal of selected elements "},
	{V3D_MANIP_GIMBAL, "GIMBAL", 0, "Gimbal", "Align each axis to the Euler rotation axis as used for input"},
	{V3D_MANIP_VIEW, "VIEW", 0, "View", "Align the transformation axes to the window"},
	// {V3D_MANIP_CUSTOM, "CUSTOM", 0, "Custom", "Use a custom transform orientation"},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_viewport_shade_items[] = {
	{OB_BOUNDBOX, "BOUNDBOX", ICON_BBOX, "Bounding Box", "Display the object's local bounding boxes only"},
	{OB_WIRE, "WIREFRAME", ICON_WIRE, "Wireframe", "Display the object as wire edges"},
	{OB_SOLID, "SOLID", ICON_SOLID, "Solid", "Display the object solid, lit with default OpenGL lights"},
	{OB_TEXTURE, "TEXTURED", ICON_POTATO, "Texture", "Display the object solid, with a texture"},
	{OB_MATERIAL, "MATERIAL", ICON_MATERIAL_DATA, "Material", "Display objects solid, with GLSL material"},
	{0, NULL, 0, NULL, NULL}
};


/* Actually populated dynamically trough a function, but helps for context-less access (e.g. doc, i18n...). */
static const EnumPropertyItem buttons_context_items[] = {
	{BCONTEXT_SCENE, "SCENE", ICON_SCENE_DATA, "Scene", "Scene"},
	{BCONTEXT_WORLD, "WORLD", ICON_WORLD, "World", "World"},
	{BCONTEXT_OBJECT, "OBJECT", ICON_OBJECT_DATA, "Object", "Object"},
	{BCONTEXT_MODIFIER, "MODIFIER", ICON_MODIFIER, "Modifiers", "Object modifiers"},
	{BCONTEXT_DATA, "DATA", ICON_NONE, "Data", "Object data"},
	{BCONTEXT_MATERIAL, "MATERIAL", ICON_MATERIAL, "Material", "Material"},
	{BCONTEXT_TEXTURE, "TEXTURE", ICON_TEXTURE, "Texture", "Texture"},
	{BCONTEXT_PHYSICS, "PHYSICS", ICON_PHYSICS, "Physics", "Physics"},
	{0, NULL, 0, NULL, NULL}
};

/* Actually populated dynamically trough a function, but helps for context-less access (e.g. doc, i18n...). */
static const EnumPropertyItem buttons_texture_context_items[] = {
	{SB_TEXC_MATERIAL, "MATERIAL", ICON_MATERIAL, "", "Show material textures"},
	{SB_TEXC_WORLD, "WORLD", ICON_WORLD, "", "Show world textures"},
	{SB_TEXC_LAMP, "LAMP", ICON_LAMP, "", "Show lamp textures"},
	{SB_TEXC_OTHER, "OTHER", ICON_TEXTURE, "", "Show other data textures"},
	{0, NULL, 0, NULL, NULL}
};


static const EnumPropertyItem fileselectparams_recursion_level_items[] = {
	{0, "NONE",  0, "None", "Only list current directory's content, with no recursion"},
	{1, "BLEND", 0, "Blend File", "List .blend files' content"},
	{2, "ALL_1", 0, "One Level", "List all sub-directories' content, one level of recursion"},
	{3, "ALL_2", 0, "Two Levels", "List all sub-directories' content, two levels of recursion"},
	{4, "ALL_3", 0, "Three Levels", "List all sub-directories' content, three levels of recursion"},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_file_sort_items[] = {
	{FILE_SORT_ALPHA, "FILE_SORT_ALPHA", ICON_SORTALPHA, "Sort alphabetically", "Sort the file list alphabetically"},
	{FILE_SORT_EXTENSION, "FILE_SORT_EXTENSION", ICON_SORTBYEXT, "Sort by extension", "Sort the file list by extension/type"},
	{FILE_SORT_TIME, "FILE_SORT_TIME", ICON_SORTTIME, "Sort by time", "Sort files by modification time"},
	{FILE_SORT_SIZE, "FILE_SORT_SIZE", ICON_SORTSIZE, "Sort by size", "Sort files by size"},
	{0, NULL, 0, NULL, NULL}
};

#ifdef RNA_RUNTIME

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_userdef_types.h"

#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_icons.h"

#include "ED_buttons.h"
#include "ED_fileselect.h"
#include "ED_image.h"
#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_material.h"

#include "IMB_imbuf_types.h"

#include "UI_interface.h"
#include "UI_view2d.h"

static StructRNA *rna_Space_refine(struct PointerRNA *ptr)
{
	SpaceLink *space = (SpaceLink *)ptr->data;

	switch (space->spacetype) {
		case SPACE_VIEW3D:
			return &RNA_SpaceView3D;
		case SPACE_OUTLINER:
			return &RNA_SpaceOutliner;
		case SPACE_BUTS:
			return &RNA_SpaceProperties;
		case SPACE_FILE:
			return &RNA_SpaceFileBrowser;
		case SPACE_IMAGE:
			return &RNA_SpaceImageEditor;
		case SPACE_INFO:
			return &RNA_SpaceInfo;
		case SPACE_TEXT:
			return &RNA_SpaceTextEditor;
		case SPACE_CONSOLE:
			return &RNA_SpaceConsole;
		case SPACE_USERPREF:
			return &RNA_SpaceUserPreferences;
		default:
			return &RNA_Space;
	}
}

static ScrArea *rna_area_from_space(PointerRNA *ptr)
{
	bScreen *sc = (bScreen *)ptr->id.data;
	SpaceLink *link = (SpaceLink *)ptr->data;
	return BKE_screen_find_area_from_space(sc, link);
}

static void area_region_from_regiondata(bScreen *sc, void *regiondata, ScrArea **r_sa, ARegion **r_ar)
{
	ScrArea *sa;
	ARegion *ar;

	*r_sa = NULL;
	*r_ar = NULL;

	for (sa = sc->areabase.first; sa; sa = sa->next) {
		for (ar = sa->regionbase.first; ar; ar = ar->next) {
			if (ar->regiondata == regiondata) {
				*r_sa = sa;
				*r_ar = ar;
				return;
			}
		}
	}
}

static void rna_area_region_from_regiondata(PointerRNA *ptr, ScrArea **r_sa, ARegion **r_ar)
{
	bScreen *sc = (bScreen *)ptr->id.data;
	void *regiondata = ptr->data;

	area_region_from_regiondata(sc, regiondata, r_sa, r_ar);
}

static PointerRNA rna_CurrentOrientation_get(PointerRNA *ptr)
{
	Scene *scene = ((bScreen *)ptr->id.data)->scene;
	View3D *v3d = (View3D *)ptr->data;

	if (v3d->twmode < V3D_MANIP_CUSTOM)
		return rna_pointer_inherit_refine(ptr, &RNA_TransformOrientation, NULL);
	else
		return rna_pointer_inherit_refine(ptr, &RNA_TransformOrientation,
		                                  BLI_findlink(&scene->transform_spaces, v3d->twmode - V3D_MANIP_CUSTOM));
}

const EnumPropertyItem *rna_TransformOrientation_itemf(
        bContext *C, PointerRNA *ptr, PropertyRNA *UNUSED(prop), bool *r_free)
{
	Scene *scene = NULL;
	ListBase *transform_spaces;
	TransformOrientation *ts = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	EnumPropertyItem *item = NULL;
	int i = V3D_MANIP_CUSTOM, totitem = 0;

	RNA_enum_items_add(&item, &totitem, transform_orientation_items);

	if (ptr->type == &RNA_SpaceView3D)
		scene = ((bScreen *)ptr->id.data)->scene;
	else
		scene = CTX_data_scene(C);  /* can't use scene from ptr->id.data because that enum is also used by operators */

	if (scene) {
		transform_spaces = &scene->transform_spaces;
		ts = transform_spaces->first;
	}

	if (ts) {
		RNA_enum_item_add_separator(&item, &totitem);

		for (; ts; ts = ts->next) {
			tmp.identifier = ts->name;
			tmp.name = ts->name;
			tmp.value = i++;
			RNA_enum_item_add(&item, &totitem, &tmp);
		}
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

/* Space 3D View */
static void rna_SpaceView3D_camera_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);
	if (v3d->scenelock) {
		scene->camera = v3d->camera;
		BKE_screen_view3d_main_sync(&bmain->screen, scene);
	}
}

static void rna_SpaceView3D_lock_camera_and_layers_set(PointerRNA *ptr, bool value)
{
	View3D *v3d = (View3D *)(ptr->data);
	bScreen *sc = (bScreen *)ptr->id.data;

	v3d->scenelock = value;

	if (value) {
		int bit;
		v3d->lay = sc->scene->lay;
		/* seek for layact */
		bit = 0;
		while (bit < 32) {
			if (v3d->lay & (1u << bit)) {
				v3d->layact = (1u << bit);
				break;
			}
			bit++;
		}
		v3d->camera = sc->scene->camera;
	}
}

static void rna_View3D_CursorLocation_get(PointerRNA *ptr, float *values)
{
	View3D *v3d = (View3D *)(ptr->data);
	bScreen *sc = (bScreen *)ptr->id.data;
	Scene *scene = (Scene *)sc->scene;
	const float *loc = ED_view3d_cursor3d_get(scene, v3d);

	copy_v3_v3(values, loc);
}

static void rna_View3D_CursorLocation_set(PointerRNA *ptr, const float *values)
{
	View3D *v3d = (View3D *)(ptr->data);
	bScreen *sc = (bScreen *)ptr->id.data;
	Scene *scene = (Scene *)sc->scene;
	float *cursor = ED_view3d_cursor3d_get(scene, v3d);

	copy_v3_v3(cursor, values);
}

static float rna_View3D_GridScaleUnit_get(PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);
	bScreen *sc = (bScreen *)ptr->id.data;
	Scene *scene = (Scene *)sc->scene;

	return ED_view3d_grid_scale(scene, v3d, NULL);
}

static void rna_SpaceView3D_layer_set(PointerRNA *ptr, const bool *values)
{
	View3D *v3d = (View3D *)(ptr->data);

	v3d->lay = ED_view3d_scene_layer_set(v3d->lay, values, &v3d->layact);
}

static int rna_SpaceView3D_active_layer_get(PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);

	return (int)(log(v3d->layact) / M_LN2);
}

static void rna_SpaceView3D_layer_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{

}

static void rna_SpaceView3D_matcap_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);

	if (v3d->defmaterial) {
		Material *ma = v3d->defmaterial;

		if (ma->preview)
			BKE_previewimg_free(&ma->preview);

		if (ma->gpumaterial.first)
			GPU_material_free(&ma->gpumaterial);

		WM_main_add_notifier(NC_MATERIAL | ND_SHADING_DRAW, ma);
	}
}

static void rna_SpaceView3D_matcap_enable(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);

	if (v3d->matcap_icon < ICON_MATCAP_01 ||
	    v3d->matcap_icon > ICON_MATCAP_24)
	{
		v3d->matcap_icon = ICON_MATCAP_01;
	}
}

static void rna_SpaceView3D_pivot_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
	if (U.uiflag & USER_LOCKAROUND) {
		View3D *v3d_act = (View3D *)(ptr->data);

		/* TODO, space looper */
		bScreen *screen;
		for (screen = bmain->screen.first; screen; screen = screen->id.next) {
			ScrArea *sa;
			for (sa = screen->areabase.first; sa; sa = sa->next) {
				SpaceLink *sl;
				for (sl = sa->spacedata.first; sl; sl = sl->next) {
					if (sl->spacetype == SPACE_VIEW3D) {
						View3D *v3d = (View3D *)sl;
						if (v3d != v3d_act) {
							v3d->around = v3d_act->around;
							v3d->flag = (v3d->flag & ~V3D_ALIGN) | (v3d_act->flag & V3D_ALIGN);
							ED_area_tag_redraw_regiontype(sa, RGN_TYPE_HEADER);
						}
					}
				}
			}
		}
	}
}

static PointerRNA rna_SpaceView3D_region_3d_get(PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);
	ScrArea *sa = rna_area_from_space(ptr);
	void *regiondata = NULL;
	if (sa) {
		ListBase *regionbase = (sa->spacedata.first == v3d) ? &sa->regionbase : &v3d->regionbase;
		ARegion *ar = regionbase->last; /* always last in list, weak .. */
		regiondata = ar->regiondata;
	}

	return rna_pointer_inherit_refine(ptr, &RNA_RegionView3D, regiondata);
}

static void rna_SpaceView3D_region_quadviews_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	View3D *v3d = (View3D *)(ptr->data);
	ScrArea *sa = rna_area_from_space(ptr);
	int i = 3;

	ARegion *ar = ((sa && sa->spacedata.first == v3d) ? &sa->regionbase : &v3d->regionbase)->last;
	ListBase lb = {NULL, NULL};

	if (ar && ar->alignment == RGN_ALIGN_QSPLIT) {
		while (i-- && ar) {
			ar = ar->prev;
		}

		if (i < 0) {
			lb.first = ar;
		}
	}

	rna_iterator_listbase_begin(iter, &lb, NULL);
}

static PointerRNA rna_SpaceView3D_region_quadviews_get(CollectionPropertyIterator *iter)
{
	void *regiondata = ((ARegion *)rna_iterator_listbase_get(iter))->regiondata;

	return rna_pointer_inherit_refine(&iter->parent, &RNA_RegionView3D, regiondata);
}

static void rna_RegionView3D_quadview_update(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
	ScrArea *sa;
	ARegion *ar;

	rna_area_region_from_regiondata(ptr, &sa, &ar);
	if (sa && ar && ar->alignment == RGN_ALIGN_QSPLIT)
		ED_view3d_quadview_update(sa, ar, false);
}

/* same as above but call clip==true */
static void rna_RegionView3D_quadview_clip_update(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
	ScrArea *sa;
	ARegion *ar;

	rna_area_region_from_regiondata(ptr, &sa, &ar);
	if (sa && ar && ar->alignment == RGN_ALIGN_QSPLIT)
		ED_view3d_quadview_update(sa, ar, true);
}

static void rna_RegionView3D_view_location_get(PointerRNA *ptr, float *values)
{
	RegionView3D *rv3d = (RegionView3D *)(ptr->data);
	negate_v3_v3(values, rv3d->ofs);
}

static void rna_RegionView3D_view_location_set(PointerRNA *ptr, const float *values)
{
	RegionView3D *rv3d = (RegionView3D *)(ptr->data);
	negate_v3_v3(rv3d->ofs, values);
}

static void rna_RegionView3D_view_rotation_get(PointerRNA *ptr, float *values)
{
	RegionView3D *rv3d = (RegionView3D *)(ptr->data);
	invert_qt_qt(values, rv3d->viewquat);
}

static void rna_RegionView3D_view_rotation_set(PointerRNA *ptr, const float *values)
{
	RegionView3D *rv3d = (RegionView3D *)(ptr->data);
	invert_qt_qt(rv3d->viewquat, values);
}

static void rna_RegionView3D_view_matrix_set(PointerRNA *ptr, const float *values)
{
	RegionView3D *rv3d = (RegionView3D *)(ptr->data);
	float mat[4][4];
	invert_m4_m4(mat, (float (*)[4])values);
	ED_view3d_from_m4(mat, rv3d->ofs, rv3d->viewquat, &rv3d->dist);
}

static int rna_SpaceView3D_viewport_shade_get(PointerRNA *ptr)
{
	View3D *v3d = (View3D *)ptr->data;
	const int drawtype = v3d->drawtype;

	return drawtype;
}

static void rna_SpaceView3D_viewport_shade_set(PointerRNA *ptr, int value)
{
	View3D *v3d = (View3D *)ptr->data;
	if (value != v3d->drawtype && value == OB_RENDER) {
		v3d->prev_drawtype = v3d->drawtype;
	}
	v3d->drawtype = value;
}

static const EnumPropertyItem *rna_SpaceView3D_viewport_shade_itemf(bContext *UNUSED(C), PointerRNA *ptr,
                                                              PropertyRNA *UNUSED(prop), bool *r_free)
{
	EnumPropertyItem *item = NULL;
	int totitem = 0;

	RNA_enum_items_add_value(&item, &totitem, rna_enum_viewport_shade_items, OB_BOUNDBOX);
	RNA_enum_items_add_value(&item, &totitem, rna_enum_viewport_shade_items, OB_WIRE);
	RNA_enum_items_add_value(&item, &totitem, rna_enum_viewport_shade_items, OB_SOLID);
	RNA_enum_items_add_value(&item, &totitem, rna_enum_viewport_shade_items, OB_TEXTURE);
	RNA_enum_items_add_value(&item, &totitem, rna_enum_viewport_shade_items, OB_MATERIAL);

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

/* Space Image Editor */

static void rna_SpaceImageEditor_mode_update(Main *bmain, Scene *scene, PointerRNA *UNUSED(ptr))
{

}

static void rna_SpaceImageEditor_image_set(PointerRNA *ptr, PointerRNA value)
{
	SpaceImage *sima = (SpaceImage *)(ptr->data);
	bScreen *sc = (bScreen *)ptr->id.data;

	BLI_assert(BKE_id_is_in_gobal_main(value.data));
	ED_space_image_set(G_MAIN, sima, sc->scene, sc->scene->obedit, (Image *)value.data);
}

static const EnumPropertyItem *rna_SpaceImageEditor_draw_channels_itemf(bContext *UNUSED(C), PointerRNA *ptr,
                                                                  PropertyRNA *UNUSED(prop), bool *r_free)
{
	SpaceImage *sima = (SpaceImage *)ptr->data;
	EnumPropertyItem *item = NULL;
	ImBuf *ibuf;
	void *lock;
	int zbuf, alpha, totitem = 0;

	ibuf = ED_space_image_acquire_buffer(sima, &lock);

	alpha = ibuf && (ibuf->channels == 4);
	zbuf = ibuf && (ibuf->zbuf || ibuf->zbuf_float || (ibuf->channels == 1));

	ED_space_image_release_buffer(sima, ibuf, lock);

	if (alpha && zbuf)
		return draw_channels_items;

	if (alpha) {
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_USE_ALPHA);
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, 0);
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_SHOW_ALPHA);
	}
	else if (zbuf) {
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, 0);
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_SHOW_ZBUF);
	}
	else {
		RNA_enum_items_add_value(&item, &totitem, draw_channels_items, 0);
	}

	RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_SHOW_R);
	RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_SHOW_G);
	RNA_enum_items_add_value(&item, &totitem, draw_channels_items, SI_SHOW_B);

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static void rna_SpaceImageEditor_zoom_get(PointerRNA *ptr, float *values)
{
	SpaceImage *sima = (SpaceImage *)ptr->data;
	ScrArea *sa;
	ARegion *ar;

	values[0] = values[1] = 1;

	/* find aregion */
	sa = rna_area_from_space(ptr); /* can be NULL */
	ar = BKE_area_find_region_type(sa, RGN_TYPE_WINDOW);
	if (ar) {
		ED_space_image_get_zoom(sima, ar, &values[0], &values[1]);
	}
}

static void rna_SpaceImageEditor_cursor_location_get(PointerRNA *ptr, float *values)
{
	SpaceImage *sima = (SpaceImage *)ptr->data;

	if (sima->flag & SI_COORDFLOATS) {
		copy_v2_v2(values, sima->cursor);
	}
	else {
		int w, h;
		ED_space_image_get_size(sima, &w, &h);

		values[0] = sima->cursor[0] * w;
		values[1] = sima->cursor[1] * h;
	}
}

static void rna_SpaceImageEditor_cursor_location_set(PointerRNA *ptr, const float *values)
{
	SpaceImage *sima = (SpaceImage *)ptr->data;

	if (sima->flag & SI_COORDFLOATS) {
		copy_v2_v2(sima->cursor, values);
	}
	else {
		int w, h;
		ED_space_image_get_size(sima, &w, &h);

		sima->cursor[0] = values[0] / w;
		sima->cursor[1] = values[1] / h;
	}
}

static void rna_SpaceImageEditor_image_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{

}

static void rna_SpaceImageEditor_scopes_update(struct bContext *C, struct PointerRNA *ptr)
{
	SpaceImage *sima = (SpaceImage *)ptr->data;
	ImBuf *ibuf;
	void *lock;

	ibuf = ED_space_image_acquire_buffer(sima, &lock);
	if (ibuf) {
		ED_space_image_scopes_update(C, sima, ibuf, true);
		WM_main_add_notifier(NC_IMAGE, sima->image);
	}
	ED_space_image_release_buffer(sima, ibuf, lock);
}

static const EnumPropertyItem *rna_SpaceImageEditor_pivot_itemf(bContext *UNUSED(C), PointerRNA *ptr,
                                                          PropertyRNA *UNUSED(prop), bool *UNUSED(r_free))
{
	static const EnumPropertyItem pivot_items[] = {
		{V3D_AROUND_CENTER_BOUNDS, "CENTER", ICON_ROTATE, "Bounding Box Center", ""},
		{V3D_AROUND_CENTER_MEDIAN, "MEDIAN", ICON_ROTATECENTER, "Median Point", ""},
		{V3D_AROUND_CURSOR, "CURSOR", ICON_CURSOR, "2D Cursor", ""},
		{V3D_AROUND_LOCAL_ORIGINS, "INDIVIDUAL_ORIGINS", ICON_ROTATECOLLECTION,
		            "Individual Origins", "Pivot around each selected island's own median point"},
		{0, NULL, 0, NULL, NULL}
	};

	SpaceImage *sima = (SpaceImage *)ptr->data;

	if (sima->mode == SI_MODE_PAINT)
		return pivot_items_full;
	else
		return pivot_items;
}

/* Space Text Editor */

static void rna_SpaceTextEditor_word_wrap_set(PointerRNA *ptr, bool value)
{
	SpaceText *st = (SpaceText *)(ptr->data);

	st->wordwrap = value;
	st->left = 0;
}

static void rna_SpaceTextEditor_text_set(PointerRNA *ptr, PointerRNA value)
{
	SpaceText *st = (SpaceText *)(ptr->data);

	st->text = value.data;

	WM_main_add_notifier(NC_TEXT | NA_SELECTED, st->text);
}

static void rna_SpaceTextEditor_updateEdited(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	SpaceText *st = (SpaceText *)ptr->data;

	if (st->text)
		WM_main_add_notifier(NC_TEXT | NA_EDITED, st->text);
}

/* Space Properties */

/* note: this function exists only to avoid id refcounting */
static void rna_SpaceProperties_pin_id_set(PointerRNA *ptr, PointerRNA value)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);
	sbuts->pinid = value.data;
}

static StructRNA *rna_SpaceProperties_pin_id_typef(PointerRNA *ptr)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);

	if (sbuts->pinid)
		return ID_code_to_RNA_type(GS(sbuts->pinid->name));

	return &RNA_ID;
}

static void rna_SpaceProperties_pin_id_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);
	ID *id = sbuts->pinid;

	if (id == NULL) {
		sbuts->flag &= ~SB_PIN_CONTEXT;
		return;
	}

	switch (GS(id->name)) {
		case ID_MA:
			WM_main_add_notifier(NC_MATERIAL | ND_SHADING, NULL);
			break;
		case ID_TE:
			WM_main_add_notifier(NC_TEXTURE, NULL);
			break;
		case ID_WO:
			WM_main_add_notifier(NC_WORLD, NULL);
			break;
		case ID_LA:
			WM_main_add_notifier(NC_LAMP, NULL);
			break;
		default:
			break;
	}
}


static void rna_SpaceProperties_context_set(PointerRNA *ptr, int value)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);

	sbuts->mainb = value;
	sbuts->mainbuser = value;
}

static const EnumPropertyItem *rna_SpaceProperties_context_itemf(bContext *UNUSED(C), PointerRNA *ptr,
                                                           PropertyRNA *UNUSED(prop), bool *r_free)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);
	EnumPropertyItem *item = NULL;
	int totitem = 0;

	if (sbuts->pathflag & (1 << BCONTEXT_SCENE)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_SCENE);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_WORLD)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_WORLD);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_OBJECT)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_OBJECT);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_MODIFIER)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_MODIFIER);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_DATA)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_DATA);
		(item + totitem - 1)->icon = sbuts->dataicon;
	}

	if (sbuts->pathflag & (1 << BCONTEXT_MATERIAL)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_MATERIAL);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_TEXTURE)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_TEXTURE);
	}

	if (sbuts->pathflag & (1 << BCONTEXT_PHYSICS)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_context_items, BCONTEXT_PHYSICS);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static void rna_SpaceProperties_context_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);
	/* XXX BCONTEXT_DATA is ugly, but required for lamps... See T51318. */
	if (ELEM(sbuts->mainb, BCONTEXT_WORLD, BCONTEXT_MATERIAL, BCONTEXT_TEXTURE, BCONTEXT_DATA)) {
		sbuts->preview = 1;
	}
}

static void rna_SpaceProperties_align_set(PointerRNA *ptr, int value)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);

	sbuts->align = value;
	sbuts->re_align = 1;
}

static const EnumPropertyItem *rna_SpaceProperties_texture_context_itemf(bContext *C, PointerRNA *UNUSED(ptr),
                                                                   PropertyRNA *UNUSED(prop), bool *r_free)
{
	EnumPropertyItem *item = NULL;
	int totitem = 0;

	if (ED_texture_context_check_world(C)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_texture_context_items, SB_TEXC_WORLD);
	}

	if (ED_texture_context_check_lamp(C)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_texture_context_items, SB_TEXC_LAMP);
	}
	else if (ED_texture_context_check_material(C)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_texture_context_items, SB_TEXC_MATERIAL);
	}

	if (ED_texture_context_check_others(C)) {
		RNA_enum_items_add_value(&item, &totitem, buttons_texture_context_items, SB_TEXC_OTHER);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static void rna_SpaceProperties_texture_context_set(PointerRNA *ptr, int value)
{
	SpaceButs *sbuts = (SpaceButs *)(ptr->data);

	/* User action, no need to keep "better" value in prev here! */
	sbuts->texture_context = sbuts->texture_context_prev = value;
}

/* Space Console */
static void rna_ConsoleLine_body_get(PointerRNA *ptr, char *value)
{
	ConsoleLine *ci = (ConsoleLine *)ptr->data;
	memcpy(value, ci->line, ci->len + 1);
}

static int rna_ConsoleLine_body_length(PointerRNA *ptr)
{
	ConsoleLine *ci = (ConsoleLine *)ptr->data;
	return ci->len;
}

static void rna_ConsoleLine_body_set(PointerRNA *ptr, const char *value)
{
	ConsoleLine *ci = (ConsoleLine *)ptr->data;
	int len = strlen(value);

	if ((len >= ci->len_alloc) || (len * 2 < ci->len_alloc) ) { /* allocate a new string */
		MEM_freeN(ci->line);
		ci->line = MEM_mallocN((len + 1) * sizeof(char), "rna_consoleline");
		ci->len_alloc = len + 1;
	}
	memcpy(ci->line, value, len + 1);
	ci->len = len;

	if (ci->cursor > len) /* clamp the cursor */
		ci->cursor = len;
}

static void rna_ConsoleLine_cursor_index_range(PointerRNA *ptr, int *min, int *max,
                                               int *UNUSED(softmin), int *UNUSED(softmax))
{
	ConsoleLine *ci = (ConsoleLine *)ptr->data;

	*min = 0;
	*max = ci->len; /* intentionally _not_ -1 */
}

static void rna_SpaceConsole_rect_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	SpaceConsole *sc = ptr->data;
	WM_main_add_notifier(NC_SPACE | ND_SPACE_CONSOLE | NA_EDITED, sc);
}

static float rna_BackgroundImage_opacity_get(PointerRNA *ptr)
{
	BGpic *bgpic = (BGpic *)ptr->data;
	return 1.0f - bgpic->blend;
}

static void rna_BackgroundImage_opacity_set(PointerRNA *ptr, float value)
{
	BGpic *bgpic = (BGpic *)ptr->data;
	bgpic->blend = 1.0f - value;
}

/* radius internally (expose as a distance value) */
static float rna_BackgroundImage_size_get(PointerRNA *ptr)
{
	BGpic *bgpic = ptr->data;
	return bgpic->size * 2.0f;
}

static void rna_BackgroundImage_size_set(PointerRNA *ptr, float value)
{
	BGpic *bgpic = ptr->data;
	bgpic->size = value * 0.5f;
}

static BGpic *rna_BackgroundImage_new(View3D *v3d)
{
	BGpic *bgpic = ED_view3d_background_image_new(v3d);

	WM_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D, v3d);

	return bgpic;
}

static void rna_BackgroundImage_remove(View3D *v3d, ReportList *reports, PointerRNA *bgpic_ptr)
{
	BGpic *bgpic = bgpic_ptr->data;
	if (BLI_findindex(&v3d->bgpicbase, bgpic) == -1) {
		BKE_report(reports, RPT_ERROR, "Background image cannot be removed");
	}

	ED_view3d_background_image_remove(v3d, bgpic);
	RNA_POINTER_INVALIDATE(bgpic_ptr);

	WM_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D, v3d);
}

static void rna_BackgroundImage_clear(View3D *v3d)
{
	ED_view3d_background_image_clear(v3d);
	WM_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D, v3d);
}

/* File browser. */

static bool rna_FileSelectParams_use_lib_get(PointerRNA *ptr)
{
	FileSelectParams *params = ptr->data;

	return params && (params->type == FILE_LOADLIB);
}

static const EnumPropertyItem *rna_FileSelectParams_recursion_level_itemf(
        bContext *UNUSED(C), PointerRNA *ptr, PropertyRNA *UNUSED(prop), bool *r_free)
{
	FileSelectParams *params = ptr->data;

	if (params && params->type != FILE_LOADLIB) {
		EnumPropertyItem *item = NULL;
		int totitem = 0;

		RNA_enum_items_add_value(&item, &totitem, fileselectparams_recursion_level_items, 0);
		RNA_enum_items_add_value(&item, &totitem, fileselectparams_recursion_level_items, 2);
		RNA_enum_items_add_value(&item, &totitem, fileselectparams_recursion_level_items, 3);
		RNA_enum_items_add_value(&item, &totitem, fileselectparams_recursion_level_items, 4);

		RNA_enum_item_end(&item, &totitem);
		*r_free = true;

		return item;
	}

	*r_free = false;
	return fileselectparams_recursion_level_items;
}

static void rna_FileSelectPrams_filter_glob_set(PointerRNA *ptr, const char *value)
{
	FileSelectParams *params = ptr->data;

	BLI_strncpy(params->filter_glob, value, sizeof(params->filter_glob));

	/* Remove stupi things like last group being a wildcard-only one... */
	BLI_path_extension_glob_validate(params->filter_glob);
}

static void rna_FileBrowser_FSMenuEntry_path_get(PointerRNA *ptr, char *value)
{
	char *path = ED_fsmenu_entry_get_path(ptr->data);

	strcpy(value, path ? path : "");
}

static int rna_FileBrowser_FSMenuEntry_path_length(PointerRNA *ptr)
{
	char *path = ED_fsmenu_entry_get_path(ptr->data);

	return (int)(path ? strlen(path) : 0);
}

static void rna_FileBrowser_FSMenuEntry_path_set(PointerRNA *ptr, const char *value)
{
	FSMenuEntry *fsm = ptr->data;

	/* Note: this will write to file immediately.
	 * Not nice (and to be fixed ultimately), but acceptable in this case for now. */
	ED_fsmenu_entry_set_path(fsm, value);
}

static void rna_FileBrowser_FSMenuEntry_name_get(PointerRNA *ptr, char *value)
{
	strcpy(value, ED_fsmenu_entry_get_name(ptr->data));
}

static int rna_FileBrowser_FSMenuEntry_name_length(PointerRNA *ptr)
{
	return (int)strlen(ED_fsmenu_entry_get_name(ptr->data));
}

static void rna_FileBrowser_FSMenuEntry_name_set(PointerRNA *ptr, const char *value)
{
	FSMenuEntry *fsm = ptr->data;

	/* Note: this will write to file immediately.
	 * Not nice (and to be fixed ultimately), but acceptable in this case for now. */
	ED_fsmenu_entry_set_name(fsm, value);
}

static int rna_FileBrowser_FSMenuEntry_name_get_editable(PointerRNA *ptr, const char **UNUSED(r_info))
{
	FSMenuEntry *fsm = ptr->data;

	return fsm->save ? PROP_EDITABLE : 0;
}

static void rna_FileBrowser_FSMenu_next(CollectionPropertyIterator *iter)
{
	ListBaseIterator *internal = &iter->internal.listbase;

	if (internal->skip) {
		do {
			internal->link = (Link *)(((FSMenuEntry *)(internal->link))->next);
			iter->valid = (internal->link != NULL);
		} while (iter->valid && internal->skip(iter, internal->link));
	}
	else {
		internal->link = (Link *)(((FSMenuEntry *)(internal->link))->next);
		iter->valid = (internal->link != NULL);
	}
}

static void rna_FileBrowser_FSMenu_begin(CollectionPropertyIterator *iter, FSMenuCategory category)
{
	ListBaseIterator *internal = &iter->internal.listbase;

	struct FSMenu *fsmenu = ED_fsmenu_get();
	struct FSMenuEntry *fsmentry = ED_fsmenu_get_category(fsmenu, category);

	internal->link = (fsmentry) ? (Link *)fsmentry : NULL;
	internal->skip = NULL;

	iter->valid = (internal->link != NULL);
}

static PointerRNA rna_FileBrowser_FSMenu_get(CollectionPropertyIterator *iter)
{
	ListBaseIterator *internal = &iter->internal.listbase;
	PointerRNA r_ptr;

	RNA_pointer_create(NULL, &RNA_FileBrowserFSMenuEntry, internal->link, &r_ptr);

	return r_ptr;
}

static void rna_FileBrowser_FSMenu_end(CollectionPropertyIterator *UNUSED(iter))
{
}

static void rna_FileBrowser_FSMenuSystem_data_begin(CollectionPropertyIterator *iter, PointerRNA *UNUSED(ptr))
{
	rna_FileBrowser_FSMenu_begin(iter, FS_CATEGORY_SYSTEM);
}

static int rna_FileBrowser_FSMenuSystem_data_length(PointerRNA *UNUSED(ptr))
{
	struct FSMenu *fsmenu = ED_fsmenu_get();

	return ED_fsmenu_get_nentries(fsmenu, FS_CATEGORY_SYSTEM);
}

static void rna_FileBrowser_FSMenuSystemBookmark_data_begin(CollectionPropertyIterator *iter, PointerRNA *UNUSED(ptr))
{
	rna_FileBrowser_FSMenu_begin(iter, FS_CATEGORY_SYSTEM_BOOKMARKS);
}

static int rna_FileBrowser_FSMenuSystemBookmark_data_length(PointerRNA *UNUSED(ptr))
{
	struct FSMenu *fsmenu = ED_fsmenu_get();

	return ED_fsmenu_get_nentries(fsmenu, FS_CATEGORY_SYSTEM_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuBookmark_data_begin(CollectionPropertyIterator *iter, PointerRNA *UNUSED(ptr))
{
	rna_FileBrowser_FSMenu_begin(iter, FS_CATEGORY_BOOKMARKS);
}

static int rna_FileBrowser_FSMenuBookmark_data_length(PointerRNA *UNUSED(ptr))
{
	struct FSMenu *fsmenu = ED_fsmenu_get();

	return ED_fsmenu_get_nentries(fsmenu, FS_CATEGORY_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuRecent_data_begin(CollectionPropertyIterator *iter, PointerRNA *UNUSED(ptr))
{
	rna_FileBrowser_FSMenu_begin(iter, FS_CATEGORY_RECENT);
}

static int rna_FileBrowser_FSMenuRecent_data_length(PointerRNA *UNUSED(ptr))
{
	struct FSMenu *fsmenu = ED_fsmenu_get();

	return ED_fsmenu_get_nentries(fsmenu, FS_CATEGORY_RECENT);
}

static int rna_FileBrowser_FSMenu_active_get(PointerRNA *ptr, const FSMenuCategory category)
{
	SpaceFile *sf = ptr->data;
	int actnr = -1;

	switch (category) {
		case FS_CATEGORY_SYSTEM:
			actnr = sf->systemnr;
			break;
		case FS_CATEGORY_SYSTEM_BOOKMARKS:
			actnr = sf->system_bookmarknr;
			break;
		case FS_CATEGORY_BOOKMARKS:
			actnr = sf->bookmarknr;
			break;
		case FS_CATEGORY_RECENT:
			actnr = sf->recentnr;
			break;
	}

	return actnr;
}

static void rna_FileBrowser_FSMenu_active_set(PointerRNA *ptr, int value, const FSMenuCategory category)
{
	SpaceFile *sf = ptr->data;
	struct FSMenu *fsmenu = ED_fsmenu_get();
	FSMenuEntry *fsm = ED_fsmenu_get_entry(fsmenu, category, value);

	if (fsm && sf->params) {
		switch (category) {
			case FS_CATEGORY_SYSTEM:
				sf->systemnr = value;
				break;
			case FS_CATEGORY_SYSTEM_BOOKMARKS:
				sf->system_bookmarknr = value;
				break;
			case FS_CATEGORY_BOOKMARKS:
				sf->bookmarknr = value;
				break;
			case FS_CATEGORY_RECENT:
				sf->recentnr = value;
				break;
		}

		BLI_strncpy(sf->params->dir, fsm->path, sizeof(sf->params->dir));
	}
}

static void rna_FileBrowser_FSMenu_active_range(
        PointerRNA *UNUSED(ptr), int *min, int *max, int *softmin, int *softmax, const FSMenuCategory category)
{
	struct FSMenu *fsmenu = ED_fsmenu_get();

	*min = *softmin = -1;
	*max = *softmax = ED_fsmenu_get_nentries(fsmenu, category) - 1;
}

static void rna_FileBrowser_FSMenu_active_update(struct bContext *C, PointerRNA *UNUSED(ptr))
{
	ED_file_change_dir(C);
}

static int rna_FileBrowser_FSMenuSystem_active_get(PointerRNA *ptr)
{
	return rna_FileBrowser_FSMenu_active_get(ptr, FS_CATEGORY_SYSTEM);
}

static void rna_FileBrowser_FSMenuSystem_active_set(PointerRNA *ptr, int value)
{
	rna_FileBrowser_FSMenu_active_set(ptr, value, FS_CATEGORY_SYSTEM);
}

static void rna_FileBrowser_FSMenuSystem_active_range(PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax)
{
	rna_FileBrowser_FSMenu_active_range(ptr, min, max, softmin, softmax, FS_CATEGORY_SYSTEM);
}

static int rna_FileBrowser_FSMenuSystemBookmark_active_get(PointerRNA *ptr)
{
	return rna_FileBrowser_FSMenu_active_get(ptr, FS_CATEGORY_SYSTEM_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuSystemBookmark_active_set(PointerRNA *ptr, int value)
{
	rna_FileBrowser_FSMenu_active_set(ptr, value, FS_CATEGORY_SYSTEM_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuSystemBookmark_active_range(PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax)
{
	rna_FileBrowser_FSMenu_active_range(ptr, min, max, softmin, softmax, FS_CATEGORY_SYSTEM_BOOKMARKS);
}

static int rna_FileBrowser_FSMenuBookmark_active_get(PointerRNA *ptr)
{
	return rna_FileBrowser_FSMenu_active_get(ptr, FS_CATEGORY_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuBookmark_active_set(PointerRNA *ptr, int value)
{
	rna_FileBrowser_FSMenu_active_set(ptr, value, FS_CATEGORY_BOOKMARKS);
}

static void rna_FileBrowser_FSMenuBookmark_active_range(PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax)
{
	rna_FileBrowser_FSMenu_active_range(ptr, min, max, softmin, softmax, FS_CATEGORY_BOOKMARKS);
}

static int rna_FileBrowser_FSMenuRecent_active_get(PointerRNA *ptr)
{
	return rna_FileBrowser_FSMenu_active_get(ptr, FS_CATEGORY_RECENT);
}

static void rna_FileBrowser_FSMenuRecent_active_set(PointerRNA *ptr, int value)
{
	rna_FileBrowser_FSMenu_active_set(ptr, value, FS_CATEGORY_RECENT);
}

static void rna_FileBrowser_FSMenuRecent_active_range(PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax)
{
	rna_FileBrowser_FSMenu_active_range(ptr, min, max, softmin, softmax, FS_CATEGORY_RECENT);
}

#else

static void rna_def_space(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "Space", NULL);
	RNA_def_struct_sdna(srna, "SpaceLink");
	RNA_def_struct_ui_text(srna, "Space", "Space data for a screen area");
	RNA_def_struct_refine_func(srna, "rna_Space_refine");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "spacetype");
	RNA_def_property_enum_items(prop, rna_enum_space_type_items);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Type", "Space data type");
}

static void rna_def_space_outliner(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem display_mode_items[] = {
		{SO_ALL_SCENES, "ALL_SCENES", 0, "All Scenes", "Display data-blocks in all scenes"},
		{SO_CUR_SCENE, "CURRENT_SCENE", 0, "Current Scene", "Display data-blocks in current scene"},
		{SO_VISIBLE, "VISIBLE_LAYERS", 0, "Visible Layers", "Display data-blocks in visible layers"},
		{SO_SELECTED, "SELECTED", 0, "Selected", "Display data-blocks of selected, visible objects"},
		{SO_ACTIVE, "ACTIVE", 0, "Active", "Display data-blocks of active object"},
		{SO_SAME_TYPE, "SAME_TYPES", 0, "Same Types",
		               "Display data-blocks of all objects of same type as selected object"},
		{SO_GROUPS, "GROUPS", 0, "Groups", "Display groups and their data-blocks"},
		{SO_LIBRARIES, "LIBRARIES", 0, "Blender File", "Display data of current file and linked libraries"},
		{SO_DATABLOCKS, "DATABLOCKS", 0, "Data-Blocks", "Display all raw data-blocks"},
		{SO_USERDEF, "USER_PREFERENCES", 0, "User Preferences", "Display user preference data"},
		{SO_ID_ORPHANS, "ORPHAN_DATA", 0, "Orphan Data",
		                "Display data-blocks which are unused and/or will be lost when the file is reloaded"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "SpaceOutliner", "Space");
	RNA_def_struct_sdna(srna, "SpaceOops");
	RNA_def_struct_ui_text(srna, "Space Outliner", "Outliner space data");

	prop = RNA_def_property(srna, "display_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "outlinevis");
	RNA_def_property_enum_items(prop, display_mode_items);
	RNA_def_property_ui_text(prop, "Display Mode", "Type of information to display");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	prop = RNA_def_property(srna, "filter_text", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "search_string");
	RNA_def_property_ui_text(prop, "Display Filter", "Live search filtering string");
	RNA_def_property_flag(prop, PROP_TEXTEDIT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	prop = RNA_def_property(srna, "use_filter_case_sensitive", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "search_flags", SO_FIND_CASE_SENSITIVE);
	RNA_def_property_ui_text(prop, "Case Sensitive Matches Only", "Only use case sensitive matches of search string");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	prop = RNA_def_property(srna, "use_filter_complete", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "search_flags", SO_FIND_COMPLETE);
	RNA_def_property_ui_text(prop, "Complete Matches Only", "Only use complete matches of search string");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	prop = RNA_def_property(srna, "use_sort_alpha", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", SO_SKIP_SORT_ALPHA);
	RNA_def_property_ui_text(prop, "Sort Alphabetically", "");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);

	prop = RNA_def_property(srna, "show_restrict_columns", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", SO_HIDE_RESTRICTCOLS);
	RNA_def_property_ui_text(prop, "Show Restriction Columns", "Show column");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_OUTLINER, NULL);
}

static void rna_def_background_image(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* note: combinations work but don't flip so arnt that useful */
	static const EnumPropertyItem bgpic_axis_items[] = {
		{0, "", 0, N_("X Axis"), ""},
		{(1 << RV3D_VIEW_LEFT), "LEFT", 0, "Left", "Show background image while looking to the left"},
		{(1 << RV3D_VIEW_RIGHT), "RIGHT", 0, "Right", "Show background image while looking to the right"},
		/*{(1<<RV3D_VIEW_LEFT)|(1<<RV3D_VIEW_RIGHT), "LEFT_RIGHT", 0, "Left/Right", ""},*/
		{0, "", 0, N_("Y Axis"), ""},
		{(1 << RV3D_VIEW_BACK), "BACK", 0, "Back", "Show background image in back view"},
		{(1 << RV3D_VIEW_FRONT), "FRONT", 0, "Front", "Show background image in front view"},
		/*{(1<<RV3D_VIEW_BACK)|(1<<RV3D_VIEW_FRONT), "BACK_FRONT", 0, "Back/Front", ""},*/
		{0, "", 0, N_("Z Axis"), ""},
		{(1 << RV3D_VIEW_BOTTOM), "BOTTOM", 0, "Bottom", "Show background image in bottom view"},
		{(1 << RV3D_VIEW_TOP), "TOP", 0, "Top", "Show background image in top view"},
		/*{(1<<RV3D_VIEW_BOTTOM)|(1<<RV3D_VIEW_TOP), "BOTTOM_TOP", 0, "Top/Bottom", ""},*/
		{0, "", 0, N_("Other"), ""},
		{0, "ALL", 0, "All Views", "Show background image in all views"},
		{(1 << RV3D_VIEW_CAMERA), "CAMERA", 0, "Camera", "Show background image in camera view"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem bgpic_source_items[] = {
		{V3D_BGPIC_IMAGE, "IMAGE", 0, "Image", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem bgpic_camera_frame_items[] = {
		{0, "STRETCH", 0, "Stretch", ""},
		{V3D_BGPIC_CAMERA_ASPECT, "FIT", 0, "Fit", ""},
		{V3D_BGPIC_CAMERA_ASPECT | V3D_BGPIC_CAMERA_CROP, "CROP", 0, "Crop", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem bgpic_draw_depth_items[] = {
		{0, "BACK", 0, "Back", ""},
		{V3D_BGPIC_FOREGROUND, "FRONT", 0, "Front", ""},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "BackgroundImage", NULL);
	RNA_def_struct_sdna(srna, "BGpic");
	RNA_def_struct_ui_text(srna, "Background Image", "Image and settings for display in the 3D View background");

	prop = RNA_def_property(srna, "source", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "source");
	RNA_def_property_enum_items(prop, bgpic_source_items);
	RNA_def_property_ui_text(prop, "Background Source", "Data source used for background");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "image", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "ima");
	RNA_def_property_ui_text(prop, "Image", "Image displayed and edited in this space");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "image_user", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "iuser");
	RNA_def_property_ui_text(prop, "Image User",
	                         "Parameters defining which layer, pass and frame of the image is displayed");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "offset_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "xof");
	RNA_def_property_ui_text(prop, "X Offset", "Offset image horizontally from the world origin");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "offset_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "yof");
	RNA_def_property_ui_text(prop, "Y Offset", "Offset image vertically from the world origin");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "size", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "size");
	RNA_def_property_float_funcs(prop, "rna_BackgroundImage_size_get", "rna_BackgroundImage_size_set", NULL);
	RNA_def_property_ui_text(prop, "Size", "Size of the background image (ortho view only)");
	RNA_def_property_range(prop, 0.0, FLT_MAX);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "rotation", PROP_FLOAT, PROP_EULER);
	RNA_def_property_float_sdna(prop, NULL, "rotation");
	RNA_def_property_ui_text(prop, "Rotation", "Rotation for the background image (ortho view only)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "use_flip_x", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_BGPIC_FLIP_X);
	RNA_def_property_ui_text(prop, "Flip Horizontally", "Flip the background image horizontally");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "use_flip_y", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_BGPIC_FLIP_Y);
	RNA_def_property_ui_text(prop, "Flip Vertically", "Flip the background image vertically");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "opacity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "blend");
	RNA_def_property_float_funcs(prop, "rna_BackgroundImage_opacity_get", "rna_BackgroundImage_opacity_set", NULL);
	RNA_def_property_ui_text(prop, "Opacity", "Image opacity to blend the image against the background color");
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "view_axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "view");
	RNA_def_property_enum_items(prop, bgpic_axis_items);
	RNA_def_property_ui_text(prop, "Image Axis", "The axis to display the image on");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_expanded", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_BGPIC_EXPANDED);
	RNA_def_property_ui_text(prop, "Show Expanded", "Show the expanded in the user interface");
	RNA_def_property_ui_icon(prop, ICON_TRIA_RIGHT, 1);

	prop = RNA_def_property(srna, "use_camera_clip", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_BGPIC_CAMERACLIP);
	RNA_def_property_ui_text(prop, "Camera Clip", "Use movie clip from active scene camera");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_background_image", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", V3D_BGPIC_DISABLED);
	RNA_def_property_ui_text(prop, "Show Background Image", "Show this image as background");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_on_foreground", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_BGPIC_FOREGROUND);
	RNA_def_property_ui_text(prop, "Show On Foreground", "Show this image in front of objects in viewport");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	/* expose 1 flag as a enum of 2 items */
	prop = RNA_def_property(srna, "draw_depth", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, bgpic_draw_depth_items);
	RNA_def_property_ui_text(prop, "Depth", "Draw under or over everything");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	/* expose 2 flags as a enum of 3 items */
	prop = RNA_def_property(srna, "frame_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, bgpic_camera_frame_items);
	RNA_def_property_ui_text(prop, "Frame Method", "How the image fits in the camera frame");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);
}

static void rna_def_backgroundImages(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "BackgroundImages");
	srna = RNA_def_struct(brna, "BackgroundImages", NULL);
	RNA_def_struct_sdna(srna, "View3D");
	RNA_def_struct_ui_text(srna, "Background Images", "Collection of background images");

	func = RNA_def_function(srna, "new", "rna_BackgroundImage_new");
	RNA_def_function_ui_description(func, "Add new background image");
	parm = RNA_def_pointer(func, "image", "BackgroundImage", "", "Image displayed as viewport background");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_BackgroundImage_remove");
	RNA_def_function_ui_description(func, "Remove background image");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "image", "BackgroundImage", "", "Image displayed as viewport background");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	func = RNA_def_function(srna, "clear", "rna_BackgroundImage_clear");
	RNA_def_function_ui_description(func, "Remove all background images");
}


static void rna_def_space_view3d(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem manipulators_items[] = {
		{V3D_MANIP_TRANSLATE, "TRANSLATE", ICON_MAN_TRANS, "Translate",
		                      "Use the manipulator for movement transformations"},
		{V3D_MANIP_ROTATE, "ROTATE", ICON_MAN_ROT, "Rotate",
		                   "Use the manipulator for rotation transformations"},
		{V3D_MANIP_SCALE, "SCALE", ICON_MAN_SCALE, "Scale",
		                  "Use the manipulator for scale transformations"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem rv3d_persp_items[] = {
		{RV3D_PERSP, "PERSP", 0, "Perspective", ""},
		{RV3D_ORTHO, "ORTHO", 0, "Orthographic", ""},
		{RV3D_CAMOB, "CAMERA", 0, "Camera", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem bundle_drawtype_items[] = {
		{OB_PLAINAXES, "PLAIN_AXES", 0, "Plain Axes", ""},
		{OB_ARROWS, "ARROWS", 0, "Arrows", ""},
		{OB_SINGLE_ARROW, "SINGLE_ARROW", 0, "Single Arrow", ""},
		{OB_CIRCLE, "CIRCLE", 0, "Circle", ""},
		{OB_CUBE, "CUBE", 0, "Cube", ""},
		{OB_EMPTY_SPHERE, "SPHERE", 0, "Sphere", ""},
		{OB_EMPTY_CONE, "CONE", 0, "Cone", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem view3d_matcap_items[] = {
		{ICON_MATCAP_01, "01", ICON_MATCAP_01, "", ""},
		{ICON_MATCAP_02, "02", ICON_MATCAP_02, "", ""},
		{ICON_MATCAP_03, "03", ICON_MATCAP_03, "", ""},
		{ICON_MATCAP_04, "04", ICON_MATCAP_04, "", ""},
		{ICON_MATCAP_05, "05", ICON_MATCAP_05, "", ""},
		{ICON_MATCAP_06, "06", ICON_MATCAP_06, "", ""},
		{ICON_MATCAP_07, "07", ICON_MATCAP_07, "", ""},
		{ICON_MATCAP_08, "08", ICON_MATCAP_08, "", ""},
		{ICON_MATCAP_09, "09", ICON_MATCAP_09, "", ""},
		{ICON_MATCAP_10, "10", ICON_MATCAP_10, "", ""},
		{ICON_MATCAP_11, "11", ICON_MATCAP_11, "", ""},
		{ICON_MATCAP_12, "12", ICON_MATCAP_12, "", ""},
		{ICON_MATCAP_13, "13", ICON_MATCAP_13, "", ""},
		{ICON_MATCAP_14, "14", ICON_MATCAP_14, "", ""},
		{ICON_MATCAP_15, "15", ICON_MATCAP_15, "", ""},
		{ICON_MATCAP_16, "16", ICON_MATCAP_16, "", ""},
		{ICON_MATCAP_17, "17", ICON_MATCAP_17, "", ""},
		{ICON_MATCAP_18, "18", ICON_MATCAP_18, "", ""},
		{ICON_MATCAP_19, "19", ICON_MATCAP_19, "", ""},
		{ICON_MATCAP_20, "20", ICON_MATCAP_20, "", ""},
		{ICON_MATCAP_21, "21", ICON_MATCAP_21, "", ""},
		{ICON_MATCAP_22, "22", ICON_MATCAP_22, "", ""},
		{ICON_MATCAP_23, "23", ICON_MATCAP_23, "", ""},
		{ICON_MATCAP_24, "24", ICON_MATCAP_24, "", ""},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "SpaceView3D", "Space");
	RNA_def_struct_sdna(srna, "View3D");
	RNA_def_struct_ui_text(srna, "3D View Space", "3D View space data");

	prop = RNA_def_property(srna, "camera", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_pointer_sdna(prop, NULL, "camera");
	RNA_def_property_ui_text(prop, "Camera",
	                         "Active camera used in this view (when unlocked from the scene's active camera)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_camera_update");

	/* render border */
	prop = RNA_def_property(srna, "use_render_border", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_RENDER_BORDER);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_ui_text(prop, "Render Border", "Use a region within the frame size for rendered viewport "
	                         "(when not viewing through the camera)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "render_border_min_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "render_border.xmin");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Border Minimum X", "Minimum X value for the render border");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "render_border_min_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "render_border.ymin");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Border Minimum Y", "Minimum Y value for the render border");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "render_border_max_x", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "render_border.xmax");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Border Maximum X", "Maximum X value for the render border");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "render_border_max_y", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "render_border.ymax");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Border Maximum Y", "Maximum Y value for the render border");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "lock_object", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_pointer_sdna(prop, NULL, "ob_centre");
	RNA_def_property_ui_text(prop, "Lock to Object", "3D View center is locked to this object's position");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "lock_cursor", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ob_centre_cursor", 1);
	RNA_def_property_ui_text(prop, "Lock to Cursor", "3D View center is locked to the cursor's position");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "viewport_shade", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "drawtype");
	RNA_def_property_enum_items(prop, rna_enum_viewport_shade_items);
	RNA_def_property_enum_funcs(prop, "rna_SpaceView3D_viewport_shade_get", "rna_SpaceView3D_viewport_shade_set",
	                            "rna_SpaceView3D_viewport_shade_itemf");
	RNA_def_property_ui_text(prop, "Viewport Shading", "Method to display/shade objects in the 3D View");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "local_view", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "localvd");
	RNA_def_property_ui_text(prop, "Local View",
	                         "Display an isolated sub-set of objects, apart from the scene visibility");

	prop = RNA_def_property(srna, "cursor_location", PROP_FLOAT, PROP_XYZ_LENGTH);
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_funcs(prop, "rna_View3D_CursorLocation_get", "rna_View3D_CursorLocation_set", NULL);
	RNA_def_property_ui_text(prop, "3D Cursor Location",
	                         "3D cursor location for this view (dependent on local view setting)");
	RNA_def_property_ui_range(prop, -10000.0, 10000.0, 1, RNA_TRANSLATION_PREC_DEFAULT);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "lens", PROP_FLOAT, PROP_UNIT_CAMERA);
	RNA_def_property_float_sdna(prop, NULL, "lens");
	RNA_def_property_ui_text(prop, "Lens", "Viewport lens angle");
	RNA_def_property_range(prop, 1.0f, 250.0f);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "clip_start", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "near");
	RNA_def_property_range(prop, 1e-6f, FLT_MAX);
	RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
	RNA_def_property_float_default(prop, 0.1f);
	RNA_def_property_ui_text(prop, "Clip Start", "3D View near clipping distance (perspective view only)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "clip_end", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "far");
	RNA_def_property_range(prop, 1e-6f, FLT_MAX);
	RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
	RNA_def_property_float_default(prop, 1000.0f);
	RNA_def_property_ui_text(prop, "Clip End", "3D View far clipping distance");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "grid_scale", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "grid");
	RNA_def_property_ui_text(prop, "Grid Scale", "Distance between 3D View grid lines");
	RNA_def_property_range(prop, 0.0f, FLT_MAX);
	RNA_def_property_ui_range(prop, 0.001f, 1000.0f, 0.1f, 3);
	RNA_def_property_float_default(prop, 1.0f);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "grid_lines", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "gridlines");
	RNA_def_property_ui_text(prop, "Grid Lines", "Number of grid lines to display in perspective view");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_int_default(prop, 16);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "grid_subdivisions", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "gridsubdiv");
	RNA_def_property_ui_text(prop, "Grid Subdivisions", "Number of subdivisions between grid lines");
	RNA_def_property_range(prop, 1, 1024);
	RNA_def_property_int_default(prop, 10);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "grid_scale_unit", PROP_FLOAT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_float_funcs(prop, "rna_View3D_GridScaleUnit_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Grid Scale Unit", "Grid cell size scaled by scene unit system settings");

	prop = RNA_def_property(srna, "show_floor", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gridflag", V3D_SHOW_FLOOR);
	RNA_def_property_ui_text(prop, "Display Grid Floor", "Show the ground plane grid in perspective view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_axis_x", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gridflag", V3D_SHOW_X);
	RNA_def_property_ui_text(prop, "Display X Axis", "Show the X axis line in perspective view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_axis_y", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gridflag", V3D_SHOW_Y);
	RNA_def_property_ui_text(prop, "Display Y Axis", "Show the Y axis line in perspective view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_axis_z", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gridflag", V3D_SHOW_Z);
	RNA_def_property_ui_text(prop, "Display Z Axis", "Show the Z axis line in perspective view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_outline_selected", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_SELECT_OUTLINE);
	RNA_def_property_ui_text(prop, "Outline Selected",
	                         "Show an outline highlight around selected objects in non-wireframe views");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_all_objects_origin", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_DRAW_CENTERS);
	RNA_def_property_ui_text(prop, "All Object Origins",
	                         "Show the object origin center dot for all (selected and unselected) objects");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_relationship_lines", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", V3D_HIDE_HELPLINES);
	RNA_def_property_ui_text(prop, "Relationship Lines",
	                         "Show dashed lines indicating parent or constraint relationships");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_textured_solid", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SOLID_TEX);
	RNA_def_property_ui_text(prop, "Textured Solid", "Display face-assigned textures in solid view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_backface_culling", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_BACKFACE_CULLING);
	RNA_def_property_ui_text(prop, "Backface Culling", "Use back face culling to hide the back side of faces");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_textured_shadeless", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SHADELESS_TEX);
	RNA_def_property_ui_text(prop, "Shadeless", "Show shadeless texture without lighting in textured draw mode");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_occlude_wire", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_OCCLUDE_WIRE);
	RNA_def_property_ui_text(prop, "Hidden Wire", "Use hidden wireframe display");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "lock_camera", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_LOCK_CAMERA);
	RNA_def_property_ui_text(prop, "Lock Camera to View", "Enable view navigation within the camera view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_only_render", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_RENDER_OVERRIDE);
	RNA_def_property_ui_text(prop, "Only Render", "Display only objects which will be rendered");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_world", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SHOW_WORLD);
	RNA_def_property_ui_text(prop, "World Background", "Display world colors in the background");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "use_occlude_geometry", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_ZBUF_SELECT);
	RNA_def_property_ui_text(prop, "Occlude Geometry", "Limit selection to visible (clipped with depth buffer)");
	RNA_def_property_ui_icon(prop, ICON_ORTHO, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "background_images", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "bgpicbase", NULL);
	RNA_def_property_struct_type(prop, "BackgroundImage");
	RNA_def_property_ui_text(prop, "Background Images", "List of background images");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);
	rna_def_backgroundImages(brna, prop);

	prop = RNA_def_property(srna, "show_background_images", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_DISPBGPICS);
	RNA_def_property_ui_text(prop, "Display Background Images",
	                         "Display reference images behind objects in the 3D View");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "pivot_point", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "around");
	RNA_def_property_enum_items(prop, pivot_items_full);
	RNA_def_property_ui_text(prop, "Pivot Point", "Pivot center for rotation/scaling");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_pivot_update");

	prop = RNA_def_property(srna, "use_pivot_point_align", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", V3D_ALIGN);
	RNA_def_property_ui_text(prop, "Align", "Manipulate center points (object, pose and weight paint mode only)");
	RNA_def_property_ui_icon(prop, ICON_ALIGN, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_pivot_update");

	prop = RNA_def_property(srna, "show_manipulator", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "twflag", V3D_USE_MANIPULATOR);
	RNA_def_property_ui_text(prop, "Manipulator", "Use a 3D manipulator widget for controlling transforms");
	RNA_def_property_ui_icon(prop, ICON_MANIPUL, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "transform_manipulators", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "twtype");
	RNA_def_property_enum_items(prop, manipulators_items);
	RNA_def_property_flag(prop, PROP_ENUM_FLAG);
	RNA_def_property_ui_text(prop, "Transform Manipulators", "Transformation manipulators");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "transform_orientation", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "twmode");
	RNA_def_property_enum_items(prop, transform_orientation_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_TransformOrientation_itemf");
	RNA_def_property_ui_text(prop, "Transform Orientation", "Transformation orientation");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "current_orientation", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "TransformOrientation");
	RNA_def_property_pointer_funcs(prop, "rna_CurrentOrientation_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Current Transform Orientation", "Current transformation orientation");

	prop = RNA_def_property(srna, "lock_camera_and_layers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "scenelock", 1);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_SpaceView3D_lock_camera_and_layers_set");
	RNA_def_property_ui_text(prop, "Lock Camera and Layers",
	                         "Use the scene's active camera and layers in this view, rather than local layers");
	RNA_def_property_ui_icon(prop, ICON_LOCKVIEW_OFF, 1);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "layers", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_SpaceView3D_layer_set");
	RNA_def_property_ui_text(prop, "Visible Layers", "Layers visible in this 3D View");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_layer_update");

	prop = RNA_def_property(srna, "active_layer", PROP_INT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
	RNA_def_property_int_funcs(prop, "rna_SpaceView3D_active_layer_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Active Layer", "Active 3D view layer index");

	prop = RNA_def_property(srna, "layers_local_view", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 0x01000000);
	RNA_def_property_array(prop, 8);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Local View Layers", "Local view layers visible in this 3D View");

	prop = RNA_def_property(srna, "layers_used", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_boolean_sdna(prop, NULL, "lay_used", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Used Layers", "Layers that contain something");

	prop = RNA_def_property(srna, "region_3d", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "RegionView3D");
	RNA_def_property_pointer_funcs(prop, "rna_SpaceView3D_region_3d_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "3D Region", "3D region in this space, in case of quad view the camera region");

	prop = RNA_def_property(srna, "region_quadviews", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "RegionView3D");
	RNA_def_property_collection_funcs(prop, "rna_SpaceView3D_region_quadviews_begin", "rna_iterator_listbase_next",
	                                  "rna_iterator_listbase_end", "rna_SpaceView3D_region_quadviews_get",
	                                  NULL, NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Quad View Regions", "3D regions (the third one defines quad view settings, "
	                                                    "the fourth one is same as 'region_3d')");

	prop = RNA_def_property(srna, "show_reconstruction", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SHOW_RECONSTRUCTION);
	RNA_def_property_ui_text(prop, "Show Reconstruction", "Display reconstruction data from active movie clip");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "tracks_draw_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, FLT_MAX);
	RNA_def_property_ui_range(prop, 0, 5, 1, 3);
	RNA_def_property_float_sdna(prop, NULL, "bundle_size");
	RNA_def_property_ui_text(prop, "Tracks Size", "Display size of tracks from reconstructed data");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "tracks_draw_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "bundle_drawtype");
	RNA_def_property_enum_items(prop, bundle_drawtype_items);
	RNA_def_property_ui_text(prop, "Tracks Display Type", "Viewport display style for tracks");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_camera_path", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SHOW_CAMERAPATH);
	RNA_def_property_ui_text(prop, "Show Camera Path", "Show reconstructed camera path");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "show_bundle_names", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SHOW_BUNDLENAME);
	RNA_def_property_ui_text(prop, "Show 3D Marker Names", "Show names for reconstructed tracks objects");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "use_matcap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag2", V3D_SOLID_MATCAP);
	RNA_def_property_ui_text(prop, "Matcap", "Active Objects draw images mapped on normals, enhancing Solid Draw Mode");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_matcap_enable");

	prop = RNA_def_property(srna, "matcap_icon", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "matcap_icon");
	RNA_def_property_enum_items(prop, view3d_matcap_items);
	RNA_def_property_ui_text(prop, "Matcap", "Image to use for Material Capture, active objects only");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_SpaceView3D_matcap_update");

	/* *** Animated *** */
	RNA_define_animate_sdna(true);
	/* region */

	srna = RNA_def_struct(brna, "RegionView3D", NULL);
	RNA_def_struct_sdna(srna, "RegionView3D");
	RNA_def_struct_ui_text(srna, "3D View Region", "3D View region data");

	prop = RNA_def_property(srna, "lock_rotation", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "viewlock", RV3D_LOCKED);
	RNA_def_property_ui_text(prop, "Lock", "Lock view rotation in side views");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_RegionView3D_quadview_update");

	prop = RNA_def_property(srna, "show_sync_view", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "viewlock", RV3D_BOXVIEW);
	RNA_def_property_ui_text(prop, "Box", "Sync view position between side views");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_RegionView3D_quadview_update");

	prop = RNA_def_property(srna, "use_box_clip", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "viewlock", RV3D_BOXCLIP);
	RNA_def_property_ui_text(prop, "Clip", "Clip objects based on what's visible in other side views");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_RegionView3D_quadview_clip_update");

	prop = RNA_def_property(srna, "perspective_matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "persmat");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* XXX: for now, it's too risky for users to do this */
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(prop, "Perspective Matrix",
	                         "Current perspective matrix (``window_matrix * view_matrix``)");

	prop = RNA_def_property(srna, "window_matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "winmat");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(prop, "Window Matrix", "Current window matrix");

	prop = RNA_def_property(srna, "view_matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "viewmat");
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_float_funcs(prop, NULL, "rna_RegionView3D_view_matrix_set", NULL);
	RNA_def_property_ui_text(prop, "View Matrix", "Current view matrix");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "view_perspective", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "persp");
	RNA_def_property_enum_items(prop, rv3d_persp_items);
	RNA_def_property_ui_text(prop, "Perspective", "View Perspective");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "is_perspective", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "is_persp", 1);
	RNA_def_property_ui_text(prop, "Is Perspective", "");
	RNA_def_property_flag(prop, PROP_EDITABLE);

	/* This isn't directly accessible from the UI, only an operator. */
	prop = RNA_def_property(srna, "use_clip_planes", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rflag", RV3D_CLIPPING);
	RNA_def_property_ui_text(prop, "Use Clip Planes", "");

	prop = RNA_def_property(srna, "clip_planes", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "clip");
	RNA_def_property_multi_array(prop, 2, (int[]){6, 4});
	RNA_def_property_ui_text(prop, "Clip Planes", "");

	prop = RNA_def_property(srna, "view_location", PROP_FLOAT, PROP_TRANSLATION);
#if 0
	RNA_def_property_float_sdna(prop, NULL, "ofs"); /* cant use because its negated */
#else
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_funcs(prop, "rna_RegionView3D_view_location_get",
	                             "rna_RegionView3D_view_location_set", NULL);
#endif
	RNA_def_property_ui_text(prop, "View Location", "View pivot location");
	RNA_def_property_ui_range(prop, -10000.0, 10000.0, 10, RNA_TRANSLATION_PREC_DEFAULT);
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop = RNA_def_property(srna, "view_rotation", PROP_FLOAT, PROP_QUATERNION); /* cant use because its inverted */
#if 0
	RNA_def_property_float_sdna(prop, NULL, "viewquat");
#else
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_RegionView3D_view_rotation_get",
	                             "rna_RegionView3D_view_rotation_set", NULL);
#endif
	RNA_def_property_ui_text(prop, "View Rotation", "Rotation in quaternions (keep normalized)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	/* not sure we need rna access to these but adding anyway */
	prop = RNA_def_property(srna, "view_distance", PROP_FLOAT, PROP_UNSIGNED);
	RNA_def_property_float_sdna(prop, NULL, "dist");
	RNA_def_property_ui_text(prop, "Distance", "Distance to the view location");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "view_camera_zoom", PROP_FLOAT, PROP_UNSIGNED);
	RNA_def_property_float_sdna(prop, NULL, "camzoom");
	RNA_def_property_ui_text(prop, "Camera Zoom", "Zoom factor in camera view");
	RNA_def_property_range(prop, RV3D_CAMZOOM_MIN, RV3D_CAMZOOM_MAX);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "view_camera_offset", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "camdx");
	RNA_def_property_array(prop, 2);
	RNA_def_property_ui_text(prop, "Camera Offset", "View shift in camera view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	RNA_api_region_view3d(srna);
}

static void rna_def_space_buttons(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem align_items[] = {
		{BUT_HORIZONTAL, "HORIZONTAL", 0, "Horizontal", ""},
		{BUT_VERTICAL, "VERTICAL", 0, "Vertical", ""},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "SpaceProperties", "Space");
	RNA_def_struct_sdna(srna, "SpaceButs");
	RNA_def_struct_ui_text(srna, "Properties Space", "Properties space data");

	prop = RNA_def_property(srna, "context", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "mainb");
	RNA_def_property_enum_items(prop, buttons_context_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_SpaceProperties_context_set", "rna_SpaceProperties_context_itemf");
	RNA_def_property_ui_text(prop, "Context", "Type of active data to display and edit");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_PROPERTIES, "rna_SpaceProperties_context_update");

	prop = RNA_def_property(srna, "align", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "align");
	RNA_def_property_enum_items(prop, align_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_SpaceProperties_align_set", NULL);
	RNA_def_property_ui_text(prop, "Align", "Arrangement of the panels");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_PROPERTIES, NULL);

	prop = RNA_def_property(srna, "texture_context", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, buttons_texture_context_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_SpaceProperties_texture_context_set",
	                            "rna_SpaceProperties_texture_context_itemf");
	RNA_def_property_ui_text(prop, "Texture Context", "Type of texture data to display and edit");
	RNA_def_property_update(prop, NC_TEXTURE, NULL);

	prop = RNA_def_property(srna, "use_limited_texture_context", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", SB_TEX_USER_LIMITED);
	RNA_def_property_ui_text(prop, "Limited Texture Context",
	                         "Use the limited version of texture user (for 'old shading' mode)");

	/* pinned data */
	prop = RNA_def_property(srna, "pin_id", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "pinid");
	RNA_def_property_struct_type(prop, "ID");
	/* note: custom set function is ONLY to avoid rna setting a user for this. */
	RNA_def_property_pointer_funcs(prop, NULL, "rna_SpaceProperties_pin_id_set",
	                               "rna_SpaceProperties_pin_id_typef", NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_PROPERTIES, "rna_SpaceProperties_pin_id_update");

	prop = RNA_def_property(srna, "use_pin_id", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", SB_PIN_CONTEXT);
	RNA_def_property_ui_text(prop, "Pin ID", "Use the pinned context");
}

static void rna_def_space_image(BlenderRNA *brna)
{
	static const EnumPropertyItem image_space_mode_items[] = {
		{SI_MODE_VIEW, "VIEW", ICON_FILE_IMAGE, "View", "View the image and UV edit in mesh editmode"},
		{SI_MODE_PAINT, "PAINT", ICON_TPAINT_HLT, "Paint", "2D image painting mode"},
		{SI_MODE_MASK, "MASK", ICON_MOD_MASK, "Mask", "Mask editing"},
		{0, NULL, 0, NULL, NULL}
	};

	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceImageEditor", "Space");
	RNA_def_struct_sdna(srna, "SpaceImage");
	RNA_def_struct_ui_text(srna, "Space Image Editor", "Image and UV editor space data");

	/* image */
	prop = RNA_def_property(srna, "image", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_funcs(prop, NULL, "rna_SpaceImageEditor_image_set", NULL, NULL);
	RNA_def_property_ui_text(prop, "Image", "Image displayed and edited in this space");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_update(prop, NC_GEOM | ND_DATA, "rna_SpaceImageEditor_image_update"); /* is handled in image editor too */

	prop = RNA_def_property(srna, "image_user", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "iuser");
	RNA_def_property_ui_text(prop, "Image User",
	                         "Parameters defining which layer, pass and frame of the image is displayed");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	prop = RNA_def_property(srna, "scopes", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "scopes");
	RNA_def_property_struct_type(prop, "Scopes");
	RNA_def_property_ui_text(prop, "Scopes", "Scopes to visualize image statistics");
	RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, "rna_SpaceImageEditor_scopes_update");

	prop = RNA_def_property(srna, "use_image_pin", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "pin", 0);
	RNA_def_property_ui_text(prop, "Image Pin", "Display current image regardless of object selection");
	RNA_def_property_ui_icon(prop, ICON_UNPINNED, 1);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	prop = RNA_def_property(srna, "sample_histogram", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "sample_line_hist");
	RNA_def_property_struct_type(prop, "Histogram");
	RNA_def_property_ui_text(prop, "Line sample", "Sampled colors along line");

	prop = RNA_def_property(srna, "zoom", PROP_FLOAT, PROP_NONE);
	RNA_def_property_array(prop, 2);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_float_funcs(prop, "rna_SpaceImageEditor_zoom_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Zoom", "Zoom factor");

	/* image draw */
	prop = RNA_def_property(srna, "show_repeat", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", SI_DRAW_TILE);
	RNA_def_property_ui_text(prop, "Draw Repeated", "Draw the image repeated outside of the main view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	prop = RNA_def_property(srna, "draw_channels", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, draw_channels_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_SpaceImageEditor_draw_channels_itemf");
	RNA_def_property_ui_text(prop, "Draw Channels", "Channels of the image to draw");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	/* mode */
	prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "mode");
	RNA_def_property_enum_items(prop, image_space_mode_items);
	RNA_def_property_ui_text(prop, "Mode", "Editing context being displayed");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, "rna_SpaceImageEditor_mode_update");

	/* transform */
	prop = RNA_def_property(srna, "cursor_location", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_array(prop, 2);
	RNA_def_property_float_funcs(prop, "rna_SpaceImageEditor_cursor_location_get",
	                             "rna_SpaceImageEditor_cursor_location_set", NULL);
	RNA_def_property_ui_text(prop, "2D Cursor Location", "2D cursor location for this view");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	prop = RNA_def_property(srna, "pivot_point", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "around");
	RNA_def_property_enum_items(prop, pivot_items_full);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_SpaceImageEditor_pivot_itemf");
	RNA_def_property_ui_text(prop, "Pivot", "Rotation/Scaling Pivot");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_IMAGE, NULL);

	/* update */
	prop = RNA_def_property(srna, "use_realtime_update", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "lock", 0);
	RNA_def_property_ui_text(prop, "Update Automatically",
	                         "Update other affected window spaces automatically to reflect changes "
	                         "during interactive operations such as transform");

}

static void rna_def_space_text(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceTextEditor", "Space");
	RNA_def_struct_sdna(srna, "SpaceText");
	RNA_def_struct_ui_text(srna, "Space Text Editor", "Text editor space data");

	/* text */
	prop = RNA_def_property(srna, "text", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Text", "Text displayed and edited in this space");
	RNA_def_property_pointer_funcs(prop, NULL, "rna_SpaceTextEditor_text_set", NULL, NULL);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	/* display */
	prop = RNA_def_property(srna, "show_word_wrap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "wordwrap", 0);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_SpaceTextEditor_word_wrap_set");
	RNA_def_property_ui_text(prop, "Word Wrap", "Wrap words if there is not enough horizontal space");
	RNA_def_property_ui_icon(prop, ICON_WORDWRAP_OFF, 1);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "show_line_numbers", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "showlinenrs", 0);
	RNA_def_property_ui_text(prop, "Line Numbers", "Show line numbers next to the text");
	RNA_def_property_ui_icon(prop, ICON_LINENUMBERS_OFF, 1);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "show_syntax_highlight", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "showsyntax", 0);
	RNA_def_property_ui_text(prop, "Syntax Highlight", "Syntax highlight for scripting");
	RNA_def_property_ui_icon(prop, ICON_SYNTAX_OFF, 1);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "show_line_highlight", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "line_hlight", 0);
	RNA_def_property_ui_text(prop, "Highlight Line", "Highlight the current line");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "tab_width", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "tabnumber");
	RNA_def_property_range(prop, 2, 8);
	RNA_def_property_ui_text(prop, "Tab Width", "Number of spaces to display tabs with");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, "rna_SpaceTextEditor_updateEdited");

	prop = RNA_def_property(srna, "font_size", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "lheight");
	RNA_def_property_range(prop, 8, 32);
	RNA_def_property_ui_text(prop, "Font Size", "Font size to use for displaying the text");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "show_margin", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", ST_SHOW_MARGIN);
	RNA_def_property_ui_text(prop, "Show Margin", "Show right margin");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "margin_column", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "margin_column");
	RNA_def_property_range(prop, 0, 1024);
	RNA_def_property_ui_text(prop, "Margin Column", "Column number to show right margin at");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "top", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "top");
	RNA_def_property_range(prop, 0, INT_MAX);
	RNA_def_property_ui_text(prop, "Top Line", "Top line visible");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "visible_lines", PROP_INT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_int_sdna(prop, NULL, "viewlines");
	RNA_def_property_ui_text(prop, "Visible Lines", "Amount of lines that can be visible in current editor");

	/* functionality options */
	prop = RNA_def_property(srna, "use_overwrite", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "overwrite", 1);
	RNA_def_property_ui_text(prop, "Overwrite", "Overwrite characters when typing rather than inserting them");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "use_live_edit", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "live_edit", 1);
	RNA_def_property_ui_text(prop, "Live Edit", "Run python while editing");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	/* find */
	prop = RNA_def_property(srna, "use_find_all", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", ST_FIND_ALL);
	RNA_def_property_ui_text(prop, "Find All", "Search in all text data-blocks, instead of only the active one");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "use_find_wrap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", ST_FIND_WRAP);
	RNA_def_property_ui_text(prop, "Find Wrap", "Search again from the start of the file when reaching the end");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "use_match_case", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", ST_MATCH_CASE);
	RNA_def_property_ui_text(prop, "Match case", "Search string is sensitive to uppercase and lowercase letters");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "find_text", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "findstr");
	RNA_def_property_ui_text(prop, "Find Text", "Text to search for with the find tool");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	prop = RNA_def_property(srna, "replace_text", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "replacestr");
	RNA_def_property_ui_text(prop, "Replace Text", "Text to replace selected text with using the replace tool");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

	RNA_api_space_text(srna);
}

static void rna_def_console_line(BlenderRNA *brna)
{
	static const EnumPropertyItem console_line_type_items[] = {
		{CONSOLE_LINE_OUTPUT, "OUTPUT", 0, "Output", ""},
		{CONSOLE_LINE_INPUT, "INPUT", 0, "Input", ""},
		{CONSOLE_LINE_INFO, "INFO", 0, "Info", ""},
		{CONSOLE_LINE_ERROR, "ERROR", 0, "Error", ""},
		{0, NULL, 0, NULL, NULL}
	};

	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ConsoleLine", NULL);
	RNA_def_struct_ui_text(srna, "Console Input", "Input line for the interactive console");

	prop = RNA_def_property(srna, "body", PROP_STRING, PROP_NONE);
	RNA_def_property_string_funcs(prop, "rna_ConsoleLine_body_get", "rna_ConsoleLine_body_length",
	                              "rna_ConsoleLine_body_set");
	RNA_def_property_ui_text(prop, "Line", "Text in the line");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_CONSOLE, NULL);
	RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_TEXT);

	prop = RNA_def_property(srna, "current_character", PROP_INT, PROP_NONE); /* copied from text editor */
	RNA_def_property_int_sdna(prop, NULL, "cursor");
	RNA_def_property_int_funcs(prop, NULL, NULL, "rna_ConsoleLine_cursor_index_range");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_CONSOLE, NULL);

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, console_line_type_items);
	RNA_def_property_ui_text(prop, "Type", "Console line type when used in scrollback");
}

static void rna_def_space_console(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceConsole", "Space");
	RNA_def_struct_sdna(srna, "SpaceConsole");
	RNA_def_struct_ui_text(srna, "Space Console", "Interactive python console");

	/* display */
	prop = RNA_def_property(srna, "font_size", PROP_INT, PROP_NONE); /* copied from text editor */
	RNA_def_property_int_sdna(prop, NULL, "lheight");
	RNA_def_property_range(prop, 8, 32);
	RNA_def_property_ui_text(prop, "Font Size", "Font size to use for displaying the text");
	RNA_def_property_update(prop, 0, "rna_SpaceConsole_rect_update");


	prop = RNA_def_property(srna, "select_start", PROP_INT, PROP_UNSIGNED); /* copied from text editor */
	RNA_def_property_int_sdna(prop, NULL, "sel_start");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_CONSOLE, NULL);

	prop = RNA_def_property(srna, "select_end", PROP_INT, PROP_UNSIGNED); /* copied from text editor */
	RNA_def_property_int_sdna(prop, NULL, "sel_end");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_CONSOLE, NULL);

	prop = RNA_def_property(srna, "prompt", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Prompt", "Command line prompt");

	prop = RNA_def_property(srna, "language", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Language", "Command line prompt language");

	prop = RNA_def_property(srna, "history", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "history", NULL);
	RNA_def_property_struct_type(prop, "ConsoleLine");
	RNA_def_property_ui_text(prop, "History", "Command history");

	prop = RNA_def_property(srna, "scrollback", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "scrollback", NULL);
	RNA_def_property_struct_type(prop, "ConsoleLine");
	RNA_def_property_ui_text(prop, "Output", "Command output");
}

static void rna_def_fileselect_params(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem file_display_items[] = {
		{FILE_SHORTDISPLAY, "LIST_SHORT", ICON_SHORTDISPLAY, "Short List", "Display files as short list"},
		{FILE_LONGDISPLAY,  "LIST_LONG", ICON_LONGDISPLAY, "Long List", "Display files as a detailed list"},
		{FILE_IMGDISPLAY, "THUMBNAIL", ICON_IMGDISPLAY, "Thumbnails", "Display files as thumbnails"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem display_size_items[] = {
	    {32,    "TINY",     0,      "Tiny", ""},
	    {64,    "SMALL",    0,      "Small", ""},
	    {128,   "NORMAL",   0,      "Normal", ""},
	    {256,   "LARGE",    0,      "Large", ""},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem file_filter_idtypes_items[] = {
		{FILTER_ID_CA, "CAMERA", ICON_CAMERA_DATA, "Cameras", "Show/hide Camera data-blocks"},
		{FILTER_ID_CF, "CACHEFILE", ICON_FILE, "Cache Files", "Show/hide Cache File data-blocks"},
		{FILTER_ID_CU, "CURVE", ICON_CURVE_DATA, "Curves", "Show/hide Curve data-blocks"},
		{FILTER_ID_GR, "GROUP", ICON_GROUP, "Groups", "Show/hide Group data-blocks"},
		{FILTER_ID_IM, "IMAGE", ICON_IMAGE_DATA, "Images", "Show/hide Image data-blocks"},
		{FILTER_ID_LA, "LAMP", ICON_LAMP_DATA, "Lamps", "Show/hide Lamp data-blocks"},
		{FILTER_ID_MA, "MATERIAL", ICON_MATERIAL_DATA, "Materials", "Show/hide Material data-blocks"},
		{FILTER_ID_ME, "MESH", ICON_MESH_DATA, "Meshes", "Show/hide Mesh data-blocks"},
		{FILTER_ID_OB, "OBJECT", ICON_OBJECT_DATA, "Objects", "Show/hide Object data-blocks"},
		{FILTER_ID_SCE, "SCENE", ICON_SCENE_DATA, "Scenes", "Show/hide Scene data-blocks"},
		{FILTER_ID_TE, "TEXTURE", ICON_TEXTURE_DATA, "Textures", "Show/hide Texture data-blocks"},
		{FILTER_ID_TXT, "TEXT", ICON_TEXT, "Texts", "Show/hide Text data-blocks"},
		{FILTER_ID_VF, "FONT", ICON_FONT_DATA, "Fonts", "Show/hide Font data-blocks"},
		{FILTER_ID_WO, "WORLD", ICON_WORLD_DATA, "Worlds", "Show/hide World data-blocks"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem file_filter_idcategories_items[] = {
	    {FILTER_ID_SCE,
	     "SCENE", ICON_SCENE_DATA, "Scenes", "Show/hide scenes"},
		{FILTER_ID_OB | FILTER_ID_GR,
	     "OBJECT", ICON_GROUP, "Objects & Groups", "Show/hide objects and groups"},
		{FILTER_ID_CU | FILTER_ID_LT | FILTER_ID_ME,
	     "GEOMETRY", ICON_MESH_DATA, "Geometry", "Show/hide meshes, and curves data"},
		{FILTER_ID_MA | FILTER_ID_TE,
	     "SHADING", ICON_MATERIAL_DATA, "Shading",
	     "Show/hide materials, textures"},
		{FILTER_ID_IM,
	     "IMAGE", ICON_IMAGE_DATA, "Images", "Show/hide images"},
		{FILTER_ID_CA | FILTER_ID_LA | FILTER_ID_WO,
	     "ENVIRONMENT", ICON_WORLD_DATA, "Environment", "Show/hide worlds, lamps, cameras"},
		{FILTER_ID_TXT | FILTER_ID_VF | FILTER_ID_CF,
	     "MISC", ICON_GREASEPENCIL, "Miscellaneous", "Show/hide other data types"},
	    {0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "FileSelectParams", NULL);
	RNA_def_struct_ui_text(srna, "File Select Parameters", "File Select Parameters");

	prop = RNA_def_property(srna, "title", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "title");
	RNA_def_property_ui_text(prop, "Title", "Title for the file browser");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_property(srna, "directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "dir");
	RNA_def_property_ui_text(prop, "Directory", "Directory displayed in the file browser");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "filename", PROP_STRING, PROP_FILENAME);
	RNA_def_property_string_sdna(prop, NULL, "file");
	RNA_def_property_ui_text(prop, "File Name", "Active file in the file browser");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_library_browsing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "Library Browser", "Whether we may browse blender files' content or not");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_boolean_funcs(prop, "rna_FileSelectParams_use_lib_get", NULL);

	prop = RNA_def_property(srna, "display_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "display");
	RNA_def_property_enum_items(prop, file_display_items);
	RNA_def_property_ui_text(prop, "Display Mode", "Display mode for the file list");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "recursion_level", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, fileselectparams_recursion_level_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_FileSelectParams_recursion_level_itemf");
	RNA_def_property_ui_text(prop, "Recursion", "Numbers of dirtree levels to show simultaneously");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", FILE_FILTER);
	RNA_def_property_ui_text(prop, "Filter Files", "Enable filtering of files");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "show_hidden", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", FILE_HIDE_DOT);
	RNA_def_property_ui_text(prop, "Show Hidden", "Show hidden dot files");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "sort_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sort");
	RNA_def_property_enum_items(prop, rna_enum_file_sort_items);
	RNA_def_property_ui_text(prop, "Sort", "");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_image", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_IMAGE);
	RNA_def_property_ui_text(prop, "Filter Images", "Show image files");
	RNA_def_property_ui_icon(prop, ICON_FILE_IMAGE, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_blender", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_BLENDER);
	RNA_def_property_ui_text(prop, "Filter Blender", "Show .blend files");
	RNA_def_property_ui_icon(prop, ICON_FILE_BLEND, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_backup", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_BLENDER_BACKUP);
	RNA_def_property_ui_text(prop, "Filter BlenderBackup files", "Show .blend1, .blend2, etc. files");
	RNA_def_property_ui_icon(prop, ICON_FILE_BACKUP, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);


	prop = RNA_def_property(srna, "use_filter_script", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_PYSCRIPT);
	RNA_def_property_ui_text(prop, "Filter Script", "Show script files");
	RNA_def_property_ui_icon(prop, ICON_FILE_SCRIPT, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_font", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_FTFONT);
	RNA_def_property_ui_text(prop, "Filter Fonts", "Show font files");
	RNA_def_property_ui_icon(prop, ICON_FILE_FONT, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);


	prop = RNA_def_property(srna, "use_filter_text", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_TEXT);
	RNA_def_property_ui_text(prop, "Filter Text", "Show text files");
	RNA_def_property_ui_icon(prop, ICON_FILE_TEXT, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_folder", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_FOLDER);
	RNA_def_property_ui_text(prop, "Filter Folder", "Show folders");
	RNA_def_property_ui_icon(prop, ICON_FILE_FOLDER, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "use_filter_blendid", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "filter", FILE_TYPE_BLENDERLIB);
	RNA_def_property_ui_text(prop, "Filter Blender IDs", "Show .blend files items (objects, materials, etc.)");
	RNA_def_property_ui_icon(prop, ICON_BLENDER, 0);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "filter_id", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "filter_id");
	RNA_def_property_enum_items(prop, file_filter_idtypes_items);
	RNA_def_property_flag(prop, PROP_ENUM_FLAG);
	RNA_def_property_ui_text(prop, "Filter ID types", "Which ID types to show/hide, when browsing a library");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "filter_id_category", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "filter_id");
	RNA_def_property_enum_items(prop, file_filter_idcategories_items);
	RNA_def_property_flag(prop, PROP_ENUM_FLAG);
	RNA_def_property_ui_text(prop, "Filter ID categories", "Which ID categories to show/hide, when browsing a library");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, NULL);

	prop = RNA_def_property(srna, "filter_glob", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "filter_glob");
	RNA_def_property_ui_text(prop, "Extension Filter",
	                         "UNIX shell-like filename patterns matching, supports wildcards ('*') "
	                         "and list of patterns separated by ';'");
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_FileSelectPrams_filter_glob_set");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_LIST, NULL);

	prop = RNA_def_property(srna, "filter_search", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "filter_search");
	RNA_def_property_ui_text(prop, "Name Filter", "Filter by name, supports '*' wildcard");
	RNA_def_property_flag(prop, PROP_TEXTEDIT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_LIST, NULL);

	prop = RNA_def_property(srna, "display_size", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "thumbnail_size");
	RNA_def_property_enum_items(prop, display_size_items);
	RNA_def_property_ui_text(prop, "Display Size",
	                         "Change the size of the display (width of columns or thumbnails size)");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_LIST, NULL);
}

static void rna_def_filemenu_entry(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "FileBrowserFSMenuEntry", NULL);
	RNA_def_struct_sdna(srna, "FSMenuEntry");
	RNA_def_struct_ui_text(srna, "File Select Parameters", "File Select Parameters");

	prop = RNA_def_property(srna, "path", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "path");
	RNA_def_property_string_funcs(prop, "rna_FileBrowser_FSMenuEntry_path_get",
	                                    "rna_FileBrowser_FSMenuEntry_path_length",
	                                    "rna_FileBrowser_FSMenuEntry_path_set");
	RNA_def_property_ui_text(prop, "Path", "");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "name");
	RNA_def_property_string_funcs(prop, "rna_FileBrowser_FSMenuEntry_name_get",
	                                    "rna_FileBrowser_FSMenuEntry_name_length",
	                                    "rna_FileBrowser_FSMenuEntry_name_set");
	RNA_def_property_editable_func(prop, "rna_FileBrowser_FSMenuEntry_name_get_editable");
	RNA_def_property_ui_text(prop, "Name", "");
	RNA_def_struct_name_property(srna, prop);

	prop = RNA_def_property(srna, "use_save", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "save", 1);
	RNA_def_property_ui_text(prop, "Save", "Whether this path is saved in bookmarks, or generated from OS");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_property(srna, "is_valid", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "valid", 1);
	RNA_def_property_ui_text(prop, "Valid", "Whether this path is currently reachable");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

static void rna_def_space_filebrowser(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceFileBrowser", "Space");
	RNA_def_struct_sdna(srna, "SpaceFile");
	RNA_def_struct_ui_text(srna, "Space File Browser", "File browser space data");

	prop = RNA_def_property(srna, "params", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "params");
	RNA_def_property_ui_text(prop, "Filebrowser Parameter", "Parameters and Settings for the Filebrowser");

	prop = RNA_def_property(srna, "active_operator", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "op");
	RNA_def_property_ui_text(prop, "Active Operator", "");

	/* keep this for compatibility with existing presets,
	 * not exposed in c++ api because of keyword conflict */
	prop = RNA_def_property(srna, "operator", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "op");
	RNA_def_property_ui_text(prop, "Active Operator", "");

	/* bookmarks, recent files etc. */
	prop = RNA_def_collection(srna, "system_folders", "FileBrowserFSMenuEntry", "System Folders",
	                          "System's folders (usually root, available hard drives, etc)");
	RNA_def_property_collection_funcs(prop, "rna_FileBrowser_FSMenuSystem_data_begin", "rna_FileBrowser_FSMenu_next",
	                                  "rna_FileBrowser_FSMenu_end", "rna_FileBrowser_FSMenu_get",
	                                  "rna_FileBrowser_FSMenuSystem_data_length", NULL, NULL, NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_int(srna, "system_folders_active", -1, -1, INT_MAX, "Active System Folder",
	                   "Index of active system folder (-1 if none)", -1, INT_MAX);
	RNA_def_property_int_sdna(prop, NULL, "systemnr");
	RNA_def_property_int_funcs(prop, "rna_FileBrowser_FSMenuSystem_active_get",
	                           "rna_FileBrowser_FSMenuSystem_active_set", "rna_FileBrowser_FSMenuSystem_active_range");
	RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, "rna_FileBrowser_FSMenu_active_update");

	prop = RNA_def_collection(srna, "system_bookmarks", "FileBrowserFSMenuEntry", "System Bookmarks",
	                          "System's bookmarks");
	RNA_def_property_collection_funcs(prop, "rna_FileBrowser_FSMenuSystemBookmark_data_begin", "rna_FileBrowser_FSMenu_next",
	                                  "rna_FileBrowser_FSMenu_end", "rna_FileBrowser_FSMenu_get",
	                                  "rna_FileBrowser_FSMenuSystemBookmark_data_length", NULL, NULL, NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_int(srna, "system_bookmarks_active", -1, -1, INT_MAX, "Active System Bookmark",
	                   "Index of active system bookmark (-1 if none)", -1, INT_MAX);
	RNA_def_property_int_sdna(prop, NULL, "system_bookmarknr");
	RNA_def_property_int_funcs(prop, "rna_FileBrowser_FSMenuSystemBookmark_active_get",
	                           "rna_FileBrowser_FSMenuSystemBookmark_active_set", "rna_FileBrowser_FSMenuSystemBookmark_active_range");
	RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, "rna_FileBrowser_FSMenu_active_update");

	prop = RNA_def_collection(srna, "bookmarks", "FileBrowserFSMenuEntry", "Bookmarks",
	                          "User's bookmarks");
	RNA_def_property_collection_funcs(prop, "rna_FileBrowser_FSMenuBookmark_data_begin", "rna_FileBrowser_FSMenu_next",
	                                  "rna_FileBrowser_FSMenu_end", "rna_FileBrowser_FSMenu_get",
	                                  "rna_FileBrowser_FSMenuBookmark_data_length", NULL, NULL, NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_int(srna, "bookmarks_active", -1, -1, INT_MAX, "Active Bookmark",
	                   "Index of active bookmark (-1 if none)", -1, INT_MAX);
	RNA_def_property_int_sdna(prop, NULL, "bookmarknr");
	RNA_def_property_int_funcs(prop, "rna_FileBrowser_FSMenuBookmark_active_get",
	                           "rna_FileBrowser_FSMenuBookmark_active_set", "rna_FileBrowser_FSMenuBookmark_active_range");
	RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, "rna_FileBrowser_FSMenu_active_update");

	prop = RNA_def_collection(srna, "recent_folders", "FileBrowserFSMenuEntry", "Recent Folders",
	                          "");
	RNA_def_property_collection_funcs(prop, "rna_FileBrowser_FSMenuRecent_data_begin", "rna_FileBrowser_FSMenu_next",
	                                  "rna_FileBrowser_FSMenu_end", "rna_FileBrowser_FSMenu_get",
	                                  "rna_FileBrowser_FSMenuRecent_data_length", NULL, NULL, NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_int(srna, "recent_folders_active", -1, -1, INT_MAX, "Active Recent Folder",
	                   "Index of active recent folder (-1 if none)", -1, INT_MAX);
	RNA_def_property_int_sdna(prop, NULL, "recentnr");
	RNA_def_property_int_funcs(prop, "rna_FileBrowser_FSMenuRecent_active_get",
	                           "rna_FileBrowser_FSMenuRecent_active_set", "rna_FileBrowser_FSMenuRecent_active_range");
	RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_FILE_PARAMS, "rna_FileBrowser_FSMenu_active_update");
}

static void rna_def_space_info(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceInfo", "Space");
	RNA_def_struct_sdna(srna, "SpaceInfo");
	RNA_def_struct_ui_text(srna, "Space Info", "Info space data");

	/* reporting display */
	prop = RNA_def_property(srna, "show_report_debug", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rpt_mask", INFO_RPT_DEBUG);
	RNA_def_property_ui_text(prop, "Show Debug", "Display debug reporting info");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_INFO_REPORT, NULL);

	prop = RNA_def_property(srna, "show_report_info", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rpt_mask", INFO_RPT_INFO);
	RNA_def_property_ui_text(prop, "Show Info", "Display general information");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_INFO_REPORT, NULL);

	prop = RNA_def_property(srna, "show_report_operator", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rpt_mask", INFO_RPT_OP);
	RNA_def_property_ui_text(prop, "Show Operator", "Display the operator log");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_INFO_REPORT, NULL);

	prop = RNA_def_property(srna, "show_report_warning", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rpt_mask", INFO_RPT_WARN);
	RNA_def_property_ui_text(prop, "Show Warn", "Display warnings");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_INFO_REPORT, NULL);

	prop = RNA_def_property(srna, "show_report_error", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "rpt_mask", INFO_RPT_ERR);
	RNA_def_property_ui_text(prop, "Show Error", "Display error text");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_INFO_REPORT, NULL);
}

static void rna_def_space_userpref(BlenderRNA *brna)
{
	static const EnumPropertyItem filter_type_items[] = {
	    {0,     "NAME",     0,      "Name",        "Filter based on the operator name"},
	    {1,     "KEY",      0,      "Key-Binding", "Filter based on key bindings"},
	    {0, NULL, 0, NULL, NULL}};

	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "SpaceUserPreferences", "Space");
	RNA_def_struct_sdna(srna, "SpaceUserPref");
	RNA_def_struct_ui_text(srna, "Space User Preferences", "User preferences space data");

	prop = RNA_def_property(srna, "filter_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "filter_type");
	RNA_def_property_enum_items(prop, filter_type_items);
	RNA_def_property_ui_text(prop, "Filter Type", "Filter method");
	RNA_def_property_update(prop, NC_SPACE, NULL);

	prop = RNA_def_property(srna, "filter_text", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "filter");
	RNA_def_property_flag(prop, PROP_TEXTEDIT_UPDATE);
	RNA_def_property_ui_text(prop, "Filter", "Search term for filtering in the UI");

}

void RNA_def_space(BlenderRNA *brna)
{
	rna_def_space(brna);
	rna_def_space_image(brna);
	rna_def_space_text(brna);
	rna_def_fileselect_params(brna);
	rna_def_filemenu_entry(brna);
	rna_def_space_filebrowser(brna);
	rna_def_space_outliner(brna);
	rna_def_background_image(brna);
	rna_def_space_view3d(brna);
	rna_def_space_buttons(brna);
	rna_def_space_console(brna);
	rna_def_console_line(brna);
	rna_def_space_info(brna);
	rna_def_space_userpref(brna);
}

#endif
