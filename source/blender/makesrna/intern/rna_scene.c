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

/** \file blender/makesrna/intern/rna_scene.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "DNA_group_types.h"
#include "DNA_modifier_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_userdef_types.h"
#include "DNA_world_types.h"

#include "IMB_imbuf_types.h"

#include "BLI_math.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "BKE_editmesh.h"

#include "GPU_extensions.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BLI_threads.h"

#ifdef WITH_OPENEXR
const EnumPropertyItem rna_enum_exr_codec_items[] = {
	{R_IMF_EXR_CODEC_NONE, "NONE", 0, "None", ""},
	{R_IMF_EXR_CODEC_PXR24, "PXR24", 0, "Pxr24 (lossy)", ""},
	{R_IMF_EXR_CODEC_ZIP, "ZIP", 0, "ZIP (lossless)", ""},
	{R_IMF_EXR_CODEC_PIZ, "PIZ", 0, "PIZ (lossless)", ""},
	{R_IMF_EXR_CODEC_RLE, "RLE", 0, "RLE (lossless)", ""},
	{R_IMF_EXR_CODEC_ZIPS, "ZIPS", 0, "ZIPS (lossless)", ""},
	{R_IMF_EXR_CODEC_B44, "B44", 0, "B44 (lossy)", ""},
	{R_IMF_EXR_CODEC_B44A, "B44A", 0, "B44A (lossy)", ""},
	{R_IMF_EXR_CODEC_DWAA, "DWAA", 0, "DWAA (lossy)", ""},
	/* NOTE: Commented out for until new OpenEXR is released, see T50673. */
	/* {R_IMF_EXR_CODEC_DWAB, "DWAB", 0, "DWAB (lossy)", ""}, */
	{0, NULL, 0, NULL, NULL}
};
#endif

const EnumPropertyItem rna_enum_snap_target_items[] = {
	{SCE_SNAP_TARGET_CLOSEST, "CLOSEST", 0, "Closest", "Snap closest point onto target"},
	{SCE_SNAP_TARGET_CENTER, "CENTER", 0, "Center", "Snap transormation center onto target"},
	{SCE_SNAP_TARGET_MEDIAN, "MEDIAN", 0, "Median", "Snap median onto target"},
	{SCE_SNAP_TARGET_ACTIVE, "ACTIVE", 0, "Active", "Snap active onto target"},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_proportional_falloff_items[] = {
	{PROP_SMOOTH, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", "Smooth falloff"},
	{PROP_SPHERE, "SPHERE", ICON_SPHERECURVE, "Sphere", "Spherical falloff"},
	{PROP_ROOT, "ROOT", ICON_ROOTCURVE, "Root", "Root falloff"},
	{PROP_INVSQUARE, "INVERSE_SQUARE", ICON_ROOTCURVE, "Inverse Square", "Inverse Square falloff"},
	{PROP_SHARP, "SHARP", ICON_SHARPCURVE, "Sharp", "Sharp falloff"},
	{PROP_LIN, "LINEAR", ICON_LINCURVE, "Linear", "Linear falloff"},
	{PROP_CONST, "CONSTANT", ICON_NOCURVE, "Constant", "Constant falloff"},
	{PROP_RANDOM, "RANDOM", ICON_RNDCURVE, "Random", "Random falloff"},
	{0, NULL, 0, NULL, NULL}
};

/* subset of the enum - only curves, missing random and const */
const EnumPropertyItem rna_enum_proportional_falloff_curve_only_items[] = {
	{PROP_SMOOTH, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", "Smooth falloff"},
	{PROP_SPHERE, "SPHERE", ICON_SPHERECURVE, "Sphere", "Spherical falloff"},
	{PROP_ROOT, "ROOT", ICON_ROOTCURVE, "Root", "Root falloff"},
	{PROP_INVSQUARE, "INVERSE_SQUARE", ICON_ROOTCURVE, "Inverse Square", "Inverse Square falloff"},
	{PROP_SHARP, "SHARP", ICON_SHARPCURVE, "Sharp", "Sharp falloff"},
	{PROP_LIN, "LINEAR", ICON_LINCURVE, "Linear", "Linear falloff"},
	{0, NULL, 0, NULL, NULL}
};


const EnumPropertyItem rna_enum_proportional_editing_items[] = {
	{PROP_EDIT_OFF, "DISABLED", ICON_PROP_OFF, "Disable", "Proportional Editing disabled"},
	{PROP_EDIT_ON, "ENABLED", ICON_PROP_ON, "Enable", "Proportional Editing enabled"},
	{PROP_EDIT_PROJECTED, "PROJECTED", ICON_PROP_ON, "Projected (2D)",
	                      "Proportional Editing using screen space locations"},
	{PROP_EDIT_CONNECTED, "CONNECTED", ICON_PROP_CON, "Connected",
	                      "Proportional Editing using connected geometry only"},
	{0, NULL, 0, NULL, NULL}
};

/* keep for operators, not used here */
const EnumPropertyItem rna_enum_mesh_select_mode_items[] = {
	{SCE_SELECT_VERTEX, "VERTEX", ICON_VERTEXSEL, "Vertex", "Vertex selection mode"},
	{SCE_SELECT_EDGE, "EDGE", ICON_EDGESEL, "Edge", "Edge selection mode"},
	{SCE_SELECT_FACE, "FACE", ICON_FACESEL, "Face", "Face selection mode"},
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_snap_element_items[] = {
	{SCE_SNAP_MODE_INCREMENT, "INCREMENT", ICON_SNAP_INCREMENT, "Increment", "Snap to increments of grid"},
	{SCE_SNAP_MODE_VERTEX, "VERTEX", ICON_SNAP_VERTEX, "Vertex", "Snap to vertices"},
	{SCE_SNAP_MODE_EDGE, "EDGE", ICON_SNAP_EDGE, "Edge", "Snap to edges"},
	{SCE_SNAP_MODE_FACE, "FACE", ICON_SNAP_FACE, "Face", "Snap to faces"},
	{SCE_SNAP_MODE_VOLUME, "VOLUME", ICON_SNAP_VOLUME, "Volume", "Snap to volume"},
	{0, NULL, 0, NULL, NULL}
};

#ifndef RNA_RUNTIME
static const EnumPropertyItem snap_uv_element_items[] = {
	{SCE_SNAP_MODE_INCREMENT, "INCREMENT", ICON_SNAP_INCREMENT, "Increment", "Snap to increments of grid"},
	{SCE_SNAP_MODE_VERTEX, "VERTEX", ICON_SNAP_VERTEX, "Vertex", "Snap to vertices"},
	{0, NULL, 0, NULL, NULL}
};
#endif

/* workaround for duplicate enums,
 * have each enum line as a define then conditionally set it or not
 */

#define R_IMF_ENUM_BMP      {R_IMF_IMTYPE_BMP, "BMP", ICON_FILE_IMAGE, "BMP", "Output image in bitmap format"},
#define R_IMF_ENUM_IRIS     {R_IMF_IMTYPE_IRIS, "IRIS", ICON_FILE_IMAGE, "Iris", \
                                                "Output image in (old!) SGI IRIS format"},
#define R_IMF_ENUM_PNG      {R_IMF_IMTYPE_PNG, "PNG", ICON_FILE_IMAGE, "PNG", "Output image in PNG format"},
#define R_IMF_ENUM_JPEG     {R_IMF_IMTYPE_JPEG90, "JPEG", ICON_FILE_IMAGE, "JPEG", "Output image in JPEG format"},
#define R_IMF_ENUM_TAGA     {R_IMF_IMTYPE_TARGA, "TARGA", ICON_FILE_IMAGE, "Targa", "Output image in Targa format"},
#define R_IMF_ENUM_TAGA_RAW {R_IMF_IMTYPE_RAWTGA, "TARGA_RAW", ICON_FILE_IMAGE, "Targa Raw", \
                                                  "Output image in uncompressed Targa format"},

#if 0 /* UNUSED (so far) */
#ifdef WITH_DDS
#  define R_IMF_ENUM_DDS {R_IMF_IMTYPE_DDS, "DDS", ICON_FILE_IMAGE, "DDS", "Output image in DDS format"},
#else
#  define R_IMF_ENUM_DDS
#endif
#endif

#ifdef WITH_OPENJPEG
#  define R_IMF_ENUM_JPEG2K {R_IMF_IMTYPE_JP2, "JPEG2000", ICON_FILE_IMAGE, "JPEG 2000", \
                                               "Output image in JPEG 2000 format"},
#else
#  define R_IMF_ENUM_JPEG2K
#endif

#ifdef WITH_CINEON
#  define R_IMF_ENUM_CINEON {R_IMF_IMTYPE_CINEON, "CINEON", ICON_FILE_IMAGE, "Cineon", \
                                                  "Output image in Cineon format"},
#  define R_IMF_ENUM_DPX    {R_IMF_IMTYPE_DPX, "DPX", ICON_FILE_IMAGE, "DPX", "Output image in DPX format"},
#else
#  define R_IMF_ENUM_CINEON
#  define R_IMF_ENUM_DPX
#endif

#ifdef WITH_OPENEXR
#  define R_IMF_ENUM_EXR_MULTILAYER  {R_IMF_IMTYPE_MULTILAYER, "OPEN_EXR_MULTILAYER", ICON_FILE_IMAGE, \
                                                          "OpenEXR MultiLayer", \
                                                          "Output image in multilayer OpenEXR format"},
#  define R_IMF_ENUM_EXR        {R_IMF_IMTYPE_OPENEXR, "OPEN_EXR", ICON_FILE_IMAGE, "OpenEXR", \
                                                       "Output image in OpenEXR format"},
#else
#  define R_IMF_ENUM_EXR_MULTILAYER
#  define R_IMF_ENUM_EXR
#endif

#ifdef WITH_HDR
#  define R_IMF_ENUM_HDR  {R_IMF_IMTYPE_RADHDR, "HDR", ICON_FILE_IMAGE, "Radiance HDR", \
                                                "Output image in Radiance HDR format"},
#else
#  define R_IMF_ENUM_HDR
#endif

#ifdef WITH_TIFF
#  define R_IMF_ENUM_TIFF {R_IMF_IMTYPE_TIFF, "TIFF", ICON_FILE_IMAGE, "TIFF", "Output image in TIFF format"},
#else
#  define R_IMF_ENUM_TIFF
#endif

#define IMAGE_TYPE_ITEMS_IMAGE_ONLY                                           \
	R_IMF_ENUM_BMP                                                            \
	/* DDS save not supported yet R_IMF_ENUM_DDS */                           \
	R_IMF_ENUM_IRIS                                                           \
	R_IMF_ENUM_PNG                                                            \
	R_IMF_ENUM_JPEG                                                           \
	R_IMF_ENUM_JPEG2K                                                         \
	R_IMF_ENUM_TAGA                                                           \
	R_IMF_ENUM_TAGA_RAW                                                       \
	{0, "", 0, " ", NULL},                                                    \
	R_IMF_ENUM_CINEON                                                         \
	R_IMF_ENUM_DPX                                                            \
	R_IMF_ENUM_EXR_MULTILAYER                                                 \
	R_IMF_ENUM_EXR                                                            \
	R_IMF_ENUM_HDR                                                            \
	R_IMF_ENUM_TIFF                                                           \


#ifdef RNA_RUNTIME
static const EnumPropertyItem image_only_type_items[] = {

	IMAGE_TYPE_ITEMS_IMAGE_ONLY

	{0, NULL, 0, NULL, NULL}
};
#endif

const EnumPropertyItem rna_enum_image_type_items[] = {
	{0, "", 0, N_("Image"), NULL},

	IMAGE_TYPE_ITEMS_IMAGE_ONLY

	{0, "", 0, N_("Movie"), NULL},
	{R_IMF_IMTYPE_AVIJPEG, "AVI_JPEG", ICON_FILE_MOVIE, "AVI JPEG", "Output video in AVI JPEG format"},
	{R_IMF_IMTYPE_AVIRAW, "AVI_RAW", ICON_FILE_MOVIE, "AVI Raw", "Output video in AVI Raw format"},
#ifdef WITH_FRAMESERVER
	{R_IMF_IMTYPE_FRAMESERVER, "FRAMESERVER", ICON_FILE_SCRIPT, "Frame Server", "Output image to a frameserver"},
#endif
	{0, NULL, 0, NULL, NULL}
};

const EnumPropertyItem rna_enum_image_color_mode_items[] = {
	{R_IMF_PLANES_BW, "BW", 0, "BW", "Images get saved in 8 bits grayscale (only PNG, JPEG, TGA, TIF)"},
	{R_IMF_PLANES_RGB, "RGB", 0, "RGB", "Images are saved with RGB (color) data"},
	{R_IMF_PLANES_RGBA, "RGBA", 0, "RGBA", "Images are saved with RGB and Alpha data (if supported)"},
	{0, NULL, 0, NULL, NULL}
};

#ifdef RNA_RUNTIME
#define IMAGE_COLOR_MODE_BW   rna_enum_image_color_mode_items[0]
#define IMAGE_COLOR_MODE_RGB  rna_enum_image_color_mode_items[1]
#define IMAGE_COLOR_MODE_RGBA rna_enum_image_color_mode_items[2]
#endif

const EnumPropertyItem rna_enum_image_color_depth_items[] = {
	/* 1 (monochrome) not used */
	{R_IMF_CHAN_DEPTH_8,   "8", 0, "8",  "8 bit color channels"},
	{R_IMF_CHAN_DEPTH_10, "10", 0, "10", "10 bit color channels"},
	{R_IMF_CHAN_DEPTH_12, "12", 0, "12", "12 bit color channels"},
	{R_IMF_CHAN_DEPTH_16, "16", 0, "16", "16 bit color channels"},
	/* 24 not used */
	{R_IMF_CHAN_DEPTH_32, "32", 0, "32", "32 bit color channels"},
	{0, NULL, 0, NULL, NULL}
};

#ifdef RNA_RUNTIME

#include "DNA_color_types.h"
#include "DNA_object_types.h"
#include "DNA_mesh_types.h"
#include "DNA_text_types.h"

#include "RNA_access.h"

#include "MEM_guardedalloc.h"

#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_image.h"
#include "BKE_main.h"
#include "BKE_scene.h"
#include "BKE_mesh.h"
#include "BKE_screen.h"

#include "ED_info.h"
#include "ED_view3d.h"
#include "ED_mesh.h"
#include "ED_image.h"

static int rna_Scene_object_bases_lookup_string(PointerRNA *ptr, const char *key, PointerRNA *r_ptr)
{
	Scene *scene = (Scene *)ptr->data;
	Base *base;

	for (base = scene->base.first; base; base = base->next) {
		if (STREQLEN(base->object->id.name + 2, key, sizeof(base->object->id.name) - 2)) {
			*r_ptr = rna_pointer_inherit_refine(ptr, &RNA_ObjectBase, base);
			return true;
		}
	}

	return false;
}

static PointerRNA rna_Scene_objects_get(CollectionPropertyIterator *iter)
{
	ListBaseIterator *internal = &iter->internal.listbase;

	/* we are actually iterating a Base list, so override get */
	return rna_pointer_inherit_refine(&iter->parent, &RNA_Object, ((Base *)internal->link)->object);
}

static Base *rna_Scene_object_link(Scene *scene, Main *UNUSED(bmain), bContext *C, ReportList *reports, Object *ob)
{
	Scene *scene_act = CTX_data_scene(C);
	Base *base;

	if (BKE_scene_base_find(scene, ob)) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' is already in scene '%s'", ob->id.name + 2, scene->id.name + 2);
		return NULL;
	}

	base = BKE_scene_base_add(scene, ob);
	id_us_plus(&ob->id);

	/* this is similar to what object_add_type and BKE_object_add do */
	base->lay = scene->lay;

	/* when linking to an inactive scene don't touch the layer */
	if (scene == scene_act)
		ob->lay = base->lay;

	/* TODO(sergey): Only update relations for the current scene. */

	WM_main_add_notifier(NC_SCENE | ND_OB_ACTIVE, scene);

	return base;
}

static void rna_Scene_object_unlink(Scene *scene, Main *bmain, ReportList *reports, Object *ob)
{
	Base *base = BKE_scene_base_find(scene, ob);
	if (!base) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' is not in this scene '%s'", ob->id.name + 2, scene->id.name + 2);
		return;
	}
	if (base == scene->basact && ob->mode != OB_MODE_OBJECT) {
		BKE_reportf(reports, RPT_ERROR, "Object '%s' must be in object mode to unlink", ob->id.name + 2);
		return;
	}
	if (scene->basact == base) {
		scene->basact = NULL;
	}

	BKE_scene_base_unlink(scene, base);
	MEM_freeN(base);

	id_us_min(&ob->id);

	WM_main_add_notifier(NC_SCENE | ND_OB_ACTIVE, scene);
}

static PointerRNA rna_Scene_active_object_get(PointerRNA *ptr)
{
	Scene *scene = (Scene *)ptr->data;
	return rna_pointer_inherit_refine(ptr, &RNA_Object, scene->basact ? scene->basact->object : NULL);
}

static void rna_Scene_active_object_set(PointerRNA *ptr, PointerRNA value)
{
	Scene *scene = (Scene *)ptr->data;
	if (value.data)
		scene->basact = BKE_scene_base_find(scene, (Object *)value.data);
	else
		scene->basact = NULL;
}

static void rna_Scene_set_set(PointerRNA *ptr, PointerRNA value)
{
	Scene *scene = (Scene *)ptr->data;
	Scene *set = (Scene *)value.data;
	Scene *nested_set;

	for (nested_set = set; nested_set; nested_set = nested_set->set) {
		if (nested_set == scene)
			return;
		/* prevent eternal loops, set can point to next, and next to set, without problems usually */
		if (nested_set->set == set)
			return;
	}

	id_lib_extern((ID *)set);
	scene->set = set;
}

static void rna_Scene_layer_set(PointerRNA *ptr, const bool *values)
{
	Scene *scene = (Scene *)ptr->data;

	scene->lay = ED_view3d_scene_layer_set(scene->lay, values, &scene->layact);
}

static int rna_Scene_active_layer_get(PointerRNA *ptr)
{
	Scene *scene = (Scene *)ptr->data;

	return (int)(log(scene->layact) / M_LN2);
}

static void rna_Scene_view3d_update(Main *bmain, Scene *UNUSED(scene_unused), PointerRNA *ptr)
{
	Scene *scene = (Scene *)ptr->data;

	BKE_screen_view3d_main_sync(&bmain->screen, scene);
}

static void rna_Scene_layer_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	rna_Scene_view3d_update(bmain, scene, ptr);
}

static void rna_Scene_glsl_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{

}

static void rna_Scene_editmesh_select_mode_set(PointerRNA *ptr, const bool *value)
{
	Scene *scene = (Scene *)ptr->id.data;
	ToolSettings *ts = (ToolSettings *)ptr->data;
	int flag = (value[0] ? SCE_SELECT_VERTEX : 0) | (value[1] ? SCE_SELECT_EDGE : 0) | (value[2] ? SCE_SELECT_FACE : 0);

	if (flag) {
		ts->selectmode = flag;

		if (scene->basact) {
			Mesh *me = BKE_mesh_from_object(scene->basact->object);
			if (me && me->edit_btmesh && me->edit_btmesh->selectmode != flag) {
				me->edit_btmesh->selectmode = flag;
				EDBM_selectmode_set(me->edit_btmesh);
			}
		}
	}
}

static void rna_Scene_editmesh_select_mode_update(Main *UNUSED(bmain), Scene *scene, PointerRNA *UNUSED(ptr))
{
	Mesh *me = NULL;

	if (scene->basact) {
		me = BKE_mesh_from_object(scene->basact->object);
		if (me && me->edit_btmesh == NULL)
			me = NULL;
	}

	WM_main_add_notifier(NC_GEOM | ND_SELECT, me);
	WM_main_add_notifier(NC_SCENE | ND_TOOLSETTINGS, NULL);
}

/* generic function to recalc geometry */
static void rna_EditMesh_update(Main *UNUSED(bmain), Scene *scene, PointerRNA *UNUSED(ptr))
{
	Mesh *me = NULL;

	if (scene->basact) {
		me = BKE_mesh_from_object(scene->basact->object);
		if (me && me->edit_btmesh == NULL)
			me = NULL;
	}

	if (me) {
		WM_main_add_notifier(NC_GEOM | ND_DATA, me);
	}
}

static char *rna_MeshStatVis_path(PointerRNA *UNUSED(ptr))
{
	return BLI_strdup("tool_settings.statvis");
}

/* this function
 * is not for general use and only for the few cases where changing scene
 * settings and NOT for general purpose updates, possibly this should be
 * given its own notifier. */
static void rna_Scene_update_active_object_data(Main *UNUSED(bmain), Scene *scene, PointerRNA *UNUSED(ptr))
{
	Object *ob = OBACT;
	if (ob) {
		WM_main_add_notifier(NC_OBJECT | ND_DRAW, &ob->id);
	}
}

static char *rna_ToolSettings_path(PointerRNA *UNUSED(ptr))
{
	return BLI_strdup("tool_settings");
}

#else

static void rna_def_transform_orientation(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "TransformOrientation", NULL);

	prop = RNA_def_property(srna, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "mat");
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_3x3);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_ui_text(prop, "Name", "Name of the custom transform orientation");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);
}

static void rna_def_tool_settings(BlenderRNA  *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* the construction of this enum is quite special - everything is stored as bitflags,
	 * with 1st position only for for on/off (and exposed as boolean), while others are mutually
	 * exclusive options but which will only have any effect when autokey is enabled
	 */
	static const EnumPropertyItem edge_tag_items[] = {
		{EDGE_MODE_SELECT, "SELECT", 0, "Select", ""},
		{EDGE_MODE_TAG_SEAM, "SEAM", 0, "Tag Seam", ""},
		{EDGE_MODE_TAG_SHARP, "SHARP", 0, "Tag Sharp", ""},
		{EDGE_MODE_TAG_CREASE, "CREASE", 0, "Tag Crease", ""},
		{EDGE_MODE_TAG_BEVEL, "BEVEL", 0, "Tag Bevel", ""},
		{EDGE_MODE_TAG_FREESTYLE, "FREESTYLE", 0, "Tag Freestyle Edge Mark", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem draw_groupuser_items[] = {
		{OB_DRAW_GROUPUSER_NONE, "NONE", 0, "None", ""},
		{OB_DRAW_GROUPUSER_ACTIVE, "ACTIVE", 0, "Active", "Show vertices with no weights in the active group"},
		{OB_DRAW_GROUPUSER_ALL, "ALL", 0, "All", "Show vertices with no weights in any group"},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem vertex_group_select_items[] = {
		{WT_VGROUP_ALL, "ALL", 0, "All", "All Vertex Groups"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "ToolSettings", NULL);
	RNA_def_struct_path_func(srna, "rna_ToolSettings_path");
	RNA_def_struct_ui_text(srna, "Tool Settings", "");

	prop = RNA_def_property(srna, "vertex_group_user", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "weightuser");
	RNA_def_property_enum_items(prop, draw_groupuser_items);
	RNA_def_property_ui_text(prop, "Mask Non-Group Vertices", "Display unweighted vertices");
	RNA_def_property_update(prop, 0, "rna_Scene_update_active_object_data");

	prop = RNA_def_property(srna, "vertex_group_subset", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "vgroupsubset");
	RNA_def_property_enum_items(prop, vertex_group_select_items);
	RNA_def_property_ui_text(prop, "Subset", "Filter Vertex groups for Display");
	RNA_def_property_update(prop, 0, "rna_Scene_update_active_object_data");

	/* Transform */
	prop = RNA_def_property(srna, "proportional_edit", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "proportional");
	RNA_def_property_enum_items(prop, rna_enum_proportional_editing_items);
	RNA_def_property_ui_text(prop, "Proportional Editing",
	                         "Proportional Editing mode, allows transforms with distance fall-off");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_proportional_edit_objects", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "proportional_objects", 0);
	RNA_def_property_ui_text(prop, "Proportional Editing Objects", "Proportional editing object mode");
	RNA_def_property_ui_icon(prop, ICON_PROP_OFF, 1);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "proportional_edit_falloff", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "prop_mode");
	RNA_def_property_enum_items(prop, rna_enum_proportional_falloff_items);
	RNA_def_property_ui_text(prop, "Proportional Editing Falloff", "Falloff type for proportional editing mode");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "proportional_size", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "proportional_size");
	RNA_def_property_ui_text(prop, "Proportional Size", "Display size for proportional editing circle");
	RNA_def_property_range(prop, 0.00001, 5000.0);

	prop = RNA_def_property(srna, "normal_size", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "normalsize");
	RNA_def_property_ui_text(prop, "Normal Size", "Display size for normals in the 3D view");
	RNA_def_property_range(prop, 0.00001, 1000.0);
	RNA_def_property_ui_range(prop, 0.01, 10.0, 10.0, 2);
	RNA_def_property_update(prop, NC_GEOM | ND_DATA, NULL);

	prop = RNA_def_property(srna, "double_threshold", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "doublimit");
	RNA_def_property_ui_text(prop, "Double Threshold", "Limit for removing duplicates and 'Auto Merge'");
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_ui_range(prop, 0.0, 0.1, 0.01, 6);

	prop = RNA_def_property(srna, "use_mesh_automerge", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "automerge", 0);
	RNA_def_property_ui_text(prop, "AutoMerge Editing", "Automatically merge vertices moved to the same location");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP);
	RNA_def_property_ui_text(prop, "Snap", "Snap during transform");
	RNA_def_property_ui_icon(prop, ICON_SNAP_OFF, 1);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap_align_rotation", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_ROTATE);
	RNA_def_property_ui_text(prop, "Snap Align Rotation", "Align rotation with the snapping target");
	RNA_def_property_ui_icon(prop, ICON_SNAP_NORMAL, 0);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap_grid_absolute", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_ABS_GRID);
	RNA_def_property_ui_text(prop, "Absolute Grid Snap",
	                         "Absolute grid alignment while translating (based on the pivot center)");
	RNA_def_property_ui_icon(prop, ICON_SNAP_GRID, 0);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "snap_element", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "snap_mode");
	RNA_def_property_enum_items(prop, rna_enum_snap_element_items);
	RNA_def_property_ui_text(prop, "Snap Element", "Type of element to snap to");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	/* image editor uses own set of snap modes */
	prop = RNA_def_property(srna, "snap_uv_element", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "snap_uv_mode");
	RNA_def_property_enum_items(prop, snap_uv_element_items);
	RNA_def_property_ui_text(prop, "Snap UV Element", "Type of element to snap to");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "snap_target", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "snap_target");
	RNA_def_property_enum_items(prop, rna_enum_snap_target_items);
	RNA_def_property_ui_text(prop, "Snap Target", "Which part to snap onto the target");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap_peel_object", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_PEEL_OBJECT);
	RNA_def_property_ui_text(prop, "Snap Peel Object", "Consider objects as whole when finding volume center");
	RNA_def_property_ui_icon(prop, ICON_SNAP_PEEL_OBJECT, 0);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap_project", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "snap_flag", SCE_SNAP_PROJECT);
	RNA_def_property_ui_text(prop, "Project Individual Elements",
	                         "Project individual elements on the surface of other objects");
	RNA_def_property_ui_icon(prop, ICON_RETOPO, 0);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	prop = RNA_def_property(srna, "use_snap_self", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_negative_sdna(prop, NULL, "snap_flag", SCE_SNAP_NO_SELF);
	RNA_def_property_ui_text(prop, "Project to Self", "Snap onto itself (editmode)");
	RNA_def_property_ui_icon(prop, ICON_ORTHO, 0);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL); /* header redraw */

	/* Mesh */
	prop = RNA_def_property(srna, "mesh_select_mode", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "selectmode", 1);
	RNA_def_property_array(prop, 3);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Scene_editmesh_select_mode_set");
	RNA_def_property_ui_text(prop, "Mesh Selection Mode", "Which mesh elements selection works on");
	RNA_def_property_update(prop, 0, "rna_Scene_editmesh_select_mode_update");

	/* use with MESH_OT_shortest_path_pick */
	prop = RNA_def_property(srna, "edge_path_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "edge_mode");
	RNA_def_property_enum_items(prop, edge_tag_items);
	RNA_def_property_ui_text(prop, "Edge Tag Mode", "The edge flag to tag when selecting the shortest path");

	prop = RNA_def_property(srna, "edge_path_live_unwrap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "edge_mode_live_unwrap", 1);
	RNA_def_property_ui_text(prop, "Live Unwrap", "Changing edges seam re-calculates UV unwrap");

	/* Mesh Statistics */
	prop = RNA_def_property(srna, "statvis", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "MeshStatVis");
	RNA_def_property_ui_text(prop, "Mesh Statistics Visualization", NULL);
}
static void rna_def_statvis(BlenderRNA  *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem stat_type[] = {
		{SCE_STATVIS_OVERHANG,  "OVERHANG",  0, "Overhang",  ""},
		{SCE_STATVIS_THICKNESS, "THICKNESS", 0, "Thickness", ""},
		{SCE_STATVIS_INTERSECT, "INTERSECT", 0, "Intersect", ""},
		{SCE_STATVIS_DISTORT,   "DISTORT",   0, "Distortion", ""},
		{SCE_STATVIS_SHARP, "SHARP", 0, "Sharp", ""},
		{0, NULL, 0, NULL, NULL}};

	srna = RNA_def_struct(brna, "MeshStatVis", NULL);
	RNA_def_struct_path_func(srna, "rna_MeshStatVis_path");
	RNA_def_struct_ui_text(srna, "Mesh Visualize Statistics", "");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, stat_type);
	RNA_def_property_ui_text(prop, "Type", "Type of data to visualize/check");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");


	/* overhang */
	prop = RNA_def_property(srna, "overhang_min", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "overhang_min");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, 0.0f, DEG2RADF(180.0f), 0.001, 3);
	RNA_def_property_ui_text(prop, "Overhang Min", "Minimum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "overhang_max", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "overhang_max");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
	RNA_def_property_ui_text(prop, "Overhang Max", "Maximum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "overhang_axis", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "overhang_axis");
	RNA_def_property_enum_items(prop, rna_enum_object_axis_items);
	RNA_def_property_ui_text(prop, "Axis", "");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");


	/* thickness */
	prop = RNA_def_property(srna, "thickness_min", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "thickness_min");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, 1000.0);
	RNA_def_property_ui_range(prop, 0.0f, 100.0, 0.001, 3);
	RNA_def_property_ui_text(prop, "Thickness Min", "Minimum for measuring thickness");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "thickness_max", PROP_FLOAT, PROP_DISTANCE);
	RNA_def_property_float_sdna(prop, NULL, "thickness_max");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, 1000.0);
	RNA_def_property_ui_range(prop, 0.0f, 100.0, 0.001, 3);
	RNA_def_property_ui_text(prop, "Thickness Max", "Maximum for measuring thickness");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "thickness_samples", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "thickness_samples");
	RNA_def_property_range(prop, 1, 32);
	RNA_def_property_ui_text(prop, "Samples", "Number of samples to test per face");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	/* distort */
	prop = RNA_def_property(srna, "distort_min", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "distort_min");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
	RNA_def_property_ui_text(prop, "Distort Min", "Minimum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "distort_max", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "distort_max");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, 0.0f, DEG2RADF(180.0f), 10, 3);
	RNA_def_property_ui_text(prop, "Distort Max", "Maximum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	/* sharp */
	prop = RNA_def_property(srna, "sharp_min", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "sharp_min");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f), 10, 3);
	RNA_def_property_ui_text(prop, "Distort Min", "Minimum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");

	prop = RNA_def_property(srna, "sharp_max", PROP_FLOAT, PROP_ANGLE);
	RNA_def_property_float_sdna(prop, NULL, "sharp_max");
	RNA_def_property_float_default(prop, 0.5f);
	RNA_def_property_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f));
	RNA_def_property_ui_range(prop, -DEG2RADF(180.0f), DEG2RADF(180.0f), 10, 3);
	RNA_def_property_ui_text(prop, "Distort Max", "Maximum angle to display");
	RNA_def_property_update(prop, 0, "rna_EditMesh_update");
}

static void rna_def_unit_settings(BlenderRNA  *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem unit_systems[] = {
		{USER_UNIT_NONE, "NONE", 0, "None", ""},
		{USER_UNIT_METRIC, "METRIC", 0, "Metric", ""},
		{USER_UNIT_IMPERIAL, "IMPERIAL", 0, "Imperial", ""},
		{0, NULL, 0, NULL, NULL}
	};

	static const EnumPropertyItem rotation_units[] = {
		{0, "DEGREES", 0, "Degrees", "Use degrees for measuring angles and rotations"},
		{USER_UNIT_ROT_RADIANS, "RADIANS", 0, "Radians", ""},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "UnitSettings", NULL);
	RNA_def_struct_ui_text(srna, "Unit Settings", "");

	/* Units */
	prop = RNA_def_property(srna, "system", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, unit_systems);
	RNA_def_property_ui_text(prop, "Unit System", "The unit system to use for button display");
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop = RNA_def_property(srna, "system_rotation", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, rotation_units);
	RNA_def_property_ui_text(prop, "Rotation Units", "Unit to use for displaying/editing rotation values");
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop = RNA_def_property(srna, "scale_length", PROP_FLOAT, PROP_UNSIGNED);
	RNA_def_property_ui_text(prop, "Unit Scale", "Scale to use when converting between blender units and dimensions");
	RNA_def_property_range(prop, 0.00001, 100000.0);
	RNA_def_property_ui_range(prop, 0.001, 100.0, 0.1, 6);
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	prop = RNA_def_property(srna, "use_separate", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", USER_UNIT_OPT_SPLIT);
	RNA_def_property_ui_text(prop, "Separate Units", "Display units in pairs (e.g. 1m 0cm)");
	RNA_def_property_update(prop, NC_WINDOW, NULL);
}

/* scene.objects */
static void rna_def_scene_objects(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "SceneObjects");
	srna = RNA_def_struct(brna, "SceneObjects", NULL);
	RNA_def_struct_sdna(srna, "Scene");
	RNA_def_struct_ui_text(srna, "Scene Objects", "Collection of scene objects");

	func = RNA_def_function(srna, "link", "rna_Scene_object_link");
	RNA_def_function_ui_description(func, "Link object to scene, run scene.update() after");
	RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "object", "Object", "", "Object to add to scene");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "base", "ObjectBase", "", "The newly created base");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "unlink", "rna_Scene_object_unlink");
	RNA_def_function_ui_description(func, "Unlink object from scene");
	RNA_def_function_flag(func, FUNC_USE_MAIN | FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "object", "Object", "", "Object to remove from scene");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_pointer_funcs(prop, "rna_Scene_active_object_get", "rna_Scene_active_object_set", NULL, NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
	RNA_def_property_ui_text(prop, "Active Object", "Active object for this scene");
	/* Could call: ED_base_object_activate(C, scene->basact);
	 * but would be a bad level call and it seems the notifier is enough */
	RNA_def_property_update(prop, NC_SCENE | ND_OB_ACTIVE, NULL);
}


/* scene.bases.* */
static void rna_def_scene_bases(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

/*	FunctionRNA *func; */
/*	PropertyRNA *parm; */

	RNA_def_property_srna(cprop, "SceneBases");
	srna = RNA_def_struct(brna, "SceneBases", NULL);
	RNA_def_struct_sdna(srna, "Scene");
	RNA_def_struct_ui_text(srna, "Scene Bases", "Collection of scene bases");

	prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "ObjectBase");
	RNA_def_property_pointer_sdna(prop, NULL, "basact");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Active Base", "Active object base in the scene");
	RNA_def_property_update(prop, NC_SCENE | ND_OB_ACTIVE, NULL);
}

static void rna_def_display_safe_areas(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static float default_title[2] = {0.035f, 0.035f};
	static float default_action[2] = {0.1f, 0.05f};

	static float default_title_center[2] = {0.175f, 0.05f};
	static float default_action_center[2] = {0.15f, 0.05f};

	srna = RNA_def_struct(brna, "DisplaySafeAreas", NULL);
	RNA_def_struct_ui_text(srna, "Safe Areas", "Safe Areas used in 3D view and the VSE");
	RNA_def_struct_sdna(srna, "DisplaySafeAreas");

	/* SAFE AREAS */
	prop = RNA_def_property(srna, "title", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "title");
	RNA_def_property_array(prop, 2);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_float_array_default(prop, default_title);
	RNA_def_property_ui_text(prop, "Title Safe Margins", "Safe area for text and graphics");
	RNA_def_property_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

	prop = RNA_def_property(srna, "action", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "action");
	RNA_def_property_array(prop, 2);
	RNA_def_property_float_array_default(prop, default_action);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Action Safe Margins", "Safe area for general elements");
	RNA_def_property_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

	prop = RNA_def_property(srna, "title_center", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "title_center");
	RNA_def_property_array(prop, 2);
	RNA_def_property_float_array_default(prop, default_title_center);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Center Title Safe Margins", "Safe area for text and graphics in a different aspect ratio");
	RNA_def_property_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);

	prop = RNA_def_property(srna, "action_center", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "action_center");
	RNA_def_property_array(prop, 2);
	RNA_def_property_float_array_default(prop, default_action_center);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Center Action Safe Margins", "Safe area for general elements in a different aspect ratio");
	RNA_def_property_update(prop, NC_SCENE | ND_DRAW_RENDER_VIEWPORT, NULL);
}


void RNA_def_scene(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	/* Struct definition */
	srna = RNA_def_struct(brna, "Scene", "ID");
	RNA_def_struct_ui_text(srna, "Scene", "Scene data-block, consisting in objects and "
	                       "defining time and render related settings");
	RNA_def_struct_ui_icon(srna, ICON_SCENE_DATA);
	RNA_def_struct_clear_flag(srna, STRUCT_ID_REFCOUNT);

	/* Global Settings */
	prop = RNA_def_property(srna, "camera", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_pointer_funcs(prop, NULL, NULL, NULL, "rna_Camera_object_poll");
	RNA_def_property_ui_text(prop, "Camera", "Active camera, used for rendering the scene");
	RNA_def_property_update(prop, NC_SCENE | NA_EDITED, "rna_Scene_view3d_update");

	prop = RNA_def_property(srna, "background_set", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "set");
	RNA_def_property_struct_type(prop, "Scene");
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
	RNA_def_property_pointer_funcs(prop, NULL, "rna_Scene_set_set", NULL, NULL);
	RNA_def_property_ui_text(prop, "Background Scene", "Background set scene");
	RNA_def_property_update(prop, NC_SCENE | NA_EDITED, "rna_Scene_glsl_update");

	prop = RNA_def_property(srna, "world", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "World", "World used for rendering the scene");
	RNA_def_property_update(prop, NC_SCENE | ND_WORLD, "rna_Scene_glsl_update");

	prop = RNA_def_property(srna, "cursor_location", PROP_FLOAT, PROP_XYZ_LENGTH);
	RNA_def_property_float_sdna(prop, NULL, "cursor");
	RNA_def_property_ui_text(prop, "Cursor Location", "3D cursor location");
	RNA_def_property_ui_range(prop, -10000.0, 10000.0, 10, 4);
	RNA_def_property_update(prop, NC_WINDOW, NULL);

	/* Bases/Objects */
	prop = RNA_def_property(srna, "object_bases", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "base", NULL);
	RNA_def_property_struct_type(prop, "ObjectBase");
	RNA_def_property_ui_text(prop, "Bases", "");
	RNA_def_property_collection_funcs(prop, NULL, NULL, NULL, NULL, NULL, NULL,
	                                  "rna_Scene_object_bases_lookup_string", NULL);
	rna_def_scene_bases(brna, prop);

	prop = RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "base", NULL);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_ui_text(prop, "Objects", "");
	RNA_def_property_collection_funcs(prop, NULL, NULL, NULL, "rna_Scene_objects_get", NULL, NULL, NULL, NULL);
	rna_def_scene_objects(brna, prop);

	/* Layers */
	prop = RNA_def_property(srna, "layers", PROP_BOOLEAN, PROP_LAYER_MEMBER);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_boolean_sdna(prop, NULL, "lay", 1);
	RNA_def_property_array(prop, 20);
	RNA_def_property_boolean_funcs(prop, NULL, "rna_Scene_layer_set");
	RNA_def_property_ui_text(prop, "Layers", "Visible layers - Shift-Click/Drag to select multiple layers");
	RNA_def_property_update(prop, NC_SCENE | ND_LAYER, "rna_Scene_layer_update");

	/* active layer */
	prop = RNA_def_property(srna, "active_layer", PROP_INT, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
	RNA_def_property_int_funcs(prop, "rna_Scene_active_layer_get", NULL, NULL);
	RNA_def_property_ui_text(prop, "Active Layer", "Active scene layer index");

	/* Readonly Properties */

	/* Rigid Body Simulation */
	prop = RNA_def_property(srna, "rigidbody_world", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "rigidbody_world");
	RNA_def_property_struct_type(prop, "RigidBodyWorld");
	RNA_def_property_ui_text(prop, "Rigid Body World", "");
	RNA_def_property_update(prop, NC_SCENE, NULL);

	/* Tool Settings */
	prop = RNA_def_property(srna, "tool_settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "toolsettings");
	RNA_def_property_struct_type(prop, "ToolSettings");
	RNA_def_property_ui_text(prop, "Tool Settings", "");

	/* Unit Settings */
	prop = RNA_def_property(srna, "unit_settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_pointer_sdna(prop, NULL, "unit");
	RNA_def_property_struct_type(prop, "UnitSettings");
	RNA_def_property_ui_text(prop, "Unit Settings", "Unit editing settings");

	/* Safe Areas */
	prop = RNA_def_property(srna, "safe_areas", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "safe_areas");
	RNA_def_property_flag(prop, PROP_NEVER_NULL);
	RNA_def_property_struct_type(prop, "DisplaySafeAreas");
	RNA_def_property_ui_text(prop, "Safe Areas", "");

	/* Statistics */
	func = RNA_def_function(srna, "statistics", "ED_info_stats_string");
	parm = RNA_def_string(func, "statistics", NULL, 0, "Statistics", "");
	RNA_def_function_return(func, parm);

	/* Transform Orientations */
	prop = RNA_def_property(srna, "orientations", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "transform_spaces", NULL);
	RNA_def_property_struct_type(prop, "TransformOrientation");
	RNA_def_property_ui_text(prop, "Transform Orientations", "");

	/* color management */
	prop = RNA_def_property(srna, "view_settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "view_settings");
	RNA_def_property_struct_type(prop, "ColorManagedViewSettings");
	RNA_def_property_ui_text(prop, "View Settings", "Color management settings applied on image before saving");

	prop = RNA_def_property(srna, "display_settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "display_settings");
	RNA_def_property_struct_type(prop, "ColorManagedDisplaySettings");
	RNA_def_property_ui_text(prop, "Display Settings", "Settings of device saved image would be displayed on");

	/* Nestled Data  */
	rna_def_tool_settings(brna);
	rna_def_statvis(brna);
	rna_def_unit_settings(brna);
	rna_def_transform_orientation(brna);
	rna_def_display_safe_areas(brna);

	/* Scene API */
	RNA_api_scene(srna);
}

#endif
