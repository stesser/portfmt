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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <libias/array.h>
#include <libias/flow.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct WalkerData {
};

// Prototypes
static enum ASTWalkState %%name%%_walker(struct AST *, struct WalkerData *);

enum ASTWalkState
%%name%%_walker(struct AST *node, struct WalkerData *this)
{
	switch (node->type) {
	case AST_ROOT:
	case AST_COMMENT:
	case AST_DELETED:
	case AST_EXPR:
	case AST_FOR:
	case AST_IF:
	case AST_INCLUDE:
	case AST_TARGET:
	case AST_TARGET_COMMAND:
	case AST_VARIABLE:
		break;
	}

	AST_WALK_DEFAULT(%%name%%_walker, node, this);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(%%name%%)
{
	%%name%%_walker(root, &(struct WalkerData){
	});

	return 1;
}
