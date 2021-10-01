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

const char *ASTType_tostring(enum ASTType);

enum ASTCommentType {
	AST_COMMENT_LINE,
};

const char *ASTCommentType_tostring(enum ASTCommentType);

enum ASTExprType {
	AST_EXPR_ERROR,			// identifier:"error"
	AST_EXPR_EXPORT_ENV,		// identifier:"export-env"
	AST_EXPR_EXPORT_LITERAL,	// identifier:"export-literal"
	AST_EXPR_EXPORT,		// identifier:"export"
	AST_EXPR_INFO,			// identifier:"info"
	AST_EXPR_UNDEF,			// identifier:"undef"
	AST_EXPR_UNEXPORT_ENV,		// identifier:"unexport-env"
	AST_EXPR_UNEXPORT,		// identifier:"unexport"
	AST_EXPR_WARNING,		// identifier:"warning"
};

const char *ASTExprType_identifier(enum ASTExprType);
const char *ASTExprType_tostring(enum ASTExprType);

enum ASTIfType {
	AST_IF_IF,	// human:"if"
	AST_IF_DEF,	// human:"ifdef"
	AST_IF_ELSE,	// human:"else"
	AST_IF_MAKE,	// human:"ifmake"
	AST_IF_NDEF,	// human:"ifndef"
	AST_IF_NMAKE,	// human:"ifnmake"
};

const char *ASTIfType_human(enum ASTIfType);
const char *ASTIfType_tostring(enum ASTIfType);

enum ASTIncludeType {
	AST_INCLUDE_BMAKE,		// identifier:"include"
	AST_INCLUDE_POSIX,		// identifier:"include"
	AST_INCLUDE_POSIX_OPTIONAL,	// identifier:"-include"
	AST_INCLUDE_POSIX_OPTIONAL_S,	// identifier:"sinclude"
	AST_INCLUDE_OPTIONAL,		// identifier:"-include"
	AST_INCLUDE_OPTIONAL_D,		// identifier:"dinclude"
	AST_INCLUDE_OPTIONAL_S,		// identifier:"sinclude"
};

const char *ASTIncludeType_identifier(enum ASTIncludeType);
const char *ASTIncludeType_tostring(enum ASTIncludeType);

enum ASTTargetType {
	AST_TARGET_NAMED,
	AST_TARGET_UNASSOCIATED,
};

const char *ASTTargetType_tostring(enum ASTTargetType);

enum ASTVariableModifier {
	AST_VARIABLE_MODIFIER_APPEND,	// human:"+="
	AST_VARIABLE_MODIFIER_ASSIGN,	// human:"="
	AST_VARIABLE_MODIFIER_EXPAND,	// human:":="
	AST_VARIABLE_MODIFIER_OPTIONAL,	// human:"?="
	AST_VARIABLE_MODIFIER_SHELL,	// human:"!="
};

const char *ASTVariableModifier_human(enum ASTVariableModifier);
const char *ASTVariableModifier_tostring(enum ASTVariableModifier);

enum ASTWalkState {
	AST_WALK_CONTINUE,
	AST_WALK_STOP,
};

const char *ASTWalkState_tostring(enum ASTWalkState);

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

void ast_free(struct AST *);
struct AST *ast_new(struct Mempool *, enum ASTType, struct ASTLineRange *, void *);
struct AST *ast_clone(struct Mempool *, struct AST *);
struct Array *ast_siblings(struct Mempool *, struct AST *);
void ast_parent_append_sibling(struct AST *, struct AST *, int);
void ast_parent_insert_before_sibling(struct AST *, struct AST *);
void ast_print(struct AST *, FILE *);
void ast_balance(struct AST *);

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
	ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) { \
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
