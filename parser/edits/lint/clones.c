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

#include <stdlib.h>
#include <stdio.h>

#include <libias/array.h>
#include <libias/color.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct WalkerData {
	struct Set *seen;
	struct Set *seen_in_cond;
	struct Set *clones;
};

// Prototypes
static void add_clones(struct WalkerData *);
static enum ASTWalkState lint_clones_walker(struct AST *, struct WalkerData *, int);

void
add_clones(struct WalkerData *this)
{
	SET_FOREACH(this->seen_in_cond, const char *, name) {
		if (set_contains(this->seen, name) && !set_contains(this->clones, name)) {
			set_add(this->clones, str_dup(NULL, name));
		}
	}
	set_truncate(this->seen_in_cond);
}

enum ASTWalkState
lint_clones_walker(struct AST *node, struct WalkerData *this, int in_conditional)
{
	switch (node->type) {
	case AST_FOR:
	case AST_IF:
	case AST_INCLUDE:
		in_conditional++;
		break;
	case AST_VARIABLE:
		if (node->variable.modifier == AST_VARIABLE_MODIFIER_ASSIGN) {
			if (in_conditional > 0) {
				set_add(this->seen_in_cond, node->variable.name);
			} else if (set_contains(this->seen, node->variable.name)) {
				if (!set_contains(this->clones, node->variable.name)) {
					set_add(this->clones, str_dup(NULL, node->variable.name));
				}
			} else {
				set_add(this->seen, node->variable.name);
			}
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(lint_clones_walker, node, this, in_conditional);

	if (in_conditional <= 0) {
		add_clones(this);
	}

	return AST_WALK_CONTINUE;
}

PARSER_EDIT(lint_clones)
{
	SCOPE_MEMPOOL(pool);

	struct Set **clones_ret = userdata;
	int no_color = parser_settings(parser).behavior & PARSER_OUTPUT_NO_COLOR;

	struct WalkerData this = {
		.seen = mempool_set(pool, str_compare, NULL, NULL),
		.seen_in_cond = mempool_set(pool, str_compare, NULL, NULL),
		.clones = mempool_set(pool, str_compare, NULL, free),
	};
	lint_clones_walker(root, &this, 0);

	if (clones_ret == NULL && set_len(this.clones) > 0) {
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_CYAN);
		}
		parser_enqueue_output(parser, "# Variables set twice or more\n");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
		SET_FOREACH(this.clones, const char *, name) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
		}
	}

	if (clones_ret != NULL) {
		*clones_ret = mempool_move(pool, this.clones, extpool);
	}
}
