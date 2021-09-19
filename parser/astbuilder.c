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

#include <stdio.h>
#include <stdlib.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/astbuilder.h"
#include "parser/astbuilder/token.h"

struct ParserASTBuilder *
parser_astbuilder_new()
{
	struct Mempool *pool = mempool_new();
	struct ParserASTBuilder *builder = mempool_alloc(pool, sizeof(struct ParserASTBuilder));
	builder->tokens = mempool_array(pool);
	builder->lines.a = 1;
	builder->lines.b = 1;
	return builder;
}

void
parser_astbuilder_free(struct ParserASTBuilder *builder)
{
	if (builder) {
		mempool_free(builder->pool);
	}
}

void
parser_astbuilder_append_token(struct ParserASTBuilder *builder, enum TokenType type, const char *data)
{
	panic_unless(builder->tokens, "AST was already built");
	struct Token *t = token_new(type, &builder->lines, data, builder->varname, builder->condname, builder->targetname);
	if (t == NULL) {
		parser_set_error(builder->parser, PARSER_ERROR_EXPECTED_TOKEN, token_type_tostring(type));
		return;
	}
	mempool_add(builder->pool, t, token_free);
	array_append(builder->tokens, t);
}

struct ASTNode *
parser_astbuilder_finish(struct ParserASTBuilder *builder)
{
	panic_unless(builder->tokens, "AST was already built");
	struct ASTNode *root = ast_from_token_stream(builder->tokens);
	mempool_release_all(builder->pool);
	builder->tokens = NULL;
	return root;
}
