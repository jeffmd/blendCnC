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

/** \file blender/python/intern/bpy_driver.c
 *  \ingroup pythonintern
 *
 * This file defines the 'BPY_driver_exec' to execute python driver expressions,
 * called by the animation system, there are also some utility functions
 * to deal with the namespace used for driver execution.
 */

/* ****************************************** */
/* Drivers - PyExpression Evaluation */

#include <Python.h>

#include "BLI_listbase.h"
#include "BLI_math_base.h"
#include "BLI_string.h"

#include "BKE_global.h"

#include "bpy_rna_driver.h"  /* for pyrna_driver_get_variable_value */

#include "bpy_intern_string.h"

#include "bpy_driver.h"

extern void BPY_update_rna_module(void);

#define USE_RNA_AS_PYOBJECT

#define USE_BYTECODE_WHITELIST

#ifdef USE_BYTECODE_WHITELIST
#  include <opcode.h>
#endif

/* for pydrivers (drivers using one-line Python expressions to express relationships between targets) */
PyObject *bpy_pydriver_Dict = NULL;

#ifdef USE_BYTECODE_WHITELIST
static PyObject *bpy_pydriver_Dict__whitelist = NULL;
#endif

/* For faster execution we keep a special dictionary for pydrivers, with
 * the needed modules and aliases.
 */
int bpy_pydriver_create_dict(void)
{
	PyObject *d, *mod;

	/* validate namespace for driver evaluation */
	if (bpy_pydriver_Dict) return -1;

	d = PyDict_New();
	if (d == NULL)
		return -1;
	else
		bpy_pydriver_Dict = d;

	/* import some modules: builtins, bpy, math, (Blender.noise)*/
	PyDict_SetItemString(d, "__builtins__", PyEval_GetBuiltins());

	mod = PyImport_ImportModule("math");
	if (mod) {
		PyDict_Merge(d, PyModule_GetDict(mod), 0); /* 0 - don't overwrite existing values */
		Py_DECREF(mod);
	}
#ifdef USE_BYTECODE_WHITELIST
	PyObject *mod_math = mod;
#endif

	/* add bpy to global namespace */
	mod = PyImport_ImportModuleLevel("bpy", NULL, NULL, NULL, 0);
	if (mod) {
		PyDict_SetItemString(bpy_pydriver_Dict, "bpy", mod);
		Py_DECREF(mod);
	}

	/* add noise to global namespace */
	mod = PyImport_ImportModuleLevel("mathutils", NULL, NULL, NULL, 0);
	if (mod) {
		PyObject *modsub = PyDict_GetItemString(PyModule_GetDict(mod), "noise");
		PyDict_SetItemString(bpy_pydriver_Dict, "noise", modsub);
		Py_DECREF(mod);
	}

#ifdef USE_BYTECODE_WHITELIST
	/* setup the whitelist */
	{
		bpy_pydriver_Dict__whitelist = PyDict_New();
		const char *whitelist[] = {
			/* builtins (basic) */
			"all",
			"any",
			"len",
			/* builtins (numeric) */
			"max",
			"min",
			"pow",
			"round",
			"sum",
			/* types */
			"bool",
			"float",
			"int",

			NULL,
		};

		for (int i = 0; whitelist[i]; i++) {
			PyDict_SetItemString(bpy_pydriver_Dict__whitelist, whitelist[i], Py_None);
		}

		/* Add all of 'math' functions. */
		if (mod_math != NULL) {
			PyObject *mod_math_dict = PyModule_GetDict(mod_math);
			PyObject *arg_key, *arg_value;
			Py_ssize_t arg_pos = 0;
			while (PyDict_Next(mod_math_dict, &arg_pos, &arg_key, &arg_value)) {
				const char *arg_str = _PyUnicode_AsString(arg_key);
				if (arg_str[0] && arg_str[1] != '_') {
					PyDict_SetItem(bpy_pydriver_Dict__whitelist, arg_key, Py_None);
				}
			}
		}
	}
#endif  /* USE_BYTECODE_WHITELIST */

	return 0;
}

/* note, this function should do nothing most runs, only when changing frame */
/* not thread safe but neither is python */
static struct {
	float evaltime;

	/* borrowed reference to the 'self' in 'bpy_pydriver_Dict'
	 * keep for as long as the same self is used. */
	PyObject *self;
} g_pydriver_state_prev = {
	.evaltime = FLT_MAX,
	.self = NULL,
};

/* Update function, it gets rid of pydrivers global dictionary, forcing
 * BPY_driver_exec to recreate it. This function is used to force
 * reloading the Blender text module "pydrivers.py", if available, so
 * updates in it reach pydriver evaluation.
 */
void BPY_driver_reset(void)
{
	PyGILState_STATE gilstate;
	bool use_gil = true; /* !PyC_IsInterpreterActive(); */

	if (use_gil)
		gilstate = PyGILState_Ensure();

	if (bpy_pydriver_Dict) { /* free the global dict used by pydrivers */
		PyDict_Clear(bpy_pydriver_Dict);
		Py_DECREF(bpy_pydriver_Dict);
		bpy_pydriver_Dict = NULL;
	}

#ifdef USE_BYTECODE_WHITELIST
	if (bpy_pydriver_Dict__whitelist) {
		PyDict_Clear(bpy_pydriver_Dict__whitelist);
		Py_DECREF(bpy_pydriver_Dict__whitelist);
		bpy_pydriver_Dict__whitelist = NULL;
	}
#endif

	g_pydriver_state_prev.evaltime = FLT_MAX;

	/* freed when clearing driver dict */
	g_pydriver_state_prev.self = NULL;

	if (use_gil)
		PyGILState_Release(gilstate);

	return;
}


#ifdef USE_BYTECODE_WHITELIST

#define OK_OP(op) [op] = 1

static const char secure_opcodes[255] = {
	OK_OP(POP_TOP),
	OK_OP(ROT_TWO),
	OK_OP(ROT_THREE),
	OK_OP(DUP_TOP),
	OK_OP(DUP_TOP_TWO),
	OK_OP(NOP),
	OK_OP(UNARY_POSITIVE),
	OK_OP(UNARY_NEGATIVE),
	OK_OP(UNARY_NOT),
	OK_OP(UNARY_INVERT),
	OK_OP(BINARY_MATRIX_MULTIPLY),
	OK_OP(INPLACE_MATRIX_MULTIPLY),
	OK_OP(BINARY_POWER),
	OK_OP(BINARY_MULTIPLY),
	OK_OP(BINARY_MODULO),
	OK_OP(BINARY_ADD),
	OK_OP(BINARY_SUBTRACT),
	OK_OP(BINARY_SUBSCR),
	OK_OP(BINARY_FLOOR_DIVIDE),
	OK_OP(BINARY_TRUE_DIVIDE),
	OK_OP(INPLACE_FLOOR_DIVIDE),
	OK_OP(INPLACE_TRUE_DIVIDE),
	OK_OP(INPLACE_ADD),
	OK_OP(INPLACE_SUBTRACT),
	OK_OP(INPLACE_MULTIPLY),
	OK_OP(INPLACE_MODULO),
	OK_OP(BINARY_LSHIFT),
	OK_OP(BINARY_RSHIFT),
	OK_OP(BINARY_AND),
	OK_OP(BINARY_XOR),
	OK_OP(BINARY_OR),
	OK_OP(INPLACE_POWER),
	OK_OP(INPLACE_LSHIFT),
	OK_OP(INPLACE_RSHIFT),
	OK_OP(INPLACE_AND),
	OK_OP(INPLACE_XOR),
	OK_OP(INPLACE_OR),
	OK_OP(RETURN_VALUE),
	OK_OP(BUILD_TUPLE),
	OK_OP(BUILD_LIST),
	OK_OP(BUILD_SET),
	OK_OP(BUILD_MAP),
	OK_OP(COMPARE_OP),
	OK_OP(JUMP_FORWARD),
	OK_OP(JUMP_IF_FALSE_OR_POP),
	OK_OP(JUMP_IF_TRUE_OR_POP),
	OK_OP(JUMP_ABSOLUTE),
	OK_OP(POP_JUMP_IF_FALSE),
	OK_OP(POP_JUMP_IF_TRUE),
	OK_OP(LOAD_GLOBAL),
	OK_OP(LOAD_FAST),
	OK_OP(STORE_FAST),
	OK_OP(DELETE_FAST),
	OK_OP(LOAD_DEREF),
	OK_OP(STORE_DEREF),

	/* special cases */
	OK_OP(LOAD_CONST),         /* ok because constants are accepted */
	OK_OP(LOAD_NAME),          /* ok, because PyCodeObject.names is checked */
	OK_OP(CALL_FUNCTION),      /* ok, because we check its 'name' before calling */
	OK_OP(CALL_FUNCTION_KW),
	OK_OP(CALL_FUNCTION_EX),
};

#undef OK_OP


#endif  /* USE_BYTECODE_WHITELIST */
