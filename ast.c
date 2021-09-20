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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/map.h>
#include <libias/mempool.h>
#include <libias/stack.h>
#include <libias/str.h>

#include "ast.h"

const char *ASTNodeExprFlatType_identifier[] = {
	[AST_NODE_EXPR_ERROR] = "error",
	[AST_NODE_EXPR_EXPORT_ENV] = "export-env",
	[AST_NODE_EXPR_EXPORT_LITERAL] = "export-literal",
	[AST_NODE_EXPR_EXPORT] = "export",
	[AST_NODE_EXPR_INFO] = "info",
	[AST_NODE_EXPR_UNDEF] = "undef",
	[AST_NODE_EXPR_UNEXPORT_ENV] = "unexport-env",
	[AST_NODE_EXPR_UNEXPORT] = "unexport",
	[AST_NODE_EXPR_WARNING] = "warning",
};

static const char *NodeExprIfType_tostring[] = {
	[AST_NODE_EXPR_IF_IF] = "AST_NODE_EXPR_IF_IF",
	[AST_NODE_EXPR_IF_DEF] = "AST_NODE_EXPR_IF_DEF",
	[AST_NODE_EXPR_IF_ELSE] = "AST_NODE_EXPR_IF_ELSE",
	[AST_NODE_EXPR_IF_MAKE] = "AST_NODE_EXPR_IF_MAKE",
	[AST_NODE_EXPR_IF_NDEF] = "AST_NODE_EXPR_IF_NDEF",
	[AST_NODE_EXPR_IF_NMAKE] = "AST_NODE_EXPR_IF_NMAKE",
};

const char *NodeExprIfType_humanize[] = {
	[AST_NODE_EXPR_IF_IF] = "if",
	[AST_NODE_EXPR_IF_ELSE] = "else",
	[AST_NODE_EXPR_IF_DEF] = "ifdef",
	[AST_NODE_EXPR_IF_MAKE] = "ifmake",
	[AST_NODE_EXPR_IF_NDEF] = "ifndef",
	[AST_NODE_EXPR_IF_NMAKE] = "ifnmake",
};

static const char *ASTNodeIncludeType_tostring[] = {
	[AST_NODE_INCLUDE_BMAKE] = "AST_NODE_INCLUDE_BMAKE",
	[AST_NODE_INCLUDE_D] = "AST_NODE_INCLUDE_D",
	[AST_NODE_INCLUDE_POSIX] = "AST_NODE_INCLUDE_POSIX",
	[AST_NODE_INCLUDE_S] = "AST_NODE_INCLUDE_S",
};

const char *ASTNodeIncludeType_identifier[] = {
	[AST_NODE_INCLUDE_BMAKE] = "include",
	[AST_NODE_INCLUDE_D] = "dinclude",
	[AST_NODE_INCLUDE_POSIX] = "include",
	[AST_NODE_INCLUDE_S] = "sinclude",
};

static const char *ASTNodeTargetType_tostring[] = {
	[AST_NODE_TARGET_NAMED] = "AST_NODE_TARGET_NAMED",
	[AST_NODE_TARGET_UNASSOCIATED] = "AST_NODE_TARGET_UNASSOCIATED",
};

const char *ASTNodeVariableModifier_humanize[] = {
	[AST_NODE_VARIABLE_MODIFIER_APPEND] = "+=",
	[AST_NODE_VARIABLE_MODIFIER_ASSIGN] = "=",
	[AST_NODE_VARIABLE_MODIFIER_EXPAND] = ":=",
	[AST_NODE_VARIABLE_MODIFIER_OPTIONAL] = "?=",
	[AST_NODE_VARIABLE_MODIFIER_SHELL] = "!=",
};

static enum ASTWalkState ast_node_print_helper(struct ASTNode *, FILE *, size_t);

struct ASTNode *
ast_node_new(struct Mempool *pool, enum ASTNodeType type, struct ASTNodeLineRange *lines, void *value)
{
	struct ASTNode *node = mempool_alloc(pool, sizeof(struct ASTNode));
	node->pool = pool;
	if (lines) {
		node->line_start = *lines;
		node->line_end = *lines;
	}
	node->type = type;

	switch (type) {
	case AST_NODE_ROOT:
		node->parent = node;
		node->root.body = mempool_array(pool);
		break;
	case AST_NODE_COMMENT: {
		struct ASTNodeComment *comment = value;
		node->comment.type = comment->type;
		node->comment.lines = mempool_array(pool);
		if (comment->lines) {
			ARRAY_FOREACH(comment->lines, const char *, line) {
				array_append(node->comment.lines, str_dup(pool, line));
			}
		}
		break;
	} case AST_NODE_EXPR_FLAT: {
		struct ASTNodeExprFlat *flatexpr = value;
		node->flatexpr.type = flatexpr->type;
		node->flatexpr.words = mempool_array(pool);
		node->flatexpr.indent = flatexpr->indent;
		if (flatexpr->words) {
			ARRAY_FOREACH(flatexpr->words, const char *, word) {
				array_append(node->flatexpr.words, str_dup(pool, word));
			}
		}
		break;
	} case AST_NODE_EXPR_FOR: {
		struct ASTNodeExprFor *forexpr = value;
		node->forexpr.bindings = mempool_array(pool);
		node->forexpr.words = mempool_array(pool);
		node->forexpr.body = mempool_array(pool);
		node->forexpr.indent = forexpr->indent;
		if (forexpr->bindings) {
			ARRAY_FOREACH(forexpr->bindings, const char *, t) {
				array_append(node->forexpr.bindings, str_dup(pool, t));
			}
		}
		if (forexpr->words) {
			ARRAY_FOREACH(forexpr->words, const char *, t) {
				array_append(node->forexpr.words, str_dup(pool, t));
			}
		}
		break;
	} case AST_NODE_EXPR_IF: {
		struct ASTNodeExprIf *ifexpr = value;
		node->ifexpr.type = ifexpr->type;
		node->ifexpr.test = mempool_array(pool);
		node->ifexpr.body = mempool_array(pool);
		node->ifexpr.orelse = mempool_array(pool);
		node->ifexpr.indent = ifexpr->indent;
		node->ifexpr.ifparent = ifexpr->ifparent;
		if (ifexpr->test) {
			ARRAY_FOREACH(ifexpr->test, const char *, word) {
				array_append(node->ifexpr.test, str_dup(pool, word));
			}
		}
		break;
	} case AST_NODE_INCLUDE: {
		struct ASTNodeInclude *include = value;
		node->include.type = include->type;
		node->include.body = mempool_array(pool);
		node->include.sys = include->sys;
		node->include.loaded = include->loaded;
		if (include->path) {
			node->include.path = str_dup(pool, include->path);
		}
		if (include->body) {
			ARRAY_FOREACH(include->body, struct ASTNode *, node) {
				array_append(node->include.body, node);
			}
		}
		break;
	} case AST_NODE_TARGET: {
		struct ASTNodeTarget *target = value;
		node->target.type = target->type;
		node->target.sources = mempool_array(pool);
		node->target.dependencies = mempool_array(pool);
		node->target.body = mempool_array(pool);
		if (target->sources) {
			ARRAY_FOREACH(target->sources, const char *, source) {
				array_append(node->target.sources, str_dup(pool, source));
			}
		}
		if (target->dependencies) {
			ARRAY_FOREACH(target->dependencies, const char *, dependency) {
				array_append(node->target.dependencies, str_dup(pool, dependency));
			}
		}
		break;
	} case AST_NODE_TARGET_COMMAND: {
		struct ASTNodeTargetCommand *targetcommand = value;
		node->targetcommand.target = targetcommand->target;
		node->targetcommand.words = mempool_array(pool);
		if (targetcommand->words) {
			ARRAY_FOREACH(targetcommand->words, const char *, word) {
				array_append(node->targetcommand.words, str_dup(pool, word));
			}
		}
		break;
	} case AST_NODE_VARIABLE: {
		struct ASTNodeVariable *variable = value;
		node->variable.name = str_dup(pool, variable->name);
		node->variable.modifier = variable->modifier;
		node->variable.words = mempool_array(pool);
		if (variable->words) {
			ARRAY_FOREACH(variable->words, const char *, t) {
				array_append(node->variable.words, str_dup(pool, t));
			}
		}
		break;
	}
	}

	return node;
}

static struct ASTNode *
ast_node_clone_helper(struct Mempool *pool, struct Map *ptrmap, struct ASTNode *template, struct ASTNode *parent)
{
	struct ASTNode *node = mempool_alloc(pool, sizeof(struct ASTNode));
	map_add(ptrmap, template, node);
	node->pool = pool;
	node->parent = parent;

	node->type = template->type;
	node->line_start = template->line_start;
	node->line_end = template->line_end;
	node->edited = template->edited;
	node->meta = template->meta;

	switch (template->type) {
	case AST_NODE_ROOT:
		node->root.body = mempool_array(pool);
		ARRAY_FOREACH(template->root.body, struct ASTNode *, child) {
			array_append(node->root.body, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_NODE_EXPR_FOR:
		if (template->forexpr.comment) {
			node->forexpr.comment = str_dup(pool, template->forexpr.comment);
		}
		if (template->forexpr.end_comment) {
			node->forexpr.end_comment = str_dup(pool, template->forexpr.end_comment);
		}
		node->forexpr.indent = template->forexpr.indent;
		node->forexpr.bindings = mempool_array(pool);
		ARRAY_FOREACH(template->forexpr.bindings, const char *, binding) {
			array_append(node->forexpr.bindings, str_dup(pool, binding));
		}
		node->forexpr.words = mempool_array(pool);
		ARRAY_FOREACH(template->forexpr.words, const char *, word) {
			array_append(node->forexpr.words, str_dup(pool, word));
		}
		node->forexpr.body = mempool_array(pool);
		ARRAY_FOREACH(template->forexpr.body, struct ASTNode *, child) {
			array_append(node->forexpr.body, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_NODE_EXPR_IF:
		if (template->ifexpr.comment) {
			node->ifexpr.comment = str_dup(pool, template->ifexpr.comment);
		}
		if (template->ifexpr.end_comment) {
			node->ifexpr.end_comment = str_dup(pool, template->ifexpr.end_comment);
		}
		node->ifexpr.indent = template->ifexpr.indent;
		node->ifexpr.ifparent = ((struct ASTNode *)map_get(ptrmap, template))->ifexpr.ifparent;
		node->ifexpr.test = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.test, const char *, word) {
			array_append(node->ifexpr.test, str_dup(pool, word));
		}
		node->ifexpr.body = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.body, struct ASTNode *, child) {
			array_append(node->ifexpr.body, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		node->ifexpr.orelse = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.orelse, struct ASTNode *, child) {
			array_append(node->ifexpr.orelse, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_NODE_INCLUDE:
		if (template->include.comment) {
			node->include.comment = str_dup(pool, template->include.comment);
		}
		if (template->include.path) {
			node->include.path = str_dup(pool, template->include.path);
		}
		node->include.indent = template->include.indent;
		node->include.sys = template->include.sys;
		node->include.loaded = template->include.loaded;
		node->include.body = mempool_array(pool);
		ARRAY_FOREACH(template->include.body, struct ASTNode *, child) {
			array_append(node->include.body, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_NODE_TARGET:
		node->target.type = template->target.type;
		if (template->target.comment) {
			node->target.comment = str_dup(pool, template->target.comment);
		}
		node->target.sources = mempool_array(pool);
		ARRAY_FOREACH(template->target.sources, const char *, source) {
			array_append(node->target.sources, str_dup(pool, source));
		}
		node->target.dependencies = mempool_array(pool);
		ARRAY_FOREACH(template->target.dependencies, const char *, dependency) {
			array_append(node->target.dependencies, str_dup(pool, dependency));
		}
		node->target.body = mempool_array(pool);
		ARRAY_FOREACH(template->target.body, struct ASTNode *, child) {
			array_append(node->target.body, ast_node_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_NODE_COMMENT:
		node->comment.type = template->comment.type;
		node->comment.lines = mempool_array(pool);
		ARRAY_FOREACH(template->comment.lines, const char *, line) {
			array_append(node->comment.lines, str_dup(pool, line));
		}
		break;
	case AST_NODE_EXPR_FLAT:
		node->flatexpr.type = template->flatexpr.type;
		node->flatexpr.indent = template->flatexpr.indent;
		if (template->flatexpr.comment) {
			node->flatexpr.comment = str_dup(pool, template->flatexpr.comment);
		}
		node->flatexpr.words = mempool_array(pool);
		ARRAY_FOREACH(template->flatexpr.words, const char *, word) {
			array_append(node->flatexpr.words, str_dup(pool, word));
		}
		break;
	case AST_NODE_TARGET_COMMAND:
		node->targetcommand.target = &((struct ASTNode *)map_get(ptrmap, template))->target;
		if (template->targetcommand.comment) {
			node->targetcommand.comment = str_dup(pool, template->targetcommand.comment);
		}
		node->targetcommand.words = mempool_array(pool);
		ARRAY_FOREACH(template->targetcommand.words, const char *, word) {
			array_append(node->targetcommand.words, str_dup(pool, word));
		}
		break;
	case AST_NODE_VARIABLE:
		node->variable.name = str_dup(pool, template->variable.name);
		node->variable.modifier = template->variable.modifier;
		if (template->variable.comment) {
			node->variable.comment = str_dup(pool, template->variable.comment);
		}
		node->variable.words = mempool_array(pool);
		ARRAY_FOREACH(template->variable.words, const char *, word) {
			array_append(node->variable.words, str_dup(pool, word));
		}
		break;
	}

	return node;
}

struct ASTNode *
ast_node_clone(struct Mempool *extpool, struct ASTNode *template)
{
	SCOPE_MEMPOOL(pool);
	struct Map *ptrmap = mempool_map(pool, NULL, NULL, NULL, NULL);
	return ast_node_clone_helper(extpool, ptrmap, template, NULL);
}

void
ast_node_parent_append_sibling(struct ASTNode *parent, struct ASTNode *node, int orelse)
{
	unless (parent) {
		panic("null parent");
	}

	node->parent = parent;
	switch (parent->type) {
	case AST_NODE_ROOT:
		array_append(parent->root.body, node);
		break;
	case AST_NODE_EXPR_FOR:
		array_append(parent->forexpr.body, node);
		break;
	case AST_NODE_EXPR_IF: {
		if (orelse) {
			array_append(parent->ifexpr.orelse, node);
		} else {
			array_append(parent->ifexpr.body, node);
		}
		break;
	} case AST_NODE_INCLUDE:
		array_append(parent->include.body, node);
		break;
	case AST_NODE_TARGET:
		array_append(parent->target.body, node);
		break;
	case AST_NODE_COMMENT:
		panic("cannot add child to AST_NODE_COMMENT");
		break;
	case AST_NODE_TARGET_COMMAND:
		panic("cannot add child to AST_NODE_TARGET_COMMAND");
		break;
	case AST_NODE_EXPR_FLAT:
		panic("cannot add child to AST_NODE_EXPR_FLAT");
		break;
	case AST_NODE_VARIABLE:
		panic("cannot add child to AST_NODE_VARIABLE");
		break;
	}
}

struct Array *
ast_node_siblings(struct ASTNode *node)
{
	ssize_t index = -1;
	struct ASTNode *parent = node->parent;
	switch (parent->type) {
	case AST_NODE_ROOT:
		return parent->root.body;
	case AST_NODE_EXPR_IF:
		index = array_find(parent->ifexpr.body, node, NULL, NULL);
		if (index < 0) {
			index = array_find(parent->ifexpr.orelse, node, NULL, NULL);
			if (index < 0) {
				panic("node does not appear in parent nodelist");
			}
			return parent->ifexpr.orelse;
		} else {
			return parent->ifexpr.body;
		}
	case AST_NODE_EXPR_FOR:
		return parent->forexpr.body;
	case AST_NODE_INCLUDE:
		return parent->include.body;
	case AST_NODE_TARGET:
		return parent->target.body;
	case AST_NODE_COMMENT:
	case AST_NODE_EXPR_FLAT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_VARIABLE:
		panic("leaf node as parent");
	}

	panic("no siblings found?");
}

void
ast_node_parent_insert_before_sibling(struct ASTNode *node, struct ASTNode *new_sibling)
// Insert `new_sibling` in the `node`'s parent just before `node`
{
	struct Array *nodelist = ast_node_siblings(node);
	ssize_t index = -1;
	index = array_find(nodelist, node, NULL, NULL);
	if (index < 0) {
		panic("node does not appear in parent nodelist");
	}
	array_insert(nodelist, index, new_sibling);
	new_sibling->parent = node->parent;
}

enum ASTWalkState
ast_node_print_helper(struct ASTNode *node, FILE *f, size_t level)
{
	SCOPE_MEMPOOL(pool);
	const char *indent = str_repeat(pool, "\t", level);
	const char *line_start = str_printf(pool, "[%zu,%zu)", node->line_start.a, node->line_start.b);
	const char *line_end = str_printf(pool, "[%zu,%zu)", node->line_end.a, node->line_end.b);
	switch(node->type) {
	case AST_NODE_COMMENT:
		fprintf(f, "%s{ .type = AST_NODE_COMMENT, .line_start = %s, .line_end = %s, .comment = %s }\n",
			indent,
			line_start,
			line_end,
			str_join(pool, node->comment.lines, "\\n"));
		break;
	case AST_NODE_EXPR_FLAT:
		fprintf(f, "%s{ .type = AST_NODE_EXPR_FLAT, .line_start = %s, .line_end = %s, .indent = %zu, .words = { %s } }\n",
			indent,
			line_start,
			line_end,
			node->flatexpr.indent,
			str_join(pool, node->flatexpr.words, ", "));
		break;
	case AST_NODE_EXPR_FOR: {
		const char *comment = "";
		if (node->ifexpr.comment) {
			comment = node->ifexpr.comment;
		}
		const char *end_comment = "";
		if (node->ifexpr.end_comment) {
			end_comment = node->ifexpr.end_comment;
		}
		fprintf(f, "%s{ .type = AST_NODE_EXPR_FOR, .line_start = %s, .line_end = %s, .indent = %zu, .bindings = { %s }, .words = { %s }, .comment = %s, .end_comment = %s }\n",
			indent,
			line_start,
			line_end,
			node->forexpr.indent,
			str_join(pool, node->forexpr.bindings, ", "),
			str_join(pool, node->forexpr.words, ", "),
			comment,
			end_comment);
		level++;
		break;
	} case AST_NODE_EXPR_IF: {
		const char *comment = "";
		if (node->ifexpr.comment) {
			comment = node->ifexpr.comment;
		}
		const char *end_comment = "";
		if (node->ifexpr.end_comment) {
			end_comment = node->ifexpr.end_comment;
		}
		fprintf(f, "%s{ .type = AST_NODE_EXPR_IF, .line_start = %s, .line_end = %s, .iftype = %s, .indent = %zu, .test = { %s }, .elseif = %d, .comment = %s, .end_comment = %s }\n",
			indent,
			line_start,
			line_end,
			NodeExprIfType_tostring[node->ifexpr.type],
			node->ifexpr.indent,
			str_join(pool, node->ifexpr.test, ", "),
			node->ifexpr.ifparent != NULL,
			comment,
			end_comment);
		if (array_len(node->ifexpr.body) > 0) {
			fprintf(f, "%s=> if:\n", indent);
			ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
				ast_node_print_helper(child, f, level + 1);
			}
		}
		if (array_len(node->ifexpr.orelse) > 0) {
			fprintf(f, "%s=> else:\n", indent);
			ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
				ast_node_print_helper(child, f, level + 1);
			}
		}
		return AST_WALK_CONTINUE;
	} case AST_NODE_INCLUDE: {
		const char *path = node->include.path;
		unless (path) {
			path = "";
		}
		const char *comment = node->include.comment;
		unless (comment) {
			comment = "";
		}
		fprintf(f, "%s{ .type = AST_NODE_INCLUDE/%s, .line_start = %s, .line_end = %s, .indent = %zu, .path = %s, .sys = %d, .loaded = %d, .comment = %s }\n",
			indent,
			ASTNodeIncludeType_tostring[node->include.type],
			line_start,
			line_end,
			node->include.indent,
			path,
			node->include.sys,
			node->include.loaded,
			comment);
		level++;
		break;
	} case AST_NODE_TARGET:
		fprintf(f, "%s{ .type = AST_NODE_TARGET, .line_start = %s, .line_end = %s, .type = %s, .sources = { %s }, .dependencies = { %s } }\n",
			indent,
			line_start,
			line_end,
			ASTNodeTargetType_tostring[node->target.type],
			str_join(pool, node->target.sources, ", "),
			str_join(pool, node->target.dependencies, ", "));
		level++;
		break;
	case AST_NODE_TARGET_COMMAND:
		fprintf(f, "%s{ .type = AST_NODE_TARGET_COMMAND, .line_start = %s, .line_end = %s, .words = { %s } }\n",
			indent,
			line_start,
			line_end,
			str_join(pool, node->targetcommand.words, ", "));
		break;
	case AST_NODE_VARIABLE:
		fprintf(f, "%s{ .type = AST_NODE_VARIABLE, .line_start = %s, .line_end = %s, .name = %s, .modifier = %s, .words = { %s } }\n",
			indent,
			line_start,
			line_end,
			node->variable.name,
			ASTNodeVariableModifier_humanize[node->variable.modifier],
			str_join(pool, node->variable.words, ", "));
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(ast_node_print_helper, node, f, level);
	return AST_WALK_CONTINUE;
}

void
ast_node_print(struct ASTNode *node, FILE *f)
{
	ast_node_print_helper(node, f, 0);
}

void
ast_free(struct ASTNode *node) {
	if (node) {
		mempool_free(node->pool);
	}
}
