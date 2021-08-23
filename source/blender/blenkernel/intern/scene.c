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

/** \file blender/blenkernel/intern/scene.c
 *  \ingroup bke
 */


#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_group_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_curve_types.h"
#include "DNA_world_types.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_callbacks.h"
#include "BLI_string.h"
#include "BLI_string_utils.h"
#include "BLI_threads.h"
#include "BLI_task.h"

#include "BLT_translation.h"

#include "BKE_cachefile.h"
#include "BKE_colortools.h"
#include "BKE_editmesh.h"
#include "BKE_global.h"
#include "BKE_group.h"
#include "BKE_icons.h"
#include "BKE_idprop.h"
#include "BKE_image.h"
#include "BKE_library.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_unit.h"
#include "BKE_world.h"

#include "PIL_time.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"

#include "bmesh.h"

/* flag -- copying options (see BKE_library.h's LIB_ID_COPY_... flags for more). */
ToolSettings *BKE_toolsettings_copy(ToolSettings *toolsettings, const int flag)
{
	if (toolsettings == NULL) {
		return NULL;
	}
	ToolSettings *ts = MEM_dupallocN(toolsettings);

	return ts;
}

void BKE_toolsettings_free(ToolSettings *toolsettings)
{
	if (toolsettings == NULL) {
		return;
	}

	MEM_freeN(toolsettings);
}

/**
 * Only copy internal data of Scene ID from source to already allocated/initialized destination.
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_scene_copy_data(Main *bmain, Scene *sce_dst, const Scene *sce_src, const int flag)
{
	/* We never handle usercount here for own data. */
	const int flag_subdata = flag | LIB_ID_CREATE_NO_USER_REFCOUNT;

	sce_dst->obedit = NULL;
	sce_dst->stats = NULL;
	sce_dst->fps_info = NULL;

	BLI_duplicatelist(&(sce_dst->base), &(sce_src->base));
	for (Base *base_dst = sce_dst->base.first, *base_src = sce_src->base.first;
	     base_dst;
	     base_dst = base_dst->next, base_src = base_src->next)
	{
		if (base_src == sce_src->basact) {
			sce_dst->basact = base_dst;
		}
	}

	BLI_duplicatelist(&(sce_dst->transform_spaces), &(sce_src->transform_spaces));

	if (sce_src->rigidbody_world) {
		sce_dst->rigidbody_world = BKE_rigidbody_world_copy(sce_src->rigidbody_world, flag_subdata);
	}

	/* copy color management settings */
	BKE_color_managed_display_settings_copy(&sce_dst->display_settings, &sce_src->display_settings);
	BKE_color_managed_view_settings_copy(&sce_dst->view_settings, &sce_src->view_settings);
	BKE_color_managed_colorspace_settings_copy(&sce_dst->sequencer_colorspace_settings, &sce_src->sequencer_colorspace_settings);

	/* tool settings */
	sce_dst->toolsettings = BKE_toolsettings_copy(sce_dst->toolsettings, flag_subdata);


	if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
		BKE_previewimg_id_copy(&sce_dst->id, &sce_src->id);
	}
	else {
		sce_dst->preview = NULL;
	}
}

Scene *BKE_scene_copy(Main *bmain, Scene *sce, int type)
{
	Scene *sce_copy;

	/* TODO this should/could most likely be replaced by call to more generic code at some point...
	 * But for now, let's keep it well isolated here. */
	if (type == SCE_COPY_EMPTY) {

		sce_copy = BKE_scene_add(bmain, sce->id.name + 2);

		sce_copy->unit = sce->unit;
		sce_copy->physics_settings = sce->physics_settings;

		if (sce->id.properties)
			sce_copy->id.properties = IDP_CopyProperty(sce->id.properties);

		MEM_freeN(sce_copy->toolsettings);

		/* copy color management settings */
		BKE_color_managed_display_settings_copy(&sce_copy->display_settings, &sce->display_settings);
		BKE_color_managed_view_settings_copy(&sce_copy->view_settings, &sce->view_settings);
		BKE_color_managed_colorspace_settings_copy(&sce_copy->sequencer_colorspace_settings, &sce->sequencer_colorspace_settings);

		/* tool settings */
		sce_copy->toolsettings = BKE_toolsettings_copy(sce->toolsettings, 0);

		sce_copy->preview = NULL;

		return sce_copy;
	}
	else {
		BKE_id_copy_ex(bmain, (ID *)sce, (ID **)&sce_copy, LIB_ID_COPY_ACTIONS, false);
		id_us_min(&sce_copy->id);
		id_us_ensure_real(&sce_copy->id);

		/* Extra actions, most notably SCE_FULL_COPY also duplicates several 'children' datablocks... */

		if (type == SCE_COPY_FULL) {

			/* Full copy of world (included animations) */
			if (sce_copy->world) {
				id_us_min(&sce_copy->world->id);
				BKE_id_copy_ex(bmain, (ID *)sce_copy->world, (ID **)&sce_copy->world, LIB_ID_COPY_ACTIONS, false);
			}

		}

		/* NOTE: part of SCE_COPY_LINK_DATA and SCE_COPY_FULL operations
		 * are done outside of blenkernel with ED_objects_single_users! */

		/*  camera */
		/* XXX This is most certainly useless? Object have not yet been duplicated... */
		if (ELEM(type, SCE_COPY_LINK_DATA, SCE_COPY_FULL)) {
			ID_NEW_REMAP(sce_copy->camera);
		}

		return sce_copy;
	}
}

void BKE_scene_groups_relink(Scene *sce)
{
	if (sce->rigidbody_world)
		BKE_rigidbody_world_groups_relink(sce->rigidbody_world);
}

void BKE_scene_make_local(Main *bmain, Scene *sce, const bool lib_local)
{
	/* For now should work, may need more work though to support all possible corner cases
	 * (also scene_copy probably needs some love). */
	BKE_id_make_local_generic(bmain, &sce->id, true, lib_local);
}

/** Free (or release) any data used by this scene (does not free the scene itself). */
void BKE_scene_free(Scene *sce)
{
	sce->basact = NULL;
	BLI_freelistN(&sce->base);

	if (sce->rigidbody_world) {
		BKE_rigidbody_free_world(sce->rigidbody_world);
		sce->rigidbody_world = NULL;
	}

	BLI_freelistN(&sce->transform_spaces);

	BKE_toolsettings_free(sce->toolsettings);
	sce->toolsettings = NULL;


	MEM_SAFE_FREE(sce->stats);
	MEM_SAFE_FREE(sce->fps_info);

	BKE_color_managed_view_settings_free(&sce->view_settings);

	BKE_previewimg_free(&sce->preview);
}

void BKE_scene_init(Scene *sce)
{
	const char *colorspace_name;

	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(sce, id));

	sce->lay = sce->layact = 1;

	sce->toolsettings = MEM_callocN(sizeof(struct ToolSettings), "Tool Settings Struct");
	sce->toolsettings->doublimit = 0.001;
	sce->toolsettings->select_thresh = 0.01f;

	sce->toolsettings->selectmode = SCE_SELECT_VERTEX;
	sce->toolsettings->normalsize = 0.1;

	sce->toolsettings->snap_node_mode = SCE_SNAP_MODE_GRID;


	sce->toolsettings->statvis.overhang_axis = OB_NEGZ;
	sce->toolsettings->statvis.overhang_min = 0;
	sce->toolsettings->statvis.overhang_max = DEG2RADF(45.0f);
	sce->toolsettings->statvis.thickness_max = 0.1f;
	sce->toolsettings->statvis.thickness_samples = 1;
	sce->toolsettings->statvis.distort_min = DEG2RADF(5.0f);
	sce->toolsettings->statvis.distort_max = DEG2RADF(45.0f);

	sce->toolsettings->statvis.sharp_min = DEG2RADF(90.0f);
	sce->toolsettings->statvis.sharp_max = DEG2RADF(180.0f);

	sce->toolsettings->proportional_size = 1.0f;

	sce->physics_settings.gravity[0] = 0.0f;
	sce->physics_settings.gravity[1] = 0.0f;
	sce->physics_settings.gravity[2] = -9.81f;
	sce->physics_settings.flag = PHYS_GLOBAL_GRAVITY;

	sce->unit.scale_length = 1.0f;


	/* color management */
	colorspace_name = IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_SEQUENCER);

	BKE_color_managed_display_settings_init(&sce->display_settings);
	BKE_color_managed_view_settings_init(&sce->view_settings,
	                                     &sce->display_settings);
	BLI_strncpy(sce->sequencer_colorspace_settings.name, colorspace_name,
	            sizeof(sce->sequencer_colorspace_settings.name));

	/* Safe Areas */
	copy_v2_fl2(sce->safe_areas.title, 3.5f / 100.0f, 3.5f / 100.0f);
	copy_v2_fl2(sce->safe_areas.action, 10.0f / 100.0f, 5.0f / 100.0f);
	copy_v2_fl2(sce->safe_areas.title_center, 17.5f / 100.0f, 5.0f / 100.0f);
	copy_v2_fl2(sce->safe_areas.action_center, 15.0f / 100.0f, 5.0f / 100.0f);

	sce->preview = NULL;

}

Scene *BKE_scene_add(Main *bmain, const char *name)
{
	Scene *sce;

	sce = BKE_libblock_alloc(bmain, ID_SCE, name, 0);
	id_us_min(&sce->id);
	id_us_ensure_real(&sce->id);

	BKE_scene_init(sce);

	return sce;
}

Base *BKE_scene_base_find_by_name(struct Scene *scene, const char *name)
{
	Base *base;

	for (base = scene->base.first; base; base = base->next) {
		if (STREQ(base->object->id.name + 2, name)) {
			break;
		}
	}

	return base;
}

Base *BKE_scene_base_find(Scene *scene, Object *ob)
{
	return BLI_findptr(&scene->base, ob, offsetof(Base, object));
}

/**
 * Sets the active scene, mainly used when running in background mode (``--scene`` command line argument).
 * This is also called to set the scene directly, bypassing windowing code.
 * Otherwise #ED_screen_set_scene is used when changing scenes by the user.
 */
void BKE_scene_set_background(Main *bmain, Scene *scene)
{
	Base *base;
	Object *ob;
	Group *group;
	GroupObject *go;
	int flag;

	/* check for cyclic sets, for reading old files but also for definite security (py?) */
	BKE_scene_validate_setscene(bmain, scene);

	/* can happen when switching modes in other scenes */
	if (scene->obedit && !(scene->obedit->mode & OB_MODE_EDIT))
		scene->obedit = NULL;

	/* deselect objects (for dataselect) */
	for (ob = bmain->object.first; ob; ob = ob->id.next)
		ob->flag &= ~(SELECT | OB_FROMGROUP);

	/* group flags again */
	for (group = bmain->group.first; group; group = group->id.next) {
		for (go = group->gobject.first; go; go = go->next) {
			if (go->ob) {
				go->ob->flag |= OB_FROMGROUP;
			}
		}
	}

	/* copy layers and flags from bases to objects */
	for (base = scene->base.first; base; base = base->next) {
		ob = base->object;
		ob->lay = base->lay;

		/* group patch... */
		base->flag &= ~(OB_FROMGROUP);
		flag = ob->flag & (OB_FROMGROUP);
		base->flag |= flag;

		/* not too nice... for recovering objects with lost data */
		//if (ob->pose == NULL) base->flag &= ~OB_POSEMODE;
		ob->flag = base->flag;
	}
	/* no full animation update, this to enable render code to work (render code calls own animation updates) */
}

/* called from creator_args.c */
Scene *BKE_scene_set_name(Main *bmain, const char *name)
{
	Scene *sce = (Scene *)BKE_libblock_find_name(bmain, ID_SCE, name);
	if (sce) {
		BKE_scene_set_background(bmain, sce);
		printf("Scene switch for render: '%s' in file: '%s'\n", name, BKE_main_blendfile_path(bmain));
		return sce;
	}

	printf("Can't find scene: '%s' in file: '%s'\n", name, BKE_main_blendfile_path(bmain));
	return NULL;
}

/* Used by metaballs, return *all* objects (including duplis) existing in the scene (including scene's sets) */
int BKE_scene_base_iter_next(Main *bmain, SceneBaseIter *iter,
                             Scene **scene, int val, Base **base, Object **ob)
{
	bool run_again = true;

	/* init */
	if (val == 0) {
		iter->phase = F_START;
		iter->dupob = NULL;
		iter->duplilist = NULL;
		iter->dupli_refob = NULL;
	}
	else {
		/* run_again is set when a duplilist has been ended */
		while (run_again) {
			run_again = false;

			/* the first base */
			if (iter->phase == F_START) {
				*base = (*scene)->base.first;
				if (*base) {
					*ob = (*base)->object;
					iter->phase = F_SCENE;
				}
				else {
					/* exception: empty scene */
					while ((*scene)->set) {
						(*scene) = (*scene)->set;
						if ((*scene)->base.first) {
							*base = (*scene)->base.first;
							*ob = (*base)->object;
							iter->phase = F_SCENE;
							break;
						}
					}
				}
			}
			else {
				if (*base && iter->phase != F_DUPLI) {
					*base = (*base)->next;
					if (*base) {
						*ob = (*base)->object;
					}
					else {
						if (iter->phase == F_SCENE) {
							/* (*scene) is finished, now do the set */
							while ((*scene)->set) {
								(*scene) = (*scene)->set;
								if ((*scene)->base.first) {
									*base = (*scene)->base.first;
									*ob = (*base)->object;
									break;
								}
							}
						}
					}
				}
			}

			if (*base == NULL) {
				iter->phase = F_START;
			}
		}
	}

#if 0
	if (ob && *ob) {
		printf("Scene: '%s', '%s'\n", (*scene)->id.name + 2, (*ob)->id.name + 2);
	}
#endif

	return iter->phase;
}

Object *BKE_scene_camera_find(Scene *sc)
{
	Base *base;

	for (base = sc->base.first; base; base = base->next)
		if (base->object->type == OB_CAMERA)
			return base->object;

	return NULL;
}


int BKE_scene_camera_switch_update(Scene *scene)
{
	return 0;
}

Base *BKE_scene_base_add(Scene *sce, Object *ob)
{
	Base *b = MEM_callocN(sizeof(*b), __func__);
	BLI_addhead(&sce->base, b);

	b->object = ob;
	b->flag = ob->flag;
	b->lay = ob->lay;

	return b;
}

void BKE_scene_base_unlink(Scene *sce, Base *base)
{
	/* remove rigid body constraint from world before removing object */
	if (base->object->rigidbody_constraint)
		BKE_rigidbody_remove_constraint(sce, base->object);
	/* remove rigid body object from world before removing object */
	if (base->object->rigidbody_object)
		BKE_rigidbody_remove_object(sce, base->object);

	BLI_remlink(&sce->base, base);
	if (sce->basact == base)
		sce->basact = NULL;
}

void BKE_scene_base_deselect_all(Scene *sce)
{
	Base *b;

	for (b = sce->base.first; b; b = b->next) {
		b->flag &= ~SELECT;
		b->object->flag = b->flag;
	}
}

void BKE_scene_base_select(Scene *sce, Base *selbase)
{
	selbase->flag |= SELECT;
	selbase->object->flag = selbase->flag;

	sce->basact = selbase;
}

/* checks for cycle, returns 1 if it's all OK */
bool BKE_scene_validate_setscene(Main *bmain, Scene *sce)
{
	Scene *sce_iter;
	int a, totscene;

	if (sce->set == NULL) return true;
	totscene = BLI_listbase_count(&bmain->scene);

	for (a = 0, sce_iter = sce; sce_iter->set; sce_iter = sce_iter->set, a++) {
		/* more iterations than scenes means we have a cycle */
		if (a > totscene) {
			/* the tested scene gets zero'ed, that's typically current scene */
			sce->set = NULL;
			return false;
		}
	}

	return true;
}

/* That's like really a bummer, because currently animation data for armatures
 * might want to use pose, and pose might be missing on the object.
 * This happens when changing visible layers, which leads to situations when
 * pose is missing or marked for recalc, animation will change it and then
 * object update will restore the pose.
 *
 * This could be solved by the new dependency graph, but for until then we'll
 * do an extra pass on the objects to ensure it's all fine.
 */
#define POSE_ANIMATION_WORKAROUND

/* Used to visualize CPU threads activity during threaded object update,
 * would pollute STDERR with whole bunch of timing information which then
 * could be parsed and nicely visualized.
 */
/* ALWAYS KEEY DISABLED! */
#  undef DETAILED_ANALYSIS_OUTPUT

typedef struct StatisicsEntry {
	struct StatisicsEntry *next, *prev;
	Object *object;
	double start_time;
	double duration;
} StatisicsEntry;

typedef struct ThreadedObjectUpdateState {
	/* TODO(sergey): We might want this to be per-thread object. */
	Main *bmain;
	Scene *scene;
	Scene *scene_parent;
	double base_time;

	int num_threads;

	/* Execution statistics */
	bool has_updated_objects;
	ListBase *statistics;
} ThreadedObjectUpdateState;

static bool check_rendered_viewport_visible(Main *bmain)
{
	wmWindowManager *wm = bmain->wm.first;
	wmWindow *window;
	for (window = wm->windows.first; window != NULL; window = window->next) {
		bScreen *screen = window->screen;
		ScrArea *area;
		for (area = screen->areabase.first; area != NULL; area = area->next) {
			View3D *v3d = area->spacedata.first;
			if (area->spacetype != SPACE_VIEW3D) {
				continue;
			}
			if (v3d->drawtype == OB_RENDER) {
				return true;
			}
		}
	}
	return false;
}

static void prepare_mesh_for_viewport_render(Main *bmain, Scene *scene)
{
	/* This is needed to prepare mesh to be used by the render
	 * engine from the viewport rendering. We do loading here
	 * so all the objects which shares the same mesh datablock
	 * are nicely tagged for update and updated.
	 *
	 * This makes it so viewport render engine doesn't need to
	 * call loading of the edit data for the mesh objects.
	 */

	Object *obedit = scene->obedit;
	if (obedit) {
		Mesh *mesh = obedit->data;
		if ((obedit->type == OB_MESH) &&
		    ((obedit->id.recalc & ID_RECALC_ALL) ||
		     (mesh->id.recalc & ID_RECALC_ALL)))
		{
			if (check_rendered_viewport_visible(bmain)) {
				BMesh *bm = mesh->edit_btmesh->bm;
				BM_mesh_bm_to_me(
				        bmain, bm, mesh,
				        (&(struct BMeshToMeshParams){
				            .calc_object_remap = true,
				        }));
			}
		}
	}
}

void BKE_scene_update_tagged(Main *bmain, Scene *scene)
{
	Scene *sce_iter;

	/* keep this first */
	BLI_callback_exec(bmain, &scene->id, BLI_CB_EVT_SCENE_UPDATE_PRE);

	/* (re-)build dependency graph if needed */
	for (sce_iter = scene; sce_iter; sce_iter = sce_iter->set) {
		/* Uncomment this to check if dependency graph was properly tagged for update. */
	}

	/* flush editing data if needed */
	prepare_mesh_for_viewport_render(bmain, scene);

	/* removed calls to quick_cache, see pointcache.c */

	/* clear "LIB_TAG_DOIT" flag from all materials, to prevent infinite recursion problems later
	 * when trying to find materials with drivers that need evaluating [#32017]
	 */
	BKE_main_id_tag_idcode(bmain, ID_MA, LIB_TAG_DOIT, false);
	BKE_main_id_tag_idcode(bmain, ID_LA, LIB_TAG_DOIT, false);

	/* notify editors and python about recalc */
	BLI_callback_exec(bmain, &scene->id, BLI_CB_EVT_SCENE_UPDATE_POST);

}

/* helper function for the SETLOOPER macro */
Base *_setlooper_base_step(Scene **sce_iter, Base *base)
{
	if (base && base->next) {
		/* common case, step to the next */
		return base->next;
	}
	else if (base == NULL && (*sce_iter)->base.first) {
		/* first time looping, return the scenes first base */
		return (Base *)(*sce_iter)->base.first;
	}
	else {
		/* reached the end, get the next base in the set */
		while ((*sce_iter = (*sce_iter)->set)) {
			base = (Base *)(*sce_iter)->base.first;
			if (base) {
				return base;
			}
		}
	}

	return NULL;
}

void BKE_scene_base_flag_to_objects(struct Scene *scene)
{
	Base *base = scene->base.first;

	while (base) {
		base->object->flag = base->flag;
		base = base->next;
	}
}

void BKE_scene_base_flag_from_objects(struct Scene *scene)
{
	Base *base = scene->base.first;

	while (base) {
		base->flag = base->object->flag;
		base = base->next;
	}
}

void BKE_scene_disable_color_management(Scene *scene)
{
	ColorManagedDisplaySettings *display_settings = &scene->display_settings;
	ColorManagedViewSettings *view_settings = &scene->view_settings;
	const char *view;
	const char *none_display_name;

	none_display_name = IMB_colormanagement_display_get_none_name();

	BLI_strncpy(display_settings->display_device, none_display_name, sizeof(display_settings->display_device));

	view = IMB_colormanagement_view_get_default_name(display_settings->display_device);

	if (view) {
		BLI_strncpy(view_settings->view_transform, view, sizeof(view_settings->view_transform));
	}
}

bool BKE_scene_check_color_management_enabled(const Scene *scene)
{
	return !STREQ(scene->display_settings.display_device, "None");
}

bool BKE_scene_check_rigidbody_active(const Scene *scene)
{
	return scene && scene->rigidbody_world && scene->rigidbody_world->group && !(scene->rigidbody_world->flag & RBW_FLAG_MUTED);
}

/* Apply the needed correction factor to value, based on unit_type (only length-related are affected currently)
 * and unit->scale_length.
 */
double BKE_scene_unit_scale(const UnitSettings *unit, const int unit_type, double value)
{
	if (unit->system == USER_UNIT_NONE) {
		/* Never apply scale_length when not using a unit setting! */
		return value;
	}

	switch (unit_type) {
		case B_UNIT_LENGTH:
			return value * (double)unit->scale_length;
		case B_UNIT_AREA:
			return value * pow(unit->scale_length, 2);
		case B_UNIT_VOLUME:
			return value * pow(unit->scale_length, 3);
		case B_UNIT_MASS:
			return value * pow(unit->scale_length, 3);
		case B_UNIT_CAMERA:  /* *Do not* use scene's unit scale for camera focal lens! See T42026. */
		default:
			return value;
	}
}
