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

/** \file blender/blenkernel/intern/lamp.c
 *  \ingroup bke
 */

#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_colortools.h"
#include "BKE_icons.h"
#include "BKE_lamp.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"

void BKE_lamp_init(Lamp *la)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(la, id));

	la->r = la->g = la->b = la->k = 1.0f;
	la->haint = la->energy = 1.0f;
	la->dist = 25.0f;
	la->spotsize = DEG2RADF(45.0f);
	la->spotblend = 0.15f;
	la->att2 = 1.0f;
	la->mode = LA_SHAD_BUF;
	la->bufsize = 512;
	la->clipsta = 0.5f;
	la->clipend = 40.0f;
	la->samp = 3;
	la->bias = 1.0f;
	la->soft = 3.0f;
	la->compressthresh = 0.05f;
	la->ray_samp = la->ray_sampy = la->ray_sampz = 1;
	la->area_size = la->area_sizey = la->area_sizez = 0.1f;
	la->buffers = 1;
	la->buftype = LA_SHADBUF_HALFWAY;
	la->ray_samp_method = LA_SAMP_HALTON;
	la->adapt_thresh = 0.001f;
	la->preview = NULL;
	la->falloff_type = LA_FALLOFF_INVSQUARE;
	la->coeff_const = 1.0f;
	la->coeff_lin = 0.0f;
	la->coeff_quad = 0.0f;
	la->curfalloff = curvemapping_add(1, 0.0f, 1.0f, 1.0f, 0.0f);
	la->sun_effect_type = 0;
	la->horizon_brightness = 1.0;
	la->spread = 1.0;
	la->sun_brightness = 1.0;
	la->sun_size = 1.0;
	la->backscattered_light = 1.0f;
	la->atm_turbidity = 2.0f;
	la->atm_inscattering_factor = 1.0f;
	la->atm_extinction_factor = 1.0f;
	la->atm_distance_factor = 1.0f;
	la->sun_intensity = 1.0f;
	la->skyblendtype = MA_RAMP_ADD;
	la->skyblendfac = 1.0f;
	la->sky_colorspace = BLI_XYZ_CIE;
	la->sky_exposure = 1.0f;
	la->shadow_frustum_size = 10.0f;

	curvemapping_initialize(la->curfalloff);
}

Lamp *BKE_lamp_add(Main *bmain, const char *name)
{
	Lamp *la;

	la =  BKE_libblock_alloc(bmain, ID_LA, name, 0);

	BKE_lamp_init(la);

	return la;
}

/**
 * Only copy internal data of Lamp ID from source to already allocated/initialized destination.
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_lamp_copy_data(Main *bmain, Lamp *la_dst, const Lamp *la_src, const int flag)
{
	for (int a = 0; a < MAX_MTEX; a++) {
		if (la_dst->mtex[a]) {
			la_dst->mtex[a] = MEM_mallocN(sizeof(*la_dst->mtex[a]), __func__);
			*la_dst->mtex[a] = *la_src->mtex[a];
		}
	}

	la_dst->curfalloff = curvemapping_copy(la_src->curfalloff);

	if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
		BKE_previewimg_id_copy(&la_dst->id, &la_src->id);
	}
	else {
		la_dst->preview = NULL;
	}
}

Lamp *BKE_lamp_copy(Main *bmain, const Lamp *la)
{
	Lamp *la_copy;
	BKE_id_copy_ex(bmain, &la->id, (ID **)&la_copy, 0, false);
	return la_copy;
}

Lamp *BKE_lamp_localize(Lamp *la)
{
	/* TODO(bastien): Replace with something like:
	 *
	 *   Lamp *la_copy;
	 *   BKE_id_copy_ex(bmain, &la->id, (ID **)&la_copy,
	 *                  LIB_ID_COPY_NO_MAIN | LIB_ID_COPY_NO_PREVIEW | LIB_ID_COPY_NO_USER_REFCOUNT,
	 *                  false);
	 *   return la_copy;
	 *
	 * NOTE: Only possible once nested node trees are fully converted to that too. */

	Lamp *lan;
	int a;

	lan = BKE_libblock_copy_nolib(&la->id, false);

	for (a = 0; a < MAX_MTEX; a++) {
		if (lan->mtex[a]) {
			lan->mtex[a] = MEM_mallocN(sizeof(MTex), __func__);
			memcpy(lan->mtex[a], la->mtex[a], sizeof(MTex));
		}
	}

	lan->curfalloff = curvemapping_copy(la->curfalloff);

	lan->preview = NULL;

	return lan;
}

void BKE_lamp_make_local(Main *bmain, Lamp *la, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &la->id, true, lib_local);
}

void BKE_lamp_free(Lamp *la)
{
	int a;

	for (a = 0; a < MAX_MTEX; a++) {
		MEM_SAFE_FREE(la->mtex[a]);
	}

	curvemapping_free(la->curfalloff);

	/* is no lib link block, but lamp extension */

	BKE_previewimg_free(&la->preview);
	BKE_icon_id_delete(&la->id);
	la->id.icon_id = 0;
}

/* Calculate all drivers for lamps, see material_drivers_update for why this is a bad hack */

void lamp_drivers_update(Scene *scene, Lamp *la, float ctime)
{
	/* Prevent infinite recursion by checking (and tagging the lamp) as having been visited already
	 * (see BKE_scene_update_tagged()). This assumes la->id.tag & LIB_TAG_DOIT isn't set by anything else
	 * in the meantime... [#32017] */
	if (la->id.tag & LIB_TAG_DOIT)
		return;

	la->id.tag |= LIB_TAG_DOIT;

	la->id.tag &= ~LIB_TAG_DOIT;
}
