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

enum DedupAction {
	DEFAULT,
	APPEND,
	USES,
};

struct WalkerData {
	struct Parser *parser;
};

static enum ASTWalkState
refactor_dedup_tokens_walker(struct WalkerData *this, struct ASTNode *node)
{
	SCOPE_MEMPOOL(pool);

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		break;
	case AST_NODE_INCLUDE:
		ARRAY_FOREACH(node->include.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_dedup_tokens_walker(this, child));
		}
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_EXPR_FLAT:
		break;
	case AST_NODE_VARIABLE:
		if (skip_dedup(this->parser, node->variable.name, node->variable.modifier)) {
			return AST_WALK_CONTINUE;
		} else {
			struct Set *seen = mempool_set(pool, str_compare, NULL, NULL);
			struct Set *uses = mempool_set(pool, str_compare, NULL, NULL);
			enum DedupAction action = DEFAULT;
			struct Array *words = mempool_array(pool);
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				// XXX: Handle *_DEPENDS (turn 'RUN_DEPENDS=foo>=1.5.6:misc/foo foo>0:misc/foo'
				// into 'RUN_DEPENDS=foo>=1.5.6:misc/foo')?
				char *helper = NULL;
				if (is_comment(word)) {
					action = APPEND;
				} else if (is_options_helper(pool, this->parser, node->variable.name, NULL, &helper, NULL)) {
					if (strcmp(helper, "USES") == 0 || strcmp(helper, "USES_OFF") == 0) {
						action = USES;
					}
				} else if (strcmp(node->variable.name, "USES") == 0) {
					action = USES;
				}
				switch (action) {
				case APPEND:
					array_append(words, word);
					set_add(seen, word);
					break;
				case USES: {
					char *buf = str_dup(pool, word);
					char *args = strchr(buf, ':');
					if (args) {
						*args = 0;
					}
					// We follow the semantics of the ports framework.
					// 'USES=compiler:c++11-lang compiler:c++14-lang' is
					// semantically equivalent to just USES=compiler:c++11-lang
					// since compiler_ARGS has already been set once before.
					// As such compiler:c++14-lang can be dropped entirely.
					if (!set_contains(uses, buf)) {
						array_append(words, word);
						set_add(uses, buf);
						set_add(seen, word);
					}
					break;
				} default:
					if (!set_contains(seen, word)) {
						array_append(words, word);
						set_add(seen, word);
					}
					break;
				}
			}
			if (array_len(words) < array_len(node->variable.words)) {
				node->edited = 1;
				array_truncate(node->variable.words);
				ARRAY_JOIN(node->variable.words, words);
			}
		}
		break;
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_dedup_tokens)
{
	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	refactor_dedup_tokens_walker(&(struct WalkerData){
		.parser = parser,
	}, root);

	return 1;
}

