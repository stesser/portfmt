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

#include <limits.h>
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

struct WalkerData {
	size_t kak_cursor_line;
	struct ASTNodeLineRange *range;
};

static void
kak_error(struct Parser *parser, const char *errstr)
{
	parser_enqueue_output(parser, "echo -markup \"{Error}");
	parser_enqueue_output(parser, errstr);
	parser_enqueue_output(parser, "\"\n");
	parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, errstr);
}

static enum ASTWalkState
kakoune_select_object_on_line_walker(struct WalkerData *this, struct ASTNode *node)
{
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		break;
	case AST_NODE_INCLUDE:
		ARRAY_FOREACH(node->include.body, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(kakoune_select_object_on_line_walker(this, child));
		}
		break;
	case AST_NODE_VARIABLE:
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_EXPR_FLAT:
		break;
	}

	if (this->kak_cursor_line >= node->line_start.a && this->kak_cursor_line < node->line_start.b) {
		this->range = &node->line_start;
		return AST_WALK_STOP;
	} else {
		return AST_WALK_CONTINUE;
	}
}


PARSER_EDIT(kakoune_select_object_on_line)
{
	SCOPE_MEMPOOL(pool);

	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		kak_error(parser, "needs PARSER_OUTPUT_RAWLINES");
		return 1;
	}

	const char *kak_cursor_line_buf = getenv("kak_cursor_line");
	if (!kak_cursor_line_buf) {
		kak_error(parser, "could not find kak_cursor_line");
		return 1;
	}

	const char *errstr;
	struct WalkerData this = {
		.range = NULL,
		.kak_cursor_line = strtonum(kak_cursor_line_buf, 1, INT_MAX, &errstr),
	};
	if (this.kak_cursor_line == 0) {
		const char *error_msg;
		if (errstr) {
			error_msg = str_printf(pool, "could not parse kak_cursor_line: %s", errstr);
		} else {
			error_msg = "could not parse kak_cursor_line";
		}
		kak_error(parser, error_msg);
		return 1;
	}

	if (AST_WALK_STOP == kakoune_select_object_on_line_walker(&this, root)) {
		parser_enqueue_output(parser, str_printf(pool, "select %zu.1,%zu.10000000\n", this.range->a, this.range->b - 1));
	} else {
		kak_error(parser, "no selectable object found on this line");
	}

	return 1;
}
