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

/** \file blender/makesrna/intern/rna_userdef.c
 *  \ingroup RNA
 */

#include <limits.h>
#include <stdlib.h>

#include "DNA_curve_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"
#include "DNA_scene_types.h"

#include "BLI_utildefines.h"
#include "BLI_math_base.h"

#include "BKE_appdir.h"
#include "BKE_DerivedMesh.h"
#include "BKE_addon.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BLT_lang.h"
#include "GPU_buffers.h"

const EnumPropertyItem rna_enum_navigation_mode_items[] = {
	{VIEW_NAVIGATION_WALK, "WALK", 0, "Walk", "Interactively walk or free navigate around the scene"},
	{VIEW_NAVIGATION_FLY, "FLY", 0, "Fly", "Use fly dynamics to navigate the scene"},
	{0, NULL, 0, NULL, NULL}
};

#if defined(WITH_INTERNATIONAL) || !defined(RNA_RUNTIME)
static const EnumPropertyItem rna_enum_language_default_items[] = {
	{0, "DEFAULT", 0, "Default (Default)", ""},
	{0, NULL, 0, NULL, NULL}
};
#endif

#ifdef RNA_RUNTIME

#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_blender.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_idprop.h"
#include "BKE_pbvh.h"

#include "GPU_draw.h"
#include "GPU_select.h"

#include "BLF_api.h"

#include "MEM_guardedalloc.h"
#include "MEM_CacheLimiterC-Api.h"

#include "UI_interface.h"

static void rna_userdef_version_get(PointerRNA *ptr, int *value)
{
	UserDef *userdef = (UserDef *)ptr->data;
	value[0] = userdef->versionfile / 100;
	value[1] = userdef->versionfile % 100;
	value[2] = userdef->subversionfile;
}

static void rna_userdef_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_WINDOW, NULL);
}

/* also used by buffer swap switching */
static void rna_userdef_dpi_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	/* font's are stored at each DPI level, without this we can easy load 100's of fonts */
	BLF_cache_clear();

	/* force setting drawable again */
	wmWindowManager *wm = bmain->wm.first;
	if (wm) {
		wm->windrawable = NULL;
	}

	WM_main_add_notifier(NC_WINDOW, NULL);      /* full redraw */
	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);    /* refresh region sizes */
}

static void rna_userdef_update_ui(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_WINDOW, NULL);
	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);    /* refresh region sizes */
}

static void rna_userdef_language_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	BLF_cache_clear();
	BLT_lang_set(NULL);
	UI_reinit_font();
}

static void rna_userdef_show_manipulator_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	UserDef *userdef = (UserDef *)ptr->data;

	/* lame, loop over all views and set */
	bScreen *sc;
	ScrArea *sa;
	SpaceLink *sl;

	/* from scene copy to the other views */
	for (sc = bmain->screen.first; sc; sc = sc->id.next) {
		for (sa = sc->areabase.first; sa; sa = sa->next) {
			for (sl = sa->spacedata.first; sl; sl = sl->next) {
				if (sl->spacetype == SPACE_VIEW3D) {
					View3D *v3d = (View3D *)sl;
					if (userdef->tw_flag & V3D_USE_MANIPULATOR)
						v3d->twflag |= V3D_USE_MANIPULATOR;
					else
						v3d->twflag &= ~V3D_USE_MANIPULATOR;
				}
			}
		}
	}

	rna_userdef_update(bmain, scene, ptr);
}


static void rna_userdef_script_autoexec_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	UserDef *userdef = (UserDef *)ptr->data;
	if (userdef->flag & USER_SCRIPT_AUTOEXEC_DISABLE) G.f &= ~G_SCRIPT_AUTOEXEC;
	else G.f |=  G_SCRIPT_AUTOEXEC;
}

static void rna_userdef_load_ui_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	UserDef *userdef = (UserDef *)ptr->data;
	if (userdef->flag & USER_FILENOUI) G.fileflags |= G_FILE_NO_UI;
	else G.fileflags &= ~G_FILE_NO_UI;
}

static void rna_userdef_anisotropic_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	GPU_set_anisotropic(bmain, U.anisotropic_filter);
	rna_userdef_update(bmain, scene, ptr);
}

static void rna_userdef_gl_gpu_mipmaps(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	GPU_set_gpu_mipmapping(bmain, U.use_gpu_mipmap);
	rna_userdef_update(bmain, scene, ptr);
}

static void rna_userdef_gl_texture_limit_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	GPU_free_images(bmain);
	rna_userdef_update(bmain, scene, ptr);
}

static void rna_userdef_gl_use_16bit_textures(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	GPU_free_images(bmain);
	rna_userdef_update(bmain, scene, ptr);
}

static void rna_userdef_undo_steps_set(PointerRNA *ptr, int value)
{
	UserDef *userdef = (UserDef *)ptr->data;

	/* Do not allow 1 undo steps, useless and breaks undo/redo process (see T42531). */
	userdef->undosteps = (value == 1) ? 2 : value;
}

static void rna_userdef_select_mouse_set(PointerRNA *ptr, int value)
{
	UserDef *userdef = (UserDef *)ptr->data;

	if (value) {
		userdef->flag |= USER_LMOUSESELECT;
		userdef->flag &= ~USER_TWOBUTTONMOUSE;
	}
	else
		userdef->flag &= ~USER_LMOUSESELECT;
}

#ifdef WITH_INPUT_NDOF
static void rna_userdef_ndof_deadzone_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	UserDef *userdef = ptr->data;
	WM_ndof_deadzone_set(userdef->ndof_deadzone);
}
#endif

static void rna_userdef_timecode_style_set(PointerRNA *ptr, int value)
{
	UserDef *userdef = (UserDef *)ptr->data;
	int required_size = userdef->v2d_min_gridsize;

	/* set the timecode style */
	userdef->timecode_style = value;

	/* adjust the v2d gridsize if needed so that timecodes don't overlap
	 * NOTE: most of these have been hand-picked to avoid overlaps while still keeping
	 * things from getting too blown out
	 */
	switch (value) {
		case USER_TIMECODE_MINIMAL:
		case USER_TIMECODE_SECONDS_ONLY:
			/* 35 is great most of the time, but not that great for full-blown */
			required_size = 35;
			break;
		case USER_TIMECODE_SMPTE_MSF:
			required_size = 50;
			break;
		case USER_TIMECODE_SMPTE_FULL:
			/* the granddaddy! */
			required_size = 65;
			break;
		case USER_TIMECODE_MILLISECONDS:
			required_size = 45;
			break;
	}

	if (U.v2d_min_gridsize < required_size)
		U.v2d_min_gridsize = required_size;
}

static PointerRNA rna_UserDef_view_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_UserPreferencesView, ptr->data);
}

static PointerRNA rna_UserDef_edit_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_UserPreferencesEdit, ptr->data);
}

static PointerRNA rna_UserDef_input_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_UserPreferencesInput, ptr->data);
}

static PointerRNA rna_UserDef_filepaths_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_UserPreferencesFilePaths, ptr->data);
}

static PointerRNA rna_UserDef_system_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_UserPreferencesSystem, ptr->data);
}

static void rna_Userdef_memcache_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	MEM_CacheLimiter_set_maximum(((size_t) U.memcachelimit) * 1024 * 1024);
}

static void rna_UserDef_weight_color_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	bTheme *btheme = UI_GetTheme();
	vDM_ColorBand_store((U.flag & USER_CUSTOM_RANGE) ? (&U.coba_weight) : NULL, btheme->tv3d.vertex_unreferenced);

	rna_userdef_update(bmain, scene, ptr);
}

static void rna_UserDef_viewport_lights_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	/* if all lights are off gpu_draw resets them all, [#27627]
	 * so disallow them all to be disabled */
	if (U.light[0].flag == 0 && U.light[1].flag == 0 && U.light[2].flag == 0) {
		SolidLight *light = ptr->data;
		light->flag |= 1;
	}

	WM_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D | NS_VIEW3D_GPU, NULL);
	rna_userdef_update(bmain, scene, ptr);
}

static void rna_userdef_autosave_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	wmWindowManager *wm = bmain->wm.first;

	if (wm)
		WM_autosave_init(wm);
	rna_userdef_update(bmain, scene, ptr);
}

static bAddon *rna_userdef_addon_new(void)
{
	ListBase *addons_list = &U.addons;
	bAddon *addon = BKE_addon_new();
	BLI_addtail(addons_list, addon);
	return addon;
}

static void rna_userdef_addon_remove(ReportList *reports, PointerRNA *addon_ptr)
{
	ListBase *addons_list = &U.addons;
	bAddon *addon = addon_ptr->data;
	if (BLI_findindex(addons_list, addon) == -1) {
		BKE_report(reports, RPT_ERROR, "Add-on is no longer valid");
		return;
	}
	BLI_remlink(addons_list, addon);
	BKE_addon_free(addon);
	RNA_POINTER_INVALIDATE(addon_ptr);
}

static bPathCompare *rna_userdef_pathcompare_new(void)
{
	bPathCompare *path_cmp = MEM_callocN(sizeof(bPathCompare), "bPathCompare");
	BLI_addtail(&U.autoexec_paths, path_cmp);
	return path_cmp;
}

static void rna_userdef_pathcompare_remove(ReportList *reports, PointerRNA *path_cmp_ptr)
{
	bPathCompare *path_cmp = path_cmp_ptr->data;
	if (BLI_findindex(&U.autoexec_paths, path_cmp) == -1) {
		BKE_report(reports, RPT_ERROR, "Excluded path is no longer valid");
		return;
	}

	BLI_freelinkN(&U.autoexec_paths, path_cmp);
	RNA_POINTER_INVALIDATE(path_cmp_ptr);
}

static void rna_userdef_temp_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	BKE_tempdir_init(U.tempdir);
}

static void rna_userdef_text_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	BLF_cache_clear();
	UI_reinit_font();
	WM_main_add_notifier(NC_WINDOW, NULL);
}

static PointerRNA rna_Theme_space_generic_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_ThemeSpaceGeneric, ptr->data);
}

static PointerRNA rna_Theme_space_gradient_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_ThemeSpaceGradient, ptr->data);
}

static PointerRNA rna_Theme_space_list_generic_get(PointerRNA *ptr)
{
	return rna_pointer_inherit_refine(ptr, &RNA_ThemeSpaceListGeneric, ptr->data);
}


#ifdef WITH_OPENSUBDIV
static const EnumPropertyItem *rna_userdef_opensubdiv_compute_type_itemf(bContext *UNUSED(C), PointerRNA *UNUSED(ptr),
                                                                   PropertyRNA *UNUSED(prop), bool *r_free)
{
	EnumPropertyItem *item = NULL;
	int totitem = 0;
	int evaluators = openSubdiv_getAvailableEvaluators();

	RNA_enum_items_add_value(&item, &totitem, opensubdiv_compute_type_items, USER_OPENSUBDIV_COMPUTE_NONE);

#define APPEND_COMPUTE(compute) \
	if (evaluators & OPENSUBDIV_EVALUATOR_## compute) { \
		RNA_enum_items_add_value(&item, &totitem, opensubdiv_compute_type_items, USER_OPENSUBDIV_COMPUTE_ ## compute); \
	} ((void)0)

	APPEND_COMPUTE(CPU);
	APPEND_COMPUTE(OPENMP);
	APPEND_COMPUTE(OPENCL);
	APPEND_COMPUTE(CUDA);
	APPEND_COMPUTE(GLSL_TRANSFORM_FEEDBACK);
	APPEND_COMPUTE(GLSL_COMPUTE);

#undef APPEND_COMPUTE

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}


#endif


#ifdef WITH_INTERNATIONAL
static const EnumPropertyItem *rna_lang_enum_properties_itemf(bContext *UNUSED(C), PointerRNA *UNUSED(ptr),
                                                        PropertyRNA *UNUSED(prop), bool *UNUSED(r_free))
{
	const EnumPropertyItem *items = BLT_lang_RNA_enum_properties();
	if (items == NULL) {
		items = rna_enum_language_default_items;
	}
	return items;
}
#endif

static IDProperty *rna_AddonPref_idprops(PointerRNA *ptr, bool create)
{
	if (create && !ptr->data) {
		IDPropertyTemplate val = {0};
		ptr->data = IDP_New(IDP_GROUP, &val, "RNA_AddonPreferences group");
	}

	return ptr->data;
}

static PointerRNA rna_Addon_preferences_get(PointerRNA *ptr)
{
	bAddon *addon = (bAddon *)ptr->data;
	bAddonPrefType *apt = BKE_addon_pref_type_find(addon->module, true);
	if (apt) {
		if (addon->prop == NULL) {
			IDPropertyTemplate val = {0};
			addon->prop = IDP_New(IDP_GROUP, &val, addon->module); /* name is unimportant  */
		}
		return rna_pointer_inherit_refine(ptr, apt->ext.srna, addon->prop);
	}
	else {
		return PointerRNA_NULL;
	}
}

static void rna_AddonPref_unregister(Main *UNUSED(bmain), StructRNA *type)
{
	bAddonPrefType *apt = RNA_struct_blender_type_get(type);

	if (!apt)
		return;

	RNA_struct_free_extension(type, &apt->ext);
	RNA_struct_free(&BLENDER_RNA, type);

	BKE_addon_pref_type_remove(apt);

	/* update while blender is running */
	WM_main_add_notifier(NC_WINDOW, NULL);
}

static StructRNA *rna_AddonPref_register(
        Main *bmain, ReportList *reports, void *data, const char *identifier,
        StructValidateFunc validate, StructCallbackFunc call, StructFreeFunc free)
{
	bAddonPrefType *apt, dummyapt = {{'\0'}};
	bAddon dummyaddon = {NULL};
	PointerRNA dummyhtr;
	// int have_function[1];

	/* setup dummy header & header type to store static properties in */
	RNA_pointer_create(NULL, &RNA_AddonPreferences, &dummyaddon, &dummyhtr);

	/* validate the python class */
	if (validate(&dummyhtr, data, NULL /* have_function */ ) != 0)
		return NULL;

	BLI_strncpy(dummyapt.idname, dummyaddon.module, sizeof(dummyapt.idname));
	if (strlen(identifier) >= sizeof(dummyapt.idname)) {
		BKE_reportf(reports, RPT_ERROR, "Registering add-on preferences class: '%s' is too long, maximum length is %d",
		            identifier, (int)sizeof(dummyapt.idname));
		return NULL;
	}

	/* check if we have registered this header type before, and remove it */
	apt = BKE_addon_pref_type_find(dummyaddon.module, true);
	if (apt && apt->ext.srna) {
		rna_AddonPref_unregister(bmain, apt->ext.srna);
	}

	/* create a new header type */
	apt = MEM_mallocN(sizeof(bAddonPrefType), "addonpreftype");
	memcpy(apt, &dummyapt, sizeof(dummyapt));
	BKE_addon_pref_type_add(apt);

	apt->ext.srna = RNA_def_struct_ptr(&BLENDER_RNA, identifier, &RNA_AddonPreferences);
	apt->ext.data = data;
	apt->ext.call = call;
	apt->ext.free = free;
	RNA_struct_blender_type_set(apt->ext.srna, apt);

//	apt->draw = (have_function[0]) ? header_draw : NULL;

	/* update while blender is running */
	WM_main_add_notifier(NC_WINDOW, NULL);

	return apt->ext.srna;
}

/* placeholder, doesn't do anything useful yet */
static StructRNA *rna_AddonPref_refine(PointerRNA *ptr)
{
	return (ptr->type) ? ptr->type : &RNA_AddonPreferences;
}

#else

/* TODO(sergey): This technically belongs to blenlib, but we don't link
 * makesrna against it.
 */

/* Get maximum addressable memory in megabytes, */
static size_t max_memory_in_megabytes(void)
{
	/* Maximum addressable bytes on this platform. */
	const size_t limit_bytes = (((size_t)1) << ((sizeof(size_t) * 8) - 1));
	/* Convert it to megabytes and return. */
	return (limit_bytes >> 20);
}

/* Same as above, but clipped to int capacity. */
static int max_memory_in_megabytes_int(void)
{
	const size_t limit_megabytes = max_memory_in_megabytes();
	/* NOTE: The result will fit into integer. */
	return (int)min_zz(limit_megabytes, (size_t)INT_MAX);
}

static void rna_def_userdef_theme_ui_font_style(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem font_kerning_style[] = {
		{0, "UNFITTED", 0, "Unfitted", "Use scaled but un-grid-fitted kerning distances"},
		{1, "FITTED", 0, "Fitted", "Use scaled and grid-fitted kerning distances"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "ThemeFontStyle", NULL);
	RNA_def_struct_sdna(srna, "uiFontStyle");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Font Style", "Theme settings for Font");

	prop = RNA_def_property(srna, "points", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 6, 48);
	RNA_def_property_ui_text(prop, "Points", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "font_kerning_style", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "kerning");
	RNA_def_property_enum_items(prop, font_kerning_style);
	RNA_def_property_ui_text(prop, "Kerning Style", "Which style to use for font kerning");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadow", PROP_INT, PROP_PIXEL);
	RNA_def_property_range(prop, 0, 5);
	RNA_def_property_ui_text(prop, "Shadow Size", "Shadow size (0, 3 and 5 supported)");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadow_offset_x", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "shadx");
	RNA_def_property_range(prop, -10, 10);
	RNA_def_property_ui_text(prop, "Shadow X Offset", "Shadow offset in pixels");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadow_offset_y", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "shady");
	RNA_def_property_range(prop, -10, 10);
	RNA_def_property_ui_text(prop, "Shadow Y Offset", "Shadow offset in pixels");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadow_alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "shadowalpha");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Shadow Alpha", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadow_value", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "shadowcolor");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Shadow Brightness", "Shadow color in gray value");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_ui_style(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	rna_def_userdef_theme_ui_font_style(brna);

	srna = RNA_def_struct(brna, "ThemeStyle", NULL);
	RNA_def_struct_sdna(srna, "uiStyle");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Style", "Theme settings for style sets");


	prop = RNA_def_property(srna, "panel_title", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "paneltitle");
	RNA_def_property_struct_type(prop, "ThemeFontStyle");
	RNA_def_property_ui_text(prop, "Panel Title Font", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "widget_label", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "widgetlabel");
	RNA_def_property_struct_type(prop, "ThemeFontStyle");
	RNA_def_property_ui_text(prop, "Widget Label Style", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "widget", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "widget");
	RNA_def_property_struct_type(prop, "ThemeFontStyle");
	RNA_def_property_ui_text(prop, "Widget Style", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

}

static void rna_def_userdef_theme_ui_wcol(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeWidgetColors", NULL);
	RNA_def_struct_sdna(srna, "uiWidgetColors");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Widget Color Set", "Theme settings for widget color sets");

	prop = RNA_def_property(srna, "outline", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Outline", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Inner", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Inner Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "item", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Item", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "text_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Text Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_shaded", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "shaded", 1);
	RNA_def_property_ui_text(prop, "Shaded", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadetop", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, -100, 100);
	RNA_def_property_ui_text(prop, "Shade Top", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shadedown", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, -100, 100);
	RNA_def_property_ui_text(prop, "Shade Down", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_ui_wcol_state(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeWidgetStateColors", NULL);
	RNA_def_struct_sdna(srna, "uiWidgetStateColors");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Widget State Color", "Theme settings for widget state colors");

	prop = RNA_def_property(srna, "inner_anim", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Animated", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_anim_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Animated Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_key", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Keyframe", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_key_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Keyframe Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_driven", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Driven", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "inner_driven_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Driven Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "blend", PROP_FLOAT, PROP_FACTOR);
	RNA_def_property_ui_text(prop, "Blend", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_ui_panel(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemePanelColors", NULL);
	RNA_def_struct_sdna(srna, "uiPanelColors");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Panel Color", "Theme settings for panel colors");

	prop = RNA_def_property(srna, "header", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_ui_text(prop, "Header", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "back", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_ui_text(prop, "Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_header", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "Show Header", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_back", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "Show Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_ui_gradient(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeGradientColors", NULL);
	RNA_def_struct_sdna(srna, "uiGradientColors");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Background Color", "Theme settings for background colors and gradient");

	prop = RNA_def_property(srna, "show_grad", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "Use Gradient",
	                         "Do a gradient for the background of the viewport working area");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "gradient", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Gradient Low", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "high_gradient", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Gradient High/Off", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_ui(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	rna_def_userdef_theme_ui_wcol(brna);
	rna_def_userdef_theme_ui_wcol_state(brna);
	rna_def_userdef_theme_ui_panel(brna);
	rna_def_userdef_theme_ui_gradient(brna);

	srna = RNA_def_struct(brna, "ThemeUserInterface", NULL);
	RNA_def_struct_sdna(srna, "ThemeUI");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme User Interface", "Theme settings for user interface elements");

	prop = RNA_def_property(srna, "wcol_regular", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Regular Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_tool", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Tool Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_radio", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Radio Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_text", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Text Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_option", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Option Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_toggle", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Toggle Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_num", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Number Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_numslider", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Slider Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_box", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Box Backdrop Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_menu", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Menu Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_pulldown", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Pulldown Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_menu_back", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Menu Backdrop Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_pie_menu", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Pie Menu Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_tooltip", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Tooltip Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_menu_item", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Menu Item Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_scroll", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Scroll Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_progress", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Progress Bar Widget Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_list_item", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "List Item Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wcol_state", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "State Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "menu_shadow_fac", PROP_FLOAT, PROP_FACTOR);
	RNA_def_property_ui_text(prop, "Menu Shadow Strength", "Blending factor for menu shadows");
	RNA_def_property_range(prop, 0.01f, 1.0f);
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "menu_shadow_width", PROP_INT, PROP_PIXEL);
	RNA_def_property_ui_text(prop, "Menu Shadow Width", "Width of menu shadows, set to zero to disable");
	RNA_def_property_range(prop, 0.0f, 24.0f);
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "icon_file", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "iconfile");
	RNA_def_property_ui_text(prop, "Icon File", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "icon_alpha", PROP_FLOAT, PROP_FACTOR);
	RNA_def_property_ui_text(prop, "Icon Alpha", "Transparency of icons in the interface, to reduce contrast");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "widget_emboss", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "widget_emboss");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Widget Emboss", "Color of the 1px shadow line underlying widgets");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* axis */
	prop = RNA_def_property(srna, "axis_x", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "xaxis");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "X Axis", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "axis_y", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "yaxis");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Y Axis", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "axis_z", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "zaxis");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Z Axis", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_space_common(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "title", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Title", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Text Highlight", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* header */
	prop = RNA_def_property(srna, "header", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Header", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "header_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Header Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "header_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Header Text Highlight", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* panel settings */
	prop = RNA_def_property(srna, "panelcolors", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Panel Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* buttons */
/*	if (! ELEM(spacetype, SPACE_BUTS, SPACE_OUTLINER)) { */
	prop = RNA_def_property(srna, "button", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Region Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "button_title", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Region Text Titles", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "button_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Region Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "button_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Region Text Highlight", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* tabs */
	prop = RNA_def_property(srna, "tab_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Tab Active", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "tab_inactive", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Tab Inactive", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "tab_back", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Tab Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "tab_outline", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Tab Outline", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

/*	} */
}

static void rna_def_userdef_theme_space_gradient(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeSpaceGradient", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_ui_text(srna, "Theme Space Settings", "");

	/* gradient/background settings */
	prop = RNA_def_property(srna, "gradients", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Gradient Colors", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	rna_def_userdef_theme_space_common(srna);
}

static void rna_def_userdef_theme_space_generic(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeSpaceGeneric", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_ui_text(srna, "Theme Space Settings", "");

	prop = RNA_def_property(srna, "back", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Window Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	rna_def_userdef_theme_space_common(srna);
}

/* list / channels */
static void rna_def_userdef_theme_space_list_generic(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ThemeSpaceListGeneric", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_ui_text(srna, "Theme Space List Settings", "");

	prop = RNA_def_property(srna, "list", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Source List", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "list_title", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Source List Title", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "list_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Source List Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "list_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Source List Text Highlight", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_spaces_main(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "space", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "ThemeSpaceGeneric");
	RNA_def_property_pointer_funcs(prop, "rna_Theme_space_generic_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Theme Space", "Settings for space");
}

static void rna_def_userdef_theme_spaces_gradient(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "space", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "ThemeSpaceGradient");
	RNA_def_property_pointer_funcs(prop, "rna_Theme_space_gradient_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Theme Space", "Settings for space");
}

static void rna_def_userdef_theme_spaces_list_main(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "space_list", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "ThemeSpaceListGeneric");
	RNA_def_property_pointer_funcs(prop, "rna_Theme_space_list_generic_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Theme Space List", "Settings for space list");
}

static void rna_def_userdef_theme_spaces_vertex(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "vertex", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vertex", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vertex_select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vertex Select", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vertex_size", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 1, 10);
	RNA_def_property_ui_text(prop, "Vertex Size", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vertex_bevel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vertex Bevel", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vertex_unreferenced", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vertex Group Unreferenced", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_spaces_edge(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "edge_select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Select", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_seam", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Seam", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_sharp", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Sharp", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_crease", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Crease", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_bevel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Bevel", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_facesel", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge UV Face Select", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

}

static void rna_def_userdef_theme_spaces_face(StructRNA *srna)
{
	PropertyRNA *prop;

	prop = RNA_def_property(srna, "face", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Face", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "face_select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Face Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "face_dot", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Face Dot Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "facedot_size", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 1, 10);
	RNA_def_property_ui_text(prop, "Face Dot Size", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

}

static void rna_def_userdef_theme_spaces_curves(StructRNA *srna, bool incl_nurbs, bool incl_lastsel,
                                                bool incl_vector, bool incl_verthandle)
{
	PropertyRNA *prop;

	if (incl_nurbs) {
		prop = RNA_def_property(srna, "nurb_uline", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "nurb_uline");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "NURBS U-lines", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "nurb_vline", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "nurb_vline");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "NURBS V-lines", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "nurb_sel_uline", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "nurb_sel_uline");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "NURBS active U-lines", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "nurb_sel_vline", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "nurb_sel_vline");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "NURBS active V-lines", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "act_spline", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "act_spline");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Active spline", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");
	}

	prop = RNA_def_property(srna, "handle_free", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_free");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Free handle color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "handle_auto", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_auto");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Auto handle color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	if (incl_vector) {
		prop = RNA_def_property(srna, "handle_vect", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "handle_vect");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Vector handle color", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "handle_sel_vect", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "handle_sel_vect");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Vector handle selected color", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");
	}

	prop = RNA_def_property(srna, "handle_align", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_align");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Align handle color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "handle_sel_free", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_sel_free");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Free handle selected color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "handle_sel_auto", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_sel_auto");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Auto handle selected color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "handle_sel_align", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "handle_sel_align");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Align handle selected color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	if (!incl_nurbs) {
		/* assume that when nurbs are off, this is for 2D (i.e. anim) editors */
		prop = RNA_def_property(srna, "handle_auto_clamped", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "handle_auto_clamped");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Auto-Clamped handle color", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "handle_sel_auto_clamped", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "handle_sel_auto_clamped");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Auto-Clamped handle selected color", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");
	}

	if (incl_lastsel) {
		prop = RNA_def_property(srna, "lastsel_point", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_float_sdna(prop, NULL, "lastsel_point");
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Last selected point", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");
	}

	if (incl_verthandle) {
		prop = RNA_def_property(srna, "handle_vertex", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Handle Vertex", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "handle_vertex_select", PROP_FLOAT, PROP_COLOR_GAMMA);
		RNA_def_property_array(prop, 3);
		RNA_def_property_ui_text(prop, "Handle Vertex Select", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");

		prop = RNA_def_property(srna, "handle_vertex_size", PROP_INT, PROP_NONE);
		RNA_def_property_range(prop, 0, 255);
		RNA_def_property_ui_text(prop, "Handle Vertex Size", "");
		RNA_def_property_update(prop, 0, "rna_userdef_update");
	}
}

static void rna_def_userdef_theme_space_view3d(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_view3d */

	srna = RNA_def_struct(brna, "ThemeView3D", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme 3D View", "Theme settings for the 3D View");

	rna_def_userdef_theme_spaces_gradient(srna);

	/* General Viewport options */

	prop = RNA_def_property(srna, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Grid", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "clipping_border_3d", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Clipping Border", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wire", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire_edit", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wire Edit", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");


	/* Object specific options */

	prop = RNA_def_property(srna, "object_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "select");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Object Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "object_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "active");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Active Object", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "object_grouped", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "group");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Object Grouped", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "object_grouped_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "group_active");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Object Grouped Active", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* Object type options */

	prop = RNA_def_property(srna, "camera", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Camera", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "empty", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Empty", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "lamp", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Lamp", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "speaker", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Speaker", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* Mesh Object specific */

	rna_def_userdef_theme_spaces_vertex(srna);
	rna_def_userdef_theme_spaces_edge(srna);
	rna_def_userdef_theme_spaces_face(srna);

	/* Mesh Object specific curves*/

	rna_def_userdef_theme_spaces_curves(srna, true, true, true, false);

	prop = RNA_def_property(srna, "extra_edge_len", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Length Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "extra_edge_angle", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Angle Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "extra_face_angle", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Face Angle Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "extra_face_area", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Face Area Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "editmesh_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Active Vert/Edge/Face", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "normal", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Face Normal", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vertex_normal", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vertex Normal", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "split_normal", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "loop_normal");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Split Normal", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* misc */

	prop = RNA_def_property(srna, "bundle_solid", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "bundle_solid");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Bundle Solid", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "camera_path", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "camera_path");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Camera Path", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "skin_root", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Skin Root", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "view_overlay", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "View Overlay", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "transform", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Transform", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "cframe");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Current Frame", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "outline_width", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 1, 5);
	RNA_def_property_ui_text(prop, "Outline Width", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

}


static void rna_def_userdef_theme_space_file(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_file  */

	srna = RNA_def_struct(brna, "ThemeFileBrowser", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme File Browser", "Theme settings for the File Browser");

	rna_def_userdef_theme_spaces_main(srna);

	prop = RNA_def_property(srna, "selected_file", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "hilite");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected File", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_space_outliner(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_outliner */

	srna = RNA_def_struct(brna, "ThemeOutliner", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Outliner", "Theme settings for the Outliner");

	rna_def_userdef_theme_spaces_main(srna);

	prop = RNA_def_property(srna, "match", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Filter Match", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "selected_highlight", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected Highlight", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_space_userpref(BlenderRNA *brna)
{
	StructRNA *srna;

	/* space_userpref */

	srna = RNA_def_struct(brna, "ThemeUserPreferences", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme User Preferences", "Theme settings for the User Preferences");

	rna_def_userdef_theme_spaces_main(srna);
}

static void rna_def_userdef_theme_space_console(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_console */

	srna = RNA_def_struct(brna, "ThemeConsole", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Console", "Theme settings for the Console");

	rna_def_userdef_theme_spaces_main(srna);

	prop = RNA_def_property(srna, "line_output", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_output");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Line Output", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "line_input", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_input");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Line Input", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "line_info", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_info");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Line Info", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "line_error", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_error");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Line Error", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "cursor", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_cursor");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Cursor", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_select");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Selection", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

}

static void rna_def_userdef_theme_space_info(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_info */

	srna = RNA_def_struct(brna, "ThemeInfo", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Info", "Theme settings for Info");

	rna_def_userdef_theme_spaces_main(srna);

	prop = RNA_def_property(srna, "info_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_selected");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected Line Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_selected_text");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected Line Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_error", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_error");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Error Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_error_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_error_text");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Error Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_warning", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_warning");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Warning Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_warning_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_warning_text");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Warning Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_info", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_info");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Info Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_info_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_info_text");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Info Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_debug", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_debug");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Debug Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "info_debug_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "info_debug_text");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Debug Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}


static void rna_def_userdef_theme_space_text(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_text */

	srna = RNA_def_struct(brna, "ThemeTextEditor", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Text Editor", "Theme settings for the Text Editor");

	rna_def_userdef_theme_spaces_main(srna);

	prop = RNA_def_property(srna, "line_numbers_background", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "grid");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Line Numbers Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* no longer used */
#if 0
	prop = RNA_def_property(srna, "scroll_bar", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "shade1");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Scroll Bar", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
#endif

	prop = RNA_def_property(srna, "selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "shade2");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "cursor", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "hilite");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Cursor", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_builtin", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxb");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Built-in", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_symbols", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxs");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Symbols", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_special", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxv");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Special", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_preprocessor", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxd");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax PreProcessor", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_reserved", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxr");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Reserved", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_comment", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxc");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Comment", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_string", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxl");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax String", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "syntax_numbers", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxn");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Syntax Numbers", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_space_node(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_node */

	srna = RNA_def_struct(brna, "ThemeNodeEditor", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Node Editor", "Theme settings for the Node Editor");

	rna_def_userdef_theme_spaces_main(srna);
	rna_def_userdef_theme_spaces_list_main(srna);

	prop = RNA_def_property(srna, "node_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "select");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Node Selected", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "node_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "active");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Active Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "wire");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wires", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire_inner", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxr");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wire Color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire_select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "edge_select");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wire Select", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "shade2");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Selected Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "node_backdrop", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxl");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Node Backdrop", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "converter_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxv");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Converter Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "color_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxb");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Color Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "group_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxc");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Group Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "group_socket_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "console_output");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Group Socket Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "frame_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "movie");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Frame Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "matte_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxs");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Matte Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "distor_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxd");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Distort Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "noodle_curving", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "noodle_curving");
	RNA_def_property_int_default(prop, 5);
	RNA_def_property_range(prop, 0, 10);
	RNA_def_property_ui_text(prop, "Noodle curving", "Curving of the noodle");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "input_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "syntaxn");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Input Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "output_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_output");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Output Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "filter_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_filter");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Filter Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "vector_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_vector");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Vector Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "texture_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_texture");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Texture Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "shader_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_shader");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Shader Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "script_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_script");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Script Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "pattern_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_pattern");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Pattern Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "layout_node", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "nodeclass_layout");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Layout Node", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_theme_space_buts(BlenderRNA *brna)
{
	StructRNA *srna;
//	PropertyRNA *prop;

	/* space_buts */

	srna = RNA_def_struct(brna, "ThemeProperties", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Properties", "Theme settings for the Properties");

	rna_def_userdef_theme_spaces_main(srna);

}

static void rna_def_userdef_theme_space_image(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* space_image */

	srna = RNA_def_struct(brna, "ThemeImageEditor", NULL);
	RNA_def_struct_sdna(srna, "ThemeSpace");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme Image Editor", "Theme settings for the Image Editor");

	rna_def_userdef_theme_spaces_main(srna);
	rna_def_userdef_theme_spaces_vertex(srna);
	rna_def_userdef_theme_spaces_face(srna);


	prop = RNA_def_property(srna, "editmesh_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Active Vert/Edge/Face", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "wire_edit", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Wire Edit", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "edge_select", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Edge Select", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "scope_back", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_back");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Scope region background color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_face", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_face");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview face color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_edge", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_edge");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview edge color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_vert", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_vert");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview vertex color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_stitchable", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_stitchable");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview stitchable color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_unstitchable", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_unstitchable");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview unstitchable color", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "preview_stitch_active", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "preview_stitch_active");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Stitch preview active island", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "uv_shadow", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "uv_shadow");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Texture paint/Modifier UVs", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "uv_others", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "uv_others");
	RNA_def_property_array(prop, 4);
	RNA_def_property_ui_text(prop, "Other Object UVs", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "cframe");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Current Frame", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "metadatabg", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "metadatabg");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Metadata Background", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "metadatatext", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "metadatatext");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Metadata Text", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	rna_def_userdef_theme_spaces_curves(srna, false, false, false, true);

}

static void rna_def_userdef_themes(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem active_theme_area[] = {
		{0, "USER_INTERFACE", ICON_UI, "User Interface", ""},
		{19, "STYLE", ICON_FONTPREVIEW, "Text Style", ""},
		{1, "VIEW_3D", ICON_VIEW3D, "3D View", ""},
		{6, "IMAGE_EDITOR", ICON_IMAGE_COL, "UV/Image Editor", ""},
		{8, "TEXT_EDITOR", ICON_TEXT, "Text Editor", ""},
		{11, "PROPERTIES", ICON_BUTS, "Properties", ""},
		{12, "OUTLINER", ICON_OOPS, "Outliner", ""},
		{14, "USER_PREFERENCES", ICON_PREFERENCES, "User Preferences", ""},
		{15, "INFO", ICON_INFO, "Info", ""},
		{16, "FILE_BROWSER", ICON_FILESEL, "File Browser", ""},
		{17, "CONSOLE", ICON_CONSOLE, "Python Console", ""},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "Theme", NULL);
	RNA_def_struct_sdna(srna, "bTheme");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Theme", "Theme settings defining draw style and colors in the user interface");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Name", "Name of the theme");
	RNA_def_struct_name_property(srna, prop);
	/* XXX: for now putting this in presets is silly - its just Default */
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);

	prop = RNA_def_property(srna, "theme_area", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "active_theme_area");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	RNA_def_property_enum_items(prop, active_theme_area);
	RNA_def_property_ui_text(prop, "Active Theme Area", "");

	prop = RNA_def_property(srna, "user_interface", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tui");
	RNA_def_property_struct_type(prop, "ThemeUserInterface");
	RNA_def_property_ui_text(prop, "User Interface", "");

	prop = RNA_def_property(srna, "view_3d", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tv3d");
	RNA_def_property_struct_type(prop, "ThemeView3D");
	RNA_def_property_ui_text(prop, "3D View", "");

	prop = RNA_def_property(srna, "file_browser", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tfile");
	RNA_def_property_struct_type(prop, "ThemeFileBrowser");
	RNA_def_property_ui_text(prop, "File Browser", "");

	prop = RNA_def_property(srna, "image_editor", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tima");
	RNA_def_property_struct_type(prop, "ThemeImageEditor");
	RNA_def_property_ui_text(prop, "Image Editor", "");

	prop = RNA_def_property(srna, "properties", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tbuts");
	RNA_def_property_struct_type(prop, "ThemeProperties");
	RNA_def_property_ui_text(prop, "Properties", "");

	prop = RNA_def_property(srna, "text_editor", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "text");
	RNA_def_property_struct_type(prop, "ThemeTextEditor");
	RNA_def_property_ui_text(prop, "Text Editor", "");

	prop = RNA_def_property(srna, "outliner", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "toops");
	RNA_def_property_struct_type(prop, "ThemeOutliner");
	RNA_def_property_ui_text(prop, "Outliner", "");

	prop = RNA_def_property(srna, "info", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tinfo");
	RNA_def_property_struct_type(prop, "ThemeInfo");
	RNA_def_property_ui_text(prop, "Info", "");

	prop = RNA_def_property(srna, "user_preferences", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tuserpref");
	RNA_def_property_struct_type(prop, "ThemeUserPreferences");
	RNA_def_property_ui_text(prop, "User Preferences", "");

	prop = RNA_def_property(srna, "console", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "tconsole");
	RNA_def_property_struct_type(prop, "ThemeConsole");
	RNA_def_property_ui_text(prop, "Console", "");

}

static void rna_def_userdef_addon(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "Addon", NULL);
	RNA_def_struct_sdna(srna, "bAddon");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Add-on", "Python add-ons to be loaded automatically");

	prop = RNA_def_property(srna, "module", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(prop, "Module", "Module name");
	RNA_def_struct_name_property(srna, prop);

	/* Collection active property */
	prop = RNA_def_property(srna, "preferences", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "AddonPreferences");
	RNA_def_property_pointer_funcs(prop, "rna_Addon_preferences_get", NULL, NULL, NULL);
}

static void rna_def_userdef_pathcompare(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "PathCompare", NULL);
	RNA_def_struct_sdna(srna, "bPathCompare");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Path Compare", "Match paths against this value");

	prop = RNA_def_property(srna, "path", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_ui_text(prop, "Path", "");
	RNA_def_struct_name_property(srna, prop);

	prop = RNA_def_property(srna, "use_glob", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_PATHCMP_GLOB);
	RNA_def_property_ui_text(prop, "Use Wildcard", "Enable wildcard globbing");
}

static void rna_def_userdef_addon_pref(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "AddonPreferences", NULL);
	RNA_def_struct_ui_text(srna, "Add-on Preferences", "");
	RNA_def_struct_sdna(srna, "bAddon");  /* WARNING: only a bAddon during registration */

	RNA_def_struct_refine_func(srna, "rna_AddonPref_refine");
	RNA_def_struct_register_funcs(srna, "rna_AddonPref_register", "rna_AddonPref_unregister", NULL);
	RNA_def_struct_idprops_func(srna, "rna_AddonPref_idprops");
	RNA_def_struct_flag(srna, STRUCT_NO_DATABLOCK_IDPROPERTIES);  /* Mandatory! */

	/* registration */
	RNA_define_verify_sdna(0);
	prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "module");
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_define_verify_sdna(1);
}


static void rna_def_userdef_dothemes(BlenderRNA *brna)
{

	rna_def_userdef_theme_ui_style(brna);
	rna_def_userdef_theme_ui(brna);

	rna_def_userdef_theme_space_generic(brna);
	rna_def_userdef_theme_space_gradient(brna);
	rna_def_userdef_theme_space_list_generic(brna);

	rna_def_userdef_theme_space_view3d(brna);
	rna_def_userdef_theme_space_file(brna);
	rna_def_userdef_theme_space_image(brna);
	rna_def_userdef_theme_space_buts(brna);
	rna_def_userdef_theme_space_text(brna);
	rna_def_userdef_theme_space_node(brna);
	rna_def_userdef_theme_space_outliner(brna);
	rna_def_userdef_theme_space_info(brna);
	rna_def_userdef_theme_space_userpref(brna);
	rna_def_userdef_theme_space_console(brna);
	rna_def_userdef_themes(brna);
}

static void rna_def_userdef_solidlight(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	static float default_dir[3] = {0.f, 1.f, 0.f};

	srna = RNA_def_struct(brna, "UserSolidLight", NULL);
	RNA_def_struct_sdna(srna, "SolidLight");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Solid Light", "Light used for OpenGL lighting in solid draw mode");

	prop = RNA_def_property(srna, "use", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", 1);
	RNA_def_property_ui_text(prop, "Enabled", "Enable this OpenGL light in solid draw mode");
	RNA_def_property_update(prop, 0, "rna_UserDef_viewport_lights_update");

	prop = RNA_def_property(srna, "direction", PROP_FLOAT, PROP_DIRECTION);
	RNA_def_property_float_sdna(prop, NULL, "vec");
	RNA_def_property_array(prop, 3);
	RNA_def_property_float_array_default(prop, default_dir);
	RNA_def_property_ui_text(prop, "Direction", "Direction that the OpenGL light is shining");
	RNA_def_property_update(prop, 0, "rna_UserDef_viewport_lights_update");

	prop = RNA_def_property(srna, "diffuse_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "col");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Diffuse Color", "Diffuse color of the OpenGL light");
	RNA_def_property_update(prop, 0, "rna_UserDef_viewport_lights_update");

	prop = RNA_def_property(srna, "specular_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "spec");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Specular Color", "Color of the light's specular highlight");
	RNA_def_property_update(prop, 0, "rna_UserDef_viewport_lights_update");
}

static void rna_def_userdef_walk_navigation(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "WalkNavigation", NULL);
	RNA_def_struct_sdna(srna, "WalkNavigation");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Walk Navigation", "Walk navigation settings");

	prop = RNA_def_property(srna, "mouse_speed", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01f, 10.0f);
	RNA_def_property_ui_text(prop, "Mouse Sensitivity", "Speed factor for when looking around, high values mean faster mouse movement");

	prop = RNA_def_property(srna, "walk_speed", PROP_FLOAT, PROP_VELOCITY);
	RNA_def_property_range(prop, 0.01f, 100.f);
	RNA_def_property_ui_text(prop, "Walk Speed", "Base speed for walking and flying");

	prop = RNA_def_property(srna, "walk_speed_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01f, 10.f);
	RNA_def_property_ui_text(prop, "Speed Factor", "Multiplication factor when using the fast or slow modifiers");

	prop = RNA_def_property(srna, "view_height", PROP_FLOAT, PROP_UNIT_LENGTH);
	RNA_def_property_ui_range(prop, 0.1f, 10.f, 0.1, 2);
	RNA_def_property_range(prop, 0.f, 1000.f);
	RNA_def_property_ui_text(prop, "View Height", "View distance from the floor when walking");

	prop = RNA_def_property(srna, "jump_height", PROP_FLOAT, PROP_UNIT_LENGTH);
	RNA_def_property_ui_range(prop, 0.1f, 10.f, 0.1, 2);
	RNA_def_property_range(prop, 0.1f, 100.f);
	RNA_def_property_ui_text(prop, "Jump Height", "Maximum height of a jump");

	prop = RNA_def_property(srna, "teleport_time", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.f, 10.f);
	RNA_def_property_ui_text(prop, "Teleport Duration", "Interval of time warp when teleporting in navigation mode");

	prop = RNA_def_property(srna, "use_gravity", PROP_BOOLEAN, PROP_BOOLEAN);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_WALK_GRAVITY);
	RNA_def_property_ui_text(prop, "Gravity", "Walk with gravity, or free navigate");

	prop = RNA_def_property(srna, "use_mouse_reverse", PROP_BOOLEAN, PROP_BOOLEAN);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_WALK_MOUSE_REVERSE);
	RNA_def_property_ui_text(prop, "Reverse Mouse", "Reverse the vertical movement of the mouse");
}

static void rna_def_userdef_view(BlenderRNA *brna)
{
	static const EnumPropertyItem timecode_styles[] = {
		{USER_TIMECODE_MINIMAL, "MINIMAL", 0, "Minimal Info",
		                        "Most compact representation, uses '+' as separator for sub-second frame numbers, "
		                        "with left and right truncation of the timecode as necessary"},
		{USER_TIMECODE_SMPTE_FULL, "SMPTE", 0, "SMPTE (Full)", "Full SMPTE timecode (format is HH:MM:SS:FF)"},
		{USER_TIMECODE_SMPTE_MSF, "SMPTE_COMPACT", 0, "SMPTE (Compact)",
		                          "SMPTE timecode showing minutes, seconds, and frames only - "
		                          "hours are also shown if necessary, but not by default"},
		{USER_TIMECODE_MILLISECONDS, "MILLISECONDS", 0, "Compact with Milliseconds",
		                             "Similar to SMPTE (Compact), except that instead of frames, "
		                             "milliseconds are shown instead"},
		{USER_TIMECODE_SECONDS_ONLY, "SECONDS_ONLY", 0, "Only Seconds",
		                             "Direct conversion of frame numbers to seconds"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem line_width[] = {
		{-1, "THIN", 0, "Thin", "Thinner lines than the default"},
		{ 0, "AUTO", 0, "Auto", "Automatic line width based on UI scale"},
		{ 1, "THICK", 0, "Thick", "Thicker lines than the default"},
		{0, NULL, 0, NULL, NULL}
	};

	PropertyRNA *prop;
	StructRNA *srna;

	srna = RNA_def_struct(brna, "UserPreferencesView", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_nested(brna, srna, "UserPreferences");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "View & Controls", "Preferences related to viewing data");

	/* View  */
	prop = RNA_def_property(srna, "ui_scale", PROP_FLOAT, PROP_FACTOR);
	RNA_def_property_ui_text(prop, "UI Scale", "Changes the size of the fonts and buttons in the interface");
	RNA_def_property_range(prop, 0.25f, 4.0f);
	RNA_def_property_ui_range(prop, 0.5f, 2.0f, 1, 2);
	RNA_def_property_float_default(prop, 1.0f);
	RNA_def_property_update(prop, 0, "rna_userdef_dpi_update");

	prop = RNA_def_property(srna, "ui_line_width", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, line_width);
	RNA_def_property_ui_text(prop, "UI Line Width",
	                         "Changes the thickness of lines and points in the interface");
	RNA_def_property_update(prop, 0, "rna_userdef_dpi_update");

	/* display */
	prop = RNA_def_property(srna, "show_tooltips", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_TOOLTIPS);
	RNA_def_property_ui_text(prop, "Tooltips", "Display tooltips (when off hold Alt to force display)");

	prop = RNA_def_property(srna, "show_tooltips_python", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", USER_TOOLTIPS_PYTHON);
	RNA_def_property_ui_text(prop, "Python Tooltips", "Show Python references in tooltips");

	prop = RNA_def_property(srna, "show_developer_ui", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_DEVELOPER_UI);
	RNA_def_property_ui_text(
	        prop, "Developer Extras",
	        "Show options for developers (edit source in context menu, geometry indices)");

	prop = RNA_def_property(srna, "show_object_info", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_DRAWVIEWINFO);
	RNA_def_property_ui_text(prop, "Display Object Info", "Display objects name and frame number in 3D view");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "use_global_scene", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_SCENEGLOBAL);
	RNA_def_property_ui_text(prop, "Global Scene", "Force the current Scene to be displayed in all Screens");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_large_cursors", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "curssize", 0);
	RNA_def_property_ui_text(prop, "Large Cursors", "Use large mouse cursors when available");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_view_name", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_SHOW_VIEWPORTNAME);
	RNA_def_property_ui_text(prop, "Show View Name", "Show the name of the view's direction in each 3D View");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "show_splash", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "uiflag", USER_SPLASH_DISABLE);
	RNA_def_property_ui_text(prop, "Show Splash", "Display splash screen on startup");


	prop = RNA_def_property(srna, "show_playback_fps", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_SHOW_FPS);
	RNA_def_property_ui_text(prop, "Show Playback FPS",
	                         "Show the frames per second screen refresh rate, while animation is played back");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* app flags (use for app-templates) */
	prop = RNA_def_property(srna, "show_layout_ui", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "app_flag", USER_APP_LOCK_UI_LAYOUT);
	RNA_def_property_ui_text(prop, "Show Layout Widgets", "Show screen layout editing UI");
	RNA_def_property_update(prop, 0, "rna_userdef_update_ui");

	prop = RNA_def_property(srna, "show_view3d_cursor", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "app_flag", USER_APP_VIEW3D_HIDE_CURSOR);
	RNA_def_property_ui_text(prop, "Show 3D View Cursor", "");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* menus */
	prop = RNA_def_property(srna, "use_mouse_over_open", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_MENUOPENAUTO);
	RNA_def_property_ui_text(prop, "Open On Mouse Over",
	                         "Open menu buttons and pulldowns automatically when the mouse is hovering");

	prop = RNA_def_property(srna, "open_toplevel_delay", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "menuthreshold1");
	RNA_def_property_range(prop, 1, 40);
	RNA_def_property_ui_text(prop, "Top Level Menu Open Delay",
	                         "Time delay in 1/10 seconds before automatically opening top level menus");

	prop = RNA_def_property(srna, "open_sublevel_delay", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "menuthreshold2");
	RNA_def_property_range(prop, 1, 40);
	RNA_def_property_ui_text(prop, "Sub Level Menu Open Delay",
	                         "Time delay in 1/10 seconds before automatically opening sub level menus");

	/* pie menus */
	prop = RNA_def_property(srna, "pie_initial_timeout", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Recenter Timeout",
	                         "Pie menus will use the initial mouse position as center for this amount of time "
	                         "(in 1/100ths of sec)");

	prop = RNA_def_property(srna, "pie_animation_timeout", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Animation Timeout",
	                         "Time needed to fully animate the pie to unfolded state (in 1/100ths of sec)");

	prop = RNA_def_property(srna, "pie_menu_radius", PROP_INT, PROP_PIXEL);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Radius", "Pie menu size in pixels");

	prop = RNA_def_property(srna, "pie_menu_threshold", PROP_INT, PROP_PIXEL);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Threshold", "Distance from center needed before a selection can be made");

	prop = RNA_def_property(srna, "pie_menu_confirm", PROP_INT, PROP_PIXEL);
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Confirm Threshold",
	                         "Distance threshold after which selection is made (zero to disable)");

	prop = RNA_def_property(srna, "use_quit_dialog", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_QUIT_PROMPT);
	RNA_def_property_ui_text(prop, "Prompt Quit",
	                         "Ask for confirmation when quitting through the window close button");

	/* Toolbox click-hold delay */
	prop = RNA_def_property(srna, "open_left_mouse_delay", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "tb_leftmouse");
	RNA_def_property_range(prop, 1, 40);
	RNA_def_property_ui_text(prop, "Hold LMB Open Toolbox Delay",
	                         "Time in 1/10 seconds to hold the Left Mouse Button before opening the toolbox");

	prop = RNA_def_property(srna, "open_right_mouse_delay", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "tb_rightmouse");
	RNA_def_property_range(prop, 1, 40);
	RNA_def_property_ui_text(prop, "Hold RMB Open Toolbox Delay",
	                         "Time in 1/10 seconds to hold the Right Mouse Button before opening the toolbox");

	prop = RNA_def_property(srna, "show_column_layout", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_PLAINMENUS);
	RNA_def_property_ui_text(prop, "Toolbox Column Layout", "Use a column layout for toolbox");

	prop = RNA_def_property(srna, "use_directional_menus", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "uiflag", USER_MENUFIXEDORDER);
	RNA_def_property_ui_text(prop, "Contents Follow Opening Direction",
	                         "Otherwise menus, etc will always be top to bottom, left to right, "
	                         "no matter opening direction");

	prop = RNA_def_property(srna, "use_global_pivot", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_LOCKAROUND);
	RNA_def_property_ui_text(prop, "Global Pivot", "Lock the same rotation/scaling pivot in all 3D Views");

	prop = RNA_def_property(srna, "use_mouse_depth_navigate", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_DEPTH_NAVIGATE);
	RNA_def_property_ui_text(prop, "Auto Depth",
	                         "Use the depth under the mouse to improve view pan/rotate/zoom functionality");

	prop = RNA_def_property(srna, "use_mouse_depth_cursor", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_DEPTH_CURSOR);
	RNA_def_property_ui_text(prop, "Cursor Depth",
	                         "Use the depth under the mouse when placing the cursor");

	prop = RNA_def_property(srna, "use_cursor_lock_adjust", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_LOCK_CURSOR_ADJUST);
	RNA_def_property_ui_text(prop, "Cursor Lock Adjust",
	                         "Place the cursor without 'jumping' to the new location (when lock-to-cursor is used)");

	prop = RNA_def_property(srna, "use_camera_lock_parent", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "uiflag", USER_CAM_LOCK_NO_PARENT);
	RNA_def_property_ui_text(prop, "Camera Parent Lock",
	                         "When the camera is locked to the view and in fly mode, "
	                         "transform the parent rather than the camera");

	/* view zoom */
	prop = RNA_def_property(srna, "use_zoom_to_mouse", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_ZOOM_TO_MOUSEPOS);
	RNA_def_property_ui_text(prop, "Zoom To Mouse Position",
	                         "Zoom in towards the mouse pointer's position in the 3D view, "
	                         "rather than the 2D window center");

	/* view rotation */
	prop = RNA_def_property(srna, "use_auto_perspective", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_AUTOPERSP);
	RNA_def_property_ui_text(prop, "Auto Perspective",
	                         "Automatically switch between orthographic and perspective when changing "
	                         "from top/front/side views");

	prop = RNA_def_property(srna, "use_rotate_around_active", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_ORBIT_SELECTION);
	RNA_def_property_ui_text(prop, "Rotate Around Selection", "Use selection as the pivot point");

	/* mini axis */
	prop = RNA_def_property(srna, "show_mini_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_SHOW_ROTVIEWICON);
	RNA_def_property_ui_text(prop, "Show Mini Axes",
	                         "Show a small rotating 3D axes in the bottom left corner of the 3D View");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "mini_axis_size", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "rvisize");
	RNA_def_property_range(prop, 10, 64);
	RNA_def_property_ui_text(prop, "Mini Axes Size", "The axes icon's size");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "mini_axis_brightness", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "rvibright");
	RNA_def_property_range(prop, 0, 10);
	RNA_def_property_ui_text(prop, "Mini Axes Brightness", "Brightness of the icon");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "smooth_view", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "smooth_viewtx");
	RNA_def_property_range(prop, 0, 1000);
	RNA_def_property_ui_text(prop, "Smooth View", "Time to animate the view in milliseconds, zero to disable");

	prop = RNA_def_property(srna, "rotation_angle", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "pad_rot_angle");
	RNA_def_property_range(prop, 0, 90);
	RNA_def_property_ui_text(prop, "Rotation Angle", "Rotation step for numerical pad keys (2 4 6 8)");

	/* 3D transform widget */
	prop = RNA_def_property(srna, "show_manipulator", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "tw_flag", V3D_USE_MANIPULATOR);
	RNA_def_property_ui_text(prop, "Manipulator", "Use 3D transform manipulator");
	RNA_def_property_update(prop, 0, "rna_userdef_show_manipulator_update");

	prop = RNA_def_property(srna, "manipulator_size", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "tw_size");
	RNA_def_property_range(prop, 10, 200);
	RNA_def_property_int_default(prop, 75);
	RNA_def_property_ui_text(prop, "Manipulator Size", "Diameter of the manipulator");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "manipulator_handle_size", PROP_INT, PROP_PERCENTAGE);
	RNA_def_property_int_sdna(prop, NULL, "tw_handlesize");
	RNA_def_property_range(prop, 2, 40);
	RNA_def_property_int_default(prop, 25);
	RNA_def_property_ui_text(prop, "Manipulator Handle Size", "Size of manipulator handles as percentage of the radius");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "manipulator_hotspot", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "tw_hotspot");
	RNA_def_property_range(prop, 4, 40);
	RNA_def_property_int_default(prop, 14);
	RNA_def_property_ui_text(prop, "Manipulator Hotspot", "Distance around the handles to accept mouse clicks");

	prop = RNA_def_property(srna, "object_origin_size", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "obcenter_dia");
	RNA_def_property_range(prop, 4, 10);
	RNA_def_property_ui_text(prop, "Object Origin Size", "Diameter in Pixels for Object/Lamp origin display");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* View2D Grid Displays */
	prop = RNA_def_property(srna, "view2d_grid_spacing_min", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "v2d_min_gridsize");
	RNA_def_property_range(prop, 1, 500); /* XXX: perhaps the lower range should only go down to 5? */
	RNA_def_property_ui_text(prop, "2D View Minimum Grid Spacing",
	                         "Minimum number of pixels between each gridline in 2D Viewports");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* TODO: add a setter for this, so that we can bump up the minimum size as necessary... */
	prop = RNA_def_property(srna, "timecode_style", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, timecode_styles);
	RNA_def_property_enum_sdna(prop, NULL, "timecode_style");
	RNA_def_property_enum_funcs(prop, NULL, "rna_userdef_timecode_style_set", NULL);
	RNA_def_property_ui_text(prop, "TimeCode Style",
	                         "Format of Time Codes displayed when not displaying timing in terms of frames");
	RNA_def_property_update(prop, 0, "rna_userdef_update");


}

static void rna_def_userdef_edit(BlenderRNA *brna)
{
	PropertyRNA *prop;
	StructRNA *srna;

	static const EnumPropertyItem material_link_items[] = {
		{0, "OBDATA", 0, "ObData", "Toggle whether the material is linked to object data or the object block"},
		{USER_MAT_ON_OB, "OBJECT", 0, "Object",
		                 "Toggle whether the material is linked to object data or the object block"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem object_align_items[] = {
		{0, "WORLD", 0, "World", "Align newly added objects to the world coordinate system"},
		{USER_ADD_VIEWALIGNED, "VIEW", 0, "View", "Align newly added objects facing the active 3D View direction"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "UserPreferencesEdit", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_nested(brna, srna, "UserPreferences");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Edit Methods", "Settings for interacting with Blender data");

	/* Edit Methods */

	prop = RNA_def_property(srna, "material_link", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, material_link_items);
	RNA_def_property_ui_text(prop, "Material Link To",
	                         "Toggle whether the material is linked to object data or the object block");

	prop = RNA_def_property(srna, "object_align", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, object_align_items);
	RNA_def_property_ui_text(prop, "Align Object To",
	                         "When adding objects from a 3D View menu, either align them with that view or "
	                         "with the world");

	prop = RNA_def_property(srna, "use_enter_edit_mode", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_ADD_EDITMODE);
	RNA_def_property_ui_text(prop, "Enter Edit Mode", "Enter Edit Mode automatically after adding a new object");

	prop = RNA_def_property(srna, "use_drag_immediately", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_RELEASECONFIRM);
	RNA_def_property_ui_text(prop, "Release confirms",
	                         "Moving things with a mouse drag confirms when releasing the button");

	/* Undo */
	prop = RNA_def_property(srna, "undo_steps", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "undosteps");
	RNA_def_property_range(prop, 0, 256);
	RNA_def_property_int_funcs(prop, NULL, "rna_userdef_undo_steps_set", NULL);
	RNA_def_property_ui_text(prop, "Undo Steps", "Number of undo steps available (smaller values conserve memory)");

	prop = RNA_def_property(srna, "undo_memory_limit", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "undomemory");
	RNA_def_property_range(prop, 0, max_memory_in_megabytes_int());
	RNA_def_property_ui_text(prop, "Undo Memory Size", "Maximum memory usage in megabytes (0 means unlimited)");

	prop = RNA_def_property(srna, "use_global_undo", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_GLOBALUNDO);
	RNA_def_property_ui_text(prop, "Global Undo",
	                         "Global undo works by keeping a full copy of the file itself in memory, "
	                         "so takes extra memory");

	/* duplication linking */
	prop = RNA_def_property(srna, "use_duplicate_mesh", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_MESH);
	RNA_def_property_ui_text(prop, "Duplicate Mesh", "Causes mesh data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_surface", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_SURF);
	RNA_def_property_ui_text(prop, "Duplicate Surface", "Causes surface data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_curve", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_CURVE);
	RNA_def_property_ui_text(prop, "Duplicate Curve", "Causes curve data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_text", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_FONT);
	RNA_def_property_ui_text(prop, "Duplicate Text", "Causes text data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_lamp", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_LAMP);
	RNA_def_property_ui_text(prop, "Duplicate Lamp", "Causes lamp data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_material", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_MAT);
	RNA_def_property_ui_text(prop, "Duplicate Material", "Causes material data to be duplicated with the object");

	prop = RNA_def_property(srna, "use_duplicate_texture", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "dupflag", USER_DUP_TEX);
	RNA_def_property_ui_text(prop, "Duplicate Texture", "Causes texture data to be duplicated with the object");

	/* xxx */
	/* currently only used for insert offset (aka auto-offset), maybe also be useful for later stuff though */
	prop = RNA_def_property(srna, "node_margin", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "node_margin");
	RNA_def_property_ui_text(prop, "Auto-offset Margin", "Minimum distance between nodes for Auto-offsetting nodes");
	RNA_def_property_update(prop, 0, "rna_userdef_update");
}

static void rna_def_userdef_system(BlenderRNA *brna)
{
	PropertyRNA *prop;
	StructRNA *srna;

	static const EnumPropertyItem gl_texture_clamp_items[] = {
		{0, "CLAMP_OFF", 0, "Off", ""},
		{8192, "CLAMP_8192", 0, "8192", ""},
		{4096, "CLAMP_4096", 0, "4096", ""},
		{2048, "CLAMP_2048", 0, "2048", ""},
		{1024, "CLAMP_1024", 0, "1024", ""},
		{512, "CLAMP_512", 0, "512", ""},
		{256, "CLAMP_256", 0, "256", ""},
		{128, "CLAMP_128", 0, "128", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem anisotropic_items[] = {
		{1, "FILTER_0", 0, "Off", ""},
		{2, "FILTER_2", 0, "2x", ""},
		{4, "FILTER_4", 0, "4x", ""},
		{8, "FILTER_8", 0, "8x", ""},
		{16, "FILTER_16", 0, "16x", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem draw_method_items[] = {
		{USER_DRAW_AUTOMATIC, "AUTOMATIC", 0, "Automatic", "Automatically set based on graphics card and driver"},
		{USER_DRAW_TRIPLE, "TRIPLE_BUFFER", 0, "Triple Buffer",
		                   "Use a third buffer for minimal redraws at the cost of more memory"},
		{USER_DRAW_OVERLAP, "OVERLAP", 0, "Overlap",
		                    "Redraw all overlapping regions, minimal memory usage but more redraws"},
		{USER_DRAW_OVERLAP_FLIP, "OVERLAP_FLIP", 0, "Overlap Flip",
		                         "Redraw all overlapping regions, minimal memory usage but more redraws "
		                         "(for graphics drivers that do flipping)"},
		{USER_DRAW_FULL, "FULL", 0, "Full",
		                 "Do a full redraw each time, slow, only use for reference or when everything else fails"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem color_picker_types[] = {
		{USER_CP_CIRCLE_HSV, "CIRCLE_HSV", 0, "Circle (HSV)", "A circular Hue/Saturation color wheel, with Value slider"},
		{USER_CP_CIRCLE_HSL, "CIRCLE_HSL", 0, "Circle (HSL)", "A circular Hue/Saturation color wheel, with Lightness slider"},
		{USER_CP_SQUARE_SV, "SQUARE_SV", 0, "Square (SV + H)", "A square showing Saturation/Value, with Hue slider"},
		{USER_CP_SQUARE_HS, "SQUARE_HS", 0, "Square (HS + V)", "A square showing Hue/Saturation, with Value slider"},
		{USER_CP_SQUARE_HV, "SQUARE_HV", 0, "Square (HV + S)", "A square showing Hue/Value, with Saturation slider"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem multi_sample_levels[] = {
		{USER_MULTISAMPLE_NONE, "NONE", 0, "No MultiSample", "Do not use OpenGL MultiSample"},
		{USER_MULTISAMPLE_2, "2", 0, "MultiSample: 2", "Use 2x OpenGL MultiSample (requires restart)"},
		{USER_MULTISAMPLE_4, "4", 0, "MultiSample: 4", "Use 4x OpenGL MultiSample (requires restart)"},
		{USER_MULTISAMPLE_8, "8", 0, "MultiSample: 8", "Use 8x OpenGL MultiSample (requires restart)"},
		{USER_MULTISAMPLE_16, "16", 0, "MultiSample: 16", "Use 16x OpenGL MultiSample (requires restart)"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem image_draw_methods[] = {
		{IMAGE_DRAW_METHOD_2DTEXTURE, "2DTEXTURE", 0, "2D Texture", "Use CPU for display transform and draw image with 2D texture"},
		{IMAGE_DRAW_METHOD_GLSL, "GLSL", 0, "GLSL", "Use GLSL shaders for display transform and draw image with 2D texture"},
		{IMAGE_DRAW_METHOD_DRAWPIXELS, "DRAWPIXELS", 0, "DrawPixels", "Use CPU for display transform and draw image using DrawPixels"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem gpu_select_method_items[] = {
	    {USER_SELECT_AUTO, "AUTO", 0, "Automatic", ""},
	    {USER_SELECT_USE_SELECT_RENDERMODE, "GL_SELECT", 0, "OpenGL Select", ""},
	    {USER_SELECT_USE_OCCLUSION_QUERY, "GL_QUERY", 0, "OpenGL Occlusion Queries", ""},
	    {0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem text_hinting_items[] = {
	    {0, "AUTO", 0, "Auto", ""},
	    {USER_TEXT_HINTING_NONE, "NONE", 0, "None", ""},
	    {USER_TEXT_HINTING_SLIGHT, "SLIGHT", 0, "Slight", ""},
	    {USER_TEXT_HINTING_FULL, "FULL", 0, "Full", ""},
	    {0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "UserPreferencesSystem", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_nested(brna, srna, "UserPreferences");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "System & OpenGL", "Graphics driver and operating system settings");

	/* Language */

	prop = RNA_def_property(srna, "use_international_fonts", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "transopts", USER_DOTRANSLATE);
	RNA_def_property_ui_text(prop, "International Fonts", "Use international fonts");
	RNA_def_property_update(prop, NC_WINDOW, "rna_userdef_language_update");

	prop = RNA_def_property(srna, "dpi", PROP_INT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "DPI",
	                         "DPI for add-ons to use when drawing custom user interface elements, controlled by "
	                         "operating system settings and Blender UI scale, with a reference value of 72 DPI "
	                         "(note that since this value includes a user defined scale, it is not always the "
	                         "actual monitor DPI)");

	prop = RNA_def_property(srna, "pixel_size", PROP_FLOAT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_float_sdna(prop, NULL, "pixelsize");
	RNA_def_property_ui_text(prop, "Pixel Size",
	                         "Suggested line thickness and point size in pixels, for add-ons drawing custom user "
	                         "interface elements, controlled by operating system settings and Blender UI scale");

	prop = RNA_def_property(srna, "font_path_ui", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "font_path_ui");
	RNA_def_property_ui_text(prop, "Interface Font", "Path to interface font");
	RNA_def_property_update(prop, NC_WINDOW, "rna_userdef_language_update");

	prop = RNA_def_property(srna, "font_path_ui_mono", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "font_path_ui_mono");
	RNA_def_property_ui_text(prop, "Mono-space Font", "Path to interface mono-space Font");
	RNA_def_property_update(prop, NC_WINDOW, "rna_userdef_language_update");

	prop = RNA_def_property(srna, "scrollback", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "scrollback");
	RNA_def_property_range(prop, 32, 32768);
	RNA_def_property_ui_text(prop, "Scrollback", "Maximum number of lines to store for the console buffer");

	prop = RNA_def_property(srna, "author", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "author");
	RNA_def_property_string_maxlength(prop, 80);
	RNA_def_property_ui_text(prop, "Author",
	                         "Name that will be used in exported files when format supports such feature");

	/* Language Selection */

	prop = RNA_def_property(srna, "language", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, rna_enum_language_default_items);
#ifdef WITH_INTERNATIONAL
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_lang_enum_properties_itemf");
#endif
	RNA_def_property_ui_text(prop, "Language", "Language used for translation");
	RNA_def_property_update(prop, NC_WINDOW, "rna_userdef_language_update");

	prop = RNA_def_property(srna, "use_translate_tooltips", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "transopts", USER_TR_TOOLTIPS);
	RNA_def_property_ui_text(prop, "Translate Tooltips", "Translate tooltips");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "use_translate_interface", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "transopts", USER_TR_IFACE);
	RNA_def_property_ui_text(prop, "Translate Interface", "Translate interface");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "use_translate_new_dataname", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "transopts", USER_TR_NEWDATANAME);
	RNA_def_property_ui_text(prop, "Translate New Names", "Translate new data names (when adding/creating some)");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* System & OpenGL */

	prop = RNA_def_property(srna, "solid_lights", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "light", "");
	RNA_def_property_struct_type(prop, "UserSolidLight");
	RNA_def_property_ui_text(prop, "Solid Lights", "Lights user to display objects in solid draw mode");

	prop = RNA_def_property(srna, "use_weight_color_range", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_CUSTOM_RANGE);
	RNA_def_property_ui_text(prop, "Use Weight Color Range",
	                         "Enable color range used for weight visualization in weight painting mode");
	RNA_def_property_update(prop, 0, "rna_UserDef_weight_color_update");

	prop = RNA_def_property(srna, "weight_color_range", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "coba_weight");
	RNA_def_property_struct_type(prop, "ColorRamp");
	RNA_def_property_ui_text(prop, "Weight Color Range",
	                         "Color range used for weight visualization in weight painting mode");
	RNA_def_property_update(prop, 0, "rna_UserDef_weight_color_update");

	prop = RNA_def_property(srna, "color_picker_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, color_picker_types);
	RNA_def_property_enum_sdna(prop, NULL, "color_picker_type");
	RNA_def_property_ui_text(prop, "Color Picker Type", "Different styles of displaying the color picker widget");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "use_preview_images", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_ALLWINCODECS);
	RNA_def_property_ui_text(prop, "Enable All Codecs",
	                         "Allow user to choose any codec (Windows only, might generate instability)");

	prop = RNA_def_property(srna, "use_scripts_auto_execute", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", USER_SCRIPT_AUTOEXEC_DISABLE);
	RNA_def_property_ui_text(prop, "Auto Run Python Scripts",
	                         "Allow any .blend file to run scripts automatically "
	                         "(unsafe with blend files from an untrusted source)");
	RNA_def_property_update(prop, 0, "rna_userdef_script_autoexec_update");

	prop = RNA_def_property(srna, "use_tabs_as_spaces", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", USER_TXT_TABSTOSPACES_DISABLE);
	RNA_def_property_ui_text(prop, "Tabs as Spaces",
	                         "Automatically convert all new tabs into spaces for new and loaded text files");

	prop = RNA_def_property(srna, "prefetch_frames", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "prefetchframes");
	RNA_def_property_range(prop, 0, INT_MAX);
	RNA_def_property_ui_range(prop, 0, 500, 1, -1);
	RNA_def_property_ui_text(prop, "Prefetch Frames", "Number of frames to render ahead during playback (sequencer only)");

	prop = RNA_def_property(srna, "memory_cache_limit", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "memcachelimit");
	RNA_def_property_range(prop, 0, max_memory_in_megabytes_int());
	RNA_def_property_ui_text(prop, "Memory Cache Limit", "Memory cache limit (in megabytes)");
	RNA_def_property_update(prop, 0, "rna_Userdef_memcache_update");

	prop = RNA_def_property(srna, "gl_clip_alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "glalphaclip");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Clip Alpha", "Clip alpha below this threshold in the 3D textured view");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "use_16bit_textures", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "use_16bit_textures", 1);
	RNA_def_property_ui_text(prop, "16 Bit Float Textures", "Use 16 bit per component texture for float images");
	RNA_def_property_update(prop, 0, "rna_userdef_gl_use_16bit_textures");

	prop = RNA_def_property(srna, "use_gpu_mipmap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "use_gpu_mipmap", 1);
	RNA_def_property_ui_text(prop, "GPU Mipmap Generation", "Generate Image Mipmaps on the GPU");
	RNA_def_property_update(prop, 0, "rna_userdef_gl_gpu_mipmaps");

	prop = RNA_def_property(srna, "image_draw_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, image_draw_methods);
	RNA_def_property_enum_sdna(prop, NULL, "image_draw_method");
	RNA_def_property_ui_text(prop, "Image Draw Method", "Method used for displaying images on the screen");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	prop = RNA_def_property(srna, "anisotropic_filter", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "anisotropic_filter");
	RNA_def_property_enum_items(prop, anisotropic_items);
	RNA_def_property_enum_default(prop, 1);
	RNA_def_property_ui_text(prop, "Anisotropic Filter",
	                         "Quality of the anisotropic filtering (values greater than 1.0 enable anisotropic "
	                         "filtering)");
	RNA_def_property_update(prop, 0, "rna_userdef_anisotropic_update");

	prop = RNA_def_property(srna, "gl_texture_limit", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "glreslimit");
	RNA_def_property_enum_items(prop, gl_texture_clamp_items);
	RNA_def_property_ui_text(prop, "GL Texture Limit", "Limit the texture size to save graphics memory");
	RNA_def_property_update(prop, 0, "rna_userdef_gl_texture_limit_update");

	prop = RNA_def_property(srna, "texture_time_out", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "textimeout");
	RNA_def_property_range(prop, 0, 3600);
	RNA_def_property_ui_text(prop, "Texture Time Out",
	                         "Time since last access of a GL texture in seconds after which it is freed "
	                         "(set to 0 to keep textures allocated)");

	prop = RNA_def_property(srna, "texture_collection_rate", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "texcollectrate");
	RNA_def_property_range(prop, 1, 3600);
	RNA_def_property_ui_text(prop, "Texture Collection Rate",
	                         "Number of seconds between each run of the GL texture garbage collector");

	prop = RNA_def_property(srna, "window_draw_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "wmdrawmethod");
	RNA_def_property_enum_items(prop, draw_method_items);
	RNA_def_property_ui_text(prop, "Window Draw Method", "Drawing method used by the window manager");
	RNA_def_property_update(prop, 0, "rna_userdef_dpi_update");

	prop = RNA_def_property(srna, "use_text_antialiasing", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "text_render", USER_TEXT_DISABLE_AA);
	RNA_def_property_ui_text(prop, "Text Anti-aliasing", "Draw user interface text anti-aliased");
	RNA_def_property_update(prop, 0, "rna_userdef_text_update");

	prop = RNA_def_property(srna, "text_hinting", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "text_render");
	RNA_def_property_enum_items(prop, text_hinting_items);
	RNA_def_property_ui_text(prop, "Text Hinting", "Method for making user interface text render sharp");
	RNA_def_property_update(prop, 0, "rna_userdef_text_update");

	prop = RNA_def_property(srna, "select_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "gpu_select_method");
	RNA_def_property_enum_items(prop, gpu_select_method_items);
	RNA_def_property_ui_text(prop, "Selection Method",
	                         "Use OpenGL occlusion queries or selection render mode to accelerate selection");

	prop = RNA_def_property(srna, "use_select_pick_depth", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "gpu_select_pick_deph", 1);
	RNA_def_property_ui_text(prop, "OpenGL Depth Picking", "Use the depth buffer for picking 3D View selection");

	/* Full scene anti-aliasing */
	prop = RNA_def_property(srna, "multi_sample", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ogl_multisamples");
	RNA_def_property_enum_items(prop, multi_sample_levels);
	RNA_def_property_ui_text(prop, "MultiSample",
	                         "Enable OpenGL multi-sampling, only for systems that support it, requires restart");

	prop = RNA_def_property(srna, "use_region_overlap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag2", USER_REGION_OVERLAP);
	RNA_def_property_ui_text(prop, "Region Overlap",
	                         "Draw tool/property regions over the main region, when using Triple Buffer");
	RNA_def_property_update(prop, 0, "rna_userdef_dpi_update");

#ifdef WITH_OPENSUBDIV
	prop = RNA_def_property(srna, "opensubdiv_compute_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_flag(prop, PROP_ENUM_NO_CONTEXT);
	RNA_def_property_enum_sdna(prop, NULL, "opensubdiv_compute_type");
	RNA_def_property_enum_items(prop, opensubdiv_compute_type_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_userdef_opensubdiv_compute_type_itemf");
	RNA_def_property_ui_text(prop, "OpenSubdiv Compute Type", "Type of computer back-end used with OpenSubdiv");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_PROPERTIES, "rna_userdef_opensubdiv_update");
#endif

#ifdef WITH_CYCLES
	prop = RNA_def_property(srna, "legacy_compute_device_type", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "compute_device_type");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_flag(prop, PROP_HIDDEN);
	RNA_def_property_ui_text(prop, "Legacy Compute Device Type", "For backwards compatibility only");
#endif
}

static void rna_def_userdef_input(BlenderRNA *brna)
{
	PropertyRNA *prop;
	StructRNA *srna;

	static const EnumPropertyItem select_mouse_items[] = {
		{USER_LMOUSESELECT, "LEFT", 0, "Left", "Use left Mouse Button for selection"},
		{0, "RIGHT", 0, "Right", "Use Right Mouse Button for selection"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem view_rotation_items[] = {
		{0, "TURNTABLE", 0, "Turntable", "Use turntable style rotation in the viewport"},
		{USER_TRACKBALL, "TRACKBALL", 0, "Trackball", "Use trackball style rotation in the viewport"},
		{0, NULL, 0, NULL, NULL}
	};

#ifdef WITH_INPUT_NDOF
	static const EnumPropertyItem ndof_view_navigation_items[] = {
		{0, "FREE", 0, "Free", "Use full 6 degrees of freedom by default"},
		{NDOF_MODE_ORBIT, "ORBIT", 0, "Orbit", "Orbit about the view center by default"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem ndof_view_rotation_items[] = {
		{NDOF_TURNTABLE, "TURNTABLE", 0, "Turntable", "Use turntable style rotation in the viewport"},
		{0, "TRACKBALL", 0, "Trackball", "Use trackball style rotation in the viewport"},
		{0, NULL, 0, NULL, NULL}
	};
#endif /* WITH_INPUT_NDOF */

	static const EnumPropertyItem view_zoom_styles[] = {
		{USER_ZOOM_CONT, "CONTINUE", 0, "Continue", "Old style zoom, continues while moving mouse up or down"},
		{USER_ZOOM_DOLLY, "DOLLY", 0, "Dolly", "Zoom in and out based on vertical mouse movement"},
		{USER_ZOOM_SCALE, "SCALE", 0, "Scale",
		                  "Zoom in and out like scaling the view, mouse movements relative to center"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem view_zoom_axes[] = {
		{0, "VERTICAL", 0, "Vertical", "Zoom in and out based on vertical mouse movement"},
		{USER_ZOOM_HORIZ, "HORIZONTAL", 0, "Horizontal", "Zoom in and out based on horizontal mouse movement"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "UserPreferencesInput", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_nested(brna, srna, "UserPreferences");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Input", "Settings for input devices");

	prop = RNA_def_property(srna, "select_mouse", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, select_mouse_items);
	RNA_def_property_enum_funcs(prop, NULL, "rna_userdef_select_mouse_set", NULL);
	RNA_def_property_ui_text(prop, "Select Mouse", "Mouse button used for selection");

	prop = RNA_def_property(srna, "view_zoom_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "viewzoom");
	RNA_def_property_enum_items(prop, view_zoom_styles);
	RNA_def_property_ui_text(prop, "Zoom Style", "Which style to use for viewport scaling");

	prop = RNA_def_property(srna, "view_zoom_axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "uiflag");
	RNA_def_property_enum_items(prop, view_zoom_axes);
	RNA_def_property_ui_text(prop, "Zoom Axis", "Axis of mouse movement to zoom in or out on");

	prop = RNA_def_property(srna, "invert_mouse_zoom", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_ZOOM_INVERT);
	RNA_def_property_ui_text(prop, "Invert Zoom Direction", "Invert the axis of mouse movement for zooming");

	prop = RNA_def_property(srna, "view_rotate_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
	RNA_def_property_enum_items(prop, view_rotation_items);
	RNA_def_property_ui_text(prop, "View Rotation", "Rotation style in the viewport");

	prop = RNA_def_property(srna, "use_mouse_continuous", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_CONTINUOUS_MOUSE);
	RNA_def_property_ui_text(prop, "Continuous Grab",
	                         "Allow moving the mouse outside the view on some manipulations "
	                         "(transform, ui control drag)");

	/* View Navigation */
	prop = RNA_def_property(srna, "navigation_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "navigation_mode");
	RNA_def_property_enum_items(prop, rna_enum_navigation_mode_items);
	RNA_def_property_ui_text(prop, "View Navigation", "Which method to use for viewport navigation");

	prop = RNA_def_property(srna, "walk_navigation", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "walk_navigation");
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "WalkNavigation");
	RNA_def_property_ui_text(prop, "Walk Navigation", "Settings for walk navigation mode");

	/* tweak tablet & mouse preset */
	prop = RNA_def_property(srna, "drag_threshold", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "dragthreshold");
	RNA_def_property_range(prop, 3, 40);
	RNA_def_property_ui_text(prop, "Drag Threshold",
	                         "Amount of pixels you have to drag before dragging UI items happens");

	prop = RNA_def_property(srna, "tweak_threshold", PROP_INT, PROP_PIXEL);
	RNA_def_property_int_sdna(prop, NULL, "tweak_threshold");
	RNA_def_property_range(prop, 3, 1024);
	RNA_def_property_ui_text(prop, "Tweak Threshold",
	                         "Number of pixels you have to drag before tweak event is triggered");

#ifdef WITH_INPUT_NDOF
	/* 3D mouse settings */
	/* global options */
	prop = RNA_def_property(srna, "ndof_sensitivity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01f, 40.0f);
	RNA_def_property_ui_text(prop, "Sensitivity", "Overall sensitivity of the 3D Mouse for panning");

	prop = RNA_def_property(srna, "ndof_orbit_sensitivity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.01f, 40.0f);
	RNA_def_property_ui_text(prop, "Orbit Sensitivity", "Overall sensitivity of the 3D Mouse for orbiting");

	prop = RNA_def_property(srna, "ndof_deadzone", PROP_FLOAT, PROP_FACTOR);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Deadzone", "Threshold of initial movement needed from the device's rest position");
	RNA_def_property_update(prop, 0, "rna_userdef_ndof_deadzone_update");

	prop = RNA_def_property(srna, "ndof_pan_yz_swap_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_PAN_YZ_SWAP_AXIS);
	RNA_def_property_ui_text(prop, "Y/Z Swap Axis",
	                         "Pan using up/down on the device (otherwise forward/backward)");

	prop = RNA_def_property(srna, "ndof_zoom_invert", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_ZOOM_INVERT);
	RNA_def_property_ui_text(prop, "Invert Zoom", "Zoom using opposite direction");

	/* 3D view */
	prop = RNA_def_property(srna, "ndof_show_guide", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_SHOW_GUIDE);
	RNA_def_property_ui_text(prop, "Show Navigation Guide", "Display the center and axis during rotation");
	/* TODO: update description when fly-mode visuals are in place  ("projected position in fly mode")*/

	/* 3D view */
	prop = RNA_def_property(srna, "ndof_view_navigate_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ndof_flag");
	RNA_def_property_enum_items(prop, ndof_view_navigation_items);
	RNA_def_property_ui_text(prop, "NDOF View Navigate", "Navigation style in the viewport");

	prop = RNA_def_property(srna, "ndof_view_rotate_method", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "ndof_flag");
	RNA_def_property_enum_items(prop, ndof_view_rotation_items);
	RNA_def_property_ui_text(prop, "NDOF View Rotation", "Rotation style in the viewport");

	/* 3D view: yaw */
	prop = RNA_def_property(srna, "ndof_rotx_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_ROTX_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert Pitch (X) Axis", "");

	/* 3D view: pitch */
	prop = RNA_def_property(srna, "ndof_roty_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_ROTY_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert Yaw (Y) Axis", "");

	/* 3D view: roll */
	prop = RNA_def_property(srna, "ndof_rotz_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_ROTZ_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert Roll (Z) Axis", "");

	/* 3D view: pan x */
	prop = RNA_def_property(srna, "ndof_panx_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_PANX_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert X Axis", "");

	/* 3D view: pan y */
	prop = RNA_def_property(srna, "ndof_pany_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_PANY_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert Y Axis", "");

	/* 3D view: pan z */
	prop = RNA_def_property(srna, "ndof_panz_invert_axis", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_PANZ_INVERT_AXIS);
	RNA_def_property_ui_text(prop, "Invert Z Axis", "");

	/* 3D view: fly */
	prop = RNA_def_property(srna, "ndof_lock_horizon", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_LOCK_HORIZON);
	RNA_def_property_ui_text(prop, "Lock Horizon", "Keep horizon level while flying with 3D Mouse");

	prop = RNA_def_property(srna, "ndof_fly_helicopter", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "ndof_flag", NDOF_FLY_HELICOPTER);
	RNA_def_property_ui_text(prop, "Helicopter Mode", "Device up/down directly controls your Z position");

	/* let Python know whether NDOF is enabled */
	prop = RNA_def_boolean(srna, "use_ndof", true, "", "");
#else
	prop = RNA_def_boolean(srna, "use_ndof", false, "", "");
#endif /* WITH_INPUT_NDOF */
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);

	prop = RNA_def_property(srna, "mouse_double_click_time", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "dbl_click_time");
	RNA_def_property_range(prop, 1, 1000);
	RNA_def_property_ui_text(prop, "Double Click Timeout", "Time/delay (in ms) for a double click");

	prop = RNA_def_property(srna, "use_mouse_emulate_3_button", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_TWOBUTTONMOUSE);
	RNA_def_property_ui_text(prop, "Emulate 3 Button Mouse",
	                         "Emulate Middle Mouse with Alt+Left Mouse (doesn't work with Left Mouse Select option)");

	prop = RNA_def_property(srna, "use_emulate_numpad", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_NONUMPAD);
	RNA_def_property_ui_text(prop, "Emulate Numpad", "Main 1 to 0 keys act as the numpad ones (useful for laptops)");

	/* middle mouse button */
	prop = RNA_def_property(srna, "use_mouse_mmb_paste", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_MMB_PASTE);
	RNA_def_property_ui_text(prop, "Middle Mouse Paste",
	                         "In text window, paste with middle mouse button instead of panning");

	prop = RNA_def_property(srna, "invert_zoom_wheel", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_WHEELZOOMDIR);
	RNA_def_property_ui_text(prop, "Wheel Invert Zoom", "Swap the Mouse Wheel zoom direction");

	prop = RNA_def_property(srna, "wheel_scroll_lines", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "wheellinescroll");
	RNA_def_property_range(prop, 0, 32);
	RNA_def_property_ui_text(prop, "Wheel Scroll Lines", "Number of lines scrolled at a time with the mouse wheel");

	prop = RNA_def_property(srna, "use_trackpad_natural", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag2", USER_TRACKPAD_NATURAL);
	RNA_def_property_ui_text(prop, "Trackpad Natural",
	                         "If your system uses 'natural' scrolling, this option keeps consistent trackpad usage throughout the UI");

	prop = RNA_def_property(srna, "active_keyconfig", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "keyconfigstr");
	RNA_def_property_ui_text(prop, "Key Config", "The name of the active key configuration");
}

static void rna_def_userdef_filepaths(BlenderRNA *brna)
{
	PropertyRNA *prop;
	StructRNA *srna;

	srna = RNA_def_struct(brna, "UserPreferencesFilePaths", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_nested(brna, srna, "UserPreferences");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "File Paths", "Default paths for external files");

	prop = RNA_def_property(srna, "show_hidden_files_datablocks", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_HIDE_DOT);
	RNA_def_property_ui_text(prop, "Hide Dot Files/Data-Blocks", "Hide files/data-blocks that start with a dot (.*)");

	prop = RNA_def_property(srna, "use_filter_files", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_FILTERFILEEXTS);
	RNA_def_property_ui_text(prop, "Filter File Extensions",
	                         "Display only files with extensions in the image select window");

	prop = RNA_def_property(srna, "hide_recent_locations", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_HIDE_RECENT);
	RNA_def_property_ui_text(prop, "Hide Recent Locations", "Hide recent locations in the file selector");

	prop = RNA_def_property(srna, "hide_system_bookmarks", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_HIDE_SYSTEM_BOOKMARKS);
	RNA_def_property_ui_text(prop, "Hide System Bookmarks", "Hide system bookmarks in the file selector");

	prop = RNA_def_property(srna, "show_thumbnails", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag", USER_SHOW_THUMBNAILS);
	RNA_def_property_ui_text(prop, "Show Thumbnails", "Open in thumbnail view for images");

	prop = RNA_def_property(srna, "use_relative_paths", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_RELPATHS);
	RNA_def_property_ui_text(prop, "Relative Paths", "Default relative path option for the file selector");

	prop = RNA_def_property(srna, "use_file_compression", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_FILECOMPRESS);
	RNA_def_property_ui_text(prop, "Compress File", "Enable file compression when saving .blend files");

	prop = RNA_def_property(srna, "use_load_ui", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", USER_FILENOUI);
	RNA_def_property_ui_text(prop, "Load UI", "Load user interface setup when loading .blend files");
	RNA_def_property_update(prop, 0, "rna_userdef_load_ui_update");

	prop = RNA_def_property(srna, "font_directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "fontdir");
	RNA_def_property_ui_text(prop, "Fonts Directory", "The default directory to search for loading fonts");

	prop = RNA_def_property(srna, "texture_directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "textudir");
	RNA_def_property_ui_text(prop, "Textures Directory", "The default directory to search for textures");

	prop = RNA_def_property(srna, "script_directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "pythondir");
	RNA_def_property_ui_text(prop, "Python Scripts Directory",
	                         "Alternate script path, matching the default layout with subdirs: "
	                         "startup, add-ons & modules (requires restart)");
	/* TODO, editing should reset sys.path! */

	prop = RNA_def_property(srna, "i18n_branches_directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "i18ndir");
	RNA_def_property_ui_text(prop, "Translation Branches Directory",
	                         "The path to the '/branches' directory of your local svn-translation copy, "
	                         "to allow translating from the UI");

	prop = RNA_def_property(srna, "temporary_directory", PROP_STRING, PROP_DIRPATH);
	RNA_def_property_string_sdna(prop, NULL, "tempdir");
	RNA_def_property_ui_text(prop, "Temporary Directory", "The directory for storing temporary save files");
	RNA_def_property_update(prop, 0, "rna_userdef_temp_update");

	prop = RNA_def_property(srna, "image_editor", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "image_editor");
	RNA_def_property_ui_text(prop, "Image Editor", "Path to an image editor");

	/* Autosave  */

	prop = RNA_def_property(srna, "save_version", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "versions");
	RNA_def_property_range(prop, 0, 32);
	RNA_def_property_ui_text(prop, "Save Versions",
	                         "The number of old versions to maintain in the current directory, when manually saving");

	prop = RNA_def_property(srna, "use_auto_save_temporary_files", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_AUTOSAVE);
	RNA_def_property_ui_text(prop, "Auto Save Temporary Files",
	                         "Automatic saving of temporary files in temp directory, uses process ID (Sculpt or edit mode data won't be saved!')");
	RNA_def_property_update(prop, 0, "rna_userdef_autosave_update");

	prop = RNA_def_property(srna, "auto_save_time", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "savetime");
	RNA_def_property_range(prop, 1, 60);
	RNA_def_property_ui_text(prop, "Auto Save Time",
	                         "The time (in minutes) to wait between automatic temporary saves");
	RNA_def_property_update(prop, 0, "rna_userdef_autosave_update");

	prop = RNA_def_property(srna, "use_keep_session", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "uiflag2", USER_KEEP_SESSION);
	RNA_def_property_ui_text(prop, "Keep Session",
	                         "Always load session recovery and save it after quitting Blender");

	prop = RNA_def_property(srna, "recent_files", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 0, 30);
	RNA_def_property_ui_text(prop, "Recent Files", "Maximum number of recently opened files to remember");

	prop = RNA_def_property(srna, "use_save_preview_images", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_SAVE_PREVIEWS);
	RNA_def_property_ui_text(prop, "Save Preview Images",
	                         "Enables automatic saving of preview images in the .blend file");
}

static void rna_def_userdef_addon_collection(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "Addons");
	srna = RNA_def_struct(brna, "Addons", NULL);
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "User Add-ons", "Collection of add-ons");

	func = RNA_def_function(srna, "new", "rna_userdef_addon_new");
	RNA_def_function_flag(func, FUNC_NO_SELF);
	RNA_def_function_ui_description(func, "Add a new add-on");
	/* return type */
	parm = RNA_def_pointer(func, "addon", "Addon", "", "Add-on data");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_userdef_addon_remove");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_USE_REPORTS);
	RNA_def_function_ui_description(func, "Remove add-on");
	parm = RNA_def_pointer(func, "addon", "Addon", "", "Add-on to remove");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_userdef_autoexec_path_collection(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "PathCompareCollection");
	srna = RNA_def_struct(brna, "PathCompareCollection", NULL);
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "Paths Compare", "Collection of paths");

	func = RNA_def_function(srna, "new", "rna_userdef_pathcompare_new");
	RNA_def_function_flag(func, FUNC_NO_SELF);
	RNA_def_function_ui_description(func, "Add a new path");
	/* return type */
	parm = RNA_def_pointer(func, "pathcmp", "PathCompare", "", "");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_userdef_pathcompare_remove");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_USE_REPORTS);
	RNA_def_function_ui_description(func, "Remove path");
	parm = RNA_def_pointer(func, "pathcmp", "PathCompare", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

void RNA_def_userdef(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem user_pref_sections[] = {
		{USER_SECTION_INTERFACE, "INTERFACE", 0, "Interface", ""},
		{USER_SECTION_EDIT, "EDITING", 0, "Editing", ""},
		{USER_SECTION_INPUT, "INPUT", 0, "Input", ""},
		{USER_SECTION_ADDONS, "ADDONS", 0, "Add-ons", ""},
		{USER_SECTION_THEME, "THEMES", 0, "Themes", ""},
		{USER_SECTION_FILE, "FILES", 0, "File", ""},
		{USER_SECTION_SYSTEM, "SYSTEM", 0, "System", ""},
		{0, NULL, 0, NULL, NULL}
	};

	rna_def_userdef_dothemes(brna);
	rna_def_userdef_solidlight(brna);
	rna_def_userdef_walk_navigation(brna);

	srna = RNA_def_struct(brna, "UserPreferences", NULL);
	RNA_def_struct_sdna(srna, "UserDef");
	RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
	RNA_def_struct_ui_text(srna, "User Preferences", "Global user preferences");

	prop = RNA_def_property(srna, "active_section", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "userpref");
	RNA_def_property_enum_items(prop, user_pref_sections);
	RNA_def_property_ui_text(prop, "Active Section",
	                         "Active section of the user preferences shown in the user interface");
	RNA_def_property_update(prop, 0, "rna_userdef_update");

	/* don't expose this directly via the UI, modify via an operator */
	prop = RNA_def_property(srna, "app_template", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "app_template");
	RNA_def_property_ui_text(prop, "Application Template", "");

	prop = RNA_def_property(srna, "themes", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "themes", NULL);
	RNA_def_property_struct_type(prop, "Theme");
	RNA_def_property_ui_text(prop, "Themes", "");

	prop = RNA_def_property(srna, "ui_styles", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "uistyles", NULL);
	RNA_def_property_struct_type(prop, "ThemeStyle");
	RNA_def_property_ui_text(prop, "Styles", "");

	prop = RNA_def_property(srna, "addons", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "addons", NULL);
	RNA_def_property_struct_type(prop, "Addon");
	RNA_def_property_ui_text(prop, "Add-on", "");
	rna_def_userdef_addon_collection(brna, prop);

	prop = RNA_def_property(srna, "autoexec_paths", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "autoexec_paths", NULL);
	RNA_def_property_struct_type(prop, "PathCompare");
	RNA_def_property_ui_text(prop, "Autoexec Paths", "");
	rna_def_userdef_autoexec_path_collection(brna, prop);

	/* nested structs */
	prop = RNA_def_property(srna, "view", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "UserPreferencesView");
	RNA_def_property_pointer_funcs(prop, "rna_UserDef_view_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "View & Controls", "Preferences related to viewing data");

	prop = RNA_def_property(srna, "edit", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "UserPreferencesEdit");
	RNA_def_property_pointer_funcs(prop, "rna_UserDef_edit_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Edit Methods", "Settings for interacting with Blender data");

	prop = RNA_def_property(srna, "inputs", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "UserPreferencesInput");
	RNA_def_property_pointer_funcs(prop, "rna_UserDef_input_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Inputs", "Settings for input devices");

	prop = RNA_def_property(srna, "filepaths", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "UserPreferencesFilePaths");
	RNA_def_property_pointer_funcs(prop, "rna_UserDef_filepaths_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "File Paths", "Default paths for external files");

	prop = RNA_def_property(srna, "system", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "UserPreferencesSystem");
	RNA_def_property_pointer_funcs(prop, "rna_UserDef_system_get", NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "System & OpenGL", "Graphics driver and operating system settings");

	prop = RNA_def_int_vector(srna, "version", 3, NULL, 0, INT_MAX,
	                   "Version", "Version of Blender the userpref.blend was saved with", 0, INT_MAX);
	RNA_def_property_int_funcs(prop, "rna_userdef_version_get", NULL, NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_flag(prop, PROP_THICK_WRAP);

	rna_def_userdef_view(brna);
	rna_def_userdef_edit(brna);
	rna_def_userdef_input(brna);
	rna_def_userdef_filepaths(brna);
	rna_def_userdef_system(brna);
	rna_def_userdef_addon(brna);
	rna_def_userdef_addon_pref(brna);
	rna_def_userdef_pathcompare(brna);

}

#endif
