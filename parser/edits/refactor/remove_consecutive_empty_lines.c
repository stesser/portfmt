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

#include <ctype.h>
#include <regex.h>
#include <stdio.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
};

static int
is_empty_line(const char *s)
{
	for (const char *p = s; *p != 0; p++) {
		if (!isspace(*p)) {
			return 0;
		}
	}
	return 1;
}

static enum ASTWalkState
refactor_remove_consecutive_empty_lines_walker(struct WalkerData *this, struct ASTNode *node)
{
	SCOPE_MEMPOOL(pool);

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_remove_consecutive_empty_lines_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_remove_consecutive_empty_lines_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_remove_consecutive_empty_lines_walker(this, child));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_remove_consecutive_empty_lines_walker(this, child));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(refactor_remove_consecutive_empty_lines_walker(this, child));
		}
		break;
	case AST_NODE_COMMENT: {
		int empty = 0;
		struct Array *lines = mempool_array(pool);
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			if (is_empty_line(line)) {
				if (empty == 0) {
					array_append(lines, line);
				}
				empty++;
			} else {
				array_append(lines, line);
				empty = 0;
			}
		}
		if (array_len(lines) < array_len(node->comment.lines)) {
			array_truncate(node->comment.lines);
			ARRAY_FOREACH(lines, const char *, line) {
				array_append(node->comment.lines, line);
			}
			node->edited = 1;
		}
		break;
	} case AST_NODE_TARGET_COMMAND:
	case AST_NODE_EXPR_FLAT:
	case AST_NODE_VARIABLE:
		break;
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_remove_consecutive_empty_lines)
{
	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	refactor_remove_consecutive_empty_lines_walker(&(struct WalkerData){
	}, root);

	return 1;
}
