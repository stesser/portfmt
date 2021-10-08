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

#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct WalkerData {
	struct Parser *parser;
	struct Mempool *pool;
	struct ParserEditOutput *param;
};

// Prototypes
static void add_word(struct WalkerData *, const char *);
static enum ASTWalkState output_conditional_token_walker(struct AST *, struct WalkerData *);

void
add_word(struct WalkerData *this, const char *word)
{
	if ((this->param->filter == NULL || this->param->filter(this->parser, word, this->param->filteruserdata))) {
		this->param->found = 1;
		if (this->param->callback) {
			this->param->callback(this->pool, word, word, NULL, this->param->callbackuserdata);
		}
	}
}

enum ASTWalkState
output_conditional_token_walker(struct AST *node, struct WalkerData *this)
{
	switch (node->type) {
	case AST_EXPR:
		ARRAY_FOREACH(node->expr.words, const char *, word) {
			add_word(this, word);
		}
		break;
	case AST_FOR:
		ARRAY_FOREACH(node->forexpr.bindings, const char *, word) {
			add_word(this, word);
		}
		ARRAY_FOREACH(node->forexpr.words, const char *, word) {
			add_word(this, word);
		}
		break;
	case AST_IF: {
		static const char *merge_with_next[] = {
			"commands(",
			"defined(",
			"empty(",
			"exists(",
			"make(",
			"target(",
			"!",
			"(",
		};
		SCOPE_MEMPOOL(pool);
		struct Array *word_groups = mempool_array(pool);
		ARRAY_FOREACH(node->ifexpr.test, const char *, word) {
			int merge = 0;
			if (word_index < array_len(node->ifexpr.test) - 1) {
				merge = 1;
				for (size_t i = 0; i < nitems(merge_with_next); i++) {
					if (strcmp(word, merge_with_next[i]) == 0) {
						merge = 0;
						break;
					}
				}
				if (merge) {
					// No merge yet when ) next
					const char *next = array_get(node->ifexpr.test, word_index + 1);
					if (next && strcmp(next, ")") == 0) {
						merge = 0;
					}
				}
			}

			struct Array *group = array_get(word_groups, array_len(word_groups) - 1);
			unless (group) {
				group = mempool_array(pool);
				array_append(word_groups, group);
			}
			array_append(group, word);
			if (merge) {
				group = mempool_array(pool);
				array_append(word_groups, group);
			}
		}
		ARRAY_FOREACH(word_groups, struct Array *, group) {
			const char *word = str_join(pool, group, "");
			add_word(this, word);
		}
		break;
	} case AST_INCLUDE:
		if (node->include.sys) {
			add_word(this, str_printf(this->pool, "<%s>", node->include.path));
		} else {
			add_word(this, str_printf(this->pool, "\"%s\"", node->include.path));
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(output_conditional_token_walker, node, this);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(output_conditional_token)
{
	struct ParserEditOutput *param = userdata;
	if (param == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, "missing parameter");
		return;
	}

	param->found = 0;
	output_conditional_token_walker(root, &(struct WalkerData){
		.parser = parser,
		.pool = extpool,
		.param = param,
	});
}
