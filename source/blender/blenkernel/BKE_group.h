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
#ifndef __BKE_GROUP_H__
#define __BKE_GROUP_H__

/** \file BKE_group.h
 *  \ingroup bke
 *  \since March 2001
 *  \author nzc
 */

struct Base;
struct Group;
struct Main;
struct Object;
struct Scene;

void          BKE_group_free(struct Group *group);
struct Group *BKE_group_add(struct Main *bmain, const char *name);
void          BKE_group_copy_data(struct Main *bmain, struct Group *group_dst, const struct Group *group_src, const int flag);
struct Group *BKE_group_copy(struct Main *bmain, const struct Group *group);
void          BKE_group_make_local(struct Main *bmain, struct Group *group, const bool lib_local);
bool          BKE_group_object_add(struct Group *group, struct Object *ob, struct Scene *scene, struct Base *base);
bool          BKE_group_object_unlink(
        struct Main *bmain, struct Group *group, struct Object *ob, struct Scene *scene, struct Base *base);
struct Group *BKE_group_object_find(struct Main *bmain, struct Group *group, struct Object *ob);
bool          BKE_group_object_exists(struct Group *group, struct Object *ob);
bool          BKE_group_object_cyclic_check(struct Main *bmain, struct Object *object, struct Group *group);
bool          BKE_group_is_animated(struct Group *group, struct Object *parent);

void          BKE_group_handle_recalc_and_update(
        struct Main *bmain, 
        struct Scene *scene, struct Object *parent, struct Group *group);

#endif  /* __BKE_GROUP_H__ */
