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

/** \file creator/creator.c
 *  \ingroup creator
 */

#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#  if defined(_MSC_VER) && defined(_M_X64)
#    include <math.h> /* needed for _set_FMA3_enable */
#  endif
#  include <windows.h>
#  include "utfconv.h"
#endif

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#include "DNA_genfile.h"

#include "BLI_args.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_callbacks.h"
#include "BLI_string.h"
#include "BLI_system.h"

/* mostly init functions */
#include "BKE_appdir.h"
#include "BKE_blender.h"
#include "BKE_cachefile.h"
#include "BKE_context.h"
#include "BKE_font.h"
#include "BKE_global.h"
#include "BKE_material.h"
#include "BKE_modifier.h"
#include "BKE_image.h"


#include "IMB_imbuf.h"  /* for IMB_init */

#include "ED_datafiles.h"

#include "WM_api.h"

#include "RNA_define.h"

/* for passing information between creator */
#define SYS_SystemHandle int

#include <signal.h>

#ifdef WITH_BINRELOC
#  include "binreloc.h"
#endif

#include "creator_intern.h"  /* own include */

/* written to by 'creator_args.c' */
struct ApplicationState app_state = {
	.signal = {
		.use_crash_handler = true,
		.use_abort_handler = true,
	},
	.exit_code_on_error = {
		.python = 0,
	}
};

/* -------------------------------------------------------------------- */

/** \name Application Level Callbacks
 *
 * Initialize callbacks for the modules that need them.
 *
 * \{ */

static void callback_mem_error(const char *errorStr)
{
	fputs(errorStr, stderr);
	fflush(stderr);
}

static void main_callback_setup(void)
{
	/* Error output from the alloc routines: */
	MEM_set_error_callback(callback_mem_error);
}

/* free data on early exit (if Python calls 'sys.exit()' while parsing args for eg). */
struct CreatorAtExitData {
	bArgs *ba;
#ifdef WIN32
	const char **argv;
	int argv_num;
#endif
};

static void callback_main_atexit(void *user_data)
{
	struct CreatorAtExitData *app_init_data = user_data;

	if (app_init_data->ba) {
		BLI_argsFree(app_init_data->ba);
		app_init_data->ba = NULL;
	}

#ifdef WIN32
	if (app_init_data->argv) {
		while (app_init_data->argv_num) {
			free((void *)app_init_data->argv[--app_init_data->argv_num]);
		}
		free((void *)app_init_data->argv);
		app_init_data->argv = NULL;
	}
#endif
}

static void callback_clg_fatal(void *fp)
{
	BLI_system_backtrace(fp);
}

/** \} */



/* -------------------------------------------------------------------- */

/** \name Main Function
 * \{ */

/**
 * Blender's main function responsibilities are:
 * - setup subsystems.
 * - handle arguments.
 * - run #WM_main() event loop,
 *   or exit immediately when running in background mode.
 */
int main(
        int argc,
        const char **argv
        )
{
	bContext *C;
	SYS_SystemHandle syshandle;

	bArgs *ba;

	/* --- end declarations --- */

	/* ensure we free data on early-exit */
	struct CreatorAtExitData app_init_data = {NULL};
	BKE_blender_atexit_register(callback_main_atexit, &app_init_data);

	/* NOTE: Special exception for guarded allocator type switch:
	 *       we need to perform switch from lock-free to fully
	 *       guarded allocator before any allocation happened.
	 */
	{
		int i;
		for (i = 0; i < argc; i++) {
			if (STREQ(argv[i], "--debug") || STREQ(argv[i], "-d") ||
			    STREQ(argv[i], "--debug-memory") || STREQ(argv[i], "--debug-all"))
			{
				printf("Switching to fully guarded memory allocator.\n");
				MEM_use_guarded_allocator();
				break;
			}
			else if (STREQ(argv[i], "--")) {
				break;
			}
		}
	}

#ifdef BUILD_DATE
	{
		time_t temp_time = build_commit_timestamp;
		struct tm *tm = gmtime(&temp_time);
		if (LIKELY(tm)) {
			strftime(build_commit_date, sizeof(build_commit_date), "%Y-%m-%d", tm);
			strftime(build_commit_time, sizeof(build_commit_time), "%H:%M", tm);
		}
		else {
			const char *unknown = "date-unknown";
			BLI_strncpy(build_commit_date, unknown, sizeof(build_commit_date));
			BLI_strncpy(build_commit_time, unknown, sizeof(build_commit_time));
		}
	}
#endif

	/* Initialize logging */
	CLG_init();
	CLG_fatal_fn_set(callback_clg_fatal);

	C = CTX_create();

#ifdef WITH_BINRELOC
	br_init(NULL);
#endif

	main_callback_setup();

#ifdef __FreeBSD__
	fpsetmask(0);
#endif

	/* initialize path to executable */
	BKE_appdir_program_path_init(argv[0]);

	BLI_threadapi_init();
	BLI_thread_put_process_on_fast_node();

	DNA_sdna_current_init();

	BKE_blender_globals_init();  /* blender.c */

	IMB_init();
	BKE_cachefiles_init();
	BKE_images_init();
	BKE_modifier_init();

	BLI_callback_global_init();

	syshandle = 0;

	/* first test for background */
	ba = BLI_argsInit(argc, (const char **)argv); /* skip binary path */

	/* ensure we free on early exit */
	app_init_data.ba = ba;

	main_args_setup(C, ba, &syshandle);

	BLI_argsParse(ba, 1, NULL, NULL);

	main_signal_setup();

	/* after level 1 args, this is so playanim skips RNA init */
	RNA_init();

	/* end second init */

	/* background render uses this font too */
	BKE_vfont_builtin_register(datatoc_bfont_pfb, datatoc_bfont_pfb_size);

	init_def_material();

	BLI_argsParse(ba, 2, NULL, NULL);
	BLI_argsParse(ba, 3, NULL, NULL);
	WM_init(C, argc, (const char **)argv);

	/* this is properly initialized with user defs, but this is default */
	/* call after loading the startup.blend so we can read U.tempdir */
	BKE_tempdir_init(U.tempdir);

#ifdef WITH_PYTHON
	/**
	 * NOTE: the U.pythondir string is NULL until WM_init() is executed,
	 * so we provide the BPY_ function below to append the user defined
	 * python-dir to Python's sys.path at this point.  Simply putting
	 * WM_init() before #BPY_python_start() crashes Blender at startup.
	 */

	/* TODO - U.pythondir */
#else
	printf("\n* WARNING * - Blender compiled without Python!\nthis is not intended for typical usage\n\n");
#endif

	CTX_py_init_set(C, 1);
	WM_keymap_init(C);

	/* OK we are ready for it */
	main_args_setup_post(C, ba);

	if (!G.file_loaded)
		if (U.uiflag2 & USER_KEEP_SESSION)
			WM_recover_last_session(C, NULL);

	/* Explicitly free data allocated for argument parsing:
	 * - 'ba'
	 */
	callback_main_atexit(&app_init_data);
	BKE_blender_atexit_unregister(callback_main_atexit, &app_init_data);

	ba = NULL;
	(void)ba;

	if (!G.file_loaded) {
		WM_init_splash(C);
	}

	WM_main(C);

	return 0;
} /* end of int main(argc, argv) */

/** \} */
