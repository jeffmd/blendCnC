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

/** \file blender/editors/space_buttons/buttons_texture.c
 *  \ingroup spbuttons
 */


#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_ID.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_world_types.h"

#include "BKE_context.h"
#include "BKE_material.h"
#include "BKE_modifier.h"
#include "BKE_scene.h"

#include "RNA_access.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "ED_buttons.h"
#include "ED_screen.h"

#include "../interface/interface_intern.h"

#include "buttons_intern.h" // own include

/****************** "Old Shading" Texture Context ****************/

bool ED_texture_context_check_world(const bContext *C)
{
	Scene *scene = CTX_data_scene(C);
	return (scene && scene->world);
}

bool ED_texture_context_check_material(const bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	return (ob && (ob->totcol != 0));
}

bool ED_texture_context_check_lamp(const bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	return (ob && (ob->type == OB_LAMP));
}

static void texture_context_check_modifier_foreach(void *userData, Object *UNUSED(ob), ModifierData *UNUSED(md),
                                                   const char *UNUSED(propname))
{
	*((bool *)userData) = true;
}

bool ED_texture_context_check_others(const bContext *C)
{
	/* We cannot rely on sbuts->texuser here, as it is NULL when in "old" tex handling, non-OTHERS tex context. */
	Object *ob = CTX_data_active_object(C);

	/* object */
	if (ob) {
		/* modifiers */
		{
			bool check = false;
			modifiers_foreachTexLink(ob, texture_context_check_modifier_foreach, &check);
			if (check) {
				return true;
			}
		}
	}


	return false;
}

static void set_texture_context(const bContext *C, SpaceButs *sbuts)
{
	{
		bool valid_world = ED_texture_context_check_world(C);
		bool valid_material = ED_texture_context_check_material(C);
		bool valid_lamp = ED_texture_context_check_lamp(C);
		bool valid_others = ED_texture_context_check_others(C);

		/* this is similar to direct user action, no need to keep "better" ctxt in _prev */
		if ((sbuts->mainb == BCONTEXT_WORLD) && valid_world) {
			sbuts->texture_context = sbuts->texture_context_prev = SB_TEXC_WORLD;
		}
		else if ((sbuts->mainb == BCONTEXT_MATERIAL) && valid_material) {
			sbuts->texture_context = sbuts->texture_context_prev = SB_TEXC_MATERIAL;
		}
		else if ((sbuts->mainb == BCONTEXT_DATA) && valid_lamp) {
			sbuts->texture_context = sbuts->texture_context_prev = SB_TEXC_LAMP;
		}
		else if ((ELEM(sbuts->mainb, BCONTEXT_MODIFIER, BCONTEXT_PHYSICS)) && valid_others) {
			sbuts->texture_context = sbuts->texture_context_prev = SB_TEXC_OTHER;
		}
		/* Else, try to revive a previous "better" ctxt... */
		else if ((sbuts->texture_context_prev != sbuts->texture_context) &&
		         (((sbuts->texture_context_prev == SB_TEXC_WORLD) && valid_world) ||
		          ((sbuts->texture_context_prev == SB_TEXC_MATERIAL) && valid_material) ||
		          ((sbuts->texture_context_prev == SB_TEXC_LAMP) && valid_lamp) ||
		          ((sbuts->texture_context_prev == SB_TEXC_OTHER) && valid_others)))
		{
			sbuts->texture_context = sbuts->texture_context_prev;
		}
		/* Else, just be sure that current context is valid! */
		else if (((sbuts->texture_context == SB_TEXC_WORLD) && !valid_world) ||
		         ((sbuts->texture_context == SB_TEXC_MATERIAL) && !valid_material) ||
		         ((sbuts->texture_context == SB_TEXC_LAMP) && !valid_lamp) ||
		         ((sbuts->texture_context == SB_TEXC_OTHER) && !valid_others))
		{
			/* this is default fallback, do keep "better" ctxt in _prev */
			sbuts->texture_context_prev = sbuts->texture_context;
			if (valid_material) {
				sbuts->texture_context = SB_TEXC_MATERIAL;
			}
			else if (valid_lamp) {
				sbuts->texture_context = SB_TEXC_LAMP;
			}
			else if (valid_world) {
				sbuts->texture_context = SB_TEXC_WORLD;
			}
			else if (valid_others) {
				sbuts->texture_context = SB_TEXC_OTHER;
			}
		}
	}
}

/************************* Texture User **************************/

static void buttons_texture_user_property_add(ListBase *users, ID *id,
                                              PointerRNA ptr, PropertyRNA *prop,
                                              const char *category, int icon, const char *name)
{
	ButsTextureUser *user = MEM_callocN(sizeof(ButsTextureUser), "ButsTextureUser");

	user->id = id;
	user->ptr = ptr;
	user->prop = prop;
	user->category = category;
	user->icon = icon;
	user->name = name;
	user->index = BLI_listbase_count(users);

	BLI_addtail(users, user);
}

static void buttons_texture_modifier_foreach(void *userData, Object *ob, ModifierData *md, const char *propname)
{
	PointerRNA ptr;
	PropertyRNA *prop;
	ListBase *users = userData;

	RNA_pointer_create(&ob->id, &RNA_Modifier, md, &ptr);
	prop = RNA_struct_find_property(&ptr, propname);

	buttons_texture_user_property_add(users, &ob->id, ptr, prop,
	                                  N_("Modifiers"), RNA_struct_ui_icon(ptr.type), md->name);
}

static void buttons_texture_users_from_context(ListBase *users, const bContext *C, SpaceButs *sbuts)
{
	Scene *scene = NULL;
	Object *ob = NULL;
	Material *ma = NULL;
	Lamp *la = NULL;
	ID *pinid = sbuts->pinid;

	/* get data from context */
	if (pinid) {
		if (GS(pinid->name) == ID_SCE)
			scene = (Scene *)pinid;
		else if (GS(pinid->name) == ID_OB)
			ob = (Object *)pinid;
		else if (GS(pinid->name) == ID_LA)
			la = (Lamp *)pinid;
		else if (GS(pinid->name) == ID_MA)
			ma = (Material *)pinid;
	}

	if (!scene)
		scene = CTX_data_scene(C);

	if (ob && ob->type == OB_LAMP && !la)
		la = ob->data;
	if (ob && !ma)
		ma = give_current_material(ob, ob->actcol);

	/* fill users */
	BLI_listbase_clear(users);

	if (ob) {
		/* modifiers */
		modifiers_foreachTexLink(ob, buttons_texture_modifier_foreach, users);

	}

}

void buttons_texture_context_compute(const bContext *C, SpaceButs *sbuts)
{
	/* gather available texture users in context. runs on every draw of
	 * properties editor, before the buttons are created. */
	ButsContextTexture *ct = sbuts->texuser;
	ID *pinid = sbuts->pinid;

	set_texture_context(C, sbuts);

	if (!((sbuts->texture_context == SB_TEXC_OTHER) )) {
		if (ct) {
			BLI_freelistN(&ct->users);
			MEM_freeN(ct);
			sbuts->texuser = NULL;
		}

		return;
	}

	if (!ct) {
		ct = MEM_callocN(sizeof(ButsContextTexture), "ButsContextTexture");
		sbuts->texuser = ct;
	}
	else {
		BLI_freelistN(&ct->users);
	}

	buttons_texture_users_from_context(&ct->users, C, sbuts);

	if (pinid && GS(pinid->name) == ID_TE) {
		ct->user = NULL;
		ct->texture = (Tex *)pinid;
	}
	else {
		/* set one user as active based on active index */
		if (ct->index >= BLI_listbase_count_at_most(&ct->users, ct->index + 1))
			ct->index = 0;

		ct->user = BLI_findlink(&ct->users, ct->index);
		ct->texture = NULL;

		if (ct->user) {
			if (ct->user->ptr.data) {
				PointerRNA texptr;
				Tex *tex;

				/* get texture datablock pointer if it's a property */
				texptr = RNA_property_pointer_get(&ct->user->ptr, ct->user->prop);
				tex = (RNA_struct_is_a(texptr.type, &RNA_Texture)) ? texptr.data : NULL;

				ct->texture = tex;
			}
		}
	}
}

static void template_texture_select(bContext *C, void *user_p, void *UNUSED(arg))
{
	/* callback when selecting a texture user in the menu */
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextTexture *ct = (sbuts) ? sbuts->texuser : NULL;
	ButsTextureUser *user = (ButsTextureUser *)user_p;
	PointerRNA texptr;
	Tex *tex;

	if (!ct)
		return;

	/* set user as active */
	{
		texptr = RNA_property_pointer_get(&user->ptr, user->prop);
		tex = (RNA_struct_is_a(texptr.type, &RNA_Texture)) ? texptr.data : NULL;

		ct->texture = tex;

		if (sbuts && tex)
			sbuts->preview = 1;
	}

	ct->user = user;
	ct->index = user->index;
}

static void template_texture_user_menu(bContext *C, uiLayout *layout, void *UNUSED(arg))
{
	/* callback when opening texture user selection menu, to create buttons. */
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextTexture *ct = sbuts->texuser;
	ButsTextureUser *user;
	uiBlock *block = uiLayoutGetBlock(layout);
	const char *last_category = NULL;

	for (user = ct->users.first; user; user = user->next) {
		uiBut *but;
		char name[UI_MAX_NAME_STR];

		/* add label per category */
		if (!last_category || !STREQ(last_category, user->category)) {
			uiItemL(layout, IFACE_(user->category), ICON_NONE);
			but = block->buttons.last;
			but->drawflag = UI_BUT_TEXT_LEFT;
		}

		/* create button */
		if (user->prop) {
			PointerRNA texptr = RNA_property_pointer_get(&user->ptr, user->prop);
			Tex *tex = texptr.data;

			if (tex)
				BLI_snprintf(name, UI_MAX_NAME_STR, "  %s - %s", user->name, tex->id.name + 2);
			else
				BLI_snprintf(name, UI_MAX_NAME_STR, "  %s", user->name);
		}
		else
			BLI_snprintf(name, UI_MAX_NAME_STR, "  %s", user->name);

		but = uiDefIconTextBut(block, UI_BTYPE_BUT, 0, user->icon, name, 0, 0, UI_UNIT_X * 4, UI_UNIT_Y,
		                       NULL, 0.0, 0.0, 0.0, 0.0, "");
		UI_but_funcN_set(but, template_texture_select, MEM_dupallocN(user), NULL);

		last_category = user->category;
	}

	UI_block_flag_enable(block, UI_BLOCK_NO_FLIP);
}

void uiTemplateTextureUser(uiLayout *layout, bContext *C)
{
	/* texture user selection dropdown menu. the available users have been
	 * gathered before drawing in ButsContextTexture, we merely need to
	 * display the current item. */
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextTexture *ct = (sbuts) ? sbuts->texuser : NULL;
	uiBlock *block = uiLayoutGetBlock(layout);
	uiBut *but;
	ButsTextureUser *user;
	char name[UI_MAX_NAME_STR];

	if (!ct)
		return;

	/* get current user */
	user = ct->user;

	if (!user) {
		uiItemL(layout, IFACE_("No textures in context"), ICON_NONE);
		return;
	}

	/* create button */
	BLI_strncpy(name, user->name, UI_MAX_NAME_STR);

	if (user->icon) {
		but = uiDefIconTextMenuBut(block, template_texture_user_menu, NULL,
		                           user->icon, name, 0, 0, UI_UNIT_X * 4, UI_UNIT_Y, "");
	}
	else {
		but = uiDefMenuBut(block, template_texture_user_menu, NULL,
		                   name, 0, 0, UI_UNIT_X * 4, UI_UNIT_Y, "");
	}

	/* some cosmetic tweaks */
	UI_but_type_set_menu_from_pulldown(but);

	but->flag &= ~UI_BUT_ICON_SUBMENU;
}

/************************* Texture Show **************************/

static void template_texture_show(bContext *C, void *data_p, void *prop_p)
{
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextTexture *ct = (sbuts) ? sbuts->texuser : NULL;
	ButsTextureUser *user;

	if (!ct)
		return;

	for (user = ct->users.first; user; user = user->next)
		if (user->ptr.data == data_p && user->prop == prop_p)
			break;

	if (user) {
		/* select texture */
		template_texture_select(C, user, NULL);

		/* change context */
		sbuts->mainb = BCONTEXT_TEXTURE;
		sbuts->mainbuser = sbuts->mainb;
		sbuts->preview = 1;

		/* redraw editor */
		ED_area_tag_redraw(CTX_wm_area(C));
	}
}

void uiTemplateTextureShow(uiLayout *layout, bContext *C, PointerRNA *ptr, PropertyRNA *prop)
{
	/* button to quickly show texture in texture tab */
	SpaceButs *sbuts = CTX_wm_space_buts(C);
	ButsContextTexture *ct = (sbuts) ? sbuts->texuser : NULL;
	ButsTextureUser *user;

	/* only show button in other tabs in properties editor */
	if (!ct || sbuts->mainb == BCONTEXT_TEXTURE)
		return;

	/* find corresponding texture user */
	for (user = ct->users.first; user; user = user->next)
		if (user->ptr.data == ptr->data && user->prop == prop)
			break;

	/* draw button */
	if (user) {
		uiBlock *block = uiLayoutGetBlock(layout);
		uiBut *but;

		but = uiDefIconBut(block, UI_BTYPE_BUT, 0, ICON_BUTS, 0, 0, UI_UNIT_X, UI_UNIT_Y,
		                   NULL, 0.0, 0.0, 0.0, 0.0, TIP_("Show texture in texture tab"));
		UI_but_func_set(but, template_texture_show, user->ptr.data, user->prop);
	}
}
