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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 */

/** \file blender/editors/object/object_lod.c
 *  \ingroup edobj
 */

#include "DNA_object_types.h"

#include "BKE_context.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "ED_screen.h"
#include "ED_object.h"

#include "object_intern.h"

static int object_lod_add_exec(bContext *C, wmOperator *UNUSED(op))
{
	return OPERATOR_FINISHED;
}

void OBJECT_OT_lod_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Level of Detail";
	ot->description = "Add a level of detail to this object";
	ot->idname = "OBJECT_OT_lod_add";

	/* api callbacks */
	ot->exec = object_lod_add_exec;
	ot->poll = ED_operator_object_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int object_lod_remove_exec(bContext *C, wmOperator *op)
{

	WM_event_add_notifier(C, NC_OBJECT | ND_LOD, CTX_wm_view3d(C));
	return OPERATOR_FINISHED;
}

void OBJECT_OT_lod_remove(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Remove Level of Detail";
	ot->description = "Remove a level of detail from this object";
	ot->idname = "OBJECT_OT_lod_remove";

	/* api callbacks */
	ot->exec = object_lod_remove_exec;
	ot->poll = ED_operator_object_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ot->prop = RNA_def_int(ot->srna, "index", 1, 1, INT_MAX, "Index", "", 1, INT_MAX);
}
