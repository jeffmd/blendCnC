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
 * The Original Code is Copyright (C) 2004 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/editors/space_outliner/outliner_tree.c
 *  \ingroup spoutliner
 */

#include <math.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_camera_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_curve_types.h"
#include "DNA_group_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"
#include "DNA_object_types.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_mempool.h"
#include "BLI_fnmatch.h"

#include "BLT_translation.h"

#include "BKE_main.h"
#include "BKE_library.h"
#include "BKE_modifier.h"
#include "BKE_idcode.h"
#include "BKE_outliner_treehash.h"

#include "ED_screen.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"

#include "outliner_intern.h"

#ifdef WIN32
#  include "BLI_math_base.h" /* M_PI */
#endif

/* ********************************************************* */
/* Persistent Data */

static void outliner_storage_cleanup(SpaceOops *soops)
{
	BLI_mempool *ts = soops->treestore;

	if (ts) {
		TreeStoreElem *tselem;
		int unused = 0;

		/* each element used once, for ID blocks with more users to have each a treestore */
		BLI_mempool_iter iter;

		BLI_mempool_iternew(ts, &iter);
		while ((tselem = BLI_mempool_iterstep(&iter))) {
			tselem->used = 0;
		}

		/* cleanup only after reading file or undo step, and always for
		 * RNA datablocks view in order to save memory */
		if (soops->storeflag & SO_TREESTORE_CLEANUP) {
			soops->storeflag &= ~SO_TREESTORE_CLEANUP;

			BLI_mempool_iternew(ts, &iter);
			while ((tselem = BLI_mempool_iterstep(&iter))) {
				if (tselem->id == NULL) unused++;
			}

			if (unused) {
				if (BLI_mempool_len(ts) == unused) {
					BLI_mempool_destroy(ts);
					soops->treestore = NULL;
					if (soops->treehash) {
						BKE_outliner_treehash_free(soops->treehash);
						soops->treehash = NULL;
					}
				}
				else {
					TreeStoreElem *tsenew;
					BLI_mempool *new_ts = BLI_mempool_create(sizeof(TreeStoreElem), BLI_mempool_len(ts) - unused,
					                                         512, BLI_MEMPOOL_ALLOW_ITER);
					BLI_mempool_iternew(ts, &iter);
					while ((tselem = BLI_mempool_iterstep(&iter))) {
						if (tselem->id) {
							tsenew = BLI_mempool_alloc(new_ts);
							*tsenew = *tselem;
						}
					}
					BLI_mempool_destroy(ts);
					soops->treestore = new_ts;
					if (soops->treehash) {
						/* update hash table to fix broken pointers */
						BKE_outliner_treehash_rebuild_from_treestore(soops->treehash, soops->treestore);
					}
				}
			}
		}
	}
}

static void check_persistent(SpaceOops *soops, TreeElement *te, ID *id, short type, short nr)
{
	TreeStoreElem *tselem;

	if (soops->treestore == NULL) {
		/* if treestore was not created in readfile.c, create it here */
		soops->treestore = BLI_mempool_create(sizeof(TreeStoreElem), 1, 512, BLI_MEMPOOL_ALLOW_ITER);

	}
	if (soops->treehash == NULL) {
		soops->treehash = BKE_outliner_treehash_create_from_treestore(soops->treestore);
	}

	/* find any unused tree element in treestore and mark it as used
	 * (note that there may be multiple unused elements in case of linked objects) */
	tselem = BKE_outliner_treehash_lookup_unused(soops->treehash, type, nr, id);
	if (tselem) {
		te->store_elem = tselem;
		tselem->used = 1;
		return;
	}

	/* add 1 element to treestore */
	tselem = BLI_mempool_alloc(soops->treestore);
	tselem->type = type;
	tselem->nr = type ? nr : 0;
	tselem->id = id;
	tselem->used = 0;
	tselem->flag = TSE_CLOSED;
	te->store_elem = tselem;
	BKE_outliner_treehash_add_element(soops->treehash, tselem);
}

/* ********************************************************* */
/* Tree Management */

void outliner_free_tree(ListBase *lb)
{
	while (lb->first) {
		TreeElement *te = lb->first;

		outliner_free_tree(&te->subtree);
		BLI_remlink(lb, te);

		if (te->flag & TE_FREE_NAME) MEM_freeN((void *)te->name);
		MEM_freeN(te);
	}
}

void outliner_cleanup_tree(SpaceOops *soops)
{
	outliner_free_tree(&soops->tree);
	outliner_storage_cleanup(soops);
}

/* Find specific item from the treestore */
TreeElement *outliner_find_tree_element(ListBase *lb, const TreeStoreElem *store_elem)
{
	TreeElement *te, *tes;
	for (te = lb->first; te; te = te->next) {
		if (te->store_elem == store_elem) return te;
		tes = outliner_find_tree_element(&te->subtree, store_elem);
		if (tes) return tes;
	}
	return NULL;
}

/* tse is not in the treestore, we use its contents to find a match */
TreeElement *outliner_find_tse(SpaceOops *soops, const TreeStoreElem *tse)
{
	TreeStoreElem *tselem;

	if (tse->id == NULL) return NULL;

	/* check if 'tse' is in treestore */
	tselem = BKE_outliner_treehash_lookup_any(soops->treehash, tse->type, tse->nr, tse->id);
	if (tselem)
		return outliner_find_tree_element(&soops->tree, tselem);

	return NULL;
}

/* Find treestore that refers to given ID */
TreeElement *outliner_find_id(SpaceOops *soops, ListBase *lb, const ID *id)
{
	for (TreeElement *te = lb->first; te; te = te->next) {
		TreeStoreElem *tselem = TREESTORE(te);
		if (tselem->type == 0) {
			if (tselem->id == id) {
				return te;
			}
			/* only deeper on scene or object */
			if (ELEM(te->idcode, ID_OB, ID_SCE) ||
			    ((soops->outlinevis == SO_GROUPS) && (te->idcode == ID_GR)))
			{
				TreeElement *tes = outliner_find_id(soops, &te->subtree, id);
				if (tes) {
					return tes;
				}
			}
		}
	}
	return NULL;
}

ID *outliner_search_back(SpaceOops *UNUSED(soops), TreeElement *te, short idcode)
{
	TreeStoreElem *tselem;
	te = te->parent;

	while (te) {
		tselem = TREESTORE(te);
		if (tselem->type == 0 && te->idcode == idcode) return tselem->id;
		te = te->parent;
	}
	return NULL;
}


/* ********************************************************* */

/* Prototype, see functions below */
static TreeElement *outliner_add_element(SpaceOops *soops, ListBase *lb, void *idv,
                                         TreeElement *parent, short type, short index);

/* -------------------------------------------------------- */

/* -------------------------------------------------------- */

#define LOG2I(x) (int)(log(x) / M_LN2)

#undef LOG2I

// can be inlined if necessary
static void outliner_add_object_contents(SpaceOops *soops, TreeElement *te, TreeStoreElem *tselem, Object *ob)
{
	if (ob->proxy && !ID_IS_LINKED(ob))
		outliner_add_element(soops, &te->subtree, ob->proxy, te, TSE_PROXY, 0);

	outliner_add_element(soops, &te->subtree, ob->data, te, 0, 0);

	for (int a = 0; a < ob->totcol; a++) {
		outliner_add_element(soops, &te->subtree, ob->mat[a], te, 0, a);
	}

	if (ob->modifiers.first) {
		ModifierData *md;
		TreeElement *ten_mod = outliner_add_element(soops, &te->subtree, ob, te, TSE_MODIFIER_BASE, 0);
		int index;

		ten_mod->name = IFACE_("Modifiers");
		for (index = 0, md = ob->modifiers.first; md; index++, md = md->next) {
			TreeElement *ten = outliner_add_element(soops, &ten_mod->subtree, ob, ten_mod, TSE_MODIFIER, index);
			ten->name = md->name;
			ten->directdata = md;

			if (md->type == eModifierType_Curve) {
				outliner_add_element(soops, &ten->subtree, ((CurveModifierData *) md)->object, ten, TSE_LINKED_OB, 0);
			}
			else if (md->type == eModifierType_Hook) {
				outliner_add_element(soops, &ten->subtree, ((HookModifierData *) md)->object, ten, TSE_LINKED_OB, 0);
			}
		}
	}

	/* vertex groups */
	if (ob->defbase.first) {
		bDeformGroup *defgroup;
		TreeElement *ten;
		TreeElement *tenla = outliner_add_element(soops, &te->subtree, ob, te, TSE_DEFGROUP_BASE, 0);
		int a;

		tenla->name = IFACE_("Vertex Groups");
		for (defgroup = ob->defbase.first, a = 0; defgroup; defgroup = defgroup->next, a++) {
			ten = outliner_add_element(soops, &tenla->subtree, ob, tenla, TSE_DEFGROUP, a);
			ten->name = defgroup->name;
			ten->directdata = defgroup;
		}
	}

	/* duplicated group */
	if (ob->dup_group)
		outliner_add_element(soops, &te->subtree, ob->dup_group, te, 0, 0);
}


// can be inlined if necessary
static void outliner_add_id_contents(SpaceOops *soops, TreeElement *te, TreeStoreElem *tselem, ID *id)
{
	/* tuck pointer back in object, to construct hierarchy */
	if (GS(id->name) == ID_OB) id->newid = (ID *)te;

	/* expand specific data always */
	switch (GS(id->name)) {
		case ID_LI:
		{
			te->name = ((Library *)id)->name;
			break;
		}
		case ID_OB:
		{
			outliner_add_object_contents(soops, te, tselem, (Object *)id);
			break;
		}
		case ID_ME:
		{
			Mesh *me = (Mesh *)id;
			int a;

			for (a = 0; a < me->totcol; a++)
				outliner_add_element(soops, &te->subtree, me->mat[a], te, 0, a);
			/* could do tfaces with image links, but the images are not grouped nicely.
			 * would require going over all tfaces, sort images in use. etc... */
			break;
		}
		case ID_CU:
		{
			Curve *cu = (Curve *)id;
			int a;

			for (a = 0; a < cu->totcol; a++)
				outliner_add_element(soops, &te->subtree, cu->mat[a], te, 0, a);
			break;
		}
		case ID_MA:
		{
			Material *ma = (Material *)id;
			int a;

			for (a = 0; a < MAX_MTEX; a++) {
				if (ma->mtex[a]) outliner_add_element(soops, &te->subtree, ma->mtex[a]->tex, te, 0, a);
			}
			break;
		}
		case ID_TE:
		{
			Tex *tex = (Tex *)id;

			outliner_add_element(soops, &te->subtree, tex->ima, te, 0, 0);
			break;
		}
		case ID_LA:
		{
			Lamp *la = (Lamp *)id;
			int a;

			for (a = 0; a < MAX_MTEX; a++) {
				if (la->mtex[a]) outliner_add_element(soops, &te->subtree, la->mtex[a]->tex, te, 0, a);
			}
			break;
		}
		case ID_WO:
		{
			World *wrld = (World *)id;
			int a;

			for (a = 0; a < MAX_MTEX; a++) {
				if (wrld->mtex[a]) outliner_add_element(soops, &te->subtree, wrld->mtex[a]->tex, te, 0, a);
			}
			break;
		}

		default:
			break;
	}
}

// TODO: this function needs to be split up! It's getting a bit too large...
// Note: "ID" is not always a real ID
static TreeElement *outliner_add_element(SpaceOops *soops, ListBase *lb, void *idv,
                                         TreeElement *parent, short type, short index)
{
	TreeElement *te;
	TreeStoreElem *tselem;
	ID *id = idv;

	if (ELEM(type, TSE_RNA_STRUCT, TSE_RNA_PROPERTY, TSE_RNA_ARRAY_ELEM)) {
		id = ((PointerRNA *)idv)->id.data;
		if (!id) id = ((PointerRNA *)idv)->data;
	}

	/* One exception */
	if (type == TSE_ID_BASE) {
		/* pass */
	}
	else if (id == NULL) {
		return NULL;
	}

	if (type == 0) {
		/* Zero type means real ID, ensure we do not get non-outliner ID types here... */
		BLI_assert(TREESTORE_ID_TYPE(id));
	}

	te = MEM_callocN(sizeof(TreeElement), "tree elem");
	/* add to the visual tree */
	BLI_addtail(lb, te);
	/* add to the storage */
	check_persistent(soops, te, id, type, index);
	tselem = TREESTORE(te);

	/* if we are searching for something expand to see child elements */
	if (SEARCHING_OUTLINER(soops))
		tselem->flag |= TSE_CHILDSEARCH;

	te->parent = parent;
	te->index = index;   // for data arrays
	if (ELEM(type, TSE_RNA_STRUCT, TSE_RNA_PROPERTY, TSE_RNA_ARRAY_ELEM)) {
		/* pass */
	}
	else if (type == TSE_ID_BASE) {
		/* pass */
	}
	else {
		/* do here too, for blend file viewer, own ID_LI then shows file name */
		if (GS(id->name) == ID_LI)
			te->name = ((Library *)id)->name;
		else
			te->name = id->name + 2; // default, can be overridden by Library or non-ID data
		te->idcode = GS(id->name);
	}

	if (type == 0) {
		TreeStoreElem *tsepar = parent ? TREESTORE(parent) : NULL;

		/* ID datablock */
		if (tsepar == NULL || tsepar->type != TSE_ID_BASE)
			outliner_add_id_contents(soops, te, tselem, id);
	}
	else if (ELEM(type, TSE_RNA_STRUCT, TSE_RNA_PROPERTY, TSE_RNA_ARRAY_ELEM)) {
		PointerRNA pptr, propptr, *ptr = (PointerRNA *)idv;
		PropertyRNA *prop, *iterprop;
		PropertyType proptype;

		/* Don't display arrays larger, weak but index is stored as a short,
		 * also the outliner isn't intended for editing such large data-sets. */
		BLI_STATIC_ASSERT(sizeof(te->index) == 2, "Index is no longer short!");
		const int tot_limit = SHRT_MAX;

		int a, tot;

		/* we do lazy build, for speed and to avoid infinite recursion */

		if (ptr->data == NULL) {
			te->name = IFACE_("(empty)");
		}
		else if (type == TSE_RNA_STRUCT) {
			/* struct */
			te->name = RNA_struct_name_get_alloc(ptr, NULL, 0, NULL);

			if (te->name)
				te->flag |= TE_FREE_NAME;
			else
				te->name = RNA_struct_ui_name(ptr->type);

			/* If searching don't expand RNA entries */
			if (SEARCHING_OUTLINER(soops) && BLI_strcasecmp("RNA", te->name) == 0) tselem->flag &= ~TSE_CHILDSEARCH;

			iterprop = RNA_struct_iterator_property(ptr->type);
			tot = RNA_property_collection_length(ptr, iterprop);
			CLAMP_MAX(tot, tot_limit);

			/* auto open these cases */
			if (!parent || (RNA_property_type(parent->directdata)) == PROP_POINTER)
				if (!tselem->used)
					tselem->flag &= ~TSE_CLOSED;

			if (TSELEM_OPEN(tselem, soops)) {
				for (a = 0; a < tot; a++) {
					RNA_property_collection_lookup_int(ptr, iterprop, a, &propptr);
					if (!(RNA_property_flag(propptr.data) & PROP_HIDDEN)) {
						outliner_add_element(soops, &te->subtree, (void *)ptr, te, TSE_RNA_PROPERTY, a);
					}
				}
			}
			else if (tot)
				te->flag |= TE_LAZY_CLOSED;

			te->rnaptr = *ptr;
		}
		else if (type == TSE_RNA_PROPERTY) {
			/* property */
			iterprop = RNA_struct_iterator_property(ptr->type);
			RNA_property_collection_lookup_int(ptr, iterprop, index, &propptr);

			prop = propptr.data;
			proptype = RNA_property_type(prop);

			te->name = RNA_property_ui_name(prop);
			te->directdata = prop;
			te->rnaptr = *ptr;

			/* If searching don't expand RNA entries */
			if (SEARCHING_OUTLINER(soops) && BLI_strcasecmp("RNA", te->name) == 0) tselem->flag &= ~TSE_CHILDSEARCH;

			if (proptype == PROP_POINTER) {
				pptr = RNA_property_pointer_get(ptr, prop);

				if (pptr.data) {
					if (TSELEM_OPEN(tselem, soops))
						outliner_add_element(soops, &te->subtree, (void *)&pptr, te, TSE_RNA_STRUCT, -1);
					else
						te->flag |= TE_LAZY_CLOSED;
				}
			}
			else if (proptype == PROP_COLLECTION) {
				tot = RNA_property_collection_length(ptr, prop);
				CLAMP_MAX(tot, tot_limit);

				if (TSELEM_OPEN(tselem, soops)) {
					for (a = 0; a < tot; a++) {
						RNA_property_collection_lookup_int(ptr, prop, a, &pptr);
						outliner_add_element(soops, &te->subtree, (void *)&pptr, te, TSE_RNA_STRUCT, a);
					}
				}
				else if (tot)
					te->flag |= TE_LAZY_CLOSED;
			}
			else if (ELEM(proptype, PROP_BOOLEAN, PROP_INT, PROP_FLOAT)) {
				tot = RNA_property_array_length(ptr, prop);
				CLAMP_MAX(tot, tot_limit);

				if (TSELEM_OPEN(tselem, soops)) {
					for (a = 0; a < tot; a++)
						outliner_add_element(soops, &te->subtree, (void *)ptr, te, TSE_RNA_ARRAY_ELEM, a);
				}
				else if (tot)
					te->flag |= TE_LAZY_CLOSED;
			}
		}
		else if (type == TSE_RNA_ARRAY_ELEM) {
			char c;

			prop = parent->directdata;

			te->directdata = prop;
			te->rnaptr = *ptr;
			te->index = index;

			c = RNA_property_array_item_char(prop, index);

			te->name = MEM_callocN(sizeof(char) * 20, "OutlinerRNAArrayName");
			if (c) sprintf((char *)te->name, "  %c", c);
			else sprintf((char *)te->name, "  %d", index + 1);
			te->flag |= TE_FREE_NAME;
		}
	}
	else if (type == TSE_KEYMAP) {
		wmKeyMap *km = (wmKeyMap *)idv;
		wmKeyMapItem *kmi;
		char opname[OP_MAX_TYPENAME];

		te->directdata = idv;
		te->name = km->idname;

		if (TSELEM_OPEN(tselem, soops)) {
			int a = 0;

			for (kmi = km->items.first; kmi; kmi = kmi->next, a++) {
				const char *key = WM_key_event_string(kmi->type, false);

				if (key[0]) {
					wmOperatorType *ot = NULL;

					if (kmi->propvalue) {
						/* pass */
					}
					else {
						ot = WM_operatortype_find(kmi->idname, 0);
					}

					if (ot || kmi->propvalue) {
						TreeElement *ten = outliner_add_element(soops, &te->subtree, kmi, te, TSE_KEYMAP_ITEM, a);

						ten->directdata = kmi;

						if (kmi->propvalue) {
							ten->name = IFACE_("Modal map, not yet");
						}
						else {
							WM_operator_py_idname(opname, ot->idname);
							ten->name = BLI_strdup(opname);
							ten->flag |= TE_FREE_NAME;
						}
					}
				}
			}
		}
		else
			te->flag |= TE_LAZY_CLOSED;
	}

	return te;
}

/* ----------------------------------------------- */


static void outliner_add_library_contents(Main *mainvar, SpaceOops *soops, TreeElement *te, Library *lib)
{
	TreeElement *ten;
	ListBase *lbarray[MAX_LIBARRAY];
	int a, tot;

	tot = set_listbasepointers(mainvar, lbarray);
	for (a = 0; a < tot; a++) {
		if (lbarray[a]->first) {
			ID *id = lbarray[a]->first;

			/* check if there's data in current lib */
			for (; id; id = id->next)
				if (id->lib == lib)
					break;

			if (id) {
				ten = outliner_add_element(soops, &te->subtree, lbarray[a], NULL, TSE_ID_BASE, 0);
				ten->directdata = lbarray[a];

				ten->name = BKE_idcode_to_name_plural(GS(id->name));
				if (ten->name == NULL)
					ten->name = "UNKNOWN";

				for (id = lbarray[a]->first; id; id = id->next) {
					if (id->lib == lib)
						outliner_add_element(soops, &ten->subtree, id, ten, 0, 0);
				}
			}
		}
	}

}

static void outliner_add_orphaned_datablocks(Main *mainvar, SpaceOops *soops)
{
	TreeElement *ten;
	ListBase *lbarray[MAX_LIBARRAY];
	int a, tot;

	tot = set_listbasepointers(mainvar, lbarray);
	for (a = 0; a < tot; a++) {
		if (lbarray[a]->first) {
			ID *id = lbarray[a]->first;

			/* check if there are any datablocks of this type which are orphans */
			for (; id; id = id->next) {
				if (ID_REAL_USERS(id) <= 0)
					break;
			}

			if (id) {
				/* header for this type of datablock */
				/* TODO's:
				 *   - Add a parameter to BKE_idcode_to_name_plural to get a sane "user-visible" name instead?
				 *   - Ensure that this uses nice icons for the datablock type involved instead of the dot?
				 */
				ten = outliner_add_element(soops, &soops->tree, lbarray[a], NULL, TSE_ID_BASE, 0);
				ten->directdata = lbarray[a];

				ten->name = BKE_idcode_to_name_plural(GS(id->name));
				if (ten->name == NULL)
					ten->name = "UNKNOWN";

				/* add the orphaned datablocks - these will not be added with any subtrees attached */
				for (id = lbarray[a]->first; id; id = id->next) {
					if (ID_REAL_USERS(id) <= 0)
						outliner_add_element(soops, &ten->subtree, id, ten, 0, 0);
				}
			}
		}
	}
}

/* ======================================================= */
/* Generic Tree Building helpers - order these are called is top to bottom */

/* Hierarchy --------------------------------------------- */

/* make sure elements are correctly nested */
static void outliner_make_hierarchy(ListBase *lb)
{
	TreeElement *te, *ten, *tep;
	TreeStoreElem *tselem;

	/* build hierarchy */
	// XXX also, set extents here...
	te = lb->first;
	while (te) {
		ten = te->next;
		tselem = TREESTORE(te);

		if (tselem->type == 0 && te->idcode == ID_OB) {
			Object *ob = (Object *)tselem->id;
			if (ob->parent && ob->parent->id.newid) {
				BLI_remlink(lb, te);
				tep = (TreeElement *)ob->parent->id.newid;
				BLI_addtail(&tep->subtree, te);
				// set correct parent pointers
				for (te = tep->subtree.first; te; te = te->next) te->parent = tep;
			}
		}
		te = ten;
	}
}

/* Sorting ------------------------------------------------------ */

typedef struct tTreeSort {
	TreeElement *te;
	ID *id;
	const char *name;
	short idcode;
} tTreeSort;

/* alphabetical comparator, tryping to put objects first */
static int treesort_alpha_ob(const void *v1, const void *v2)
{
	const tTreeSort *x1 = v1, *x2 = v2;
	int comp;

	/* first put objects last (hierarchy) */
	comp = (x1->idcode == ID_OB);
	if (x2->idcode == ID_OB) comp += 2;

	if (comp == 1) return 1;
	else if (comp == 2) return -1;
	else if (comp == 3) {
		comp = strcmp(x1->name, x2->name);

		if (comp > 0) return 1;
		else if (comp < 0) return -1;
		return 0;
	}
	return 0;
}

/* alphabetical comparator */
static int treesort_alpha(const void *v1, const void *v2)
{
	const tTreeSort *x1 = v1, *x2 = v2;
	int comp;

	comp = strcmp(x1->name, x2->name);

	if (comp > 0) return 1;
	else if (comp < 0) return -1;
	return 0;
}


/* this is nice option for later? doesn't look too useful... */
#if 0
static int treesort_obtype_alpha(const void *v1, const void *v2)
{
	const tTreeSort *x1 = v1, *x2 = v2;

	/* first put objects last (hierarchy) */
	if (x1->idcode == ID_OB && x2->idcode != ID_OB) {
		return 1;
	}
	else if (x2->idcode == ID_OB && x1->idcode != ID_OB) {
		return -1;
	}
	else {
		/* 2nd we check ob type */
		if (x1->idcode == ID_OB && x2->idcode == ID_OB) {
			if      (((Object *)x1->id)->type > ((Object *)x2->id)->type) return  1;
			else if (((Object *)x1->id)->type > ((Object *)x2->id)->type) return -1;
			else return 0;
		}
		else {
			int comp = strcmp(x1->name, x2->name);

			if      (comp > 0) return  1;
			else if (comp < 0) return -1;
			return 0;
		}
	}
}
#endif

/* sort happens on each subtree individual */
static void outliner_sort(ListBase *lb)
{
	TreeElement *te;
	TreeStoreElem *tselem;
	int totelem = 0;

	te = lb->last;
	if (te == NULL) return;
	tselem = TREESTORE(te);

	/* sorting rules; only object lists, ID lists, or deformgroups */
	if ( ELEM(tselem->type, TSE_DEFGROUP, TSE_ID_BASE) || (tselem->type == 0 && te->idcode == ID_OB)) {

		/* count first */
		for (te = lb->first; te; te = te->next) totelem++;

		if (totelem > 1) {
			tTreeSort *tear = MEM_mallocN(totelem * sizeof(tTreeSort), "tree sort array");
			tTreeSort *tp = tear;
			int skip = 0;

			for (te = lb->first; te; te = te->next, tp++) {
				tselem = TREESTORE(te);
				tp->te = te;
				tp->name = te->name;
				tp->idcode = te->idcode;

				if (tselem->type && tselem->type != TSE_DEFGROUP)
					tp->idcode = 0;  // don't sort this
				if (tselem->type == TSE_ID_BASE)
					tp->idcode = 1; // do sort this

				tp->id = tselem->id;
			}

			/* just sort alphabetically */
			if (tear->idcode == 1) {
				qsort(tear, totelem, sizeof(tTreeSort), treesort_alpha);
			}
			else {
				/* keep beginning of list */
				for (tp = tear, skip = 0; skip < totelem; skip++, tp++)
					if (tp->idcode) break;

				if (skip < totelem)
					qsort(tear + skip, totelem - skip, sizeof(tTreeSort), treesort_alpha_ob);
			}

			BLI_listbase_clear(lb);
			tp = tear;
			while (totelem--) {
				BLI_addtail(lb, tp->te);
				tp++;
			}
			MEM_freeN(tear);
		}
	}

	for (te = lb->first; te; te = te->next) {
		outliner_sort(&te->subtree);
	}
}

/* Filtering ----------------------------------------------- */

static bool outliner_filter_has_name(TreeElement *te, const char *name, int flags)
{
	int fn_flag = 0;

	if ((flags & SO_FIND_CASE_SENSITIVE) == 0)
		fn_flag |= FNM_CASEFOLD;

	return fnmatch(name, te->name, fn_flag) == 0;
}

static int outliner_filter_tree(SpaceOops *soops, ListBase *lb)
{
	TreeElement *te, *ten;
	TreeStoreElem *tselem;
	char search_buff[sizeof(((struct SpaceOops *)NULL)->search_string) + 2];
	char *search_string;

	/* although we don't have any search string, we return true
	 * since the entire tree is ok then...
	 */
	if (soops->search_string[0] == 0)
		return 1;

	if (soops->search_flags & SO_FIND_COMPLETE) {
		search_string = soops->search_string;
	}
	else {
		/* Implicitly add heading/trailing wildcards if needed. */
		BLI_strncpy_ensure_pad(search_buff, soops->search_string, '*', sizeof(search_buff));
		search_string = search_buff;
	}

	for (te = lb->first; te; te = ten) {
		ten = te->next;

		if (!outliner_filter_has_name(te, search_string, soops->search_flags)) {
			/* item isn't something we're looking for, but...
			 * - if the subtree is expanded, check if there are any matches that can be easily found
			 *     so that searching for "cu" in the default scene will still match the Cube
			 * - otherwise, we can't see within the subtree and the item doesn't match,
			 *     so these can be safely ignored (i.e. the subtree can get freed)
			 */
			tselem = TREESTORE(te);

			/* flag as not a found item */
			tselem->flag &= ~TSE_SEARCHMATCH;

			if ((!TSELEM_OPEN(tselem, soops)) || outliner_filter_tree(soops, &te->subtree) == 0) {
				outliner_free_tree(&te->subtree);
				BLI_remlink(lb, te);

				if (te->flag & TE_FREE_NAME) MEM_freeN((void *)te->name);
				MEM_freeN(te);
			}
		}
		else {
			tselem = TREESTORE(te);

			/* flag as a found item - we can then highlight it */
			tselem->flag |= TSE_SEARCHMATCH;

			/* filter subtree too */
			outliner_filter_tree(soops, &te->subtree);
		}
	}

	/* if there are still items in the list, that means that there were still some matches */
	return (BLI_listbase_is_empty(lb) == false);
}

/* ======================================================= */
/* Main Tree Building API */

/* Main entry point for building the tree data-structure that the outliner represents */
// TODO: split each mode into its own function?
void outliner_build_tree(Main *mainvar, Scene *scene, SpaceOops *soops)
{
	Base *base;
	TreeElement *te = NULL, *ten;
	TreeStoreElem *tselem;
	/* on first view, we open scenes */
	int show_opened = !soops->treestore || !BLI_mempool_len(soops->treestore);

	/* Are we looking for something - we want to tag parents to filter child matches
	 * - NOT in datablocks view - searching all datablocks takes way too long to be useful
	 * - this variable is only set once per tree build */
	if (soops->search_string[0] != 0 && soops->outlinevis != SO_DATABLOCKS)
		soops->search_flags |= SO_SEARCH_RECURSIVE;
	else
		soops->search_flags &= ~SO_SEARCH_RECURSIVE;

	if (soops->treehash && (soops->storeflag & SO_TREESTORE_REBUILD)) {
		soops->storeflag &= ~SO_TREESTORE_REBUILD;
		BKE_outliner_treehash_rebuild_from_treestore(soops->treehash, soops->treestore);
	}

	if (soops->tree.first && (soops->storeflag & SO_TREESTORE_REDRAW))
		return;

	outliner_free_tree(&soops->tree);
	outliner_storage_cleanup(soops);

	/* options */
	if (soops->outlinevis == SO_LIBRARIES) {
		Library *lib;

		/* current file first - mainvar provides tselem with unique pointer - not used */
		ten = outliner_add_element(soops, &soops->tree, mainvar, NULL, TSE_ID_BASE, 0);
		ten->name = IFACE_("Current File");

		tselem = TREESTORE(ten);
		if (!tselem->used)
			tselem->flag &= ~TSE_CLOSED;

		outliner_add_library_contents(mainvar, soops, ten, NULL);

		for (lib = mainvar->library.first; lib; lib = lib->id.next) {
			ten = outliner_add_element(soops, &soops->tree, lib, NULL, 0, 0);
			lib->id.newid = (ID *)ten;

			outliner_add_library_contents(mainvar, soops, ten, lib);

		}
		/* make hierarchy */
		ten = soops->tree.first;
		ten = ten->next;  /* first one is main */
		while (ten) {
			TreeElement *nten = ten->next, *par;
			tselem = TREESTORE(ten);
			lib = (Library *)tselem->id;
			if (lib && lib->parent) {
				par = (TreeElement *)lib->parent->id.newid;
				if (tselem->id->tag & LIB_TAG_INDIRECT) {
					/* Only remove from 'first level' if lib is not also directly used. */
					BLI_remlink(&soops->tree, ten);
					BLI_addtail(&par->subtree, ten);
					ten->parent = par;
				}
				else {
					/* Else, make a new copy of the libtree for our parent. */
					TreeElement *dupten = outliner_add_element(soops, &par->subtree, lib, NULL, 0, 0);
					outliner_add_library_contents(mainvar, soops, dupten, lib);
					dupten->parent = par;
				}
			}
			ten = nten;
		}
		/* restore newid pointers */
		for (lib = mainvar->library.first; lib; lib = lib->id.next)
			lib->id.newid = NULL;

	}
	else if (soops->outlinevis == SO_ALL_SCENES) {
		Scene *sce;
		for (sce = mainvar->scene.first; sce; sce = sce->id.next) {
			te = outliner_add_element(soops, &soops->tree, sce, NULL, 0, 0);
			tselem = TREESTORE(te);
			if (sce == scene && show_opened)
				tselem->flag &= ~TSE_CLOSED;

			for (base = sce->base.first; base; base = base->next) {
				ten = outliner_add_element(soops, &te->subtree, base->object, te, 0, 0);
				ten->directdata = base;
			}
			outliner_make_hierarchy(&te->subtree);
			/* clear id.newid, to prevent objects be inserted in wrong scenes (parent in other scene) */
			for (base = sce->base.first; base; base = base->next) base->object->id.newid = NULL;
		}
	}
	else if (soops->outlinevis == SO_CUR_SCENE) {

		for (base = scene->base.first; base; base = base->next) {
			ten = outliner_add_element(soops, &soops->tree, base->object, NULL, 0, 0);
			ten->directdata = base;
		}
		outliner_make_hierarchy(&soops->tree);
	}
	else if (soops->outlinevis == SO_VISIBLE) {
		for (base = scene->base.first; base; base = base->next) {
			if (base->lay & scene->lay)
				outliner_add_element(soops, &soops->tree, base->object, NULL, 0, 0);
		}
		outliner_make_hierarchy(&soops->tree);
	}
	else if (soops->outlinevis == SO_GROUPS) {
		Group *group;
		GroupObject *go;

		for (group = mainvar->group.first; group; group = group->id.next) {
			if (group->gobject.first) {
				te = outliner_add_element(soops, &soops->tree, group, NULL, 0, 0);

				for (go = group->gobject.first; go; go = go->next) {
					ten = outliner_add_element(soops, &te->subtree, go->ob, te, 0, 0);
					ten->directdata = NULL; /* eh, why? */
				}
				outliner_make_hierarchy(&te->subtree);
				/* clear id.newid, to prevent objects be inserted in wrong scenes (parent in other scene) */
				for (go = group->gobject.first; go; go = go->next) go->ob->id.newid = NULL;
			}
		}
	}
	else if (soops->outlinevis == SO_SAME_TYPE) {
		Object *ob = OBACT;
		if (ob) {
			for (base = scene->base.first; base; base = base->next) {
				if (base->object->type == ob->type) {
					ten = outliner_add_element(soops, &soops->tree, base->object, NULL, 0, 0);
					ten->directdata = base;
				}
			}
			outliner_make_hierarchy(&soops->tree);
		}
	}
	else if (soops->outlinevis == SO_SELECTED) {
		for (base = scene->base.first; base; base = base->next) {
			if (base->lay & scene->lay) {
				if (base->flag & SELECT) {
					ten = outliner_add_element(soops, &soops->tree, base->object, NULL, 0, 0);
					ten->directdata = base;
				}
			}
		}
		outliner_make_hierarchy(&soops->tree);
	}
	else if (soops->outlinevis == SO_DATABLOCKS) {
		PointerRNA mainptr;

		RNA_main_pointer_create(mainvar, &mainptr);

		ten = outliner_add_element(soops, &soops->tree, (void *)&mainptr, NULL, TSE_RNA_STRUCT, -1);

		if (show_opened) {
			tselem = TREESTORE(ten);
			tselem->flag &= ~TSE_CLOSED;
		}
	}
	else if (soops->outlinevis == SO_USERDEF) {
		PointerRNA userdefptr;

		RNA_pointer_create(NULL, &RNA_UserPreferences, &U, &userdefptr);

		ten = outliner_add_element(soops, &soops->tree, (void *)&userdefptr, NULL, TSE_RNA_STRUCT, -1);

		if (show_opened) {
			tselem = TREESTORE(ten);
			tselem->flag &= ~TSE_CLOSED;
		}
	}
	else if (soops->outlinevis == SO_ID_ORPHANS) {
		outliner_add_orphaned_datablocks(mainvar, soops);
	}
	else {
		ten = outliner_add_element(soops, &soops->tree, OBACT, NULL, 0, 0);
		if (ten) ten->directdata = BASACT;
	}

	if ((soops->flag & SO_SKIP_SORT_ALPHA) == 0) {
		outliner_sort(&soops->tree);
	}
	outliner_filter_tree(soops, &soops->tree);

	BKE_main_id_clear_newpoins(mainvar);
}
