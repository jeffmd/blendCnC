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

/** \file blender/blenloader/intern/versioning_defaults.c
 *  \ingroup blenloader
 */

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_mesh_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"

#include "BKE_library.h"
#include "BKE_main.h"

#include "BLO_readfile.h"


/**
 * Override values in in-memory startup.blend, avoids resaving for small changes.
 */
void BLO_update_defaults_userpref_blend(void)
{
	/* defaults from T37518 */

	U.uiflag |= USER_DEPTH_CURSOR;
	U.uiflag |= USER_QUIT_PROMPT;
	U.uiflag |= USER_CONTINUOUS_MOUSE;

	/* See T45301 */
	U.uiflag |= USER_LOCK_CURSOR_ADJUST;

	U.versions = 1;
	U.savetime = 2;

	/* Keep this a very small, non-zero number so zero-alpha doesn't mask out objects behind it.
	 * but take care since some hardware has driver bugs here (T46962).
	 * Further hardware workarounds should be made in gpu_extensions.c */
	U.glalphaclip = (1.0f / 255);

	/* default so DPI is detected automatically */
	U.dpi = 0;
	U.ui_scale = 1.0f;

#ifdef WITH_PYTHON_SECURITY
	/* use alternative setting for security nuts
	 * otherwise we'd need to patch the binary blob - startup.blend.c */
	U.flag |= USER_SCRIPT_AUTOEXEC_DISABLE;
#else
	U.flag &= ~USER_SCRIPT_AUTOEXEC_DISABLE;
#endif
}

/**
 * Update defaults in startup.blend, without having to save and embed the file.
 * This function can be emptied each time the startup.blend is updated. */
void BLO_update_defaults_startup_blend(Main *bmain)
{
	for (bScreen *screen = bmain->screen.first; screen; screen = screen->id.next) {
		ScrArea *area;
		for (area = screen->areabase.first; area; area = area->next) {
			ARegion *ar;

			for (ar = area->regionbase.first; ar; ar = ar->next) {
				/* Remove all stored panels, we want to use defaults (order, open/closed) as defined by UI code here! */
				BLI_freelistN(&ar->panels);

				/* some toolbars have been saved as initialized,
				 * we don't want them to have odd zoom-level or scrolling set, see: T47047 */
				if (ELEM(ar->regiontype, RGN_TYPE_UI, RGN_TYPE_TOOLS, RGN_TYPE_TOOL_PROPS)) {
					ar->v2d.flag &= ~V2D_IS_INITIALISED;
				}
			}
		}
	}

	for (Mesh *me = bmain->mesh.first; me; me = me->id.next) {
		me->smoothresh = DEG2RADF(180.0f);
		me->flag &= ~ME_TWOSIDED;
	}

	for (Material *mat = bmain->mat.first; mat; mat = mat->id.next) {
		mat->line_col[0] = mat->line_col[1] = mat->line_col[2] = 0.0f;
		mat->line_col[3] = 1.0f;
	}

	{
		Object *ob;

		ob = (Object *)BKE_libblock_find_name(bmain, ID_OB, "Camera");
		if (ob) {
			ob->rot[1] = 0.0f;
		}
	}

}
