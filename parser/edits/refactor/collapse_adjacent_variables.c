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

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
	struct Parser *parser;
	struct Mempool *pool;
};

static int
is_candidate(struct ASTNode *node)
{
	if (node->type != AST_NODE_VARIABLE) {
		return 0;
	}

	switch (node->variable.modifier) {
	case AST_NODE_VARIABLE_MODIFIER_APPEND:
	case AST_NODE_VARIABLE_MODIFIER_ASSIGN:
		return 1;
	default:
		return 0;
	}
}

static int
has_eol_comment(struct ASTNode *node)
{
	ARRAY_FOREACH(node->variable.words, const char *, word) {
		if (is_comment(word)) {
			return 1;
		}
	}
	return 0;
}

static void
merge_variables(struct Array *nodelist, struct Array *group)
{
	if (array_len(group) < 2) {
		return;
	}

	SCOPE_MEMPOOL(pool);

	struct ASTNode *first = array_get(group, 0);
	struct ASTNode *last = array_get(group, array_len(group) - 1);
	ARRAY_FOREACH_SLICE(group, 1, -1, struct ASTNode *, node) {
		ARRAY_FOREACH(node->variable.words, const char *, word) {
			array_append(first->variable.words, word);
		}
	}
	first->edited = 1;
	first->line_end = last->line_end;

	struct Array *newnodelist = mempool_array(pool);
	ARRAY_FOREACH(nodelist, struct ASTNode *, node) {
		if (node->type == AST_NODE_VARIABLE) {
			if (array_find(group, node, NULL, NULL) < 1) {
				array_append(newnodelist, node);
			}
		} else {
			array_append(newnodelist, node);
		}
	}
	array_truncate(nodelist);
	ARRAY_FOREACH(newnodelist, struct ASTNode *, node) {
		array_append(nodelist, node);
	}
}

static void
process_siblings(struct Array *nodelist, struct Array *siblings)
{
	SCOPE_MEMPOOL(pool);

	struct Array *group = mempool_array(pool);
	const char *name = NULL;
	ARRAY_FOREACH(siblings, struct ASTNode *, node) {
		unless (name) {
			name = node->variable.name;
		}
		if (!is_candidate(node) || strcmp(name, node->variable.name) != 0 || has_eol_comment(node)) {
			merge_variables(nodelist, group);
			group = mempool_array(pool);
			name = NULL;
		} else {
			array_append(group, node);
		}
	}
	merge_variables(nodelist, group);

	array_truncate(siblings);
}

static enum ASTWalkState
refactor_collapse_adjacent_variables_walker(struct WalkerData *this, struct ASTNode *node, struct Array *last_siblings)
{
	SCOPE_MEMPOOL(pool);
	struct Array *siblings = mempool_array(pool);

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(this, child, siblings));
		}
		process_siblings(node->root.body, siblings);
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(this, child, siblings));
		}
		process_siblings(node->forexpr.body, siblings);
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(this, child, siblings));
		}
		process_siblings(node->ifexpr.body, siblings);

		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(this, child, siblings));
		}
		process_siblings(node->ifexpr.orelse, siblings);
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(this, child, siblings));
		}
		process_siblings(node->target.body, siblings);
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_VARIABLE:
	case AST_NODE_EXPR_FLAT:
		array_append(last_siblings, node);
		break;
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_collapse_adjacent_variables)
{
	SCOPE_MEMPOOL(pool);

	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	refactor_collapse_adjacent_variables_walker(&(struct WalkerData){
		.parser = parser,
		.pool = pool,
	}, root, mempool_array(pool));

	return 1;
}

