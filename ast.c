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

const char *ASTType_tostring[] = {
	[AST_ROOT] = "AST_ROOT",
	[AST_DELETED] = "AST_DELETED",
	[AST_COMMENT] = "AST_COMMENT",
	[AST_EXPR] = "AST_EXPR",
	[AST_IF] = "AST_IF",
	[AST_FOR] = "AST_FOR",
	[AST_INCLUDE] = "AST_INCLUDE",
	[AST_TARGET] = "AST_TARGET",
	[AST_TARGET_COMMAND] = "AST_TARGET_COMMAND",
	[AST_VARIABLE] = "AST_VARIABLE",
};

const char *ASTExprType_identifier[] = {
	[AST_EXPR_ERROR] = "error",
	[AST_EXPR_EXPORT_ENV] = "export-env",
	[AST_EXPR_EXPORT_LITERAL] = "export-literal",
	[AST_EXPR_EXPORT] = "export",
	[AST_EXPR_INFO] = "info",
	[AST_EXPR_UNDEF] = "undef",
	[AST_EXPR_UNEXPORT_ENV] = "unexport-env",
	[AST_EXPR_UNEXPORT] = "unexport",
	[AST_EXPR_WARNING] = "warning",
};

static const char *ASTIfType_tostring[] = {
	[AST_IF_IF] = "AST_IF_IF",
	[AST_IF_DEF] = "AST_IF_DEF",
	[AST_IF_ELSE] = "AST_IF_ELSE",
	[AST_IF_MAKE] = "AST_IF_MAKE",
	[AST_IF_NDEF] = "AST_IF_NDEF",
	[AST_IF_NMAKE] = "AST_IF_NMAKE",
};

const char *ASTIfType_humanize[] = {
	[AST_IF_IF] = "if",
	[AST_IF_ELSE] = "else",
	[AST_IF_DEF] = "ifdef",
	[AST_IF_MAKE] = "ifmake",
	[AST_IF_NDEF] = "ifndef",
	[AST_IF_NMAKE] = "ifnmake",
};

static const char *ASTIncludeType_tostring[] = {
	[AST_INCLUDE_BMAKE] = "AST_INCLUDE_BMAKE",
	[AST_INCLUDE_D] = "AST_INCLUDE_D",
	[AST_INCLUDE_POSIX] = "AST_INCLUDE_POSIX",
	[AST_INCLUDE_S] = "AST_INCLUDE_S",
};

const char *ASTIncludeType_identifier[] = {
	[AST_INCLUDE_BMAKE] = "include",
	[AST_INCLUDE_D] = "dinclude",
	[AST_INCLUDE_POSIX] = "include",
	[AST_INCLUDE_S] = "sinclude",
};

static const char *ASTTargetType_tostring[] = {
	[AST_TARGET_NAMED] = "AST_TARGET_NAMED",
	[AST_TARGET_UNASSOCIATED] = "AST_TARGET_UNASSOCIATED",
};

const char *ASTVariableModifier_humanize[] = {
	[AST_VARIABLE_MODIFIER_APPEND] = "+=",
	[AST_VARIABLE_MODIFIER_ASSIGN] = "=",
	[AST_VARIABLE_MODIFIER_EXPAND] = ":=",
	[AST_VARIABLE_MODIFIER_OPTIONAL] = "?=",
	[AST_VARIABLE_MODIFIER_SHELL] = "!=",
};

static enum ASTWalkState ast_print_helper(struct AST *, FILE *, size_t);

struct AST *
ast_new(struct Mempool *pool, enum ASTType type, struct ASTLineRange *lines, void *value)
{
	struct AST *node = mempool_alloc(pool, sizeof(struct AST));
	node->pool = pool;
	if (lines) {
		node->line_start = *lines;
		node->line_end = *lines;
	}
	node->type = type;

	switch (type) {
	case AST_ROOT:
		node->parent = node;
		node->root.body = mempool_array(pool);
		break;
	case AST_DELETED:
		panic("cannot create deleted node");
		break;
	case AST_COMMENT: {
		struct ASTComment *comment = value;
		node->comment.type = comment->type;
		node->comment.lines = mempool_array(pool);
		if (comment->lines) {
			ARRAY_FOREACH(comment->lines, const char *, line) {
				array_append(node->comment.lines, str_dup(pool, line));
			}
		}
		break;
	} case AST_EXPR: {
		struct ASTExpr *expr = value;
		node->expr.type = expr->type;
		node->expr.words = mempool_array(pool);
		node->expr.indent = expr->indent;
		if (expr->words) {
			ARRAY_FOREACH(expr->words, const char *, word) {
				array_append(node->expr.words, str_dup(pool, word));
			}
		}
		break;
	} case AST_FOR: {
		struct ASTFor *forexpr = value;
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
	} case AST_IF: {
		struct ASTIf *ifexpr = value;
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
	} case AST_INCLUDE: {
		struct ASTInclude *include = value;
		node->include.type = include->type;
		node->include.body = mempool_array(pool);
		node->include.sys = include->sys;
		node->include.loaded = include->loaded;
		node->include.indent = include->indent;
		if (include->path) {
			node->include.path = str_dup(pool, include->path);
		}
		if (include->body) {
			ARRAY_FOREACH(include->body, struct AST *, node) {
				array_append(node->include.body, node);
			}
		}
		break;
	} case AST_TARGET: {
		struct ASTTarget *target = value;
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
	} case AST_TARGET_COMMAND: {
		struct ASTTargetCommand *targetcommand = value;
		node->targetcommand.target = targetcommand->target;
		node->targetcommand.words = mempool_array(pool);
		if (targetcommand->words) {
			ARRAY_FOREACH(targetcommand->words, const char *, word) {
				array_append(node->targetcommand.words, str_dup(pool, word));
			}
		}
		break;
	} case AST_VARIABLE: {
		struct ASTVariable *variable = value;
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

static struct AST *
ast_clone_helper(struct Mempool *pool, struct Map *ptrmap, struct AST *template, struct AST *parent)
{
	struct AST *node = mempool_alloc(pool, sizeof(struct AST));
	map_add(ptrmap, template, node);
	node->pool = pool;
	node->parent = parent;

	node->type = template->type;
	node->line_start = template->line_start;
	node->line_end = template->line_end;
	node->edited = template->edited;
	node->meta = template->meta;

	switch (template->type) {
	case AST_ROOT:
		node->root.body = mempool_array(pool);
		ARRAY_FOREACH(template->root.body, struct AST *, child) {
			array_append(node->root.body, ast_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_DELETED:
		break;
	case AST_FOR:
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
		ARRAY_FOREACH(template->forexpr.body, struct AST *, child) {
			array_append(node->forexpr.body, ast_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_IF:
		if (template->ifexpr.comment) {
			node->ifexpr.comment = str_dup(pool, template->ifexpr.comment);
		}
		if (template->ifexpr.end_comment) {
			node->ifexpr.end_comment = str_dup(pool, template->ifexpr.end_comment);
		}
		node->ifexpr.indent = template->ifexpr.indent;
		node->ifexpr.ifparent = ((struct AST *)map_get(ptrmap, template))->ifexpr.ifparent;
		node->ifexpr.test = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.test, const char *, word) {
			array_append(node->ifexpr.test, str_dup(pool, word));
		}
		node->ifexpr.body = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.body, struct AST *, child) {
			array_append(node->ifexpr.body, ast_clone_helper(pool, ptrmap, child, node));
		}
		node->ifexpr.orelse = mempool_array(pool);
		ARRAY_FOREACH(template->ifexpr.orelse, struct AST *, child) {
			array_append(node->ifexpr.orelse, ast_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_INCLUDE:
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
		ARRAY_FOREACH(template->include.body, struct AST *, child) {
			array_append(node->include.body, ast_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_TARGET:
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
		ARRAY_FOREACH(template->target.body, struct AST *, child) {
			array_append(node->target.body, ast_clone_helper(pool, ptrmap, child, node));
		}
		break;
	case AST_COMMENT:
		node->comment.type = template->comment.type;
		node->comment.lines = mempool_array(pool);
		ARRAY_FOREACH(template->comment.lines, const char *, line) {
			array_append(node->comment.lines, str_dup(pool, line));
		}
		break;
	case AST_EXPR:
		node->expr.type = template->expr.type;
		node->expr.indent = template->expr.indent;
		if (template->expr.comment) {
			node->expr.comment = str_dup(pool, template->expr.comment);
		}
		node->expr.words = mempool_array(pool);
		ARRAY_FOREACH(template->expr.words, const char *, word) {
			array_append(node->expr.words, str_dup(pool, word));
		}
		break;
	case AST_TARGET_COMMAND:
		node->targetcommand.target = &((struct AST *)map_get(ptrmap, template))->target;
		if (template->targetcommand.comment) {
			node->targetcommand.comment = str_dup(pool, template->targetcommand.comment);
		}
		node->targetcommand.words = mempool_array(pool);
		ARRAY_FOREACH(template->targetcommand.words, const char *, word) {
			array_append(node->targetcommand.words, str_dup(pool, word));
		}
		break;
	case AST_VARIABLE:
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

struct AST *
ast_clone(struct Mempool *extpool, struct AST *template)
{
	SCOPE_MEMPOOL(pool);
	struct Map *ptrmap = mempool_map(pool, NULL, NULL, NULL, NULL);
	return ast_clone_helper(extpool, ptrmap, template, NULL);
}

void
ast_parent_append_sibling(struct AST *parent, struct AST *node, int orelse)
{
	panic_unless(parent, "null parent");
	panic_unless(node, "null node");

	node->parent = parent;
	switch (parent->type) {
	case AST_ROOT:
		array_append(parent->root.body, node);
		break;
	case AST_DELETED:
		break;
	case AST_FOR:
		array_append(parent->forexpr.body, node);
		break;
	case AST_IF: {
		if (orelse) {
			array_append(parent->ifexpr.orelse, node);
		} else {
			array_append(parent->ifexpr.body, node);
		}
		break;
	} case AST_INCLUDE:
		array_append(parent->include.body, node);
		break;
	case AST_TARGET:
		array_append(parent->target.body, node);
		break;
	case AST_COMMENT:
		panic("cannot add child to AST_COMMENT");
		break;
	case AST_TARGET_COMMAND:
		panic("cannot add child to AST_TARGET_COMMAND");
		break;
	case AST_EXPR:
		panic("cannot add child to AST_EXPR");
		break;
	case AST_VARIABLE:
		panic("cannot add child to AST_VARIABLE");
		break;
	}
}

static struct Array *
ast_siblings_helper(struct AST *node)
{
	ssize_t index = -1;
	struct AST *parent = node->parent;
	switch (parent->type) {
	case AST_ROOT:
		return parent->root.body;
	case AST_DELETED:
		panic("cannot return siblings of deleted node");
	case AST_IF:
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
	case AST_FOR:
		return parent->forexpr.body;
	case AST_INCLUDE:
		return parent->include.body;
	case AST_TARGET:
		return parent->target.body;
	case AST_COMMENT:
	case AST_EXPR:
	case AST_TARGET_COMMAND:
	case AST_VARIABLE:
		panic("leaf node as parent");
	}

	panic("no siblings found?");
}

struct Array *
ast_siblings(struct Mempool *pool, struct AST *node)
{
	struct Array *siblings = mempool_array(pool);
	ARRAY_FOREACH(ast_siblings_helper(node), struct AST *, sibling) {
		array_append(siblings, sibling);
	}
	return siblings;
}

void
ast_parent_insert_before_sibling(struct AST *node, struct AST *new_sibling)
// Insert `new_sibling` in the `node`'s parent just before `node`
{
	panic_unless(node, "null node");
	panic_unless(new_sibling, "null new_sibling");

	struct Array *nodelist = ast_siblings_helper(node);
	ssize_t index = -1;
	index = array_find(nodelist, node, NULL, NULL);
	if (index < 0) {
		panic("node does not appear in parent nodelist");
	}
	array_insert(nodelist, index, new_sibling);
	new_sibling->parent = node->parent;
}

char *
ast_line_range_tostring(struct ASTLineRange *range, int want_prefix, struct Mempool *pool)
{
	if (range->a == range->b) {
		return str_dup(pool, "-");
	}
	panic_unless(range, "range_tostring() is not NULL-safe");
	panic_unless(range->a < range->b, "range is inverted");

	const char *prefix = "";
	if (range->a == range->b - 1) {
		if (want_prefix) {
			prefix = "line ";
		}
		return str_printf(pool, "%s%zu", prefix, range->a);
	} else {
		if (want_prefix) {
			prefix = "lines ";
		}
		return str_printf(pool, "%s[%zu,%zu)", prefix, range->a, range->b);
	}
}

enum ASTWalkState
ast_print_helper(struct AST *node, FILE *f, size_t level)
{
	SCOPE_MEMPOOL(pool);
	const char *indent = str_repeat(pool, "\t", level);
	const char *lines = ast_line_range_tostring(&node->line_start, 1, pool);
	const char *edited = "";
	if (node->edited) {
		edited = "*";
	}
	switch(node->type) {
	case AST_COMMENT:
		fprintf(f, "%s{ %sCOMMENT, %s, .comment = %s }\n",
			indent,
			edited,
			lines,
			str_join(pool, node->comment.lines, "\\n"));
		break;
	case AST_EXPR: {
		const char *comment = "";
		if (node->expr.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->expr.comment);
		}
		fprintf(f, "%s{ %sEXPR, %s, .indent = %zu, %s.words = { %s } }\n",
			indent,
			edited,
			lines,
			node->expr.indent,
			comment,
			str_join(pool, node->expr.words, ", "));
		break;
	} case AST_FOR: {
		const char *comment = "";
		if (node->forexpr.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->forexpr.comment);
		}
		const char *end_comment = "";
		if (node->forexpr.end_comment) {
			end_comment = str_printf(pool, ".end_comment = %s, ", node->forexpr.end_comment);
		}
		fprintf(f, "%s{ %sFOR, %s, .indent = %zu, .bindings = { %s }, %s%s.words = { %s } }\n",
			indent,
			edited,
			lines,
			node->forexpr.indent,
			str_join(pool, node->forexpr.bindings, ", "),
			comment,
			end_comment,
			str_join(pool, node->forexpr.words, ", "));
		level++;
		break;
	} case AST_IF: {
		const char *comment = "";
		if (node->ifexpr.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->ifexpr.comment);
		}
		const char *end_comment = "";
		if (node->ifexpr.end_comment) {
			end_comment = str_printf(pool, ".end_comment = %s, ", node->ifexpr.end_comment);
		}
		fprintf(f, "%s{ %sIF/%s, %s, .indent = %zu, .test = { %s }, %s%s.elseif = %d }\n",
			indent,
			edited,
			ASTIfType_tostring[node->ifexpr.type] + strlen("AST_IF_"),
			lines,
			node->ifexpr.indent,
			str_join(pool, node->ifexpr.test, ", "),
			comment,
			end_comment,
			node->ifexpr.ifparent != NULL);
		if (array_len(node->ifexpr.body) > 0) {
			fprintf(f, "%s=> if:\n", indent);
			ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
				ast_print_helper(child, f, level + 1);
			}
		}
		if (array_len(node->ifexpr.orelse) > 0) {
			fprintf(f, "%s=> else:\n", indent);
			ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
				ast_print_helper(child, f, level + 1);
			}
		}
		return AST_WALK_CONTINUE;
	} case AST_INCLUDE: {
		const char *path = node->include.path;
		unless (path) {
			path = "";
		}
		const char *comment = "";
		if (node->include.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->include.comment);
		}
		fprintf(f, "%s{ %sINCLUDE/%s, %s, .indent = %zu, %s.path = %s, .sys = %d, .loaded = %d }\n",
			indent,
			edited,
			ASTIncludeType_tostring[node->include.type] + strlen("AST_INCLUDE_"),
			lines,
			node->include.indent,
			comment,
			path,
			node->include.sys,
			node->include.loaded);
		level++;
		break;
	} case AST_TARGET: {
		const char *comment = "";
		if (node->target.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->target.comment);
		}
		fprintf(f, "%s{ %sTARGET/%s, %s, %s.sources = { %s }, .dependencies = { %s } }\n",
			indent,
			edited,
			ASTTargetType_tostring[node->target.type] + strlen("AST_TARGET_"),
			lines,
			comment,
			str_join(pool, node->target.sources, ", "),
			str_join(pool, node->target.dependencies, ", "));
		level++;
		break;
	} case AST_TARGET_COMMAND: {
		const char *comment = "";
		if (node->targetcommand.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->targetcommand.comment);
		}
		fprintf(f, "%s{ %sTARGET_COMMAND, %s, %s.words = { %s } }\n",
			indent,
			edited,
			lines,
			comment,
			str_join(pool, node->targetcommand.words, ", "));
		break;
	} case AST_VARIABLE: {
		const char *comment = "";
		if (node->variable.comment) {
			comment = str_printf(pool, ".comment = %s, ", node->variable.comment);
		}
		fprintf(f, "%s{ %sVARIABLE, %s, .name = %s, .modifier = %s, %s.words = { %s } }\n",
			indent,
			edited,
			lines,
			node->variable.name,
			ASTVariableModifier_humanize[node->variable.modifier],
			comment,
			str_join(pool, node->variable.words, ", "));
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(ast_print_helper, node, f, level);
	return AST_WALK_CONTINUE;
}

void
ast_print(struct AST *node, FILE *f)
{
	ast_print_helper(node, f, 0);
}

static void
ast_balance_comments_join(struct Array *comments)
{
	if (array_len(comments) == 0) {
		return;
	}

	struct AST *node = array_get(comments, 0);
	ARRAY_FOREACH_SLICE(comments, 1, -1, struct AST *, sibling) {
		panic_if(sibling->type != AST_COMMENT, "unexpected node type");
		ARRAY_FOREACH(sibling->comment.lines, const char *, line) {
			array_append(node->comment.lines, line);
			node->edited = 1;
		}
		sibling->type = AST_DELETED;
	}

	array_truncate(comments);
}

static enum ASTWalkState
ast_balance_comments_walker(struct AST *node, struct Array *comments)
{
	switch (node->type) {
	case AST_DELETED:
		break;
	case AST_ROOT:
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->root.body, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		break;
	case AST_FOR:
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->forexpr.body, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		break;
	case AST_IF:
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		break;
	case AST_INCLUDE:
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->include.body, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		break;
	case AST_TARGET:
		ast_balance_comments_join(comments);
		ARRAY_FOREACH(node->target.body, struct AST *, child) {
			AST_WALK_RECUR(ast_balance_comments_walker(child, comments));
		}
		ast_balance_comments_join(comments);
		break;
	case AST_COMMENT:
		array_append(comments, node);
		break;
	case AST_EXPR:
	case AST_TARGET_COMMAND:
	case AST_VARIABLE:
		ast_balance_comments_join(comments);
		break;
	}

	return AST_WALK_CONTINUE;
}

void
ast_balance(struct AST *node)
// Clean up the AST.  This function should be called after editing the AST.
// We might have some artifacts like two consecutive AST_COMMENT
// siblings that should be merge into one for easier editing down
// the line.
{
	SCOPE_MEMPOOL(pool);
	struct Array *comments = mempool_array(pool);
	ast_balance_comments_walker(node, comments);
	ast_balance_comments_join(comments);
}

void
ast_free(struct AST *node)
// This will free the entire tree not just this particular node
{
	if (node) {
		mempool_free(node->pool);
	}
}
