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

/** \file creator/creator_args.c
 *  \ingroup creator
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#ifdef WIN32
#  include "BLI_winstuff.h"
#endif

#include "BLI_args.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_path_util.h"
#include "BLI_fileops.h"
#include "BLI_mempool.h"
#include "BLI_system.h"

#include "BLO_readfile.h"  /* only for BLO_has_bfile_extension */

#include "BKE_blender_version.h"
#include "BKE_context.h"

#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_scene.h"
#include "BKE_report.h"
#include "BKE_image.h"

#include "DNA_screen_types.h"
#include "DNA_scene_types.h"


#ifdef WITH_PYTHON
#include "BPY_extern.h"
#endif

#include "ED_datafiles.h"

#include "WM_api.h"

#include "GPU_basic_shader.h"
#include "GPU_draw.h"
#include "GPU_extensions.h"

#define SYS_SystemHandle int

#ifdef WITH_CYCLES_LOGGING
#  include "CCL_api.h"
#endif

#include "creator_intern.h"  /* own include */


/* -------------------------------------------------------------------- */

/** \name Utility String Parsing
 * \{ */

static bool parse_int_relative(
        const char *str, const char *str_end_test, int pos, int neg,
        int *r_value, const char **r_err_msg)
{
	char *str_end = NULL;
	long value;

	errno = 0;

	switch (*str) {
		case '+':
			value = pos + strtol(str + 1, &str_end, 10);
			break;
		case '-':
			value = (neg - strtol(str + 1, &str_end, 10)) + 1;
			break;
		default:
			value = strtol(str, &str_end, 10);
			break;
	}


	if (*str_end != '\0' && (str_end != str_end_test)) {
		static const char *msg = "not a number";
		*r_err_msg = msg;
		return false;
	}
	else if ((errno == ERANGE) || ((value < INT_MIN || value > INT_MAX))) {
		static const char *msg = "exceeds range";
		*r_err_msg = msg;
		return false;
	}
	else {
		*r_value = (int)value;
		return true;
	}
}

/**
 * Parse a number as a range, eg: `1..4`.
 *
 * The \a str_end_range argument is a result of #parse_int_range_sep_search.
 */
static bool parse_int_range_relative(
        const char *str, const char *str_end_range, const char *str_end_test, int pos, int neg,
        int r_value_range[2], const char **r_err_msg)
{
	if (parse_int_relative(str,               str_end_range, pos, neg, &r_value_range[0], r_err_msg) &&
	    parse_int_relative(str_end_range + 2, str_end_test,  pos, neg, &r_value_range[1], r_err_msg))
	{
		return true;
	}
	else {
		return false;
	}
}

/**
 * No clamping, fails with any number outside the range.
 */
static bool parse_int_strict_range(
        const char *str, const char *str_end_test, const int min, const int max,
        int *r_value, const char **r_err_msg)
{
	char *str_end = NULL;
	long value;

	errno = 0;
	value = strtol(str, &str_end, 10);

	if (*str_end != '\0' && (str_end != str_end_test)) {
		static const char *msg = "not a number";
		*r_err_msg = msg;
		return false;
	}
	else if ((errno == ERANGE) || ((value < min || value > max))) {
		static const char *msg = "exceeds range";
		*r_err_msg = msg;
		return false;
	}
	else {
		*r_value = (int)value;
		return true;
	}
}

static bool parse_int(
        const char *str, const char *str_end_test,
        int *r_value, const char **r_err_msg)
{
	return parse_int_strict_range(str, str_end_test, INT_MIN, INT_MAX, r_value, r_err_msg);
}

static bool parse_int_clamp(
        const char *str, const char *str_end_test, int min, int max,
        int *r_value, const char **r_err_msg)
{
	if (parse_int(str, str_end_test, r_value, r_err_msg)) {
		CLAMP(*r_value, min, max);
		return true;
	}
	else {
		return false;
	}
}

/* -------------------------------------------------------------------- */

#ifdef WITH_PYTHON

/** \name Utilities Python Context Macro (#BPY_CTX_SETUP)
 * \{ */
struct BlendePyContextStore {
	wmWindowManager *wm;
	Scene *scene;
	wmWindow *win;
	bool has_win;
};

static void arg_py_context_backup(
        bContext *C, struct BlendePyContextStore *c_py,
        const char *script_id)
{
	c_py->wm = CTX_wm_manager(C);
	c_py->scene = CTX_data_scene(C);
	c_py->has_win = !BLI_listbase_is_empty(&c_py->wm->windows);
	if (c_py->has_win) {
		c_py->win = CTX_wm_window(C);
		CTX_wm_window_set(C, c_py->wm->windows.first);
	}
	else {
		c_py->win = NULL;
		fprintf(stderr, "Python script \"%s\" "
		        "running with missing context data.\n", script_id);
	}
}

static void arg_py_context_restore(
        bContext *C, struct BlendePyContextStore *c_py)
{
	/* script may load a file, check old data is valid before using */
	if (c_py->has_win) {
		if ((c_py->win == NULL) ||
		    ((BLI_findindex(&G_MAIN->wm, c_py->wm) != -1) &&
		     (BLI_findindex(&c_py->wm->windows, c_py->win) != -1)))
		{
			CTX_wm_window_set(C, c_py->win);
		}
	}

	if ((c_py->scene == NULL) ||
	    BLI_findindex(&G_MAIN->scene, c_py->scene) != -1)
	{
		CTX_data_scene_set(C, c_py->scene);
	}
}

/* macro for context setup/reset */
#define BPY_CTX_SETUP(_cmd) \
	{ \
		struct BlendePyContextStore py_c; \
		arg_py_context_backup(C, &py_c, argv[1]); \
		{ _cmd; } \
		arg_py_context_restore(C, &py_c); \
	} ((void)0)

#endif /* WITH_PYTHON */

/** \} */


/* -------------------------------------------------------------------- */

/** \name Handle Argument Callbacks
 *
 * \note Doc strings here are used in differently:
 *
 * - The `--help` message.
 * - The man page (for Unix systems),
 *   see: `doc/manpage/blender.1.py`
 * - Parsed and extracted for the manual,
 *   which converts our ad-hoc formatting to reStructuredText.
 *   see: https://docs.blender.org/manual/en/dev/advanced/command_line.html
 *
 * \{ */

static void print_version_full(void)
{
	printf(BLEND_VERSION_STRING_FMT);
#ifdef BUILD_DATE
	printf("\tbuild date: %s\n", build_date);
	printf("\tbuild time: %s\n", build_time);
	printf("\tbuild commit date: %s\n", build_commit_date);
	printf("\tbuild commit time: %s\n", build_commit_time);
	printf("\tbuild hash: %s\n", build_hash);
	printf("\tbuild platform: %s\n", build_platform);
	printf("\tbuild type: %s\n", build_type);
	printf("\tbuild c flags: %s\n", build_cflags);
	printf("\tbuild c++ flags: %s\n", build_cxxflags);
	printf("\tbuild link flags: %s\n", build_linkflags);
	printf("\tbuild system: %s\n", build_system);
#endif
}

static void print_version_short(void)
{
#ifdef BUILD_DATE
	/* NOTE: We include built time since sometimes we need to tell broken from
	 * working built of the same hash. */
	printf(BLEND_VERSION_FMT " (hash %s built %s %s)\n",
	       BLEND_VERSION_ARG, build_hash, build_date, build_time);
#else
	printf(BLEND_VERSION_STRING_FMT);
#endif
}

static const char arg_handle_print_version_doc[] =
"\n\tPrint Blender version and exit."
;
static int arg_handle_print_version(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	print_version_full();
	exit(0);
	return 0;
}

static const char arg_handle_print_help_doc[] =
"\n\tPrint this help text and exit."
;
static const char arg_handle_print_help_doc_win32[] =
"\n\tPrint this help text and exit (windows only)."
;
static int arg_handle_print_help(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	bArgs *ba = (bArgs *)data;

	printf(BLEND_VERSION_STRING_FMT);
	printf("Usage: blender [args ...] [file] [args ...]\n\n");

	printf("\n");

	printf("\n");
	printf("Window Options:\n");
	BLI_argsPrintArgDoc(ba, "--window-border");
	BLI_argsPrintArgDoc(ba, "--window-fullscreen");
	BLI_argsPrintArgDoc(ba, "--window-geometry");
	BLI_argsPrintArgDoc(ba, "--start-console");
	BLI_argsPrintArgDoc(ba, "--no-native-pixels");
	BLI_argsPrintArgDoc(ba, "--no-window-focus");

	printf("\n");
	printf("Python Options:\n");
	BLI_argsPrintArgDoc(ba, "--enable-autoexec");
	BLI_argsPrintArgDoc(ba, "--disable-autoexec");

	printf("\n");
	BLI_argsPrintArgDoc(ba, "--python");
	BLI_argsPrintArgDoc(ba, "--python-text");
	BLI_argsPrintArgDoc(ba, "--python-expr");
	BLI_argsPrintArgDoc(ba, "--python-console");
	BLI_argsPrintArgDoc(ba, "--python-exit-code");
	BLI_argsPrintArgDoc(ba, "--addons");

	printf("\n");
	printf("Logging Options:\n");
	BLI_argsPrintArgDoc(ba, "--log");
	BLI_argsPrintArgDoc(ba, "--log-level");
	BLI_argsPrintArgDoc(ba, "--log-show-basename");
	BLI_argsPrintArgDoc(ba, "--log-show-backtrace");
	BLI_argsPrintArgDoc(ba, "--log-file");

	printf("\n");
	printf("Debug Options:\n");
	BLI_argsPrintArgDoc(ba, "--debug");
	BLI_argsPrintArgDoc(ba, "--debug-value");

	printf("\n");
	BLI_argsPrintArgDoc(ba, "--debug-events");
	BLI_argsPrintArgDoc(ba, "--debug-handlers");
	BLI_argsPrintArgDoc(ba, "--debug-memory");
	BLI_argsPrintArgDoc(ba, "--debug-jobs");
	BLI_argsPrintArgDoc(ba, "--debug-python");
	BLI_argsPrintArgDoc(ba, "--debug-depsgraph");
	BLI_argsPrintArgDoc(ba, "--debug-depsgraph-eval");
	BLI_argsPrintArgDoc(ba, "--debug-depsgraph-build");
	BLI_argsPrintArgDoc(ba, "--debug-depsgraph-tag");
	BLI_argsPrintArgDoc(ba, "--debug-depsgraph-no-threads");

	BLI_argsPrintArgDoc(ba, "--debug-gpumem");
	BLI_argsPrintArgDoc(ba, "--debug-gpu-shaders");
	BLI_argsPrintArgDoc(ba, "--debug-wm");
	BLI_argsPrintArgDoc(ba, "--debug-all");
	BLI_argsPrintArgDoc(ba, "--debug-io");

	printf("\n");
	BLI_argsPrintArgDoc(ba, "--debug-fpe");
	BLI_argsPrintArgDoc(ba, "--disable-crash-handler");

	printf("\n");
	printf("Misc Options:\n");
	BLI_argsPrintArgDoc(ba, "--app-template");
	BLI_argsPrintArgDoc(ba, "--factory-startup");
	printf("\n");
	BLI_argsPrintArgDoc(ba, "--env-system-datafiles");
	BLI_argsPrintArgDoc(ba, "--env-system-scripts");
	BLI_argsPrintArgDoc(ba, "--env-system-python");
	printf("\n");
	BLI_argsPrintArgDoc(ba, "-nojoystick");
	BLI_argsPrintArgDoc(ba, "-noglsl");
	BLI_argsPrintArgDoc(ba, "-noaudio");
	BLI_argsPrintArgDoc(ba, "-setaudio");

	printf("\n");

	BLI_argsPrintArgDoc(ba, "--help");

#ifdef WIN32
	BLI_argsPrintArgDoc(ba, "-R");
	BLI_argsPrintArgDoc(ba, "-r");
#endif
	BLI_argsPrintArgDoc(ba, "--version");

	BLI_argsPrintArgDoc(ba, "--");

	printf("\n");
	printf("Experimental Features:\n");
	BLI_argsPrintArgDoc(ba, "--enable-new-depsgraph");
	BLI_argsPrintArgDoc(ba, "--enable-new-basic-shader-glsl");

	/* Other options _must_ be last (anything not handled will show here) */
	printf("\n");
	printf("Other Options:\n");
	BLI_argsPrintOtherDoc(ba);

	printf("\n");
	printf("Argument Parsing:\n");
	printf("\tArguments must be separated by white space, eg:\n");
	printf("\t# blender -ba test.blend\n");
	printf("\t...will ignore the 'a'.\n");
	printf("\t# blender -b test.blend -f8\n");
	printf("\t...will ignore '8' because there is no space between the '-f' and the frame value.\n\n");

	printf("Environment Variables:\n");
	printf("  $BLENDER_USER_CONFIG      Directory for user configuration files.\n");
	printf("  $BLENDER_USER_SCRIPTS     Directory for user scripts.\n");
	printf("  $BLENDER_SYSTEM_SCRIPTS   Directory for system wide scripts.\n");
	printf("  $BLENDER_USER_DATAFILES   Directory for user data files (icons, translations, ..).\n");
	printf("  $BLENDER_SYSTEM_DATAFILES Directory for system wide data files.\n");
	printf("  $BLENDER_SYSTEM_PYTHON    Directory for system Python libraries.\n");
	printf("  $TMP or $TMPDIR           Store temporary files here.\n");
	printf("  $PYTHONHOME               Path to the Python directory, eg. /usr/lib/python.\n\n");

	exit(0);

	return 0;
}

static const char arg_handle_arguments_end_doc[] =
"\n\tEnd option processing, following arguments passed unchanged. Access via Python's 'sys.argv'."
;
static int arg_handle_arguments_end(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	return -1;
}

/* only to give help message */
#ifndef WITH_PYTHON_SECURITY /* default */
#  define   PY_ENABLE_AUTO ", (default)"
#  define   PY_DISABLE_AUTO ""
#else
#  define   PY_ENABLE_AUTO ""
#  define   PY_DISABLE_AUTO ", (compiled as non-standard default)"
#endif

static const char arg_handle_python_set_doc_enable[] =
"\n\tEnable automatic Python script execution" PY_ENABLE_AUTO "."
;
static const char arg_handle_python_set_doc_disable[] =
"\n\tDisable automatic Python script execution (pydrivers & startup scripts)" PY_DISABLE_AUTO "."
;
#undef PY_ENABLE_AUTO
#undef PY_DISABLE_AUTO

static int arg_handle_python_set(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	if ((bool)data) {
		G.f |= G_SCRIPT_AUTOEXEC;
	}
	else {
		G.f &= ~G_SCRIPT_AUTOEXEC;
	}
	G.f |= G_SCRIPT_OVERRIDE_PREF;
	return 0;
}

static const char arg_handle_crash_handler_disable_doc[] =
"\n\tDisable the crash handler."
;
static int arg_handle_crash_handler_disable(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	app_state.signal.use_crash_handler = false;
	return 0;
}

static const char arg_handle_abort_handler_disable_doc[] =
"\n\tDisable the abort handler."
;
static int arg_handle_abort_handler_disable(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	app_state.signal.use_abort_handler = false;
	return 0;
}

static const char arg_handle_log_level_set_doc[] =
"<level>\n"
"\n"
"\tSet the logging verbosity level (higher for more details) defaults to 1, use -1 to log all levels."
;
static int arg_handle_log_level_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--log-level";
	if (argc > 1) {
		const char *err_msg = NULL;
		if (!parse_int_clamp(argv[1], NULL, -1, INT_MAX, &G.log.level, &err_msg)) {
			printf("\nError: %s '%s %s'.\n", err_msg, arg_id, argv[1]);
		}
		else {
			if (G.log.level == -1) {
				G.log.level = INT_MAX;
			}
			CLG_level_set(G.log.level);
		}
		return 1;
	}
	else {
		printf("\nError: '%s' no args given.\n", arg_id);
		return 0;
	}
}

static const char arg_handle_log_show_basename_set_doc[] =
"\n\tOnly show file name in output (not the leading path)."
;
static int arg_handle_log_show_basename_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	CLG_output_use_basename_set(true);
	return 0;
}

static const char arg_handle_log_show_backtrace_set_doc[] =
"\n\tShow a back trace for each log message (debug builds only)."
;
static int arg_handle_log_show_backtrace_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	/* Ensure types don't become incompatible. */
	void (*fn)(FILE *fp) = BLI_system_backtrace;
	CLG_backtrace_fn_set((void (*)(void *))fn);
	return 0;
}

static const char arg_handle_log_file_set_doc[] =
"<filename>\n"
"\n"
"\tSet a file to output the log to."
;
static int arg_handle_log_file_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--log-file";
	if (argc > 1) {
		errno = 0;
		FILE *fp = BLI_fopen(argv[1], "w");
		if (fp == NULL) {
			const char *err_msg = errno ? strerror(errno) : "unknown";
			printf("\nError: %s '%s %s'.\n", err_msg, arg_id, argv[1]);
		}
		else {
			if (UNLIKELY(G.log.file != NULL)) {
				fclose(G.log.file);
			}
			G.log.file = fp;
			CLG_output_set(G.log.file);
		}
		return 1;
	}
	else {
		printf("\nError: '%s' no args given.\n", arg_id);
		return 0;
	}
}

static const char arg_handle_log_set_doc[] =
"<match>\n"
"\tEnable logging categories, taking a single comma separated argument.\n"
"\tMultiple categories can be matched using a '.*' suffix,\n"
"\tso '--log \"wm.*\"' logs every kind of window-manager message.\n"
"\tUse \"^\" prefix to ignore, so '--log \"*,^wm.operator.*\"' logs all except for 'wm.operators.*'\n"
"\tUse \"*\" to log everything."
;
static int arg_handle_log_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--log";
	if (argc > 1) {
		const char *str_step = argv[1];
		while (*str_step) {
			const char *str_step_end = strchr(str_step, ',');
			int str_step_len = str_step_end ? (str_step_end - str_step) : strlen(str_step);

			if (str_step[0] == '^') {
				CLG_type_filter_exclude(str_step + 1, str_step_len - 1);
			}
			else {
				CLG_type_filter_include(str_step, str_step_len);
			}

			if (str_step_end) {
				/* typically only be one, but don't fail on multiple.*/
				while (*str_step_end == ',') {
					str_step_end++;
				}
				str_step = str_step_end;
			}
			else {
				break;
			}
		}
		return 1;
	}
	else {
		printf("\nError: '%s' no args given.\n", arg_id);
		return 0;
	}
}

static const char arg_handle_debug_mode_set_doc[] =
"\n"
"\tTurn debugging on.\n"
"\n"
"\t* Enables memory error detection\n"
"\t* Disables mouse grab (to interact with a debugger in some cases)\n"
"\t* Keeps Python's 'sys.stdin' rather than setting it to None"
;
static int arg_handle_debug_mode_set(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	G.debug |= G_DEBUG;  /* std output printf's */
	printf(BLEND_VERSION_STRING_FMT);
	MEM_set_memory_debug();
#ifndef NDEBUG
	BLI_mempool_set_memory_debug();
#endif

#ifdef WITH_BUILDINFO
	printf("Build: %s %s %s %s\n", build_date, build_time, build_platform, build_type);
#endif

	BLI_argsPrint(data);
	return 0;
}

static const char arg_handle_debug_mode_generic_set_doc_python[] =
"\n\tEnable debug messages for Python.";
static const char arg_handle_debug_mode_generic_set_doc_events[] =
"\n\tEnable debug messages for the event system.";
static const char arg_handle_debug_mode_generic_set_doc_handlers[] =
"\n\tEnable debug messages for event handling.";
static const char arg_handle_debug_mode_generic_set_doc_wm[] =
"\n\tEnable debug messages for the window manager, also prints every operator call.";
static const char arg_handle_debug_mode_generic_set_doc_jobs[] =
"\n\tEnable time profiling for background jobs.";
static const char arg_handle_debug_mode_generic_set_doc_gpu[] =
"\n\tEnable gpu debug context and information for OpenGL 4.3+.";
static const char arg_handle_debug_mode_generic_set_doc_gpumem[] =
"\n\tEnable GPU memory stats in status bar.";

static int arg_handle_debug_mode_generic_set(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	G.debug |= POINTER_AS_INT(data);
	return 0;
}

static const char arg_handle_debug_mode_io_doc[] =
"\n\tEnable debug messages for I/O (collada, ...).";
static int arg_handle_debug_mode_io(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	G.debug |= G_DEBUG_IO;
	return 0;
}

static const char arg_handle_debug_mode_all_doc[] =
"\n\tEnable all debug messages.";
static int arg_handle_debug_mode_all(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	G.debug |= G_DEBUG_ALL;
	return 0;
}

static const char arg_handle_debug_mode_memory_set_doc[] =
"\n\tEnable fully guarded memory allocation and debugging."
;
static int arg_handle_debug_mode_memory_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	MEM_set_memory_debug();
	return 0;
}

static const char arg_handle_debug_value_set_doc[] =
"<value>\n"
"\tSet debug value of <value> on startup."
;
static int arg_handle_debug_value_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--debug-value";
	if (argc > 1) {
		const char *err_msg = NULL;
		int value;
		if (!parse_int(argv[1], NULL, &value, &err_msg)) {
			printf("\nError: %s '%s %s'.\n", err_msg, arg_id, argv[1]);
			return 1;
		}

		G.debug_value = value;

		return 1;
	}
	else {
		printf("\nError: you must specify debug value to set.\n");
		return 0;
	}
}

static const char arg_handle_debug_fpe_set_doc[] =
"\n\tEnable floating point exceptions."
;
static int arg_handle_debug_fpe_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	main_signal_setup_fpe();
	return 0;
}

static const char arg_handle_app_template_doc[] =
"\n\tSet the application template, use 'default' for none."
;
static int arg_handle_app_template(int argc, const char **argv, void *UNUSED(data))
{
	if (argc > 1) {
		const char *app_template = STREQ(argv[1], "default") ? "" : argv[1];
		WM_init_state_app_template_set(app_template);
		return 1;
	}
	else {
		printf("\nError: App template must follow '--app-template'.\n");
		return 0;
	}
}

static const char arg_handle_factory_startup_set_doc[] =
"\n\tSkip reading the " STRINGIFY(BLENDER_STARTUP_FILE) " in the users home directory."
;
static int arg_handle_factory_startup_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	G.factory_startup = 1;
	return 0;
}

static const char arg_handle_env_system_set_doc_datafiles[] =
"\n\tSet the "STRINGIFY_ARG (BLENDER_SYSTEM_DATAFILES)" environment variable.";
static const char arg_handle_env_system_set_doc_scripts[] =
"\n\tSet the "STRINGIFY_ARG (BLENDER_SYSTEM_SCRIPTS)" environment variable.";
static const char arg_handle_env_system_set_doc_python[] =
"\n\tSet the "STRINGIFY_ARG (BLENDER_SYSTEM_PYTHON)" environment variable.";

static int arg_handle_env_system_set(int argc, const char **argv, void *UNUSED(data))
{
	/* "--env-system-scripts" --> "BLENDER_SYSTEM_SCRIPTS" */

	char env[64] = "BLENDER";
	char *ch_dst = env + 7; /* skip BLENDER */
	const char *ch_src = argv[0] + 5; /* skip --env */

	if (argc < 2) {
		printf("%s requires one argument\n", argv[0]);
		exit(1);
	}

	for (; *ch_src; ch_src++, ch_dst++) {
		*ch_dst = (*ch_src == '-') ? '_' : (*ch_src) - 32; /* toupper() */
	}

	*ch_dst = '\0';
	BLI_setenv(env, argv[1]);
	return 1;
}

static const char arg_handle_window_geometry_doc[] =
"<sx> <sy> <w> <h>\n"
"\tOpen with lower left corner at <sx>, <sy> and width and height as <w>, <h>."
;

static int arg_handle_window_geometry(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "-p / --window-geometry";
	int params[4], i;

	if (argc < 5) {
		fprintf(stderr, "Error: requires four arguments '%s'\n", arg_id);
		exit(1);
	}

	for (i = 0; i < 4; i++) {
		const char *err_msg = NULL;
		if (!parse_int(argv[i + 1], NULL, &params[i], &err_msg)) {
			printf("\nError: %s '%s %s'.\n", err_msg, arg_id, argv[1]);
			exit(1);
		}
	}

	WM_init_state_size_set(UNPACK4(params));

	return 4;
}

static const char arg_handle_native_pixels_set_doc[] =
"\n\tDo not use native pixel size, for high resolution displays (MacBook 'Retina')."
;
static int arg_handle_native_pixels_set(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	WM_init_native_pixels(false);
	return 0;
}

static const char arg_handle_with_borders_doc[] =
"\n\tForce opening with borders."
;
static int arg_handle_with_borders(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	WM_init_state_normal_set();
	return 0;
}

static const char arg_handle_without_borders_doc[] =
"\n\tForce opening in fullscreen mode."
;
static int arg_handle_without_borders(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	WM_init_state_fullscreen_set();
	return 0;
}

static const char arg_handle_no_window_focus_doc[] =
"\n\tOpen behind other windows and without taking focus."
;
static int arg_handle_no_window_focus(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	WM_init_window_focus_set(false);
	return 0;
}

extern bool wm_start_with_console; /* wm_init_exit.c */

static const char arg_handle_start_with_console_doc[] =
"\n\tStart with the console window open (ignored if -b is set), (Windows only)."
;
static int arg_handle_start_with_console(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	wm_start_with_console = true;
	return 0;
}

static const char arg_handle_register_extension_doc[] =
"\n\tRegister blend-file extension, then exit (Windows only)."
;
static const char arg_handle_register_extension_doc_silent[] =
"\n\tSilently register blend-file extension, then exit (Windows only)."
;
static int arg_handle_register_extension(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	(void)data; /* unused */
	return 0;
}

static const char arg_handle_joystick_disable_doc[] =
"\n\tDisable joystick support."
;
static int arg_handle_joystick_disable(int UNUSED(argc), const char **UNUSED(argv), void *data)
{
	(void)data;

	return 0;
}

static const char arg_handle_glsl_disable_doc[] =
"\n\tDisable GLSL shading."
;
static int arg_handle_glsl_disable(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	GPU_extensions_disable();
	return 0;
}

static const char arg_handle_threads_set_doc[] =
"<threads>\n"
"\tUse amount of <threads> for rendering and other operations\n"
"\t[1-" STRINGIFY(BLENDER_MAX_THREADS) "], 0 for systems processor count."
;
static int arg_handle_threads_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "-t / --threads";
	const int min = 0, max = BLENDER_MAX_THREADS;
	if (argc > 1) {
		const char *err_msg = NULL;
		int threads;
		if (!parse_int_strict_range(argv[1], NULL, min, max, &threads, &err_msg)) {
			printf("\nError: %s '%s %s', expected number in [%d..%d].\n", err_msg, arg_id, argv[1], min, max);
			return 1;
		}

		BLI_system_num_threads_override_set(threads);
		return 1;
	}
	else {
		printf("\nError: you must specify a number of threads in [%d..%d] '%s'.\n", min, max, arg_id);
		return 0;
	}
}

static const char arg_handle_basic_shader_glsl_use_new_doc[] =
"\n\tUse new GLSL basic shader."
;
static int arg_handle_basic_shader_glsl_use_new(int UNUSED(argc), const char **UNUSED(argv), void *UNUSED(data))
{
	printf("Using new GLSL basic shader.\n");
	GPU_basic_shader_use_glsl_set(true);
	return 0;
}

static const char arg_handle_verbosity_set_doc[] =
"<verbose>\n"
"\tSet logging verbosity level."
;
static int arg_handle_verbosity_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--verbose";
	if (argc > 1) {
		const char *err_msg = NULL;
		int level;
		if (!parse_int(argv[1], NULL, &level, &err_msg)) {
			printf("\nError: %s '%s %s'.\n", err_msg, arg_id, argv[1]);
		}

		(void)level;

		return 1;
	}
	else {
		printf("\nError: you must specify a verbosity level.\n");
		return 0;
	}
}

static const char arg_handle_extension_set_doc[] =
"<bool>\n"
"\tSet option to add the file extension to the end of the file."
;
static int arg_handle_extension_set(int argc, const char **argv, void *data)
{
	if (argc > 1) {
		return 1;
	}
	else {
		printf("\nError: you must specify a path after '- '.\n");
		return 0;
	}
}

static const char arg_handle_scene_set_doc[] =
"<name>\n"
"\tSet the active scene <name> for rendering."
;
static int arg_handle_scene_set(int argc, const char **argv, void *data)
{
	if (argc > 1) {
		bContext *C = data;
		Scene *scene = BKE_scene_set_name(CTX_data_main(C), argv[1]);
		if (scene) {
			CTX_data_scene_set(C, scene);

			/* Set the scene of the first window, see: T55991,
			 * otherwise scrips that run later won't get this scene back from the context. */
			wmWindow *win = CTX_wm_window(C);
			if (win == NULL) {
				win = CTX_wm_manager(C)->windows.first;
			}
			if (win != NULL) {
				win->screen->scene = scene;
			}
		}
		return 1;
	}
	else {
		printf("\nError: Scene name must follow '-S / --scene'.\n");
		return 0;
	}
}

static const char arg_handle_python_file_run_doc[] =
"<filename>\n"
"\tRun the given Python script file."
;
static int arg_handle_python_file_run(int argc, const char **argv, void *data)
{
#ifdef WITH_PYTHON
	bContext *C = data;

	/* workaround for scripts not getting a bpy.context.scene, causes internal errors elsewhere */
	if (argc > 1) {
		/* Make the path absolute because its needed for relative linked blends to be found */
		char filename[FILE_MAX];
		BLI_strncpy(filename, argv[1], sizeof(filename));
		BLI_path_cwd(filename, sizeof(filename));

		bool ok;
		BPY_CTX_SETUP(ok = BPY_execute_filepath(C, filename, NULL));
		if (!ok && app_state.exit_code_on_error.python) {
			printf("\nError: script failed, file: '%s', exiting.\n", argv[1]);
			exit(app_state.exit_code_on_error.python);
		}
		return 1;
	}
	else {
		printf("\nError: you must specify a filepath after '%s'.\n", argv[0]);
		return 0;
	}
#else
	UNUSED_VARS(argc, argv, data);
	printf("This Blender was built without Python support\n");
	return 0;
#endif /* WITH_PYTHON */
}

static const char arg_handle_python_text_run_doc[] =
"<name>\n"
"\tRun the given Python script text block."
;
static int arg_handle_python_text_run(int argc, const char **argv, void *data)
{
#ifdef WITH_PYTHON
	bContext *C = data;

	/* workaround for scripts not getting a bpy.context.scene, causes internal errors elsewhere */
	if (argc > 1) {
		Main *bmain = CTX_data_main(C);
		/* Make the path absolute because its needed for relative linked blends to be found */
		struct Text *text = (struct Text *)BKE_libblock_find_name(bmain, ID_TXT, argv[1]);
		bool ok;

		if (text) {
			BPY_CTX_SETUP(ok = BPY_execute_text(C, text, NULL, false));
		}
		else {
			printf("\nError: text block not found %s.\n", argv[1]);
			ok = false;
		}

		if (!ok && app_state.exit_code_on_error.python) {
			printf("\nError: script failed, text: '%s', exiting.\n", argv[1]);
			exit(app_state.exit_code_on_error.python);
		}

		return 1;
	}
	else {
		printf("\nError: you must specify a text block after '%s'.\n", argv[0]);
		return 0;
	}
#else
	UNUSED_VARS(argc, argv, data);
	printf("This Blender was built without Python support\n");
	return 0;
#endif /* WITH_PYTHON */
}

static const char arg_handle_python_expr_run_doc[] =
"<expression>\n"
"\tRun the given expression as a Python script."
;
static int arg_handle_python_expr_run(int argc, const char **argv, void *data)
{
#ifdef WITH_PYTHON
	bContext *C = data;

	/* workaround for scripts not getting a bpy.context.scene, causes internal errors elsewhere */
	if (argc > 1) {
		bool ok;
		BPY_CTX_SETUP(ok = BPY_execute_string_ex(C, NULL, argv[1], false));
		if (!ok && app_state.exit_code_on_error.python) {
			printf("\nError: script failed, expr: '%s', exiting.\n", argv[1]);
			exit(app_state.exit_code_on_error.python);
		}
		return 1;
	}
	else {
		printf("\nError: you must specify a Python expression after '%s'.\n", argv[0]);
		return 0;
	}
#else
	UNUSED_VARS(argc, argv, data);
	printf("This Blender was built without Python support\n");
	return 0;
#endif /* WITH_PYTHON */
}

static const char arg_handle_python_console_run_doc[] =
"\n\tRun Blender with an interactive console."
;
static int arg_handle_python_console_run(int UNUSED(argc), const char **argv, void *data)
{
#ifdef WITH_PYTHON
	bContext *C = data;

	BPY_CTX_SETUP(BPY_execute_string(C, (const char *[]){"code", NULL}, "code.interact()"));

	return 0;
#else
	UNUSED_VARS(argv, data);
	printf("This Blender was built without python support\n");
	return 0;
#endif /* WITH_PYTHON */
}

static const char arg_handle_python_exit_code_set_doc[] =
"<code>\n"
"\tSet the exit-code in [0..255] to exit if a Python exception is raised\n"
"\t(only for scripts executed from the command line), zero disables."
;
static int arg_handle_python_exit_code_set(int argc, const char **argv, void *UNUSED(data))
{
	const char *arg_id = "--python-exit-code";
	if (argc > 1) {
		const char *err_msg = NULL;
		const int min = 0, max = 255;
		int exit_code;
		if (!parse_int_strict_range(argv[1], NULL, min, max, &exit_code, &err_msg)) {
			printf("\nError: %s '%s %s', expected number in [%d..%d].\n", err_msg, arg_id, argv[1], min, max);
			return 1;
		}

		app_state.exit_code_on_error.python = (unsigned char)exit_code;
		return 1;
	}
	else {
		printf("\nError: you must specify an exit code number '%s'.\n", arg_id);
		return 0;
	}
}

static const char arg_handle_addons_set_doc[] =
"<addon(s)>\n"
"\tComma separated list of add-ons (no spaces)."
;
static int arg_handle_addons_set(int argc, const char **argv, void *data)
{
	/* workaround for scripts not getting a bpy.context.scene, causes internal errors elsewhere */
	if (argc > 1) {
#ifdef WITH_PYTHON
		const char script_str[] =
		        "from addon_utils import check, enable\n"
		        "for m in '%s'.split(','):\n"
		        "    if check(m)[1] is False:\n"
		        "        enable(m, persistent=True)";
		const int slen = strlen(argv[1]) + (sizeof(script_str) - 2);
		char *str = malloc(slen);
		bContext *C = data;
		BLI_snprintf(str, slen, script_str, argv[1]);

		BLI_assert(strlen(str) + 1 == slen);
		BPY_CTX_SETUP(BPY_execute_string_ex(C, NULL, str, false));
		free(str);
#else
		UNUSED_VARS(argv, data);
#endif /* WITH_PYTHON */
		return 1;
	}
	else {
		printf("\nError: you must specify a comma separated list after '--addons'.\n");
		return 0;
	}
}

static int arg_handle_load_file(int UNUSED(argc), const char **argv, void *data)
{
	bContext *C = data;
	ReportList reports;
	bool success;

	/* Make the path absolute because its needed for relative linked blends to be found */
	char filename[FILE_MAX];

	/* note, we could skip these, but so far we always tried to load these files */
	if (argv[0][0] == '-') {
		fprintf(stderr, "unknown argument, loading as file: %s\n", argv[0]);
	}

	BLI_strncpy(filename, argv[0], sizeof(filename));
	BLI_path_cwd(filename, sizeof(filename));

	/* load the file */
	BKE_reports_init(&reports, RPT_PRINT);
	WM_file_autoexec_init(filename);
	success = WM_file_read(C, filename, &reports);
	BKE_reports_clear(&reports);

	if (success) {
	}
	else {
		if (BLO_has_bfile_extension(filename)) {
			/* Just pretend a file was loaded, so the user can press Save and it'll
			 * save at the filename from the CLI. */
			BLI_strncpy(G_MAIN->name, filename, FILE_MAX);
			G.relbase_valid = true;
			G.save_over = true;
			printf("... opened default scene instead; saving will write to: %s\n", filename);
		}
		else {
			printf("Error: argument has no '.blend' file extension, not using as new file, exiting! %s\n", filename);
			G.is_break = true;
			WM_exit(C);
		}
	}

	G.file_loaded = 1;

	return 0;
}


void main_args_setup(bContext *C, bArgs *ba, SYS_SystemHandle *syshandle)
{

#define CB(a) a##_doc, a
#define CB_EX(a, b) a##_doc_##b, a

	//BLI_argsAdd(ba, pass, short_arg, long_arg, doc, cb, C);

	/* end argument processing after -- */
	BLI_argsAdd(ba, -1, "--", NULL, CB(arg_handle_arguments_end), NULL);

	/* first pass: background mode, disable python and commands that exit after usage */
	BLI_argsAdd(ba, 1, "-h", "--help", CB(arg_handle_print_help), ba);
	/* Windows only */
	BLI_argsAdd(ba, 1, "/?", NULL, CB_EX(arg_handle_print_help, win32), ba);

	BLI_argsAdd(ba, 1, "-v", "--version", CB(arg_handle_print_version), NULL);

	BLI_argsAdd(ba, 1, "-y", "--enable-autoexec", CB_EX(arg_handle_python_set, enable), (void *)true);
	BLI_argsAdd(ba, 1, "-Y", "--disable-autoexec", CB_EX(arg_handle_python_set, disable), (void *)false);

	BLI_argsAdd(ba, 1, NULL, "--disable-crash-handler", CB(arg_handle_crash_handler_disable), NULL);
	BLI_argsAdd(ba, 1, NULL, "--disable-abort-handler", CB(arg_handle_abort_handler_disable), NULL);

	BLI_argsAdd(ba, 1, NULL, "--log", CB(arg_handle_log_set), ba);
	BLI_argsAdd(ba, 1, NULL, "--log-level", CB(arg_handle_log_level_set), ba);
	BLI_argsAdd(ba, 1, NULL, "--log-show-basename", CB(arg_handle_log_show_basename_set), ba);
	BLI_argsAdd(ba, 1, NULL, "--log-show-backtrace", CB(arg_handle_log_show_backtrace_set), ba);
	BLI_argsAdd(ba, 1, NULL, "--log-file", CB(arg_handle_log_file_set), ba);

	BLI_argsAdd(ba, 1, "-d", "--debug", CB(arg_handle_debug_mode_set), ba);

	BLI_argsAdd(ba, 1, NULL, "--debug-python",
	            CB_EX(arg_handle_debug_mode_generic_set, python), (void *)G_DEBUG_PYTHON);
	BLI_argsAdd(ba, 1, NULL, "--debug-events",
	            CB_EX(arg_handle_debug_mode_generic_set, events), (void *)G_DEBUG_EVENTS);
	BLI_argsAdd(ba, 1, NULL, "--debug-handlers",
	            CB_EX(arg_handle_debug_mode_generic_set, handlers), (void *)G_DEBUG_HANDLERS);
	BLI_argsAdd(ba, 1, NULL, "--debug-wm",
	            CB_EX(arg_handle_debug_mode_generic_set, wm), (void *)G_DEBUG_WM);
	BLI_argsAdd(ba, 1, NULL, "--debug-all", CB(arg_handle_debug_mode_all), NULL);

	BLI_argsAdd(ba, 1, NULL, "--debug-io", CB(arg_handle_debug_mode_io), NULL);

	BLI_argsAdd(ba, 1, NULL, "--debug-fpe",
	            CB(arg_handle_debug_fpe_set), NULL);

	BLI_argsAdd(ba, 1, NULL, "--debug-memory", CB(arg_handle_debug_mode_memory_set), NULL);

	BLI_argsAdd(ba, 1, NULL, "--debug-value",
	            CB(arg_handle_debug_value_set), NULL);
	BLI_argsAdd(ba, 1, NULL, "--debug-jobs",
	            CB_EX(arg_handle_debug_mode_generic_set, jobs), (void *)G_DEBUG_JOBS);
	BLI_argsAdd(ba, 1, NULL, "--debug-gpu",
	            CB_EX(arg_handle_debug_mode_generic_set, gpu), (void *)G_DEBUG_GPU);
	BLI_argsAdd(ba, 1, NULL, "--debug-gpumem",
	            CB_EX(arg_handle_debug_mode_generic_set, gpumem), (void *)G_DEBUG_GPU_MEM);
	BLI_argsAdd(ba, 1, NULL, "--debug-gpu-shaders",
	            CB_EX(arg_handle_debug_mode_generic_set, gpumem), (void *)G_DEBUG_GPU_SHADERS);

	BLI_argsAdd(ba, 1, NULL, "--enable-new-basic-shader-glsl", CB(arg_handle_basic_shader_glsl_use_new), NULL);

	BLI_argsAdd(ba, 1, NULL, "--verbose", CB(arg_handle_verbosity_set), NULL);

	BLI_argsAdd(ba, 1, NULL, "--app-template", CB(arg_handle_app_template), NULL);
	BLI_argsAdd(ba, 1, NULL, "--factory-startup", CB(arg_handle_factory_startup_set), NULL);

	/* TODO, add user env vars? */
	BLI_argsAdd(ba, 1, NULL, "--env-system-datafiles", CB_EX(arg_handle_env_system_set, datafiles), NULL);
	BLI_argsAdd(ba, 1, NULL, "--env-system-scripts", CB_EX(arg_handle_env_system_set, scripts), NULL);
	BLI_argsAdd(ba, 1, NULL, "--env-system-python", CB_EX(arg_handle_env_system_set, python), NULL);

	/* second pass: custom window stuff */
	BLI_argsAdd(ba, 2, "-p", "--window-geometry", CB(arg_handle_window_geometry), NULL);
	BLI_argsAdd(ba, 2, "-w", "--window-border", CB(arg_handle_with_borders), NULL);
	BLI_argsAdd(ba, 2, "-W", "--window-fullscreen", CB(arg_handle_without_borders), NULL);
	BLI_argsAdd(ba, 2, NULL, "--no-window-focus", CB(arg_handle_no_window_focus), NULL);
	BLI_argsAdd(ba, 2, "-con", "--start-console", CB(arg_handle_start_with_console), NULL);
	BLI_argsAdd(ba, 2, "-R", NULL, CB(arg_handle_register_extension), NULL);
	BLI_argsAdd(ba, 2, "-r", NULL, CB_EX(arg_handle_register_extension, silent), ba);
	BLI_argsAdd(ba, 2, NULL, "--no-native-pixels", CB(arg_handle_native_pixels_set), ba);

	/* third pass: disabling things and forcing settings */
	BLI_argsAddCase(ba, 3, "-nojoystick", 1, NULL, 0, CB(arg_handle_joystick_disable), syshandle);
	BLI_argsAddCase(ba, 3, "-noglsl", 1, NULL, 0, CB(arg_handle_glsl_disable), NULL);

	/* fourth pass: processing arguments */
	BLI_argsAdd(ba, 4, "-S", "--scene", CB(arg_handle_scene_set), C);
	BLI_argsAdd(ba, 4, "-P", "--python", CB(arg_handle_python_file_run), C);
	BLI_argsAdd(ba, 4, NULL, "--python-text", CB(arg_handle_python_text_run), C);
	BLI_argsAdd(ba, 4, NULL, "--python-expr", CB(arg_handle_python_expr_run), C);
	BLI_argsAdd(ba, 4, NULL, "--python-console", CB(arg_handle_python_console_run), C);
	BLI_argsAdd(ba, 4, NULL, "--python-exit-code", CB(arg_handle_python_exit_code_set), NULL);
	BLI_argsAdd(ba, 4, NULL, "--addons", CB(arg_handle_addons_set), C);

	BLI_argsAdd(ba, 1, "-t", "--threads", CB(arg_handle_threads_set), NULL);
	BLI_argsAdd(ba, 4, "-x", "--use-extension", CB(arg_handle_extension_set), C);

#undef CB
#undef CB_EX

}

/**
 * Needs to be added separately.
 */
void main_args_setup_post(bContext *C, bArgs *ba)
{
	BLI_argsParse(ba, 4, arg_handle_load_file, C);
}

/** \} */
