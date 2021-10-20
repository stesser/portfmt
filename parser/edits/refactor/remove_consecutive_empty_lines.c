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
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct WalkerData {
	size_t counter;
};

// Prototypes
static bool is_empty_line(const char *);
static enum ASTWalkState refactor_remove_consecutive_empty_lines_walker(struct AST *, struct WalkerData *);

bool
is_empty_line(const char *s)
{
	for (const char *p = s; *p != 0; p++) {
		if (!isspace(*p)) {
			return false;
		}
	}
	return true;
}

enum ASTWalkState
refactor_remove_consecutive_empty_lines_walker(struct AST *node, struct WalkerData *this)
{
	SCOPE_MEMPOOL(pool);

	this->counter++;

	switch (node->type) {
	case AST_COMMENT: {
		uint32_t empty = 0;
		struct Array *lines = mempool_array(pool);
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			if (is_empty_line(line)) {
				if (empty == 0 && this->counter > 2) {
					array_append(lines, line);
				}
				empty++;
			} else {
				array_append(lines, line);
				empty = 0;
			}
			this->counter++;
		}
		if (array_len(lines) < array_len(node->comment.lines)) {
			array_truncate(node->comment.lines);
			ARRAY_JOIN(node->comment.lines, lines);
			node->edited = true;
		}
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(refactor_remove_consecutive_empty_lines_walker, node, this);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_remove_consecutive_empty_lines)
{
	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return;
	}

	refactor_remove_consecutive_empty_lines_walker(root, &(struct WalkerData){
		.counter = 0,
	});
}
