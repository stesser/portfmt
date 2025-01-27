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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/trait/compare.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
	struct Parser *parser;
	struct Mempool *pool;
};

// Prototypes
static bool is_candidate(struct AST *);
static bool has_eol_comment(struct AST *);
static void merge_variables(struct Array *, struct Array *);
static void process_siblings(struct Array *, struct Array *);
static enum ASTWalkState refactor_collapse_adjacent_variables_walker(struct AST *, struct WalkerData *, struct Array *);

bool
is_candidate(struct AST *node)
{
	if (node->type != AST_VARIABLE) {
		return false;
	}

	switch (node->variable.modifier) {
	case AST_VARIABLE_MODIFIER_APPEND:
	case AST_VARIABLE_MODIFIER_ASSIGN:
		return true;
	default:
		return false;
	}
}

bool
has_eol_comment(struct AST *node)
{
	return node->variable.comment && strlen(node->variable.comment) > 0;
}

void
merge_variables(struct Array *nodelist, struct Array *group)
{
	if (array_len(group) < 2) {
		return;
	}

	SCOPE_MEMPOOL(pool);

	struct AST *first = array_get(group, 0);
	struct AST *last = array_get(group, array_len(group) - 1);
	ARRAY_FOREACH_SLICE(group, 1, -1, struct AST *, node) {
		ARRAY_FOREACH(node->variable.words, const char *, word) {
			array_append(first->variable.words, word);
		}
	}
	first->edited = true;
	first->line_end = last->line_end;

	struct Array *newnodelist = mempool_array(pool);
	ARRAY_FOREACH(nodelist, struct AST *, node) {
		if (node->type == AST_VARIABLE) {
			if (array_find(group, node, id_compare) < 1) {
				array_append(newnodelist, node);
			}
		} else {
			array_append(newnodelist, node);
		}
	}
	array_truncate(nodelist);
	ARRAY_JOIN(nodelist, newnodelist);
}

void
process_siblings(struct Array *nodelist, struct Array *siblings)
{
	SCOPE_MEMPOOL(pool);

	struct Array *group = mempool_array(pool);
	const char *name = NULL;
	ARRAY_FOREACH(siblings, struct AST *, node) {
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

enum ASTWalkState
refactor_collapse_adjacent_variables_walker(struct AST *node, struct WalkerData *this, struct Array *last_siblings)
{
	SCOPE_MEMPOOL(pool);
	struct Array *siblings = mempool_array(pool);

	switch (node->type) {
	case AST_ROOT:
		ARRAY_FOREACH(node->root.body, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		process_siblings(node->root.body, siblings);
		break;
	case AST_DELETED:
		break;
	case AST_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		process_siblings(node->forexpr.body, siblings);
		break;
	case AST_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		process_siblings(node->ifexpr.body, siblings);

		ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		process_siblings(node->ifexpr.orelse, siblings);
		break;
	case AST_INCLUDE:
		ARRAY_FOREACH(node->include.body, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		break;
	case AST_TARGET:
		ARRAY_FOREACH(node->target.body, struct AST *, child) {
			AST_WALK_RECUR(refactor_collapse_adjacent_variables_walker(child, this, siblings));
		}
		process_siblings(node->target.body, siblings);
		break;
	case AST_COMMENT:
	case AST_TARGET_COMMAND:
	case AST_VARIABLE:
	case AST_EXPR:
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
		return;
	}

	refactor_collapse_adjacent_variables_walker(root, &(struct WalkerData){
		.parser = parser,
		.pool = pool,
	}, mempool_array(pool));
}
