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

/** \file blender/makesrna/intern/rna_world.c
 *  \ingroup RNA
 */


#include <float.h>
#include <stdlib.h>

#include "RNA_define.h"

#include "rna_internal.h"

#include "DNA_material_types.h"
#include "DNA_texture_types.h"
#include "DNA_world_types.h"

#include "WM_types.h"

#ifdef RNA_RUNTIME

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_main.h"
#include "BKE_texture.h"

#include "WM_api.h"

static void rna_World_mtex_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	World *wo = (World *)ptr->data;
	rna_iterator_array_begin(iter, (void *)wo->mtex, sizeof(MTex *), MAX_MTEX, 0, NULL);
}

static PointerRNA rna_World_active_texture_get(PointerRNA *ptr)
{
	World *wo = (World *)ptr->data;
	Tex *tex;

	tex = give_current_world_texture(wo);
	return rna_pointer_inherit_refine(ptr, &RNA_Texture, tex);
}

static void rna_World_active_texture_set(PointerRNA *ptr, PointerRNA value)
{
	World *wo = (World *)ptr->data;

	set_current_world_texture(wo, value.data);
}

static void rna_World_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	World *wo = ptr->id.data;

	WM_main_add_notifier(NC_WORLD | ND_WORLD, wo);
}

#if 0
static void rna_World_draw_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	World *wo = ptr->id.data;

	WM_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, wo);
}
#endif

static void rna_World_draw_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	World *wo = ptr->id.data;

	WM_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, wo);
	WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

#else

static void rna_def_world_mtex(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static const EnumPropertyItem texco_items[] = {
		{TEXCO_VIEW, "VIEW", 0, "View", "Use view vector for the texture coordinates"},
		{TEXCO_GLOB, "GLOBAL", 0, "Global", "Use global coordinates for the texture coordinates (interior mist)"},
		{TEXCO_ANGMAP, "ANGMAP", 0, "AngMap", "Use 360 degree angular coordinates, e.g. for spherical light probes"},
		{TEXCO_H_SPHEREMAP, "SPHERE", 0, "Sphere", "For 360 degree panorama sky, spherical mapped, only top half"},
		{TEXCO_EQUIRECTMAP, "EQUIRECT", 0, "Equirectangular", "For 360 degree panorama sky, equirectangular mapping"},
		{TEXCO_H_TUBEMAP, "TUBE", 0, "Tube", "For 360 degree panorama sky, cylindrical mapped, only top half"},
		{TEXCO_OBJECT, "OBJECT", 0, "Object", "Use linked object's coordinates for texture coordinates"},
		{0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "WorldTextureSlot", "TextureSlot");
	RNA_def_struct_sdna(srna, "MTex");
	RNA_def_struct_ui_text(srna, "World Texture Slot", "Texture slot for textures in a World data-block");

	/* map to */
	prop = RNA_def_property(srna, "use_map_blend", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mapto", WOMAP_BLEND);
	RNA_def_property_ui_text(prop, "Blend", "Affect the color progression of the background");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "use_map_horizon", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mapto", WOMAP_HORIZ);
	RNA_def_property_ui_text(prop, "Horizon", "Affect the color of the horizon");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "use_map_zenith_up", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mapto", WOMAP_ZENUP);
	RNA_def_property_ui_text(prop, "Zenith Up", "Affect the color of the zenith above");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "use_map_zenith_down", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "mapto", WOMAP_ZENDOWN);
	RNA_def_property_ui_text(prop, "Zenith Down", "Affect the color of the zenith below");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "texture_coords", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "texco");
	RNA_def_property_enum_items(prop, texco_items);
	RNA_def_property_ui_text(prop, "Texture Coordinates",
	                         "Texture coordinates used to map the texture onto the background");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "object", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "object");
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Object", "Object to use for mapping with Object texture coordinates");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "blend_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "blendfac");
	RNA_def_property_ui_range(prop, 0, 1, 10, 3);
	RNA_def_property_ui_text(prop, "Blend Factor", "Amount texture affects color progression of the background");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "horizon_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "colfac");
	RNA_def_property_ui_range(prop, 0, 1, 10, 3);
	RNA_def_property_ui_text(prop, "Horizon Factor", "Amount texture affects color of the horizon");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "zenith_up_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "zenupfac");
	RNA_def_property_ui_range(prop, 0, 1, 10, 3);
	RNA_def_property_ui_text(prop, "Zenith Up Factor", "Amount texture affects color of the zenith above");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "zenith_down_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "zendownfac");
	RNA_def_property_ui_range(prop, 0, 1, 10, 3);
	RNA_def_property_ui_text(prop, "Zenith Down Factor", "Amount texture affects color of the zenith below");
	RNA_def_property_update(prop, 0, "rna_World_update");
}

void RNA_def_world(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "World", "ID");
	RNA_def_struct_ui_text(srna, "World",
	                       "World data-block describing the environment and ambient lighting of a scene");
	RNA_def_struct_ui_icon(srna, ICON_WORLD_DATA);

	rna_def_mtex_common(brna, srna, "rna_World_mtex_begin", "rna_World_active_texture_get",
	                    "rna_World_active_texture_set", NULL, "WorldTextureSlot", "WorldTextureSlots",
	                    "rna_World_update", "rna_World_update");

	/* colors */
	prop = RNA_def_property(srna, "horizon_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "horr");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Horizon Color", "Color at the horizon");
	/* RNA_def_property_update(prop, 0, "rna_World_update"); */
	/* render-only uses this */
	RNA_def_property_update(prop, 0, "rna_World_draw_update");

	prop = RNA_def_property(srna, "zenith_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "zenr");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Zenith Color", "Color at the zenith");
	RNA_def_property_update(prop, 0, "rna_World_draw_update");

	prop = RNA_def_property(srna, "ambient_color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "ambr");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Ambient Color", "Ambient color of the world");
	RNA_def_property_update(prop, 0, "rna_World_draw_update");

	/* exp, range */
	prop = RNA_def_property(srna, "exposure", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "exp");
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_ui_text(prop, "Exposure", "Amount of exponential color correction for light");
	RNA_def_property_update(prop, 0, "rna_World_update");

	prop = RNA_def_property(srna, "color_range", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "range");
	RNA_def_property_range(prop, 0.2, 5.0);
	RNA_def_property_ui_text(prop, "Range", "The color range that will be mapped to 0-1");
	RNA_def_property_update(prop, 0, "rna_World_update");

	/* sky type */
	prop = RNA_def_property(srna, "use_sky_blend", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "skytype", WO_SKYBLEND);
	RNA_def_property_ui_text(prop, "Blend Sky", "Render background with natural progression from horizon to zenith");
	RNA_def_property_update(prop, NC_WORLD | ND_WORLD_DRAW, "rna_World_update");

	prop = RNA_def_property(srna, "use_sky_paper", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "skytype", WO_SKYPAPER);
	RNA_def_property_ui_text(prop, "Paper Sky", "Flatten blend or texture coordinates");
	RNA_def_property_update(prop, NC_WORLD | ND_WORLD_DRAW, "rna_World_update");

	prop = RNA_def_property(srna, "use_sky_real", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "skytype", WO_SKYREAL);
	RNA_def_property_ui_text(prop, "Real Sky", "Render background with a real horizon, relative to the camera angle");
	RNA_def_property_update(prop, NC_WORLD | ND_WORLD_DRAW, "rna_World_update");

	/* nested structs */

	rna_def_world_mtex(brna);
}

#endif
