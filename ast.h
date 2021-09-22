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
#pragma once

enum ASTType {
	AST_ROOT,
	AST_DELETED,
	AST_COMMENT,
	AST_EXPR,
	AST_IF,
	AST_FOR,
	AST_INCLUDE,
	AST_TARGET,
	AST_TARGET_COMMAND,
	AST_VARIABLE,
};

enum ASTCommentType {
	AST_COMMENT_LINE,
};

enum ASTExprType {
	AST_EXPR_ERROR,
	AST_EXPR_EXPORT_ENV,
	AST_EXPR_EXPORT_LITERAL,
	AST_EXPR_EXPORT,
	AST_EXPR_INFO,
	AST_EXPR_UNDEF,
	AST_EXPR_UNEXPORT_ENV,
	AST_EXPR_UNEXPORT,
	AST_EXPR_WARNING,
};

enum ASTIfType {
	AST_IF_IF,
	AST_IF_DEF,
	AST_IF_ELSE,
	AST_IF_MAKE,
	AST_IF_NDEF,
	AST_IF_NMAKE,
};

enum ASTIncludeType {
	AST_INCLUDE_BMAKE,
	AST_INCLUDE_D,
	AST_INCLUDE_POSIX,
	AST_INCLUDE_S,
};

enum ASTTargetType {
	AST_TARGET_NAMED,
	AST_TARGET_UNASSOCIATED,
};

enum ASTVariableModifier {
	AST_VARIABLE_MODIFIER_APPEND,
	AST_VARIABLE_MODIFIER_ASSIGN,
	AST_VARIABLE_MODIFIER_EXPAND,
	AST_VARIABLE_MODIFIER_OPTIONAL,
	AST_VARIABLE_MODIFIER_SHELL,
};

struct ASTComment {
	enum ASTCommentType type;
	struct Array *lines;
};

struct ASTFor {
	// .for $bindings in $words
	// $body
	// .endfor
	struct Array *bindings;
	struct Array *words;
	struct Array *body;
	const char *comment;
	const char *end_comment;
	size_t indent;
};

struct ASTIf {
	// .if $test
	// $body
	// .else
	// $orelse
	// .endif
	//
	// Elif:
	// 
	// .if $test1
	// $body1
	// .elif $test2
	// $body2
	// .else
	// $orelse
	// .endif
	//
	// =>
	//
	// .if $test1
	// $body1
	// .else
	// .if $test2
	// $body2
	// .else
	// $orelse
	// .endif
	// .endif
	enum ASTIfType type;
	struct Array *test;
	struct Array *body;
	struct Array *orelse;
	const char *comment;
	const char *end_comment;
	size_t indent;
	struct AST *ifparent;
};

struct ASTExpr {
	enum ASTExprType type;
	struct Array *words;
	const char *comment;
	size_t indent;
};

struct ASTInclude {
	enum ASTIncludeType type;
	struct Array *body;
	const char *comment;
	size_t indent;
	const char *path;
	int sys;
	int loaded;
};

struct ASTTarget {
	enum ASTTargetType type;
	struct Array *sources;
	struct Array *dependencies;
	struct Array *body;
	const char *comment;
};

struct ASTTargetCommand {
	struct ASTTarget *target;
	struct Array *words;
	const char *comment;
};

struct ASTVariable {
	const char *name;
	enum ASTVariableModifier modifier;
	struct Array *words;
	const char *comment;
};

struct ASTRoot {
	struct Array *body;
};

struct ASTLineRange { // [a,b)
	size_t a;
	size_t b;
};

struct AST {
	enum ASTType type;
	struct AST *parent;
	struct Mempool *pool;
	struct ASTLineRange line_start;
	struct ASTLineRange line_end;
	int edited;
	struct {
		size_t goalcol;
	} meta;
	union {
		struct ASTRoot root;
		struct ASTComment comment;
		struct ASTExpr expr;
		struct ASTIf ifexpr;
		struct ASTInclude include;
		struct ASTFor forexpr;
		struct ASTTarget target;
		struct ASTTargetCommand targetcommand;
		struct ASTVariable variable;
	};
};

enum ASTWalkState {
	AST_WALK_CONTINUE,
	AST_WALK_STOP,
};

extern const char *ASTType_tostring[];
extern const char *ASTExprType_identifier[];
extern const char *ASTIncludeType_identifier[];
extern const char *ASTVariableModifier_humanize[];
extern const char *ASTIfType_humanize[];

void ast_free(struct AST *);
struct AST *ast_new(struct Mempool *, enum ASTType, struct ASTLineRange *, void *);
struct AST *ast_clone(struct Mempool *, struct AST *);
struct Array *ast_siblings(struct AST *);
void ast_parent_append_sibling(struct AST *, struct AST *, int);
void ast_parent_insert_before_sibling(struct AST *, struct AST *);
void ast_print(struct AST *, FILE *);

char *ast_line_range_tostring(struct ASTLineRange *, int, struct Mempool *);

#define AST_WALK_RECUR(x) \
	if ((x) == AST_WALK_STOP) { \
		return AST_WALK_STOP; \
	}

#define AST_WALK_DEFAULT(f, node, ...) \
switch (node->type) { \
case AST_ROOT: \
	ARRAY_FOREACH(node->root.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_FOR: \
	ARRAY_FOREACH(node->forexpr.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_IF: \
	ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_INCLUDE: \
	ARRAY_FOREACH(node->include.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_TARGET: \
	ARRAY_FOREACH(node->target.body, struct AST *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_DELETED: \
case AST_COMMENT: \
case AST_EXPR: \
case AST_TARGET_COMMAND: \
case AST_VARIABLE: \
	break; \
}
