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
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
	struct Set *seen;
};

static enum ASTWalkState
refactor_sanitize_append_modifier_walker(struct AST *node, struct WalkerData *this)
{
	switch (node->type) {
	case AST_INCLUDE:
		if (is_include_bsd_port_mk(node)) {
			return AST_WALK_STOP;
		}
		break;
	case AST_VARIABLE:
		if (set_contains(this->seen, node->variable.name)) {
			if (node->variable.modifier == AST_VARIABLE_MODIFIER_APPEND) {
				node->edited = 1;
			} else {
				set_remove(this->seen, node->variable.name);
			}
		} else {
			set_add(this->seen, node->variable.name);
			if (strcmp(node->variable.name, "CXXFLAGS") != 0 &&
			    strcmp(node->variable.name, "CFLAGS") != 0 &&
			    strcmp(node->variable.name, "LDFLAGS") != 0 &&
			    strcmp(node->variable.name, "RUSTFLAGS") != 0 &&
			    node->variable.modifier == AST_VARIABLE_MODIFIER_APPEND) {
				if (node->parent->type != AST_IF && node->parent->type != AST_FOR) {
					node->variable.modifier = AST_VARIABLE_MODIFIER_ASSIGN;
				}
				node->edited = 1;
			}
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(refactor_sanitize_append_modifier_walker, node, this);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_sanitize_append_modifier)
{
	SCOPE_MEMPOOL(pool);

	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return;
	}

	/* Sanitize += before bsd.options.mk */
	refactor_sanitize_append_modifier_walker(root, &(struct WalkerData){
		.seen = mempool_set(pool, str_compare, NULL, NULL),
	});
}
