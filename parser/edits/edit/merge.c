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
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>

#include "ast.h"
#include "parser.h"
#include "parser/astbuilder.h"
#include "parser/astbuilder/conditional.h"
#include "parser/astbuilder/token.h"
#include "parser/astbuilder/variable.h"
#include "parser/edits.h"
#include "rules.h"

enum InsertVariableState {
	INSERT_VARIABLE_NO_POINT_FOUND = -1,
	INSERT_VARIABLE_PREPEND = -2,
};

struct VariableMergeParameter {
	enum ParserMergeBehavior behavior;
	struct Variable *var;
	struct Array *nonvars;
	struct Array *values;
};

static void append_empty_line(struct Parser *, struct Array *, struct ASTNodeLineRange *);
static void append_new_variable(struct Parser *, struct Array *, struct Variable *, struct ASTNodeLineRange *);
static struct Token *find_next_token(struct Array *, size_t, int, int, int);
static PARSER_EDIT(extract_tokens);
static PARSER_EDIT(insert_variable);
static PARSER_EDIT(merge_existent_var);
static void append_tokens(struct Parser *, struct Array *, struct Array *);
static void append_values(struct Parser *, struct Array *, enum ASTNodeVariableModifier, struct VariableMergeParameter *);
static void append_values_last(struct Parser *, struct Array *, enum ASTNodeVariableModifier, struct VariableMergeParameter *);
static void assign_values(struct Parser *, struct Array *, enum ASTNodeVariableModifier, const struct VariableMergeParameter *);

static int
is_include_bsd_port_mk_token(struct Token *t)
{
	struct Conditional *c = token_conditional(t);
	return c && token_type(t) == CONDITIONAL_TOKEN &&
		conditional_type(c) == COND_INCLUDE &&
		(strcmp(token_data(t), "<bsd.port.options.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.pre.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.post.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.mk>") == 0);
}

static int
skip_conditional(struct Token *t, int *ignore)
{
	if (*ignore > 0) {
		if (token_type(t) == CONDITIONAL_END) {
			switch (conditional_type(token_conditional(t))) {
			case COND_ENDFOR:
			case COND_ENDIF:
				(*ignore)--;
				break;
			default:
				break;
			}
		}
		return 1;
	}

	if (token_type(t) == CONDITIONAL_START) {
		switch (conditional_type(token_conditional(t))) {
		case COND_IF:
		case COND_IFDEF:
		case COND_IFMAKE:
		case COND_IFNDEF:
		case COND_IFNMAKE:
		case COND_FOR:
			(*ignore)++;
			break;
		default:
			break;
		}
	}

	return 0;
}

PARSER_EDIT(extract_tokens)
{
	struct Array **tokens = userdata;
	struct ParserASTBuilder *builder = mempool_add(extpool, parser_astbuilder_from_ast(parser, root), parser_astbuilder_free);
	*tokens = builder->tokens;
	return 0;
}

void
append_values(struct Parser *parser, struct Array *tokens, enum ASTNodeVariableModifier mod, struct VariableMergeParameter *params)
{
	ARRAY_FOREACH(params->values, struct Token *, v) {
		switch (token_type(v)) {
		case VARIABLE_TOKEN:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				variable_set_modifier(token_variable(edited), mod);
				array_append(tokens, edited);
				token_mark_edited(edited);
			}
			break;
		default:
			break;
		}
	}
}

void
append_values_last(struct Parser *parser, struct Array *tokens, enum ASTNodeVariableModifier mod, struct VariableMergeParameter *params)
{
	struct Token *last_token = array_get(tokens, array_len(tokens) - 1);
	if (last_token) {
		struct ASTNodeLineRange *lines = token_lines(last_token);
		struct Token *t;
		if (token_type(last_token) == VARIABLE_END) {
			params->var = variable_clone(params->var);
			variable_set_modifier(params->var, AST_NODE_VARIABLE_MODIFIER_APPEND);

			t = token_new_variable_start(lines, params->var);
			array_append(tokens, t);
			token_mark_edited(t);

			append_values(parser, tokens, AST_NODE_VARIABLE_MODIFIER_APPEND, params);

			t = token_new_variable_end(lines, params->var);
			array_append(tokens, t);
			token_mark_edited(t);
		} else if (is_comment(token_data(last_token))) {
			t = token_new_variable_end(lines, params->var);
			array_append(tokens, t);
			token_mark_edited(t);

			params->var = variable_clone(params->var);
			variable_set_modifier(params->var, AST_NODE_VARIABLE_MODIFIER_APPEND);
			t = token_new_variable_start(lines, params->var);
			array_append(tokens, t);
			token_mark_edited(t);

			append_values(parser, tokens, AST_NODE_VARIABLE_MODIFIER_APPEND, params);
		} else {
			append_values(parser, tokens, mod, params);
		}
	} else {
		append_values(parser, tokens, mod, params);
	}
}

void
assign_values(struct Parser *parser, struct Array *tokens, enum ASTNodeVariableModifier mod, const struct VariableMergeParameter *params)
{
	ARRAY_FOREACH(params->values, struct Token *, v) {
		switch (token_type(v)) {
		case VARIABLE_START:
		case VARIABLE_TOKEN:
		case VARIABLE_END:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				variable_set_modifier(token_variable(edited), mod);
				array_append(tokens, edited);
				token_mark_edited(edited);
			}
			break;
		default:
			break;
		}
	}
}

void
append_tokens(struct Parser *parser, struct Array *tokens, struct Array *nonvars)
{
	ARRAY_FOREACH(nonvars, struct Token *, t) {
		struct Token *c = token_clone(t, NULL);
		array_append(tokens, c);
		token_mark_edited(c);
	}
	array_truncate(nonvars);
}

void
append_empty_line(struct Parser *parser, struct Array *tokens, struct ASTNodeLineRange *lines)
{
	struct Token *t = token_new_comment(lines, "", NULL);
	array_append(tokens, t);
	token_mark_edited(t);
}

void
append_new_variable(struct Parser *parser, struct Array *tokens, struct Variable *var, struct ASTNodeLineRange *lines) 
{
	struct Token *t = token_new_variable_start(lines, var);
	array_append(tokens, t);
	token_mark_edited(t);
	t = token_new_variable_end(lines, var);
	array_append(tokens, t);
	token_mark_edited(t);
}

struct Token *
find_next_token(struct Array *tokens, size_t start, int a1, int a2, int a3)
{
	ARRAY_FOREACH_SLICE(tokens, start, -1, struct Token *, t) {
		int type = token_type(t);
		if (type == a1 || type == a2 || type == a3) {
			return t;
		}
	}

	return NULL;
}

static ssize_t
find_insert_point_generic(struct Parser *parser, struct Array *ptokens, struct Variable *var, enum BlockType *block_before_var)
{
	ssize_t insert_after = INSERT_VARIABLE_NO_POINT_FOUND;
	*block_before_var = BLOCK_UNKNOWN;
	int always_greater = 1;
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if (insert_after >= 0 && is_include_bsd_port_mk_token(t)) {
			break;
		} else if (token_type(t) != VARIABLE_END) {;
			continue;
		}

		char *a = variable_name(token_variable(t));
		enum BlockType block = variable_order_block(parser, a, NULL, NULL);
		char *b = variable_name(var);
		int cmp = compare_order(&a, &b, parser);
		if (cmp < 0) {
			*block_before_var = block;
			insert_after = t_index;
			always_greater = 0;
		}
	}

	if (always_greater) {
		insert_after = INSERT_VARIABLE_PREPEND;
	}

	return insert_after;
}

static ssize_t
find_insert_point_same_block(struct Parser *parser, struct Array *ptokens, struct Variable *var, enum BlockType *block_before_var)
{
	ssize_t insert_after = INSERT_VARIABLE_NO_POINT_FOUND;
	enum BlockType block_var = variable_order_block(parser, variable_name(var), NULL, NULL);
	*block_before_var = BLOCK_UNKNOWN;
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if (is_include_bsd_port_mk_token(t)) {
			break;
		} else if (token_type(t) != VARIABLE_END) {;
			continue;
		}

		char *a = variable_name(token_variable(t));
		enum BlockType block = variable_order_block(parser, a, NULL, NULL);
		if (block != block_var) {
			continue;
		}
		char *b = variable_name(var);
		int cmp = compare_order(&a, &b, parser);
		if (cmp < 0) {
			*block_before_var = block;
			insert_after = t_index;
		}
	}

	return insert_after;
}

static int
insert_newline_before_block(enum BlockType before, enum BlockType block)
{
	return before < block && (before < BLOCK_USES || block > BLOCK_PLIST);
}

static void
prepend_variable(struct Parser *parser, struct Array *ptokens, struct Array *tokens, struct Variable *var, enum BlockType block_var)
{
	array_truncate(tokens);
	struct ASTNodeLineRange *lines = &(struct ASTNodeLineRange){ .a = 0, .b = 1 };
	if (array_len(ptokens) > 0) {
		lines = token_lines(array_get(ptokens, array_len(ptokens) - 1));
	}
	// Append only after initial comments
	size_t i = 0;
	for (; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		if (token_type(t) != COMMENT) {
			break;
		}
		array_append(tokens, t);
	}
	append_new_variable(parser, tokens, var, lines);
	int empty_line_added = 0;
	for (; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		if (!empty_line_added) {
			switch (token_type(t)) {
			case VARIABLE_START:
				if (variable_order_block(parser, variable_name(token_variable(t)), NULL, NULL) != block_var) {
					append_empty_line(parser, tokens, token_lines(t));
					empty_line_added = 1;
				}
				break;
			case CONDITIONAL_START:
			case TARGET_START:
				append_empty_line(parser, tokens, token_lines(t));
				empty_line_added = 1;
				break;
			default:
				break;
			}
		}
		array_append(tokens, t);
	}
}

PARSER_EDIT(insert_variable)
{
	struct Variable *var = userdata;
	struct ParserASTBuilder *builder = mempool_add(extpool, parser_astbuilder_from_ast(parser, root), parser_astbuilder_free);
	struct Array *ptokens = builder->tokens;
	enum BlockType block_var = variable_order_block(parser, variable_name(var), NULL, NULL);
	enum BlockType block_before_var = BLOCK_UNKNOWN;
	ssize_t insert_after = find_insert_point_same_block(parser, ptokens, var, &block_before_var);
	if (insert_after < 0) {
		insert_after = find_insert_point_generic(parser, ptokens, var, &block_before_var);
	}

	struct Array *tokens = array_new();
	int added = 0;
	switch (insert_after) {
	case INSERT_VARIABLE_PREPEND:
		prepend_variable(parser, ptokens, tokens, var, block_var);
		*new_tokens = tokens;
		return 0;
	case INSERT_VARIABLE_NO_POINT_FOUND:
		// No variable found where we could insert our new
		// var.  Insert it before any conditional or target
		// if there are any.
		ARRAY_FOREACH(ptokens, struct Token *, t) {
			if (!added && (token_type(t) == CONDITIONAL_START ||
				       token_type(t) == TARGET_START)) {
				append_new_variable(parser, tokens, var, token_lines(t));
				append_empty_line(parser, tokens, token_lines(t));
				added = 1;
			}
			array_append(tokens, t);
		}
		if (!added) {
			// Prepend it instead if there are no conditionals or targets
			prepend_variable(parser, ptokens, tokens, var, block_var);
		}
		*new_tokens = tokens;
		return 0;
	default:
		break;
	}

	ssize_t ptokenslen = array_len(ptokens);
	panic_unless(insert_after >= 0 && insert_after < ptokenslen, "insert location out of bounds");
	int insert_flag = 0;
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if (insert_flag) {
			insert_flag = 0;
			if (block_before_var != block_var) {
				if (insert_newline_before_block(block_before_var, block_var)) {
					append_empty_line(parser, tokens, token_lines(t));
				}
				append_new_variable(parser, tokens, var, token_lines(t));
				added = 1;
				struct Token *next = find_next_token(ptokens, t_index, CONDITIONAL_START, TARGET_START, VARIABLE_START);
				if (next && token_type(next) != VARIABLE_START) {
					append_empty_line(parser, tokens, token_lines(t));
				}
				if (token_type(t) == COMMENT && strcmp(token_data(t), "") == 0) {
					next = find_next_token(ptokens, t_index, VARIABLE_START, -1, -1);
					if (next) {
						enum BlockType block_next = variable_order_block(parser, variable_name(token_variable(next)), NULL, NULL);
						if (block_next == block_var) {
							continue;
						}
					} else {
						continue;
					}
				}
			} else {
				append_new_variable(parser, tokens, var, token_lines(t));
				added = 1;
			}
		} else if (token_type(t) == VARIABLE_END && insert_after == (ssize_t)t_index) {
			insert_flag = 1;
		}

		array_append(tokens, t);
	}

	if (!added) {
		struct ASTNodeLineRange *lines = &(struct ASTNodeLineRange){ .a = 0, .b = 1 };
		if (array_len(ptokens) > 0) {
			lines = token_lines(array_get(ptokens, array_len(ptokens) - 1));
		}
		if (insert_newline_before_block(block_before_var, block_var)) {
			append_empty_line(parser, tokens, lines);
		}
		append_new_variable(parser, tokens, var, lines);
	}

	*new_tokens = tokens;
	return 0;
}

static size_t
find_last_occurrence_of_var(struct Parser *parser, struct Array *tokens, struct VariableMergeParameter *params, size_t i)
{
	size_t index = array_len(tokens) + 1;
	int skip = 0;
	for (; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if ((params->behavior & PARSER_MERGE_IGNORE_VARIABLES_IN_CONDITIONALS) &&
		    skip_conditional(t, &skip)) {
			continue;
		}
		switch (token_type(t)) {
		case VARIABLE_END:
			if (variable_cmp(params->var, token_variable(t)) == 0) {
				index = i;
			} else {
				return index;
			}
			break;
		default:
			break;
		}
	}
	return index;
}

PARSER_EDIT(merge_existent_var)
{
	struct ParserASTBuilder *builder = mempool_add(extpool, parser_astbuilder_from_ast(parser, root), parser_astbuilder_free);
	struct Array *ptokens = builder->tokens;
	struct VariableMergeParameter *params = userdata;
	struct Array *tokens = array_new();

	int found = 0;
	enum ASTNodeVariableModifier mod = variable_modifier(params->var);
	size_t last_occ = array_len(ptokens) + 1;
	int skip = 0;
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if ((params->behavior & PARSER_MERGE_IGNORE_VARIABLES_IN_CONDITIONALS) &&
		    skip_conditional(t, &skip)) {
			array_append(tokens, t);
			continue;
		}
		switch (token_type(t)) {
		case VARIABLE_START:
			if (variable_cmp(params->var, token_variable(t)) == 0) {
				last_occ = find_last_occurrence_of_var(parser, ptokens, params, t_index);
				found = 1;
				if (mod == AST_NODE_VARIABLE_MODIFIER_ASSIGN ||
				    (mod == AST_NODE_VARIABLE_MODIFIER_OPTIONAL && (params->behavior & PARSER_MERGE_OPTIONAL_LIKE_ASSIGN))) {
					append_tokens(parser, tokens, params->nonvars);
					assign_values(parser, tokens, variable_modifier(token_variable(t)), params);
				} else if (mod == AST_NODE_VARIABLE_MODIFIER_APPEND) {
					append_tokens(parser, tokens, params->nonvars);
					array_append(tokens, t);
					token_mark_edited(t);
				} else if (mod == AST_NODE_VARIABLE_MODIFIER_SHELL) {
					mempool_add(extpool, t, token_free);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_TOKEN:
			if (found) {
				if (mod == AST_NODE_VARIABLE_MODIFIER_ASSIGN || mod == AST_NODE_VARIABLE_MODIFIER_OPTIONAL) {
					// nada
				} else if (mod == AST_NODE_VARIABLE_MODIFIER_SHELL) {
					mempool_add(extpool, t, token_free);
				} else if (mod == AST_NODE_VARIABLE_MODIFIER_APPEND) {
					array_append(tokens, t);
					token_mark_edited(t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_END:
			if (found) {
				found = 0;
				if (mod == AST_NODE_VARIABLE_MODIFIER_APPEND) {
					if (params->behavior & PARSER_MERGE_AFTER_LAST_IN_GROUP) {
						if (t_index == last_occ) {
							append_values_last(parser, tokens, variable_modifier(token_variable(t)), params);
							array_append(tokens, t);
							token_mark_edited(t);
							t_index = array_len(ptokens) + 1;
						} else {
							array_append(tokens, t);
						}
					} else {
						append_values(parser, tokens, variable_modifier(token_variable(t)), params);
						array_append(tokens, t);
					}
				} else if (mod == AST_NODE_VARIABLE_MODIFIER_SHELL) {
					mempool_add(extpool, t, token_free);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case COMMENT:
			token_mark_edited(t); // XXX
		default:
			array_append(tokens, t);
			break;
		}
	}

	append_tokens(parser, tokens, params->nonvars);

	*new_tokens = tokens;
	return 0;
}

PARSER_EDIT(edit_merge)
{
	SCOPE_MEMPOOL(pool);
	struct ParserASTBuilder *builder = mempool_add(extpool, parser_astbuilder_from_ast(parser, root), parser_astbuilder_free);
	struct Array *ptokens = builder->tokens;

	const struct ParserEdit *params = userdata;
	if (params == NULL ||
	    params->arg1 != NULL ||
	    params->subparser == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	struct Array *subtokens = NULL;
	if (parser_edit(params->subparser, pool, extract_tokens, &subtokens) != PARSER_ERROR_OK) {
		return 0;
	}

	struct Variable *var = NULL;
	int merge = 0;
	struct Array *mergetokens = mempool_array(pool);
	struct Array *nonvars = mempool_array(pool);
	ARRAY_FOREACH(subtokens, struct Token *, t) {
		switch (token_type(t)) {
		case VARIABLE_START:
			var = token_variable(t);
			switch (variable_modifier(var)) {
			case AST_NODE_VARIABLE_MODIFIER_SHELL:
				if (!(params->merge_behavior & PARSER_MERGE_SHELL_IS_DELETE)) {
					break;
				}
				/* fallthrough */
			case AST_NODE_VARIABLE_MODIFIER_OPTIONAL:
				if (variable_modifier(var) == AST_NODE_VARIABLE_MODIFIER_OPTIONAL &&
				    !(params->merge_behavior & PARSER_MERGE_OPTIONAL_LIKE_ASSIGN)) {
					break;
				}
				/* fallthrough */
			case AST_NODE_VARIABLE_MODIFIER_APPEND:
			case AST_NODE_VARIABLE_MODIFIER_ASSIGN: {
				enum ParserLookupVariableBehavior behavior = PARSER_LOOKUP_DEFAULT;
				if (params->merge_behavior & PARSER_MERGE_IGNORE_VARIABLES_IN_CONDITIONALS) {
					behavior |= PARSER_LOOKUP_IGNORE_VARIABLES_IN_CONDITIIONALS;
				}
				if (!parser_lookup_variable(parser, variable_name(var), behavior, pool, NULL, NULL)) {
					enum ParserError error = parser_edit(parser, extpool, insert_variable, var);
					if (error != PARSER_ERROR_OK) {
						return 0;
					}
					parser_edit(parser, pool, extract_tokens, &ptokens);
				}
				merge = 1;
				array_append(mergetokens, t);
				break;
			} default:
				merge = 0;
				break;
			}
			break;
		case VARIABLE_TOKEN:
			if (merge) {
				array_append(mergetokens, t);
			}
			break;
		case VARIABLE_END:
			if (merge) {
				array_append(mergetokens, t);
				struct VariableMergeParameter par;
				par.behavior = params->merge_behavior;
				par.var = var;
				par.nonvars = nonvars;
				par.values = mergetokens;
				if (parser_edit(parser, extpool, merge_existent_var, &par) != PARSER_ERROR_OK) {
					return 0;
				}
				parser_edit(parser, pool, extract_tokens, &ptokens);
				array_truncate(nonvars);
			}
			var = NULL;
			merge = 0;
			array_truncate(mergetokens);
			break;
		case COMMENT:
			if ((params->merge_behavior & PARSER_MERGE_COMMENTS) &&
			    (array_len(nonvars) > 0 || strcmp(token_data(t), "") != 0)) {
				array_append(nonvars, t);
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

