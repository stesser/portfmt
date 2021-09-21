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
#include <string.h>

#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "conditional.h"
#include "enum.h"
#include "target.h"
#include "token.h"
#include "variable.h"

struct Token {
	enum ParserASTBuilderTokenType type;
	char *data;
	enum ParserASTBuilderConditionalType cond;
	struct {
		char *name;
		enum ASTVariableModifier modifier;
	} variable;
	struct Target *target;
	int goalcol;
	int edited;
	struct ASTLineRange lines;
};

struct Token *
token_new(enum ParserASTBuilderTokenType type, struct ASTLineRange *lines, const char *data,
	  const char *varname, const char *condname, const char *targetname)
{
	SCOPE_MEMPOOL(pool);

	if (((type == PARSER_AST_BUILDER_TOKEN_VARIABLE_END || type == PARSER_AST_BUILDER_TOKEN_VARIABLE_START ||
	      type == PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN) && varname == NULL) ||
	    ((type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END || type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START ||
	      type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN) && condname  == NULL ) ||
	    ((type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END || type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START ||
	      type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN || type == PARSER_AST_BUILDER_TOKEN_TARGET_END ||
	      type == PARSER_AST_BUILDER_TOKEN_TARGET_START) && targetname == NULL)) {
		return NULL;
	}

	struct Token *t = mempool_alloc(pool, sizeof(struct Token));
	t->type = type;
	t->lines = *lines;

	struct Target *target = NULL;
	if (targetname && (target = target_new(targetname)) == NULL) {
		return NULL;
	}
	mempool_add(pool, target, target_free);

	if (condname && (t->cond = parse_conditional(condname)) == PARSER_AST_BUILDER_CONDITIONAL_INVALID) {
		return NULL;
	}

	if (varname && !parse_variable(pool, varname, &t->variable.name, &t->variable.modifier)) {
		return NULL;
	}

	if (data) {
		t->data = str_dup(NULL, data);
	}
	t->target = target;

	mempool_forget(pool, t);
	mempool_forget(pool, target);
	mempool_forget(pool, t->variable.name);
	return t;
}

struct Token *
token_new_comment(struct ASTLineRange *lines, const char *data, enum ParserASTBuilderConditionalType cond)
{
	if (lines == NULL || data == NULL) {
		return NULL;
	}

	struct Token *t = xmalloc(sizeof(struct Token));
	t->type = PARSER_AST_BUILDER_TOKEN_COMMENT;
	t->lines = *lines;
	t->cond = cond;
	t->data = str_dup(NULL, data);
	return t;
}

void
token_free(struct Token *token)
{
	if (token == NULL) {
		return;
	}
	free(token->data);
	free(token->variable.name);
	target_free(token->target);
	free(token);
}

struct Token *
token_as_comment(struct Token *token)
{
	return token_new_comment(token_lines(token), token_data(token), token_conditional(token));
}

enum ParserASTBuilderConditionalType
token_conditional(struct Token *token)
{
	return token->cond;
}

char *
token_data(struct Token *token)
{
	return token->data;
}

int
token_edited(struct Token *token)
{
	return token->edited;
}

void
token_mark_edited(struct Token *token)
{
	token->edited = 1;
}

struct ASTLineRange *
token_lines(struct Token *token)
{
	return &token->lines;
}

struct Target *
token_target(struct Token *token)
{
	return token->target;
}

enum ParserASTBuilderTokenType
token_type(struct Token *token)
{
	return token->type;
}

const char *
token_variable(struct Token *token)
{
	return token->variable.name;
}

enum ASTVariableModifier
token_variable_modifier(struct Token *token)
{
	return token->variable.modifier;
}
