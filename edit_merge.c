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

#include <sys/param.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

struct MergeParameter {
	struct Variable *var;
	struct Array *values;
};

static struct Variable *has_variable(struct Array *, struct Variable *);
static struct Array *extract_tokens(struct Parser *, struct Array *, enum ParserError *, const void *);
static struct Array *insert_variable(struct Parser *, struct Array *, enum ParserError *, const void *);
static struct Array *merge_existent(struct Parser *, struct Array *, enum ParserError *, const void *);
static void append_values(struct Parser *, struct Array *, enum VariableModifier, const struct MergeParameter *);
static void assign_values(struct Parser *, struct Array *, const struct MergeParameter *);

static struct Variable *
has_variable(struct Array *tokens, struct Variable *var)
{
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (token_type(t) == VARIABLE_START &&
		    (variable_modifier(token_variable(t)) == MODIFIER_APPEND ||
		     variable_modifier(token_variable(t)) == MODIFIER_ASSIGN) &&
		    variable_cmp(token_variable(t), var) == 0) {
			return token_variable(t);
		}
	}
	return NULL;
}

struct Array *
extract_tokens(struct Parser *subparser, struct Array *subtokens, enum ParserError *error, const void *userdata)
{
	struct Array **tokens = (struct Array **)userdata;

	*tokens = subtokens;
	return NULL;
}

void
append_values(struct Parser *parser, struct Array *tokens, enum VariableModifier mod, const struct MergeParameter *params)
{
	for (size_t j = 0; j < array_len(params->values); j++) {
		struct Token *v = array_get(params->values, j);
		switch (token_type(v)) {
		case VARIABLE_TOKEN:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				variable_set_modifier(token_variable(edited), mod);
				array_append(tokens, edited);
				parser_mark_edited(parser, edited);
			}
			break;
		default:
			break;
		}
	}
}

void
assign_values(struct Parser *parser, struct Array *tokens, const struct MergeParameter *params)
{
	for (size_t j = 0; j < array_len(params->values); j++) {
		struct Token *v = array_get(params->values, j);
		switch (token_type(v)) {
		case VARIABLE_START:
		case VARIABLE_TOKEN:
		case VARIABLE_END:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				array_append(tokens, edited);
				parser_mark_edited(parser, edited);
			}
			break;
		default:
			break;
		}
	}
}

struct Array *
insert_variable(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	struct Variable *var = (struct Variable *)userdata;

	ssize_t insert_after = -1;

	if (insert_after == -1) {
		struct Range lines = { 0, 1 };
		struct Token *t = token_new2(VARIABLE_START, &lines, NULL, var, NULL, NULL);
		array_append(ptokens, t);
		parser_mark_edited(parser, t);
		t = token_new2(VARIABLE_END, &lines, NULL, var, NULL, NULL);
		array_append(ptokens, t);
		parser_mark_edited(parser, t);
		return NULL;
	}

	*error = PARSER_ERROR_EDIT_FAILED;
	return NULL;
}

struct Array *
merge_existent(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	const struct MergeParameter *params = userdata;
	struct Array *tokens = array_new(sizeof(struct Token *));

	int found = 0;
	enum VariableModifier mod = variable_modifier(params->var);
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			if (variable_cmp(params->var, token_variable(t)) == 0) {
				found = 1;
				if (mod == MODIFIER_ASSIGN) {
					assign_values(parser, tokens, params);
				} else if (mod == MODIFIER_APPEND) {
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_TOKEN:
			if (found) {
				if (mod == MODIFIER_ASSIGN) {
					// nada
				} else if (mod == MODIFIER_APPEND) {
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_END:
			if (found) {
				found = 0;
				if (mod == MODIFIER_APPEND) {
					append_values(parser, tokens, variable_modifier(token_variable(t)), params);
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		default:
			array_append(tokens, t);
			break;
		}
	}

	return tokens;
}

struct Array *
edit_merge(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	struct Parser *subparser = (struct Parser*)userdata;

	struct Array *subtokens = NULL;
	parser_edit(subparser, extract_tokens, &subtokens);

	struct Variable *var = NULL;
	int merge = 0;
	for (size_t i = 0; i < array_len(subtokens); i++) {
		struct Token *t = array_get(subtokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			var = token_variable(t);
			if (!has_variable(ptokens, var)) {
				*error = parser_edit(parser, insert_variable, var);
				if (*error != PARSER_ERROR_OK) {
					goto cleanup;
				}
			}
			switch (variable_modifier(var)) {
			case MODIFIER_APPEND:
			case MODIFIER_ASSIGN:
				merge = 1;
				break;
			default:
				merge = 0;
				break;
			}
			break;
		case VARIABLE_TOKEN:
			if (merge) {
				struct MergeParameter params;
				params.var = var;
				params.values = subtokens;
				*error = parser_edit(parser, merge_existent, &params);
				if (*error != PARSER_ERROR_OK) {
					goto cleanup;
				}
			}
			break;
		case VARIABLE_END:
			var = NULL;
			merge = 0;
			break;
		default:
			break;
		}
	}

cleanup:
	return NULL;
}
