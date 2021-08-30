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

/** \file creator/creator_signals.c
 *  \ingroup creator
 */

#if defined(__linux__) && defined(__GNUC__)
#  define _GNU_SOURCE
#  include <fenv.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "BLI_sys_types.h"

#include "BLI_utildefines.h"
#include "BLI_string.h"
#include "BLI_path_util.h"
#include "BLI_fileops.h"
#include "BLI_system.h"
#include BLI_SYSTEM_PID_H

#include "BKE_appdir.h"  /* BKE_tempdir_base */
#include "BKE_blender_version.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"

#  define SYS_SystemHandle int

#include <signal.h>

#include "creator_intern.h"  /* own include */

// #define USE_WRITE_CRASH_BLEND
#ifdef USE_WRITE_CRASH_BLEND
#  include "BKE_undo_system.h"
#  include "BLO_undofile.h"
#endif

/* set breakpoints here when running in debug mode, useful to catch floating point errors */
#if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
static void sig_handle_fpe(int UNUSED(sig))
{
	fprintf(stderr, "debug: SIGFPE trapped\n");
}
#endif

/* handling ctrl-c event in console */
#if !defined(WITH_HEADLESS)
static void sig_handle_blender_esc(int sig)
{
	static int count = 0;

	G.is_break = true;  /* forces render loop to read queue, not sure if its needed */

	if (sig == 2) {
		if (count) {
			printf("\nBlender killed\n");
			exit(2);
		}
		printf("\nSent an internal break event. Press ^C again to kill Blender\n");
		count++;
	}
}
#endif

static void sig_handle_crash_backtrace(FILE *fp)
{
	fputs("\n# backtrace\n", fp);
	BLI_system_backtrace(fp);
}

static void sig_handle_crash(int signum)
{
	wmWindowManager *wm = G_MAIN->wm.first;

#ifdef USE_WRITE_CRASH_BLEND
	if (wm->undo_stack) {
		struct MemFile *memfile = BKE_undosys_stack_memfile_get_active(wm->undo_stack);
		if (memfile) {
			char fname[FILE_MAX];

			if (!G_MAIN->name[0]) {
				BLI_make_file_string("/", fname, BKE_tempdir_base(), "crash.blend");
			}
			else {
				BLI_strncpy(fname, G_MAIN->name, sizeof(fname));
				BLI_path_extension_replace(fname, sizeof(fname), ".crash.blend");
			}

			printf("Writing: %s\n", fname);
			fflush(stdout);

			BLO_memfile_write_file(memfile, fname);
		}
	}
#endif

	FILE *fp;
	char header[512];

	char fname[FILE_MAX];

	if (!G_MAIN->name[0]) {
		BLI_join_dirfile(fname, sizeof(fname), BKE_tempdir_base(), "blender.crash.txt");
	}
	else {
		BLI_join_dirfile(fname, sizeof(fname), BKE_tempdir_base(), BLI_path_basename(G_MAIN->name));
		BLI_path_extension_replace(fname, sizeof(fname), ".crash.txt");
	}

	printf("Writing: %s\n", fname);
	fflush(stdout);

#ifndef BUILD_DATE
	BLI_snprintf(header, sizeof(header), "# " BLEND_VERSION_FMT ", Unknown revision\n", BLEND_VERSION_ARG);
#else
	BLI_snprintf(header, sizeof(header), "# " BLEND_VERSION_FMT ", Commit date: %s %s, Hash %s\n",
	             BLEND_VERSION_ARG, build_commit_date, build_commit_time, build_hash);
#endif

	/* open the crash log */
	errno = 0;
	fp = BLI_fopen(fname, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Unable to save '%s': %s\n",
		        fname, errno ? strerror(errno) : "Unknown error opening file");
	}
	else {
		if (wm) {
			BKE_report_write_file_fp(fp, &wm->reports, header);
		}

		sig_handle_crash_backtrace(fp);

		fclose(fp);
	}

	/* Delete content of temp dir! */
	BKE_tempdir_session_purge();

	/* really crash */
	signal(signum, SIG_DFL);
#ifndef WIN32
	kill(getpid(), signum);
#else
	TerminateProcess(GetCurrentProcess(), signum);
#endif
}

static void sig_handle_abort(int UNUSED(signum))
{
	/* Delete content of temp dir! */
	BKE_tempdir_session_purge();
}


void main_signal_setup(void)
{
	if (app_state.signal.use_crash_handler) {
		/* after parsing args */
		signal(SIGSEGV, sig_handle_crash);
	}

	if (app_state.signal.use_abort_handler) {
		signal(SIGABRT, sig_handle_abort);
	}
}

void main_signal_setup_background(void)
{
	/* for all platforms, even windos has it! */
	BLI_assert(G.background);

#if !defined(WITH_HEADLESS)
	signal(SIGINT, sig_handle_blender_esc);  /* ctrl c out bg render */
#endif
}


void main_signal_setup_fpe(void)
{
#if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
	/* zealous but makes float issues a heck of a lot easier to find!
	 * set breakpoints on sig_handle_fpe */
	signal(SIGFPE, sig_handle_fpe);

# if defined(__linux__) && defined(__GNUC__)
	feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
# endif /* defined(__linux__) && defined(__GNUC__) */
#endif
}
