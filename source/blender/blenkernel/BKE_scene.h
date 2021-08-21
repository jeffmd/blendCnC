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
#ifndef __BKE_SCENE_H__
#define __BKE_SCENE_H__

/** \file BKE_scene.h
 *  \ingroup bke
 *  \since March 2001
 *  \author nzc
 */

#ifdef __cplusplus
extern "C" {
#endif

struct Base;
struct Main;
struct Main;
struct Object;
struct Scene;
struct UnitSettings;

#define SCE_COPY_NEW        0
#define SCE_COPY_EMPTY      1
#define SCE_COPY_LINK_OB    2
#define SCE_COPY_LINK_DATA  3
#define SCE_COPY_FULL       4

/* Use as the contents of a 'for' loop: for (SETLOOPER(...)) { ... */
#define SETLOOPER(_sce_basis, _sce_iter, _base)                               \
	_sce_iter = _sce_basis, _base = _setlooper_base_step(&_sce_iter, NULL);   \
	_base;                                                                    \
	_base = _setlooper_base_step(&_sce_iter, _base)

struct Base *_setlooper_base_step(struct Scene **sce_iter, struct Base *base);

void BKE_scene_free(struct Scene *sce);
void BKE_scene_init(struct Scene *sce);
struct Scene *BKE_scene_add(struct Main *bmain, const char *name);

/* base functions */
struct Base *BKE_scene_base_find_by_name(struct Scene *scene, const char *name);
struct Base *BKE_scene_base_find(struct Scene *scene, struct Object *ob);
struct Base *BKE_scene_base_add(struct Scene *sce, struct Object *ob);
void         BKE_scene_base_unlink(struct Scene *sce, struct Base *base);
void         BKE_scene_base_deselect_all(struct Scene *sce);
void         BKE_scene_base_select(struct Scene *sce, struct Base *selbase);

/* Scene base iteration function.
 * Define struct here, so no need to bother with alloc/free it.
 */
typedef struct SceneBaseIter {
	struct ListBase *duplilist;
	struct DupliObject *dupob;
	float omat[4][4];
	struct Object *dupli_refob;
	int phase;
} SceneBaseIter;

int BKE_scene_base_iter_next(struct Main *bmain, struct SceneBaseIter *iter,
                             struct Scene **scene, int val, struct Base **base, struct Object **ob);

void BKE_scene_base_flag_to_objects(struct Scene *scene);
void BKE_scene_base_flag_from_objects(struct Scene *scene);

void BKE_scene_set_background(struct Main *bmain, struct Scene *sce);
struct Scene *BKE_scene_set_name(struct Main *bmain, const char *name);

struct ToolSettings *BKE_toolsettings_copy(struct ToolSettings *toolsettings, const int flag);
void BKE_toolsettings_free(struct ToolSettings *toolsettings);

void BKE_scene_copy_data(struct Main *bmain, struct Scene *sce_dst, const struct Scene *sce_src, const int flag);
struct Scene *BKE_scene_copy(struct Main *bmain, struct Scene *sce, int type);
void BKE_scene_groups_relink(struct Scene *sce);

void BKE_scene_make_local(struct Main *bmain, struct Scene *sce, const bool lib_local);

struct Object *BKE_scene_camera_find(struct Scene *sc);
int BKE_scene_camera_switch_update(struct Scene *scene);

/* checks for cycle, returns 1 if it's all OK */
bool BKE_scene_validate_setscene(struct Main *bmain, struct Scene *sce);

/* **  Scene evaluation ** */
void BKE_scene_update_tagged(struct Main *bmain, struct Scene *sce);

void BKE_scene_disable_color_management(struct Scene *scene);
bool BKE_scene_check_color_management_enabled(const struct Scene *scene);
bool BKE_scene_check_rigidbody_active(const struct Scene *scene);

double BKE_scene_unit_scale(const struct UnitSettings *unit, const int unit_type, double value);

#ifdef __cplusplus
}
#endif

#endif
