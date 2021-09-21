/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Tobias Kortkamp <tobik@FreeBSD.org>
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
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

enum State {
	NONE,
	CMAKE_ARGS,
	CMAKE_D,
};

struct WalkerData {
	struct Parser *parser;
};

static enum ASTWalkState
refactor_sanitize_cmake_args_walker(struct AST *node, struct WalkerData *this)
{
	switch (node->type) {
	case AST_VARIABLE: {
		SCOPE_MEMPOOL(pool);

		char *helper = NULL;
		enum State state = NONE;
		if (is_options_helper(pool, this->parser, node->variable.name, NULL, &helper, NULL)) {
			if (strcmp(helper, "CMAKE_ON") == 0 || strcmp(helper, "CMAKE_OFF") == 0 ||
			    strcmp(helper, "MESON_ON") == 0 || strcmp(helper, "MESON_OFF") == 0) {
				state = CMAKE_ARGS;
			}
		} else if (strcmp(node->variable.name, "CMAKE_ARGS") == 0 ||
			   strcmp(node->variable.name, "MESON_ARGS") == 0) {
			state = CMAKE_ARGS;
		}
		struct Array *words = mempool_array(pool);
		ARRAY_FOREACH(node->variable.words, const char *, word) {
			if (state == CMAKE_ARGS && strcmp(word, "-D") == 0) {
				state = CMAKE_D;
				node->edited = 1;
				if (word_index == array_len(node->variable.words) - 1) {
					array_append(words, word);
				}
			} else if (state == CMAKE_D) {
				array_append(words, str_printf(node->pool, "-D%s", word));
				state = CMAKE_ARGS;
				node->edited = 1;
			} else {
				array_append(words, word);
			}
		}
		array_truncate(node->variable.words);
		ARRAY_JOIN(node->variable.words, words);
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(refactor_sanitize_cmake_args_walker, node, this);

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_sanitize_cmake_args)
{
	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return;
	}

	refactor_sanitize_cmake_args_walker(root, &(struct WalkerData){
		.parser = parser,
	});
}
