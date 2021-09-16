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
 * The Original Code is Copyright (C) 2014 by Blender Foundation.
 * All rights reserved.
 */

/** \file blender/blenkernel/intern/library_query.c
 *  \ingroup bke
 */

#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_camera_types.h"
#include "DNA_curve_types.h"
#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_text_types.h"
#include "DNA_vfont_types.h"
#include "DNA_world_types.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_ghash.h"
#include "BLI_linklist_stack.h"

#include "BKE_idprop.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_main.h"
#include "BKE_modifier.h"
#include "BKE_rigidbody.h"


#define FOREACH_FINALIZE _finalize
#define FOREACH_FINALIZE_VOID \
	if (0) { goto FOREACH_FINALIZE; } \
	FOREACH_FINALIZE: ((void)0)

#define FOREACH_CALLBACK_INVOKE_ID_PP(_data, id_pp, _cb_flag) \
	CHECK_TYPE(id_pp, ID **); \
	if (!((_data)->status & IDWALK_STOP)) { \
		const int _flag = (_data)->flag; \
		ID *old_id = *(id_pp); \
		const int callback_return = (_data)->callback((_data)->user_data, (_data)->self_id, id_pp, _cb_flag | (_data)->cb_flag); \
		if (_flag & IDWALK_READONLY) { \
			BLI_assert(*(id_pp) == old_id); \
		} \
		if (old_id && (_flag & IDWALK_RECURSE)) { \
			if (!BLI_gset_haskey((_data)->ids_handled, old_id)) { \
				BLI_gset_add((_data)->ids_handled, old_id); \
				if (!(callback_return & IDWALK_RET_STOP_RECURSION)) { \
					BLI_LINKSTACK_PUSH((_data)->ids_todo, old_id); \
				} \
			} \
		} \
		if (callback_return & IDWALK_RET_STOP_ITER) { \
			(_data)->status |= IDWALK_STOP; \
			goto FOREACH_FINALIZE; \
		} \
	} \
	else { \
		goto FOREACH_FINALIZE; \
	} ((void)0)

#define FOREACH_CALLBACK_INVOKE_ID(_data, id, cb_flag) \
	{ \
		CHECK_TYPE_ANY(id, ID *, void *); \
		FOREACH_CALLBACK_INVOKE_ID_PP(_data, (ID **)&(id), cb_flag); \
	} ((void)0)

#define FOREACH_CALLBACK_INVOKE(_data, id_super, cb_flag) \
	{ \
		CHECK_TYPE(&((id_super)->id), ID *); \
		FOREACH_CALLBACK_INVOKE_ID_PP(_data, (ID **)&(id_super), cb_flag); \
	} ((void)0)

/* status */
enum {
	IDWALK_STOP     = 1 << 0,
};

typedef struct LibraryForeachIDData {
	ID *self_id;
	int flag;
	int cb_flag;
	LibraryIDLinkCallback callback;
	void *user_data;
	int status;

	/* To handle recursion. */
	GSet *ids_handled;  /* All IDs that are either already done, or still in ids_todo stack. */
	BLI_LINKSTACK_DECLARE(ids_todo, ID *);
} LibraryForeachIDData;

static void library_foreach_idproperty_ID_link(LibraryForeachIDData *data, IDProperty *prop, int flag)
{
	if (!prop)
		return;

	switch (prop->type) {
		case IDP_GROUP:
		{
			for (IDProperty *loop = prop->data.group.first; loop; loop = loop->next) {
				library_foreach_idproperty_ID_link(data, loop, flag);
			}
			break;
		}
		case IDP_IDPARRAY:
		{
			IDProperty *loop = IDP_Array(prop);
			for (int i = 0; i < prop->len; i++) {
				library_foreach_idproperty_ID_link(data, &loop[i], flag);
			}
			break;
		}
		case IDP_ID:
			FOREACH_CALLBACK_INVOKE_ID(data, prop->data.pointer, flag);
			break;
		default:
			break;  /* Nothing to do here with other types of IDProperties... */
	}

	FOREACH_FINALIZE_VOID;
}

static void library_foreach_rigidbodyworldSceneLooper(
        struct RigidBodyWorld *UNUSED(rbw), ID **id_pointer, void *user_data, int cb_flag)
{
	LibraryForeachIDData *data = (LibraryForeachIDData *) user_data;
	FOREACH_CALLBACK_INVOKE_ID_PP(data, id_pointer, cb_flag);

	FOREACH_FINALIZE_VOID;
}

static void library_foreach_modifiersForeachIDLink(
        void *user_data, Object *UNUSED(object), ID **id_pointer, int cb_flag)
{
	LibraryForeachIDData *data = (LibraryForeachIDData *) user_data;
	FOREACH_CALLBACK_INVOKE_ID_PP(data, id_pointer, cb_flag);

	FOREACH_FINALIZE_VOID;
}

static void library_foreach_mtex(LibraryForeachIDData *data, MTex *mtex)
{
	FOREACH_CALLBACK_INVOKE(data, mtex->object, IDWALK_CB_NOP);
	FOREACH_CALLBACK_INVOKE(data, mtex->tex, IDWALK_CB_USER);

	FOREACH_FINALIZE_VOID;
}

/**
 * Loop over all of the ID's this datablock links to.
 *
 * \note: May be extended to be recursive in the future.
 */
void BKE_library_foreach_ID_link(Main *bmain, ID *id, LibraryIDLinkCallback callback, void *user_data, int flag)
{
	LibraryForeachIDData data;
	int i;

	if (flag & IDWALK_RECURSE) {
		/* For now, recusion implies read-only. */
		flag |= IDWALK_READONLY;

		data.ids_handled = BLI_gset_new(BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp, __func__);
		BLI_LINKSTACK_INIT(data.ids_todo);

		BLI_gset_add(data.ids_handled, id);
	}
	else {
		data.ids_handled = NULL;
	}
	data.flag = flag;
	data.status = 0;
	data.callback = callback;
	data.user_data = user_data;

#define CALLBACK_INVOKE_ID(check_id, cb_flag) \
	FOREACH_CALLBACK_INVOKE_ID(&data, check_id, cb_flag)

#define CALLBACK_INVOKE(check_id_super, cb_flag) \
	FOREACH_CALLBACK_INVOKE(&data, check_id_super, cb_flag)

	for (; id != NULL; id = (flag & IDWALK_RECURSE) ? BLI_LINKSTACK_POP(data.ids_todo) : NULL) {
		data.self_id = id;
		data.cb_flag = ID_IS_LINKED(id) ? IDWALK_CB_INDIRECT_USAGE : 0;

		if (bmain != NULL && bmain->relations != NULL && (flag & IDWALK_READONLY)) {
			/* Note that this is minor optimization, even in worst cases (like id being an object with lots of
			 * drivers and constraints and modifiers, or material etc. with huge node tree),
			 * but we might as well use it (Main->relations is always assumed valid, it's responsibility of code
			 * creating it to free it, especially if/when it starts modifying Main database). */
			MainIDRelationsEntry *entry = BLI_ghash_lookup(bmain->relations->id_user_to_used, id);
			for (; entry != NULL; entry = entry->next) {
				FOREACH_CALLBACK_INVOKE_ID_PP(&data, entry->id_pointer, entry->usage_flag);
			}
			continue;
		}

		library_foreach_idproperty_ID_link(&data, id->properties, IDWALK_CB_USER);


		switch ((ID_Type)GS(id->name)) {
			case ID_LI:
			{
				Library *lib = (Library *) id;
				CALLBACK_INVOKE(lib->parent, IDWALK_CB_NOP);
				break;
			}
			case ID_SCE:
			{
				Scene *scene = (Scene *) id;
				Base *base;

				CALLBACK_INVOKE(scene->camera, IDWALK_CB_NOP);
				CALLBACK_INVOKE(scene->world, IDWALK_CB_USER);
				CALLBACK_INVOKE(scene->set, IDWALK_CB_NOP);
				/* DO NOT handle scene->basact here, it's doubling with the loop over whole scene->base later,
				 * since basact is just a pointer to one of those items. */
				CALLBACK_INVOKE(scene->obedit, IDWALK_CB_NOP);

				for (base = scene->base.first; base; base = base->next) {
					CALLBACK_INVOKE(base->object, IDWALK_CB_USER);
				}

				if (scene->rigidbody_world) {
					BKE_rigidbody_world_id_loop(scene->rigidbody_world, library_foreach_rigidbodyworldSceneLooper, &data);
				}

				break;
			}

			case ID_OB:
			{
				Object *object = (Object *) id;

				/* Object is special, proxies make things hard... */
				const int data_cb_flag = data.cb_flag;
				const int proxy_cb_flag = ((data.flag & IDWALK_NO_INDIRECT_PROXY_DATA_USAGE) == 0 && (object->proxy || object->proxy_group)) ?
				                              IDWALK_CB_INDIRECT_USAGE : 0;

				/* object data special case */
				data.cb_flag |= proxy_cb_flag;
				if (object->type == OB_EMPTY) {
					/* empty can have NULL or Image */
					CALLBACK_INVOKE_ID(object->data, IDWALK_CB_USER);
				}
				else {
					/* when set, this can't be NULL */
					if (object->data) {
						CALLBACK_INVOKE_ID(object->data, IDWALK_CB_USER | IDWALK_CB_NEVER_NULL);
					}
				}
				data.cb_flag = data_cb_flag;

				CALLBACK_INVOKE(object->parent, IDWALK_CB_NOP);
				/* object->proxy is refcounted, but not object->proxy_group... *sigh* */
				CALLBACK_INVOKE(object->proxy, IDWALK_CB_USER);
				CALLBACK_INVOKE(object->proxy_group, IDWALK_CB_NOP);

				/* Special case!
				 * Since this field is set/owned by 'user' of this ID (and not ID itself), it is only indirect usage
				 * if proxy object is linked... Twisted. */
				if (object->proxy_from) {
					data.cb_flag = ID_IS_LINKED(object->proxy_from) ? IDWALK_CB_INDIRECT_USAGE : 0;
				}
				CALLBACK_INVOKE(object->proxy_from, IDWALK_CB_LOOPBACK);
				data.cb_flag = data_cb_flag;

				data.cb_flag |= proxy_cb_flag;
				for (i = 0; i < object->totcol; i++) {
					CALLBACK_INVOKE(object->mat[i], IDWALK_CB_USER);
				}
				data.cb_flag = data_cb_flag;

				CALLBACK_INVOKE(object->dup_group, IDWALK_CB_USER);

				if (object->rigidbody_constraint) {
					CALLBACK_INVOKE(object->rigidbody_constraint->ob1, IDWALK_CB_NOP);
					CALLBACK_INVOKE(object->rigidbody_constraint->ob2, IDWALK_CB_NOP);
				}

				if (object->lodlevels.first) {
					LodLevel *level;
					for (level = object->lodlevels.first; level; level = level->next) {
						CALLBACK_INVOKE(level->source, IDWALK_CB_NOP);
					}
				}

				modifiers_foreachIDLink(object, library_foreach_modifiersForeachIDLink, &data);

				break;
			}

			case ID_ME:
			{
				Mesh *mesh = (Mesh *) id;
				CALLBACK_INVOKE(mesh->texcomesh, IDWALK_CB_USER);
				for (i = 0; i < mesh->totcol; i++) {
					CALLBACK_INVOKE(mesh->mat[i], IDWALK_CB_USER);
				}

				/* XXX Really not happy with this - probably texface should rather use some kind of
				 * 'texture slots' and just set indices in each poly/face item - would also save some memory.
				 * Maybe a nice TODO for blender2.8? */
				if (mesh->mtface || mesh->mtpoly) {
					for (i = 0; i < mesh->pdata.totlayer; i++) {
						if (mesh->pdata.layers[i].type == CD_MTEXPOLY) {
							MTexPoly *txface = (MTexPoly *)mesh->pdata.layers[i].data;

							for (int j = 0; j < mesh->totpoly; j++, txface++) {
								CALLBACK_INVOKE(txface->tpage, IDWALK_CB_USER_ONE);
							}
						}
					}

					for (i = 0; i < mesh->fdata.totlayer; i++) {
						if (mesh->fdata.layers[i].type == CD_MTFACE) {
							MTFace *tface = (MTFace *)mesh->fdata.layers[i].data;

							for (int j = 0; j < mesh->totface; j++, tface++) {
								CALLBACK_INVOKE(tface->tpage, IDWALK_CB_USER_ONE);
							}
						}
					}
				}
				break;
			}

			case ID_CU:
			{
				Curve *curve = (Curve *) id;
				CALLBACK_INVOKE(curve->bevobj, IDWALK_CB_NOP);
				CALLBACK_INVOKE(curve->taperobj, IDWALK_CB_NOP);
				CALLBACK_INVOKE(curve->textoncurve, IDWALK_CB_NOP);
				for (i = 0; i < curve->totcol; i++) {
					CALLBACK_INVOKE(curve->mat[i], IDWALK_CB_USER);
				}
				CALLBACK_INVOKE(curve->vfont, IDWALK_CB_USER);
				CALLBACK_INVOKE(curve->vfontb, IDWALK_CB_USER);
				CALLBACK_INVOKE(curve->vfonti, IDWALK_CB_USER);
				CALLBACK_INVOKE(curve->vfontbi, IDWALK_CB_USER);
				break;
			}

			case ID_MA:
			{
				Material *material = (Material *) id;
				for (i = 0; i < MAX_MTEX; i++) {
					if (material->mtex[i]) {
						library_foreach_mtex(&data, material->mtex[i]);
					}
				}
				CALLBACK_INVOKE(material->group, IDWALK_CB_USER);
				if (material->texpaintslot != NULL) {
					CALLBACK_INVOKE(material->texpaintslot->ima, IDWALK_CB_NOP);
				}
				break;
			}

			case ID_TE:
			{
				Tex *texture = (Tex *) id;
				CALLBACK_INVOKE(texture->ima, IDWALK_CB_USER);

				if (texture->env) {
					CALLBACK_INVOKE(texture->env->object, IDWALK_CB_NOP);
					CALLBACK_INVOKE(texture->env->ima, IDWALK_CB_USER);
				}

				if (texture->vd)
					CALLBACK_INVOKE(texture->vd->object, IDWALK_CB_NOP);
				break;
			}

			case ID_LA:
			{
				Lamp *lamp = (Lamp *) id;
				for (i = 0; i < MAX_MTEX; i++) {
					if (lamp->mtex[i]) {
						library_foreach_mtex(&data, lamp->mtex[i]);
					}
				}
				break;
			}

			case ID_CA:
			{
				Camera *camera = (Camera *) id;
				CALLBACK_INVOKE(camera->dof_ob, IDWALK_CB_NOP);
				break;
			}

			case ID_SCR:
			{
				bScreen *screen = (bScreen *) id;
				CALLBACK_INVOKE(screen->scene, IDWALK_CB_USER_ONE);
				break;
			}

			case ID_WO:
			{
				World *world = (World *) id;
				for (i = 0; i < MAX_MTEX; i++) {
					if (world->mtex[i]) {
						library_foreach_mtex(&data, world->mtex[i]);
					}
				}
				break;
			}


			case ID_GR:
			{
				Group *group = (Group *) id;
				GroupObject *gob;
				for (gob = group->gobject.first; gob; gob = gob->next) {
					CALLBACK_INVOKE(gob->ob, IDWALK_CB_USER_ONE);
				}
				break;
			}

			default:
				break;

		}
	}

FOREACH_FINALIZE:
	if (data.ids_handled) {
		BLI_gset_free(data.ids_handled, NULL);
		BLI_LINKSTACK_FREE(data.ids_todo);
	}

#undef CALLBACK_INVOKE_ID
#undef CALLBACK_INVOKE
}

#undef FOREACH_CALLBACK_INVOKE_ID
#undef FOREACH_CALLBACK_INVOKE

/**
 * re-usable function, use when replacing ID's
 */
void BKE_library_update_ID_link_user(ID *id_dst, ID *id_src, const int cb_flag)
{
	if (cb_flag & IDWALK_CB_USER) {
		id_us_min(id_src);
		id_us_plus(id_dst);
	}
	else if (cb_flag & IDWALK_CB_USER_ONE) {
		id_us_ensure_real(id_dst);
	}
}

/**
 * Say whether given \a id_type_owner can use (in any way) a datablock of \a id_type_used.
 *
 * This is a 'simplified' abstract version of #BKE_library_foreach_ID_link() above, quite useful to reduce
 * useless iterations in some cases.
 */
/* XXX This has to be fully rethink, basing check on ID type is not really working anymore (and even worth once
 *     IDProps will support ID pointers), we'll have to do some quick checks on IDs themselves... */
bool BKE_library_id_can_use_idtype(ID *id_owner, const short id_type_used)
{
	/* any type of ID can be used in custom props. */
	if (id_owner->properties) {
		return true;
	}

	const short id_type_owner = GS(id_owner->name);

	switch ((ID_Type)id_type_owner) {
		case ID_LI:
			return ELEM(id_type_used, ID_LI);
		case ID_SCE:
			return (ELEM(id_type_used, ID_OB, ID_WO, ID_SCE, ID_MA, ID_GR, ID_TXT,
			                           ID_IM));
		case ID_OB:
			/* Could be the following, but simpler to just always say 'yes' here. */
			return true;
		case ID_ME:
			return ELEM(id_type_used, ID_ME, ID_MA, ID_IM);
		case ID_CU:
			return ELEM(id_type_used, ID_OB, ID_MA, ID_VF);
		case ID_MA:
			return (ELEM(id_type_used, ID_TE, ID_GR));
		case ID_TE:
			return (ELEM(id_type_used, ID_IM, ID_OB));
		case ID_LA:
			return (ELEM(id_type_used, ID_TE));
		case ID_CA:
			return ELEM(id_type_used, ID_OB);
		case ID_SCR:
			return ELEM(id_type_used, ID_SCE);
		case ID_WO:
			return (ELEM(id_type_used, ID_TE));
		case ID_GR:
			return ELEM(id_type_used, ID_OB);

		default:
			/* Those types never use/reference other IDs... */
			return false;
	}
	return false;
}


/* ***** ID users iterator. ***** */
typedef struct IDUsersIter {
	ID *id;

	ListBase *lb_array[MAX_LIBARRAY];
	int lb_idx;

	ID *curr_id;
	int count_direct, count_indirect;  /* Set by callback. */
} IDUsersIter;

static int foreach_libblock_id_users_callback(void *user_data, ID *UNUSED(self_id), ID **id_p, int cb_flag)
{
	IDUsersIter *iter = user_data;

	if (*id_p) {
		/* 'Loopback' ID pointers (the ugly 'from' ones, Object->proxy_from and Key->from).
		 * Those are not actually ID usage, we can ignore them here.
		 */
		if (cb_flag & IDWALK_CB_LOOPBACK) {
			return IDWALK_RET_NOP;
		}

		if (*id_p == iter->id) {
#if 0
			printf("%s uses %s (refcounted: %d, userone: %d, used_one: %d, used_one_active: %d, indirect_usage: %d)\n",
				   iter->curr_id->name, iter->id->name, (cb_flag & IDWALK_USER) ? 1 : 0, (cb_flag & IDWALK_USER_ONE) ? 1 : 0,
				   (iter->id->tag & LIB_TAG_EXTRAUSER) ? 1 : 0, (iter->id->tag & LIB_TAG_EXTRAUSER_SET) ? 1 : 0,
				   (cb_flag & IDWALK_INDIRECT_USAGE) ? 1 : 0);
#endif
			if (cb_flag & IDWALK_CB_INDIRECT_USAGE) {
				iter->count_indirect++;
			}
			else {
				iter->count_direct++;
			}
		}
	}

	return IDWALK_RET_NOP;
}

/**
 * Return the number of times given \a id_user uses/references \a id_used.
 *
 * \note This only checks for pointer references of an ID, shallow usages (like e.g. by RNA paths, as done
 *       for FCurves) are not detected at all.
 *
 * \param id_user: the ID which is supposed to use (reference) \a id_used.
 * \param id_used: the ID which is supposed to be used (referenced) by \a id_user.
 * \return the number of direct usages/references of \a id_used by \a id_user.
 */
int BKE_library_ID_use_ID(ID *id_user, ID *id_used)
{
	IDUsersIter iter;

	/* We do not care about iter.lb_array/lb_idx here... */
	iter.id = id_used;
	iter.curr_id = id_user;
	iter.count_direct = iter.count_indirect = 0;

	BKE_library_foreach_ID_link(NULL, iter.curr_id, foreach_libblock_id_users_callback, (void *)&iter, IDWALK_READONLY);

	return iter.count_direct + iter.count_indirect;
}

static bool library_ID_is_used(Main *bmain, void *idv, const bool check_linked)
{
	IDUsersIter iter;
	ListBase *lb_array[MAX_LIBARRAY];
	ID *id = idv;
	int i = set_listbasepointers(bmain, lb_array);
	bool is_defined = false;

	iter.id = id;
	iter.count_direct = iter.count_indirect = 0;
	while (i-- && !is_defined) {
		ID *id_curr = lb_array[i]->first;

		if (!id_curr || !BKE_library_id_can_use_idtype(id_curr, GS(id->name))) {
			continue;
		}

		for (; id_curr && !is_defined; id_curr = id_curr->next) {
			if (id_curr == id) {
				/* We are not interested in self-usages (mostly from drivers or bone constraints...). */
				continue;
			}
			iter.curr_id = id_curr;
			BKE_library_foreach_ID_link(
			            bmain, id_curr, foreach_libblock_id_users_callback, &iter, IDWALK_READONLY);

			is_defined = ((check_linked ? iter.count_indirect : iter.count_direct) != 0);
		}
	}

	return is_defined;
}

/**
 * Check whether given ID is used locally (i.e. by another non-linked ID).
 */
bool BKE_library_ID_is_locally_used(Main *bmain, void *idv)
{
	return library_ID_is_used(bmain, idv, false);
}

/**
 * Check whether given ID is used indirectly (i.e. by another linked ID).
 */
bool BKE_library_ID_is_indirectly_used(Main *bmain, void *idv)
{
	return library_ID_is_used(bmain, idv, true);
}

/**
 * Combine \a BKE_library_ID_is_locally_used() and \a BKE_library_ID_is_indirectly_used() in a single call.
 */
void BKE_library_ID_test_usages(Main *bmain, void *idv, bool *is_used_local, bool *is_used_linked)
{
	IDUsersIter iter;
	ListBase *lb_array[MAX_LIBARRAY];
	ID *id = idv;
	int i = set_listbasepointers(bmain, lb_array);
	bool is_defined = false;

	iter.id = id;
	iter.count_direct = iter.count_indirect = 0;
	while (i-- && !is_defined) {
		ID *id_curr = lb_array[i]->first;

		if (!id_curr || !BKE_library_id_can_use_idtype(id_curr, GS(id->name))) {
			continue;
		}

		for (; id_curr && !is_defined; id_curr = id_curr->next) {
			if (id_curr == id) {
				/* We are not interested in self-usages (mostly from drivers or bone constraints...). */
				continue;
			}
			iter.curr_id = id_curr;
			BKE_library_foreach_ID_link(bmain, id_curr, foreach_libblock_id_users_callback, &iter, IDWALK_READONLY);

			is_defined = (iter.count_direct != 0 && iter.count_indirect != 0);
		}
	}

	*is_used_local = (iter.count_direct != 0);
	*is_used_linked = (iter.count_indirect != 0);
}

/* ***** IDs usages.checking/tagging. ***** */
static int foreach_libblock_used_linked_data_tag_clear_cb(
        void *user_data, ID *self_id, ID **id_p, int UNUSED(cb_flag))
{
	bool *is_changed = user_data;

	if (*id_p) {
		/* XXX another hack, for similar reasons as above one. */
		if ((GS(self_id->name) == ID_OB) && (((Object *)self_id)->proxy_from == (Object *)*id_p)) {
			return IDWALK_RET_NOP;
		}

		/* If checked id is used by an assumed used ID, then it is also used and not part of any linked archipelago. */
		if (!(self_id->tag & LIB_TAG_DOIT) && ((*id_p)->tag & LIB_TAG_DOIT)) {
			(*id_p)->tag &= ~LIB_TAG_DOIT;
			*is_changed = true;
		}
	}

	return IDWALK_RET_NOP;
}

/**
 * Detect orphaned linked data blocks (i.e. linked data not used (directly or indirectly) in any way by any local data),
 * including complex cases like 'linked archipelagoes', i.e. linked datablocks that use each other in loops,
 * which prevents their deletion by 'basic' usage checks...
 *
 * \param do_init_tag: if \a true, all linked data are checked, if \a false, only linked datablocks already tagged with
 *                    LIB_TAG_DOIT are checked.
 */
void BKE_library_unused_linked_data_set_tag(Main *bmain, const bool do_init_tag)
{
	ListBase *lb_array[MAX_LIBARRAY];

	if (do_init_tag) {
		int i = set_listbasepointers(bmain, lb_array);

		while (i--) {
			for (ID *id = lb_array[i]->first; id; id = id->next) {
				if (id->lib && (id->tag & LIB_TAG_INDIRECT) != 0) {
					id->tag |= LIB_TAG_DOIT;
				}
				else {
					id->tag &= ~LIB_TAG_DOIT;
				}
			}
		}
	}

	bool do_loop = true;
	while (do_loop) {
		int i = set_listbasepointers(bmain, lb_array);
		do_loop = false;

		while (i--) {
			for (ID *id = lb_array[i]->first; id; id = id->next) {
				if (id->tag & LIB_TAG_DOIT) {
					/* Unused ID (so far), no need to check it further. */
					continue;
				}
				BKE_library_foreach_ID_link(
				            bmain, id, foreach_libblock_used_linked_data_tag_clear_cb, &do_loop, IDWALK_READONLY);
			}
		}
	}
}

/**
 * Untag linked data blocks used by other untagged linked datablocks.
 * Used to detect datablocks that we can forcefully make local (instead of copying them to later get rid of original):
 * All datablocks we want to make local are tagged by caller, after this function has ran caller knows datablocks still
 * tagged can directly be made local, since they are only used by other datablocks that will also be made fully local.
 */
void BKE_library_indirectly_used_data_tag_clear(Main *bmain)
{
	ListBase *lb_array[MAX_LIBARRAY];

	bool do_loop = true;
	while (do_loop) {
		int i = set_listbasepointers(bmain, lb_array);
		do_loop = false;

		while (i--) {
			for (ID *id = lb_array[i]->first; id; id = id->next) {
				if (id->lib == NULL || id->tag & LIB_TAG_DOIT) {
					/* Local or non-indirectly-used ID (so far), no need to check it further. */
					continue;
				}
				BKE_library_foreach_ID_link(
				            bmain, id, foreach_libblock_used_linked_data_tag_clear_cb, &do_loop, IDWALK_READONLY);
			}
		}
	}
}
