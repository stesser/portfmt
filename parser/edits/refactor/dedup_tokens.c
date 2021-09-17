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

#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"
#include "token.h"
#include "variable.h"

enum DedupAction {
	APPEND,
	DEFAULT,
	SKIP,
	USES,
};

PARSER_EDIT(refactor_dedup_tokens)
{
	SCOPE_MEMPOOL(pool);

	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	struct Array *tokens = array_new();
	struct Set *seen = mempool_set(pool, str_compare, NULL, NULL);
	struct Set *uses = mempool_set(pool, str_compare, NULL, NULL);
	enum DedupAction action = DEFAULT;
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		switch (token_type(t)) {
		case VARIABLE_START:
			set_truncate(seen);
			set_truncate(uses);
			action = DEFAULT;
			if (skip_dedup(parser, variable_name(token_variable(t)), variable_modifier(token_variable(t)))) {
				action = SKIP;
			} else {
				// XXX: Handle *_DEPENDS (turn 'RUN_DEPENDS=foo>=1.5.6:misc/foo foo>0:misc/foo'
				// into 'RUN_DEPENDS=foo>=1.5.6:misc/foo')?
				char *helper = NULL;
				if (is_options_helper(pool, parser, variable_name(token_variable(t)), NULL, &helper, NULL)) {
					if (strcmp(helper, "USES") == 0 || strcmp(helper, "USES_OFF") == 0) {
						action = USES;
					}
				} else if (strcmp(variable_name(token_variable(t)), "USES") == 0) {
					action = USES;
				}
			}
			array_append(tokens, t);
			break;
		case VARIABLE_TOKEN:
			if (action == SKIP) {
				array_append(tokens, t);
			} else {
				if (is_comment(token_data(t))) {
					action = APPEND;
				}
				if (action == APPEND) {
					array_append(tokens, t);
					set_add(seen, token_data(t));
				} else if (action == USES) {
					char *buf = str_dup(pool, token_data(t));
					char *args = strchr(buf, ':');
					if (args) {
						*args = 0;
					}
					// We follow the semantics of the ports framework.
					// 'USES=compiler:c++11-lang compiler:c++14-lang' is
					// semantically equivalent to just USES=compiler:c++11-lang
					// since compiler_ARGS has already been set once before.
					// As such compiler:c++14-lang can be dropped entirely.
					if (set_contains(uses, buf)) {
						parser_mark_for_gc(parser, t);
					} else {
						array_append(tokens, t);
						set_add(uses, buf);
						set_add(seen, token_data(t));
					}
				} else if (!set_contains(seen, token_data(t))) {
					array_append(tokens, t);
					set_add(seen, token_data(t));
				} else {
					parser_mark_for_gc(parser, t);
				}
			}
			break;
		default:
			array_append(tokens, t);
			break;
		}
	}

	*new_tokens = tokens;
	return 0;
}

