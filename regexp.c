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

#include <sys/types.h>
#include <inttypes.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "regexp.h"

struct Regexp {
	bool exec;
	regex_t *regex;
	regex_t restorage;
	regmatch_t *match;
	size_t nmatch;
	const char *buf;
};

// Prototypes
static void regexp_init(struct Regexp *, regex_t *);

void
regexp_init(struct Regexp *regexp, regex_t *regex)
{
	regexp->regex = regex;
	regexp->nmatch = 8;
	regexp->match = xrecallocarray(NULL, 0, regexp->nmatch, sizeof(regmatch_t));
}

struct Regexp *
regexp_new_from_str(struct Mempool *pool, const char *pattern, int flags)
{
	struct Regexp *regexp = xmalloc(sizeof(struct Regexp));
	if (regcomp(&regexp->restorage, pattern, flags) != 0) {
		free(regexp);
		return NULL;
	}
	regexp_init(regexp, &regexp->restorage);
	return mempool_add(pool, regexp, regexp_free);
}


struct Regexp *
regexp_new(struct Mempool *pool, regex_t *regex)
{
	struct Regexp *regexp = xmalloc(sizeof(struct Regexp));
	regexp_init(regexp, regex);
	return mempool_add(pool, regexp, regexp_free);
}

void
regexp_free(struct Regexp *regexp)
{
	if (regexp == NULL) {
		return;
	}
	if (regexp->regex == &regexp->restorage) {
		regfree(regexp->regex);
	}
	free(regexp->match);
	free(regexp);
}

size_t
regexp_length(struct Regexp *regexp, size_t group)
{
	panic_unless(regexp->exec, "missing regexp_exec() call");

	if (group >= regexp->nmatch || regexp->match[group].rm_eo < 0 ||
	    regexp->match[group].rm_so < 0) {
		return 0;
	}
	return regexp->match[group].rm_eo - regexp->match[group].rm_so;
}

size_t
regexp_end(struct Regexp *regexp, size_t group)
{
	panic_unless(regexp->exec, "missing regexp_exec() call");

	if (group >= regexp->nmatch || regexp->match[group].rm_eo < 0) {
		return 0;
	}
	return regexp->match[group].rm_eo;
}

size_t
regexp_start(struct Regexp *regexp, size_t group)
{
	panic_unless(regexp->exec, "missing regexp_exec() call");

	if (group >= regexp->nmatch || regexp->match[group].rm_so < 0) {
		return 0;
	}
	return regexp->match[group].rm_so;
}

char *
regexp_substr(struct Regexp *regexp, struct Mempool *pool, size_t group)
{
	panic_unless(regexp->exec && regexp->buf, "missing regexp_exec() call");

	if (group >= regexp->nmatch) {
		return NULL;
	}
	return str_slice(pool, regexp->buf, regexp_start(regexp, group), regexp_end(regexp, group));
}

int
regexp_exec(struct Regexp *regexp, const char *buf)
{
	regexp->buf = buf;
	regexp->exec = true;
	return regexec(regexp->regex, regexp->buf, regexp->nmatch, regexp->match, 0);
}
