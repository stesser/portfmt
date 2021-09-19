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

static enum ASTWalkState
output_variable_value_walker(struct WalkerData *this, struct ASTNode *node)
{
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		break;
	case AST_NODE_INCLUDE:
		ARRAY_FOREACH(node->include.body, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(output_variable_value_walker(this, child));
		}
		break;
	case AST_NODE_VARIABLE:
		if ((this->param->keyfilter == NULL || this->param->keyfilter(this->parser, node->variable.name, this->param->keyuserdata))) {
			this->param->found = 1;
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				if ((this->param->filter == NULL || this->param->filter(this->parser, word, this->param->filteruserdata))) {
					if (this->param->callback) {
						this->param->callback(this->pool, node->variable.name, word, NULL, this->param->callbackuserdata);
					}
				}
			}
		}
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_EXPR_FLAT:
		break;
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(output_variable_value)
{
	struct ParserEditOutput *param = userdata;
	if (param == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, "missing parameter");
		return 0;
	}

	param->found = 0;
	output_variable_value_walker(&(struct WalkerData){
		.parser = parser,
		.pool = extpool,
		.param = param,
	}, root);

	return 0;
}

