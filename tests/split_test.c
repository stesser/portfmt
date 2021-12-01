/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Tobias Kortkamp <tobik@FreeBSD.org>
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

#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/io.h>
#include <libias/mempool.h>
#include <libias/mempool/file.h>
#include <libias/path.h>
#include <libias/str.h>

int
main(int argc, char *argv[])
{
	SCOPE_MEMPOOL(pool);
	if (argc != 4) {
		errx(1, "invalid argument");
	}
	const char *srcdir = argv[1];
	const char *builddir = argv[2];
	const char *testname = argv[3];
	const char *testfile = str_printf(pool, "%s/%s", srcdir, testname);
	FILE *f = mempool_fopenat(pool, AT_FDCWD, testfile, "r", 0);
	unless (f) {
		err(1, "%s: fopen", testfile);
	}
	const char *buf = slurp(f, pool);
	unless (buf) {
		err(1, "%s: slurp", testfile);
	}
	struct Array *files = str_split(pool, buf, "<<<<<<<<<\n");
	unless (array_len(files) == 3 || array_len(files) == 2) {
		errx(1, "%s: unsupported number of chunks: %zu", testfile, array_len(files));
	}
	ARRAY_FOREACH(files, const char *, test) {
		const char *suffix = NULL;
		if (array_len(files) == 2) {
			if (test_index == 0) {
				suffix = "in";
			} else if (test_index == 1) {
				suffix = "expected";
			}
		} else if (array_len(files) == 3) {
			if (test_index == 0) {
				suffix = "sh";
			} else if (test_index == 1) {
				suffix = "in";
			} else if (test_index == 2) {
				suffix = "expected";
			}
		}
		unless (suffix) {
			errx(1, "%s: no suffix set", testfile);
		}
		FILE *in = mempool_fopenat(pool, AT_FDCWD, str_printf(pool, "%s/%s.%s", builddir, testname, suffix), "w", 0644);
		fputs(test, in);
		if (ferror(in)) {
			err(1, "%s: ferror", testfile);
		}
	}
	return 0;
}
