/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <err.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_install(void)
{
	fprintf(stderr,
	    "usage: pkg install [-AfgLnqRXxy] [-r reponame] <pkg-name> ...\n\n");
	fprintf(stderr, "For more information see 'pkg help install'.\n");
}

int
exec_install(int argc, char **argv)
{
	struct pkg *pkg = NULL;
	struct pkgdb_it *it = NULL;
	struct pkgdb *db = NULL;
	struct pkg_jobs *jobs = NULL;
	const char *reponame = NULL;
	int retcode = EX_SOFTWARE;
	int ch;
	bool yes = false;
	bool auto_update = true;
	bool recursive = false;
	bool automatic = false;

	match_t match = MATCH_EXACT;
	bool force = false;
	bool dry_run = false;

	while ((ch = getopt(argc, argv, "AfgLnqRr:Xxy")) != -1) {
		switch (ch) {
		case 'A':
			automatic = true;
			break;
		case 'f':
			force = true;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'L':
			auto_update = false;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'R':
			recursive = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'X':
			match = MATCH_EREGEX;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'y':
			yes = true;
			break;
		default:
			usage_install();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_install();
		return (EX_USAGE);
	}

	if (geteuid() != 0) {
		warnx("Installing packages can only be done as root");
		return (EX_NOPERM);
	}

	/* first update the remote repositories if needed */
	if (auto_update && (retcode = pkgcli_update(false)) != EPKG_OK)
		return (retcode);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, db, force, dry_run)
	    != EPKG_OK) {
		goto cleanup;
	}

	if ((it = pkgdb_query_installs(db, match, argc, argv, reponame,
	    force, recursive)) == NULL)
		goto cleanup;

	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC|PKG_LOAD_DEPS) ==
	    EPKG_OK) {
		if (automatic)
			pkg_set(pkg, PKG_AUTOMATIC, true);
		pkg_jobs_add(jobs, pkg);
		pkg = NULL;
	}
	pkgdb_it_free(it);

	if (pkg_jobs_is_empty(jobs))
		goto cleanup;

	/* print a summary before applying the jobs */
	pkg = NULL;
	if (!quiet || dry_run) {
		print_jobs_summary(jobs, PKG_JOBS_INSTALL,
		    "The following packages will be installed:\n\n");

		if (!yes)
			pkg_config_bool(PKG_CONFIG_ASSUME_ALWAYS_YES, &yes);
		if (!yes && !dry_run)
			yes = query_yesno(
			    "\nProceed with installing packages [y/N]: ");
		if (dry_run)
			yes = false;
	}

	if (yes)
		if (pkg_jobs_apply(jobs) != EPKG_OK)
			goto cleanup;

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	retcode = EX_OK;

	cleanup:
	pkg_jobs_free(jobs);
	pkgdb_close(db);

	return (retcode);
}
