/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Tobias Kortkamp <tobik@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#if HAVE_CAPSICUM
# include <sys/capsicum.h>
#endif
#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libias/array.h>
#include <libias/mempool.h>
#include <libias/mempool/file.h>
#include <libias/str.h>

#include "capsicum_helpers.h"
#include "mainutils.h"
#include "parser.h"

// Prototypes
static FILE *open_file_helper(struct Mempool *, const char *, const char *, const char **);

void
enter_sandbox()
{
#if HAVE_CAPSICUM
	if (caph_limit_stderr() < 0) {
		err(1, "caph_limit_stderr");
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif
#if HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1) {
		err(1, "pledge");
	}
#endif
}

bool
read_common_args(int *argc, char ***argv, struct ParserSettings *settings, const char *optstr, struct Mempool *pool, struct Array *expressions)
{
	int ch;
	while ((ch = getopt(*argc, *argv, optstr)) != -1) {
		switch (ch) {
		case 'D':
			settings->behavior |= PARSER_OUTPUT_DIFF;
			if (optarg) {
				const char *errstr = NULL;
				settings->diff_context = strtonum(optarg, 0, INT_MAX, &errstr);
				if (errstr != NULL) {
					errx(1, "-D%s is %s", optarg, errstr);
				}
			}
			break;
		case 'd':
			settings->behavior |= PARSER_OUTPUT_DUMP_TOKENS;
			settings->debug_level++;
			break;
		case 'e':
			if (expressions && optarg) {
				array_append(expressions, str_dup(pool, optarg));
			} else {
				return false;
			}
			break;
		case 'i':
			settings->behavior |= PARSER_OUTPUT_INPLACE;
			break;
		case 't':
			settings->behavior |= PARSER_FORMAT_TARGET_COMMANDS;
			break;
		case 'u':
			settings->behavior |= PARSER_UNSORTED_VARIABLES;
			break;
		case 'U':
			settings->behavior |= PARSER_ALWAYS_SORT_VARIABLES;
			break;
		case 'w':
			if (optarg) {
				const char *errstr = NULL;
				settings->variable_wrapcol = strtonum(optarg, -1, INT_MAX, &errstr);
				settings->if_wrapcol = settings->variable_wrapcol;
				settings->for_wrapcol = settings->variable_wrapcol;
				if (errstr != NULL) {
					errx(1, "-w%s is %s", optarg, errstr);
				}
			} else {
				return false;
			}
			break;
		default:
			return false;
		}
	}
	*argc -= optind;
	*argv += optind;

	if ((settings->behavior & PARSER_OUTPUT_DUMP_TOKENS) ||
	    (settings->behavior & PARSER_OUTPUT_DIFF) ||
	    (settings->behavior & PARSER_OUTPUT_RAWLINES)) {
		settings->behavior &= ~PARSER_OUTPUT_INPLACE;
	}

	return true;
}

FILE *
open_file_helper(struct Mempool *extpool, const char *path, const char *mode, const char **retval)
{
	SCOPE_MEMPOOL(pool);

	char pwd[PATH_MAX];
	if (getcwd(pwd, PATH_MAX) == NULL) {
		*retval = NULL;
		return NULL;
	}

	char *filename = str_printf(pool, "%s/Makefile", path);
	FILE *f = mempool_fopenat(pool, AT_FDCWD, filename, mode, 0644);
	if (f == NULL) {
		f = mempool_fopenat(pool, AT_FDCWD, path, mode, 0644);
		if (f == NULL) {
			*retval = NULL;
			return NULL;
		}
		filename = str_dup(pool, path);
	}

	filename = mempool_take(pool, realpath(filename, NULL));
	if (filename == NULL) {
		*retval = NULL;
		return NULL;
	}

	if (str_startswith(filename, pwd) && filename[strlen(pwd)] == '/') {
		filename = str_dup(pool, filename + strlen(pwd) + 1);
	}

	*retval = mempool_add(extpool, mempool_forget(pool, filename), free);
	return mempool_add(extpool, mempool_forget(pool, f), fclose);
}

bool
open_file(enum MainutilsOpenFileBehavior behavior, int *argc, char ***argv, struct Mempool *pool, FILE **fp_in, FILE **fp_out, const char **filename)
{
#if HAVE_CAPSICUM
	closefrom(STDERR_FILENO + 1);
#endif

	if (*argc > 1 || ((behavior & MAINUTILS_OPEN_FILE_INPLACE) && *argc == 0)) {
		return false;
	} else if (*argc == 1) {
		if (behavior & MAINUTILS_OPEN_FILE_INPLACE) {
			if (!(behavior & MAINUTILS_OPEN_FILE_KEEP_STDIN)) {
				close(STDIN_FILENO);
			}
			close(STDOUT_FILENO);

			*fp_in = open_file_helper(pool, *argv[0], "r+", filename);
			*fp_out = *fp_in;
			if (*fp_in == NULL) {
				return false;
			}
#if HAVE_CAPSICUM
			if (caph_limit_stream(fileno(*fp_in), CAPH_READ | CAPH_WRITE | CAPH_FTRUNCATE) < 0) {
				return false;
			}
#endif
		} else  {
			if (!(behavior & MAINUTILS_OPEN_FILE_KEEP_STDIN)) {
				close(STDIN_FILENO);
			}
			*fp_in = open_file_helper(pool, *argv[0], "r", filename);
			if (*fp_in == NULL) {
				return false;
			}
#if HAVE_CAPSICUM
			if (caph_limit_stream(fileno(*fp_in), CAPH_READ) < 0) {
				return false;
			}
			if (caph_limit_stdio() < 0) {
				return false;
			}
#endif
		}
	} else {
#if HAVE_CAPSICUM
		if (caph_limit_stdio() < 0) {
			return false;
		}
#endif
	}

	*argc -= 1;
	*argv += 1;

	return true;
}
