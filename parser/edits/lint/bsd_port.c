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

#include <regex.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
	int found;
};

static enum ASTWalkState
lint_bsd_port_walker(struct WalkerData *this, struct ASTNode *node)
{
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(lint_bsd_port_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(lint_bsd_port_walker(this, child));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(lint_bsd_port_walker(this, child));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(lint_bsd_port_walker(this, child));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(lint_bsd_port_walker(this, child));
		}
		break;
	case AST_NODE_VARIABLE:
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
		break;
	case AST_NODE_EXPR_FLAT:
		if (is_include_bsd_port_mk(node)) {
			this->found = 1;
			return AST_WALK_STOP;
		}
		break;
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(lint_bsd_port)
{
	if (parser_metadata(parser, PARSER_METADATA_MASTERDIR)) {
		return 0;
	}

	struct WalkerData this = {
		.found = 0,
	};
	lint_bsd_port_walker(&this, root);

	unless (this.found) {
		parser_set_error(parser, PARSER_ERROR_EDIT_FAILED, "not a FreeBSD Ports Makefile");
	}

	return 0;
}
