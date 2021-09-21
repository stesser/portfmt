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

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct WalkerData {
	struct Parser *parser;
	struct Mempool *pool;
	struct ParserEditOutput *param;
};

static void
add_word(struct WalkerData *this, const char *word)
{
	if ((this->param->filter == NULL || this->param->filter(this->parser, word, this->param->filteruserdata))) {
		this->param->found = 1;
		if (this->param->callback) {
			this->param->callback(this->pool, word, word, NULL, this->param->callbackuserdata);
		}
	}
}

static enum ASTWalkState
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
	case AST_IF:
		ARRAY_FOREACH(node->ifexpr.test, const char *, word) {
			add_word(this, word);
		}
		break;
	case AST_INCLUDE:
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
