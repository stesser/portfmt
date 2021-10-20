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
#include <stdbool.h>
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
#include "rules.h"

enum InsertVariableState {
	INSERT_VARIABLE_NO_POINT_FOUND = -1,
	INSERT_VARIABLE_PREPEND = -2,
};

struct WalkerData {
	struct Parser *parser;
	struct AST *root;
	enum ParserMergeBehavior merge_behavior;
};

struct VariableMergeParameter {
	enum ParserMergeBehavior behavior;
	struct Variable *var;
	struct Array *nonvars;
	struct Array *values;
};

// Prototypes
static struct AST *empty_line(struct AST *);
static bool insert_empty_line_before_block(enum BlockType, enum BlockType);
static void prepend_variable(struct Parser *, struct AST *, struct AST *, enum BlockType);
static enum ASTWalkState delete_variable(struct AST *, struct Parser *, const char *);
static ssize_t find_insert_point_generic(struct Parser *, struct AST *, const char *, enum BlockType *);
static ssize_t find_insert_point_same_block(struct Parser *, struct AST *, const char *, enum BlockType *);
static void insert_variable(struct Parser *, struct AST *, struct AST *);
static enum ASTWalkState find_variable_helper(struct AST *, const char *, uint32_t, struct AST **);
static struct AST *find_variable(struct AST *, const char *, uint32_t);
static enum ASTWalkState edit_merge_walker(struct AST *, struct WalkerData *, uint32_t);

struct AST *
empty_line(struct AST *parent)
{
	struct AST *node = ast_new(parent->pool, AST_COMMENT, &parent->line_start, &(struct ASTComment){
		.type = AST_COMMENT_LINE,
	});
	array_append(node->comment.lines, str_dup(node->pool, ""));
	node->edited = true;
	return node;
}

bool
insert_empty_line_before_block(enum BlockType before, enum BlockType block)
{
	return before < block && (before < BLOCK_USES || block > BLOCK_PLIST);
}

void
prepend_variable(struct Parser *parser, struct AST *root, struct AST *node, enum BlockType block_var)
{
	SCOPE_MEMPOOL(pool);

	// Append only after initial comments
	size_t start_index = 0;
	struct Array *siblings = ast_siblings(pool, root);
	ARRAY_FOREACH(siblings, struct AST *, sibling) {
		if (sibling->type == AST_COMMENT) {
			start_index = sibling_index + 1;
		} else {
			break;
		}
	}

	bool added = false;
	ARRAY_FOREACH_SLICE(siblings, start_index, -1, struct AST *, sibling) {
		if (sibling_index == start_index) {
			ast_parent_insert_before_sibling(sibling, node);
			added = true;
		}
		// Insert new empty line
		switch (sibling->type) {
		case AST_COMMENT:
		case AST_EXPR:
		case AST_FOR:
		case AST_IF:
		case AST_INCLUDE:
		case AST_TARGET:
			if (added) {
				ast_parent_insert_before_sibling(sibling, empty_line(sibling));
				return;
			}
			break;
		case AST_VARIABLE: {
			enum BlockType block = variable_order_block(parser, sibling->variable.name, NULL, NULL);
			if (block != block_var && insert_empty_line_before_block(block, block_var)) {
				ast_parent_insert_before_sibling(sibling, empty_line(sibling));
				return;
			}
			break;
		} case AST_ROOT:
		case AST_DELETED:
		case AST_TARGET_COMMAND:
			break;
		}
	}

	unless (added) {
		// If all else fails just append it
		ast_parent_append_sibling(root, node, false);
	}
}

enum ASTWalkState
delete_variable(struct AST *node, struct Parser *parser, const char *var)
{
	switch (node->type) {
	case AST_INCLUDE:
		if (is_include_bsd_port_mk(node)) {
			return AST_WALK_STOP;
		}
		break;
	case AST_VARIABLE:
		if (strcmp(var, node->variable.name) == 0) {
			node->type = AST_DELETED;
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(delete_variable, node, parser, var);
	return AST_WALK_CONTINUE;
}

ssize_t
find_insert_point_generic(struct Parser *parser, struct AST *root, const char *var, enum BlockType *block_before_var)
{
	SCOPE_MEMPOOL(pool);

	ssize_t insert_after = INSERT_VARIABLE_NO_POINT_FOUND;
	*block_before_var = BLOCK_UNKNOWN;
	bool always_greater = true;

	ARRAY_FOREACH(ast_siblings(pool, root), struct AST *, sibling) {
		switch (sibling->type) {
		case AST_ROOT:
		case AST_DELETED:
		case AST_IF:
		case AST_FOR:
		case AST_COMMENT:
		case AST_EXPR:
		case AST_TARGET_COMMAND:
		case AST_TARGET:
			break;
		case AST_VARIABLE:
			if (compare_order(&sibling->variable.name, &var, parser) < 0) {
				*block_before_var = variable_order_block(parser, sibling->variable.name, NULL, NULL);
				insert_after = sibling_index;
				always_greater = false;
			}
			break;
		case AST_INCLUDE:
			if (insert_after >=0 && is_include_bsd_port_mk(sibling)) {
				goto loop_end;
			}
			break;
		}
	}

loop_end:
	if (always_greater) {
		insert_after = INSERT_VARIABLE_PREPEND;
	}

	return insert_after;
}

ssize_t
find_insert_point_same_block(struct Parser *parser, struct AST *root, const char *var, enum BlockType *block_before_var)
{
	SCOPE_MEMPOOL(pool);

	ssize_t insert_after = INSERT_VARIABLE_NO_POINT_FOUND;
	enum BlockType block_var = variable_order_block(parser, var, NULL, NULL);
	*block_before_var = BLOCK_UNKNOWN;

	ARRAY_FOREACH(ast_siblings(pool, root), struct AST *, sibling) {
		switch (sibling->type) {
		case AST_ROOT:
		case AST_DELETED:
		case AST_IF:
		case AST_FOR:
		case AST_COMMENT:
		case AST_EXPR:
		case AST_TARGET_COMMAND:
		case AST_TARGET:
			break;
		case AST_VARIABLE: {
			enum BlockType block = variable_order_block(parser, sibling->variable.name, NULL, NULL);
			if (block != block_var) {
				continue;
			}
			int cmp = compare_order(&sibling->variable.name, &var, parser);
			if (cmp < 0) {
				*block_before_var = block;
				insert_after = sibling_index;
			} else if (cmp == 0) {
				insert_after = sibling_index;
			}
			break;
		} case AST_INCLUDE:
			if (is_include_bsd_port_mk(sibling)) {
				return insert_after;
			}
			break;
		}
	}

	return insert_after;
}

void
insert_variable(struct Parser *parser, struct AST *root, struct AST *template)
{
	SCOPE_MEMPOOL(pool);

	struct AST *node = ast_clone(root->pool, template);
	node->edited = true;

	enum BlockType block_var = variable_order_block(parser, node->variable.name, NULL, NULL);
	enum BlockType block_before_var = BLOCK_UNKNOWN;
	ssize_t insert_after = find_insert_point_same_block(parser, root, node->variable.name, &block_before_var);
	if (insert_after < 0) {
		insert_after = find_insert_point_generic(parser, root, node->variable.name, &block_before_var);
	}

	switch (insert_after) {
	case INSERT_VARIABLE_PREPEND:
		prepend_variable(parser, root, node, block_var);
		return;
	case INSERT_VARIABLE_NO_POINT_FOUND:
		// No variable found where we could insert our new
		// var.  Insert it before any conditional or target
		// if there are any.
		ARRAY_FOREACH(ast_siblings(pool, root), struct AST *, sibling) {
			switch (sibling->type) {
			case AST_ROOT:
			case AST_DELETED:
			case AST_IF:
			case AST_FOR:
			case AST_COMMENT:
			case AST_EXPR:
			case AST_TARGET_COMMAND:
			case AST_VARIABLE:
				break;
			case AST_INCLUDE:
			case AST_TARGET:
				ast_parent_insert_before_sibling(sibling, node);
				ast_parent_insert_before_sibling(sibling, empty_line(sibling));
				return;
			}
		}
		// Prepend it instead if there are no conditionals or targets
		prepend_variable(parser, root, node, block_var);
		return;
	default:
		panic_if(insert_after < 0, "negative insertion point");
	}

	size_t insert_point = insert_after;
	ARRAY_FOREACH(ast_siblings(pool, root), struct AST *, sibling) {
		if (sibling_index > insert_point) {
			ast_parent_insert_before_sibling(sibling, node);
			if (block_before_var != BLOCK_UNKNOWN && block_before_var != block_var &&
			    insert_empty_line_before_block(block_before_var, block_var)) {
				ast_parent_insert_before_sibling(node, empty_line(node));
			}
			return;
		}
	}

	// If all else fails just append it
	if (block_before_var != BLOCK_UNKNOWN && block_before_var != block_var &&
	    insert_empty_line_before_block(block_before_var, block_var)) {
		ast_parent_append_sibling(root, empty_line(node), false);
	}
	ast_parent_append_sibling(root, node, false);
}

enum ASTWalkState
find_variable_helper(struct AST *node, const char *var, uint32_t level, struct AST **retval)
{
	if (level > 1) {
		return AST_WALK_STOP;
	}

	switch (node->type) {
	case AST_ROOT:
	case AST_FOR:
	case AST_IF:
	case AST_INCLUDE:
	case AST_TARGET:
		level++;
		break;
	case AST_VARIABLE:
		if (strcmp(node->variable.name, var) == 0) {
			*retval = node;
			return AST_WALK_STOP;
		}
		break;
	default:
		break;
	}
	AST_WALK_DEFAULT(find_variable_helper, node, var, level, retval);
	return AST_WALK_CONTINUE;
}

struct AST *
find_variable(struct AST *root, const char *var, uint32_t level)
{
	struct AST *node = NULL;
	find_variable_helper(root, var, level, &node);
	return node;
}

enum ASTWalkState
edit_merge_walker(struct AST *node, struct WalkerData *this, uint32_t level)
{
	if (level > 1) {
		return AST_WALK_STOP;
	}

	switch (node->type) {
	case AST_ROOT:
	case AST_FOR:
	case AST_IF:
	case AST_INCLUDE:
	case AST_TARGET:
		level++;
		break;
	case AST_VARIABLE: {
		struct AST *mergenode = NULL;
		if (node->variable.modifier == AST_VARIABLE_MODIFIER_SHELL &&
		    (this->merge_behavior & PARSER_MERGE_SHELL_IS_DELETE)) {
			delete_variable(this->root, this->parser, node->variable.name);
			return AST_WALK_CONTINUE;
		} else if ((mergenode = find_variable(this->root, node->variable.name, 0))) {
			switch (node->variable.modifier) {
			case AST_VARIABLE_MODIFIER_ASSIGN:
				array_truncate(mergenode->variable.words);
				ARRAY_FOREACH(node->variable.words, const char *, word) {
					array_append(mergenode->variable.words, str_dup(mergenode->pool, word));
				}
				mergenode->edited = true;
				return AST_WALK_CONTINUE;
			case AST_VARIABLE_MODIFIER_APPEND:
				if ((!mergenode->variable.comment || strlen(mergenode->variable.comment) == 0)) {
					ARRAY_FOREACH(node->variable.words, const char *, word) {
						array_append(mergenode->variable.words, str_dup(mergenode->pool, word));
					}
					mergenode->edited = true;
					return AST_WALK_CONTINUE;
				}
				break;
			case AST_VARIABLE_MODIFIER_EXPAND: // TODO
				array_truncate(mergenode->variable.words);
				ARRAY_FOREACH(node->variable.words, const char *, word) {
					array_append(mergenode->variable.words, str_dup(mergenode->pool, word));
				}
				mergenode->edited = true;
				mergenode->variable.modifier = AST_VARIABLE_MODIFIER_EXPAND;
				return AST_WALK_CONTINUE;
			case AST_VARIABLE_MODIFIER_OPTIONAL:
				array_truncate(mergenode->variable.words);
				ARRAY_FOREACH(node->variable.words, const char *, word) {
					array_append(mergenode->variable.words, str_dup(mergenode->pool, word));
				}
				mergenode->edited = true;
				mergenode->variable.modifier = AST_VARIABLE_MODIFIER_OPTIONAL;
				return AST_WALK_CONTINUE;
			case AST_VARIABLE_MODIFIER_SHELL:
				array_truncate(mergenode->variable.words);
				ARRAY_FOREACH(node->variable.words, const char *, word) {
					array_append(mergenode->variable.words, str_dup(mergenode->pool, word));
				}
				mergenode->edited = true;
				mergenode->variable.modifier = AST_VARIABLE_MODIFIER_SHELL;
				return AST_WALK_CONTINUE;
			}
		}
		insert_variable(this->parser, this->root, node);
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(edit_merge_walker, node, this, level);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(edit_merge)
{
	SCOPE_MEMPOOL(pool);

	const struct ParserEdit *params = userdata;
	if (params == NULL ||
	    params->arg1 != NULL ||
	    params->subparser == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return;
	}

	struct AST *mergetree = parser_ast(params->subparser);
	unless (mergetree) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, parser_error_tostring(params->subparser, pool));
		return;
	}

	// fputs("to merge:\n", stderr);
	// ast_print(mergetree, stderr);
	// fputs("\nbefore:\n", stderr);
	// ast_print(root, stderr);

	edit_merge_walker(mergetree, &(struct WalkerData){
		.parser = parser,
		.root = root,
		.merge_behavior = params->merge_behavior,
	}, 0);

	// fputs("\nafter:\n", stderr);
	// ast_print(root, stderr);
}
