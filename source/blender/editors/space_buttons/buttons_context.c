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
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 */

/** \file blender/editors/space_buttons/buttons_context.c
 *  \ingroup spbuttons
 */


#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"
#include "DNA_object_types.h"

#include "BKE_context.h"
#include "BKE_material.h"
#include "BKE_modifier.h"
#include "BKE_screen.h"
#include "BKE_texture.h"

#include "RNA_access.h"

#include "ED_buttons.h"
#include "ED_screen.h"
#include "ED_physics.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "buttons_intern.h" // own include

static int set_pointer_type(ButsContextPath *path, bContextDataResult *result, StructRNA *type)
{
	PointerRNA *ptr;
	int a;

	for (a = 0; a < path->len; a++) {
		ptr = &path->ptr[a];

		if (RNA_struct_is_a(ptr->type, type)) {
			CTX_data_pointer_set(result, ptr->id.data, ptr->type, ptr->data);
			return 1;
		}
	}

	return 0;
}

static PointerRNA *get_pointer_type(ButsContextPath *path, StructRNA *type)
{
	PointerRNA *ptr;
	int a;

	for (a = 0; a < path->len; a++) {
		ptr = &path->ptr[a];

		if (RNA_struct_is_a(ptr->type, type))
			return ptr;
	}

	return NULL;
}

/************************* Creating the Path ************************/

static int buttons_context_path_scene(ButsContextPath *path)
{
	PointerRNA *ptr = &path->ptr[path->len - 1];

	/* this one just verifies */
	return RNA_struct_is_a(ptr->type, &RNA_Scene);
}

/* note: this function can return 1 without adding a world to the path
 * so the buttons stay visible, but be sure to check the ID type if a ID_WO */
static int buttons_context_path_world(ButsContextPath *path)
{
	Scene *scene;
	World *world;
	PointerRNA *ptr = &path->ptr[path->len - 1];

	/* if we already have a (pinned) world, we're done */
	if (RNA_struct_is_a(ptr->type, &RNA_World)) {
		return 1;
	}
	/* if we have a scene, use the scene's world */
	else if (buttons_context_path_scene(path)) {
		scene = path->ptr[path->len - 1].data;
		world = scene->world;

		if (world) {
			RNA_id_pointer_create(&scene->world->id, &path->ptr[path->len]);
			path->len++;
			return 1;
		}
		else {
			return 1;
		}
	}

	/* no path to a world possible */
	return 0;
}

static int buttons_context_path_object(ButsContextPath *path)
{
	Scene *scene;
	Object *ob;
	PointerRNA *ptr = &path->ptr[path->len - 1];

	/* if we already have a (pinned) object, we're done */
	if (RNA_struct_is_a(ptr->type, &RNA_Object)) {
		return 1;
	}
	/* if we have a scene, use the scene's active object */
	else if (buttons_context_path_scene(path)) {
		scene = path->ptr[path->len - 1].data;
		ob = (scene->basact) ? scene->basact->object : NULL;

		if (ob) {
			RNA_id_pointer_create(&ob->id, &path->ptr[path->len]);
			path->len++;

			return 1;
		}
	}

	/* no path to a object possible */
	return 0;
}

static int buttons_context_path_data(ButsContextPath *path, int type)
{
	Object *ob;
	PointerRNA *ptr = &path->ptr[path->len - 1];

	/* if we already have a data, we're done */
	if (RNA_struct_is_a(ptr->type, &RNA_Mesh) && (type == -1 || type == OB_MESH)) return 1;
	else if (RNA_struct_is_a(ptr->type, &RNA_Curve) && (type == -1 || ELEM(type, OB_CURVE, OB_SURF, OB_FONT))) return 1;
	else if (RNA_struct_is_a(ptr->type, &RNA_Camera) && (type == -1 || type == OB_CAMERA)) return 1;
	else if (RNA_struct_is_a(ptr->type, &RNA_Lamp) && (type == -1 || type == OB_LAMP)) return 1;
	/* try to get an object in the path, no pinning supported here */
	else if (buttons_context_path_object(path)) {
		ob = path->ptr[path->len - 1].data;

		if (ob && (type == -1 || type == ob->type)) {
			RNA_id_pointer_create(ob->data, &path->ptr[path->len]);
			path->len++;

			return 1;
		}
	}

	/* no path to data possible */
	return 0;
}

static int buttons_context_path_modifier(ButsContextPath *path)
{
	Object *ob;

	if (buttons_context_path_object(path)) {
		ob = path->ptr[path->len - 1].data;

		if (ob && ELEM(ob->type, OB_MESH, OB_CURVE, OB_FONT, OB_SURF))
			return 1;
	}

	return 0;
}

static int buttons_context_path_material(ButsContextPath *path, bool for_texture, bool new_shading)
{
	Object *ob;
	PointerRNA *ptr = &path->ptr[path->len - 1];
	Material *ma;

	/* if we already have a (pinned) material, we're done */
	if (RNA_struct_is_a(ptr->type, &RNA_Material)) {
		return 1;
	}
	/* if we have an object, use the object material slot */
	else if (buttons_context_path_object(path)) {
		ob = path->ptr[path->len - 1].data;

		if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type)) {
			ma = give_current_material(ob, ob->actcol);
			RNA_id_pointer_create(&ma->id, &path->ptr[path->len]);
			path->len++;

			if (!new_shading) {
				RNA_id_pointer_create(&ma->id, &path->ptr[path->len]);
				path->len++;
			}

			return 1;
		}
	}

	/* no path to a material possible */
	return 0;
}

static int buttons_context_path_texture(ButsContextPath *path, ButsContextTexture *ct)
{
	if (ct) {
		/* new shading system */
		PointerRNA *ptr = &path->ptr[path->len - 1];
		ID *id;

		/* if we already have a (pinned) texture, we're done */
		if (RNA_struct_is_a(ptr->type, &RNA_Texture))
			return 1;

		if (!ct->user)
			return 0;

		id = ct->user->id;

		if (id) {
			if (GS(id->name) == ID_MA)
				buttons_context_path_material(path, false, true);
			else if (GS(id->name) == ID_WO)
				buttons_context_path_world(path);
			else if (GS(id->name) == ID_LA)
				buttons_context_path_data(path, OB_LAMP);
			else if (GS(id->name) == ID_OB)
				buttons_context_path_object(path);
		}

		if (ct->texture) {
			RNA_id_pointer_create(&ct->texture->id, &path->ptr[path->len]);
			path->len++;
		}

		return 1;
	}
	else {
		/* old shading system */
		Material *ma;
		Lamp *la;
		World *wo;
		Tex *tex;
		PointerRNA *ptr = &path->ptr[path->len - 1];

		/* if we already have a (pinned) texture, we're done */
		if (RNA_struct_is_a(ptr->type, &RNA_Texture)) {
			return 1;
		}
		/* try world */
		else if ((path->tex_ctx == SB_TEXC_WORLD) && buttons_context_path_world(path)) {
			wo = path->ptr[path->len - 1].data;

			if (wo && GS(wo->id.name) == ID_WO) {
				tex = give_current_world_texture(wo);

				RNA_id_pointer_create(&tex->id, &path->ptr[path->len]);
				path->len++;
				return 1;
			}
		}
		/* try material */
		else if ((path->tex_ctx == SB_TEXC_MATERIAL) && buttons_context_path_material(path, true, false)) {
			ma = path->ptr[path->len - 1].data;

			if (ma) {
				tex = give_current_material_texture(ma);

				RNA_id_pointer_create(&tex->id, &path->ptr[path->len]);
				path->len++;
				return 1;
			}
		}
		/* try lamp */
		else if ((path->tex_ctx == SB_TEXC_LAMP) && buttons_context_path_data(path, OB_LAMP)) {
			la = path->ptr[path->len - 1].data;

			if (la) {
				tex = give_current_lamp_texture(la);

				RNA_id_pointer_create(&tex->id, &path->ptr[path->len]);
				path->len++;
				return 1;
			}
		}
	}

	/* no path to a texture possible */
	return 0;
}

static int buttons_context_path(const bContext *C, ButsContextPath *path, int mainb, int flag)
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ID *id;
	int found;

	memset(path, 0, sizeof(*path));
	path->flag = flag;
	path->tex_ctx = sbuts->texture_context;

	/* if some ID datablock is pinned, set the root pointer */
	if (sbuts->pinid) {
		id = sbuts->pinid;

		RNA_id_pointer_create(id, &path->ptr[0]);
		path->len++;
	}

	/* no pinned root, use scene as root */
	if (path->len == 0) {
		id = (ID *)CTX_data_scene(C);
		RNA_id_pointer_create(id, &path->ptr[0]);
		path->len++;
	}

	/* now for each buttons context type, we try to construct a path,
	 * tracing back recursively */
	switch (mainb) {
		case BCONTEXT_SCENE:
		case BCONTEXT_CNC:
			found = buttons_context_path_scene(path);
			break;
		case BCONTEXT_WORLD:
			found = buttons_context_path_world(path);
			break;
		case BCONTEXT_OBJECT:
		case BCONTEXT_PHYSICS:
		case BCONTEXT_CONSTRAINT:
			found = buttons_context_path_object(path);
			break;
		case BCONTEXT_MODIFIER:
			found = buttons_context_path_modifier(path);
			break;
		case BCONTEXT_DATA:
			found = buttons_context_path_data(path, -1);
			break;
		case BCONTEXT_MATERIAL:
			found = buttons_context_path_material(path, false, (sbuts->texuser != NULL));
			break;
		case BCONTEXT_TEXTURE:
			found = buttons_context_path_texture(path, sbuts->texuser);
			break;
		default:
			found = 0;
			break;
	}

	return found;
}

static int buttons_shading_context(const bContext *C, int mainb)
{
	Object *ob = CTX_data_active_object(C);

	if (ELEM(mainb, BCONTEXT_MATERIAL, BCONTEXT_WORLD, BCONTEXT_TEXTURE))
		return 1;
	if (mainb == BCONTEXT_DATA && ob && ELEM(ob->type, OB_LAMP, OB_CAMERA))
		return 1;

	return 0;
}

static int buttons_shading_new_context(const bContext *C, int flag)
{
	Object *ob = CTX_data_active_object(C);

	if (flag & (1 << BCONTEXT_MATERIAL))
		return BCONTEXT_MATERIAL;
	else if (ob && ELEM(ob->type, OB_LAMP, OB_CAMERA) && (flag & (1 << BCONTEXT_DATA)))
		return BCONTEXT_DATA;
	else if (flag & (1 << BCONTEXT_WORLD))
		return BCONTEXT_WORLD;

	return BCONTEXT_CNC;
}

void buttons_context_compute(const bContext *C, SpaceButs *sbuts)
{
	ButsContextPath *path;
	PointerRNA *ptr;
	int a, pflag = 0, flag = 0;

	if (!sbuts->path)
		sbuts->path = MEM_callocN(sizeof(ButsContextPath), "ButsContextPath");

	path = sbuts->path;

	/* We need to set Scene path now! Else, buttons_texture_context_compute() might not get a valid scene. */
	buttons_context_path(C, path, BCONTEXT_SCENE, pflag);

	buttons_texture_context_compute(C, sbuts);

	/* for each context, see if we can compute a valid path to it, if
	 * this is the case, we know we have to display the button */
	for (a = 0; a < BCONTEXT_TOT; a++) {
		if (buttons_context_path(C, path, a, pflag)) {
			flag |= (1 << a);

			/* setting icon for data context */
			if (a == BCONTEXT_DATA) {
				ptr = &path->ptr[path->len - 1];

				if (ptr->type)
					sbuts->dataicon = RNA_struct_ui_icon(ptr->type);
				else
					sbuts->dataicon = ICON_EMPTY_DATA;
			}
		}
	}

	/* always try to use the tab that was explicitly
	 * set to the user, so that once that context comes
	 * back, the tab is activated again */
	sbuts->mainb = sbuts->mainbuser;

	/* in case something becomes invalid, change */
	if ((flag & (1 << sbuts->mainb)) == 0) {
		if (sbuts->flag & SB_SHADING_CONTEXT) {
			/* try to keep showing shading related buttons */
			sbuts->mainb = buttons_shading_new_context(C, flag);
		}
		else if (flag & BCONTEXT_OBJECT) {
			sbuts->mainb = BCONTEXT_OBJECT;
		}
		else {
			for (a = 0; a < BCONTEXT_TOT; a++) {
				if (flag & (1 << a)) {
					sbuts->mainb = a;
					break;
				}
			}
		}
	}

	buttons_context_path(C, path, sbuts->mainb, pflag);

	if (!(flag & (1 << sbuts->mainb))) {
		if (flag & (1 << BCONTEXT_OBJECT))
			sbuts->mainb = BCONTEXT_OBJECT;
		else
			sbuts->mainb = BCONTEXT_SCENE;
	}

	if (buttons_shading_context(C, sbuts->mainb))
		sbuts->flag |= SB_SHADING_CONTEXT;
	else
		sbuts->flag &= ~SB_SHADING_CONTEXT;

	sbuts->pathflag = flag;
}

/************************* Context Callback ************************/

const char *buttons_context_dir[] = {
	"texture_slot", "scene", "world", "object", "mesh", "curve",
	"lamp", "camera", "material", "material_slot",
	"texture", "texture_user", "texture_user_property", "collision", 
	NULL
};

int buttons_context(const bContext *C, const char *member, bContextDataResult *result)
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextPath *path = sbuts ? sbuts->path : NULL;

	if (!path)
		return 0;

	/* here we handle context, getting data from precomputed path */
	if (CTX_data_dir(member)) {
		/* in case of new shading system we skip texture_slot, complex python
		 * UI script logic depends on checking if this is available */
		if (sbuts->texuser)
			CTX_data_dir_set(result, buttons_context_dir + 1);
		else
			CTX_data_dir_set(result, buttons_context_dir);
		return 1;
	}
	else if (CTX_data_equals(member, "scene")) {
		/* Do not return one here if scene not found in path,
		 * in this case we want to get default context scene! */
		return set_pointer_type(path, result, &RNA_Scene);
	}
	else if (CTX_data_equals(member, "world")) {
		set_pointer_type(path, result, &RNA_World);
		return 1;
	}
	else if (CTX_data_equals(member, "object")) {
		set_pointer_type(path, result, &RNA_Object);
		return 1;
	}
	else if (CTX_data_equals(member, "mesh")) {
		set_pointer_type(path, result, &RNA_Mesh);
		return 1;
	}
	else if (CTX_data_equals(member, "curve")) {
		set_pointer_type(path, result, &RNA_Curve);
		return 1;
	}
	else if (CTX_data_equals(member, "lamp")) {
		set_pointer_type(path, result, &RNA_Lamp);
		return 1;
	}
	else if (CTX_data_equals(member, "camera")) {
		set_pointer_type(path, result, &RNA_Camera);
		return 1;
	}
	else if (CTX_data_equals(member, "material")) {
		set_pointer_type(path, result, &RNA_Material);
		return 1;
	}
	else if (CTX_data_equals(member, "texture")) {
		ButsContextTexture *ct = sbuts->texuser;

		if (ct) {
			/* new shading system */
			CTX_data_pointer_set(result, &ct->texture->id, &RNA_Texture, ct->texture);
		}
		else {
			/* old shading system */
			set_pointer_type(path, result, &RNA_Texture);
		}

		return 1;
	}
	else if (CTX_data_equals(member, "material_slot")) {
		PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

		if (ptr) {
			Object *ob = ptr->data;

			if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type) && ob->totcol) {
				/* a valid actcol isn't ensured [#27526] */
				int matnr = ob->actcol - 1;
				if (matnr < 0) matnr = 0;
				CTX_data_pointer_set(result, &ob->id, &RNA_MaterialSlot, &ob->mat[matnr]);
			}
		}

		return 1;
	}
	else if (CTX_data_equals(member, "texture_user")) {
		ButsContextTexture *ct = sbuts->texuser;

		if (!ct)
			return -1;  /* old shading system (found but not available) */

		if (ct->user && ct->user->ptr.data) {
			ButsTextureUser *user = ct->user;
			CTX_data_pointer_set(result, user->ptr.id.data, user->ptr.type, user->ptr.data);
		}

		return 1;
	}
	else if (CTX_data_equals(member, "texture_user_property")) {
		ButsContextTexture *ct = sbuts->texuser;

		if (!ct)
			return -1;  /* old shading system (found but not available) */

		if (ct->user && ct->user->ptr.data) {
			ButsTextureUser *user = ct->user;
			CTX_data_pointer_set(result, NULL, &RNA_Property, user->prop);
		}

		return 1;
	}
	else if (CTX_data_equals(member, "texture_slot")) {
		ButsContextTexture *ct = sbuts->texuser;
		PointerRNA *ptr;

		if (ct) {
			return 0;  /* new shading system */
		}
		else if ((ptr = get_pointer_type(path, &RNA_Material))) {
			Material *ma = ptr->data;

			/* if we have a node material, get slot from material in material node */
			if (ma) {
				CTX_data_pointer_set(result, &ma->id, &RNA_MaterialTextureSlot, ma->mtex[(int)ma->texact]);
			}
		}
		else if ((ptr = get_pointer_type(path, &RNA_Lamp))) {
			Lamp *la = ptr->data;

			if (la)
				CTX_data_pointer_set(result, &la->id, &RNA_LampTextureSlot, la->mtex[(int)la->texact]);
		}
		else if ((ptr = get_pointer_type(path, &RNA_World))) {
			World *wo = ptr->data;

			if (wo)
				CTX_data_pointer_set(result, &wo->id, &RNA_WorldTextureSlot, wo->mtex[(int)wo->texact]);
		}

		return 1;
	}
	else if (CTX_data_equals(member, "collision")) {
		PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

		if (ptr && ptr->data) {
			Object *ob = ptr->data;
			ModifierData *md = modifiers_findByType(ob, eModifierType_Collision);
			CTX_data_pointer_set(result, &ob->id, &RNA_CollisionModifier, md);
			return 1;
		}
	}
	else {
		return 0; /* not found */
	}

	return -1; /* found but not available */
}

/************************* Drawing the Path ************************/

static void pin_cb(bContext *C, void *UNUSED(arg1), void *UNUSED(arg2))
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);

	if (sbuts->flag & SB_PIN_CONTEXT) {
		sbuts->pinid = buttons_context_id_path(C);
	}
	else
		sbuts->pinid = NULL;

	ED_area_tag_redraw(CTX_wm_area(C));
}

void buttons_context_draw(const bContext *C, uiLayout *layout)
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextPath *path = sbuts->path;
	uiLayout *row;
	uiBlock *block;
	uiBut *but;
	PointerRNA *ptr;
	char namebuf[128], *name;
	int a, icon;

	if (!path)
		return;

	row = uiLayoutRow(layout, true);
	uiLayoutSetAlignment(row, UI_LAYOUT_ALIGN_LEFT);

	block = uiLayoutGetBlock(row);
	UI_block_emboss_set(block, UI_EMBOSS_NONE);
	but = uiDefIconButBitC(block, UI_BTYPE_ICON_TOGGLE, SB_PIN_CONTEXT, 0, ICON_UNPINNED, 0, 0, UI_UNIT_X, UI_UNIT_Y, &sbuts->flag,
	                       0, 0, 0, 0, TIP_("Follow context or keep fixed data-block displayed"));
	UI_but_flag_disable(but, UI_BUT_UNDO); /* skip undo on screen buttons */
	UI_but_func_set(but, pin_cb, NULL, NULL);

	for (a = 0; a < path->len; a++) {
		ptr = &path->ptr[a];

		if (a != 0)
			uiItemL(row, "", VICO_SMALL_TRI_RIGHT_VEC);

		if (ptr->data) {
			icon = RNA_struct_ui_icon(ptr->type);
			name = RNA_struct_name_get_alloc(ptr, namebuf, sizeof(namebuf), NULL);

			if (name) {
				if (!ELEM(sbuts->mainb, BCONTEXT_CNC, BCONTEXT_SCENE) && ptr->type == &RNA_Scene)
					uiItemLDrag(row, ptr, "", icon);  /* save some space */
				else
					uiItemLDrag(row, ptr, name, icon);

				if (name != namebuf)
					MEM_freeN(name);
			}
			else
				uiItemL(row, "", icon);
		}
	}
}

static void buttons_panel_context(const bContext *C, Panel *pa)
{
	buttons_context_draw(C, pa->layout);
}

void buttons_context_register(ARegionType *art)
{
	PanelType *pt;

	pt = MEM_callocN(sizeof(PanelType), "spacetype buttons panel context");
	strcpy(pt->idname, "BUTTONS_PT_context");
	strcpy(pt->label, N_("Context"));  /* XXX C panels unavailable through RNA bpy.types! */
	strcpy(pt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
	pt->draw = buttons_panel_context;
	pt->flag = PNL_NO_HEADER;
	BLI_addtail(&art->paneltypes, pt);
}

ID *buttons_context_id_path(const bContext *C)
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextPath *path = sbuts->path;
	PointerRNA *ptr;
	int a;

	if (path->len) {
		for (a = path->len - 1; a >= 0; a--) {
			ptr = &path->ptr[a];


			if (ptr->id.data) {
				return ptr->id.data;
			}
		}
	}

	return NULL;
}
