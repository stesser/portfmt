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

enum ASTNodeType {
	AST_NODE_ROOT,
	AST_NODE_DELETED,
	AST_NODE_COMMENT,
	AST_NODE_EXPR_FLAT,
	AST_NODE_EXPR_IF,
	AST_NODE_EXPR_FOR,
	AST_NODE_INCLUDE,
	AST_NODE_TARGET,
	AST_NODE_TARGET_COMMAND,
	AST_NODE_VARIABLE,
};

enum ASTNodeCommentType {
	AST_NODE_COMMENT_LINE,
	// TODO: AST_NODE_COMMENT_EOL,
};

enum ASTNodeExprFlatType {
	AST_NODE_EXPR_ERROR,
	AST_NODE_EXPR_EXPORT_ENV,
	AST_NODE_EXPR_EXPORT_LITERAL,
	AST_NODE_EXPR_EXPORT,
	AST_NODE_EXPR_INFO,
	AST_NODE_EXPR_UNDEF,
	AST_NODE_EXPR_UNEXPORT_ENV,
	AST_NODE_EXPR_UNEXPORT,
	AST_NODE_EXPR_WARNING,
};

enum ASTNodeExprIfType {
	AST_NODE_EXPR_IF_IF,
	AST_NODE_EXPR_IF_DEF,
	AST_NODE_EXPR_IF_ELSE,
	AST_NODE_EXPR_IF_MAKE,
	AST_NODE_EXPR_IF_NDEF,
	AST_NODE_EXPR_IF_NMAKE,
};

enum ASTNodeIncludeType {
	AST_NODE_INCLUDE_BMAKE,
	AST_NODE_INCLUDE_D,
	AST_NODE_INCLUDE_POSIX,
	AST_NODE_INCLUDE_S,
};

enum ASTNodeTargetType {
	AST_NODE_TARGET_NAMED,
	AST_NODE_TARGET_UNASSOCIATED,
};

enum ASTNodeVariableModifier {
	AST_NODE_VARIABLE_MODIFIER_APPEND,
	AST_NODE_VARIABLE_MODIFIER_ASSIGN,
	AST_NODE_VARIABLE_MODIFIER_EXPAND,
	AST_NODE_VARIABLE_MODIFIER_OPTIONAL,
	AST_NODE_VARIABLE_MODIFIER_SHELL,
};

struct ASTNodeComment {
	enum ASTNodeCommentType type;
	struct Array *lines;
};

struct ASTNodeExprFor {
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

struct ASTNodeExprIf {
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
	enum ASTNodeExprIfType type;
	struct Array *test;
	struct Array *body;
	struct Array *orelse;
	const char *comment;
	const char *end_comment;
	size_t indent;
	struct ASTNode *ifparent;
};

struct ASTNodeExprFlat {
	enum ASTNodeExprFlatType type;
	struct Array *words;
	const char *comment;
	size_t indent;
};

struct ASTNodeInclude {
	enum ASTNodeIncludeType type;
	struct Array *body;
	const char *comment;
	size_t indent;
	const char *path;
	int sys;
	int loaded;
};

struct ASTNodeTarget {
	enum ASTNodeTargetType type;
	struct Array *sources;
	struct Array *dependencies;
	struct Array *body;
	const char *comment;
};

struct ASTNodeTargetCommand {
	struct ASTNodeTarget *target;
	struct Array *words;
	const char *comment;
};

struct ASTNodeVariable {
	const char *name;
	enum ASTNodeVariableModifier modifier;
	struct Array *words;
	const char *comment;
};

struct ASTNodeRoot {
	struct Array *body;
};

struct ASTNodeLineRange { // [a,b)
	size_t a;
	size_t b;
};

struct ASTNode {
	enum ASTNodeType type;
	struct ASTNode *parent;
	struct Mempool *pool;
	struct ASTNodeLineRange line_start;
	struct ASTNodeLineRange line_end;
	int edited;
	struct {
		size_t goalcol;
	} meta;
	union {
		struct ASTNodeRoot root;
		struct ASTNodeComment comment;
		struct ASTNodeExprFlat flatexpr;
		struct ASTNodeExprIf ifexpr;
		struct ASTNodeInclude include;
		struct ASTNodeExprFor forexpr;
		struct ASTNodeTarget target;
		struct ASTNodeTargetCommand targetcommand;
		struct ASTNodeVariable variable;
	};
};

enum ASTWalkState {
	AST_WALK_CONTINUE,
	AST_WALK_STOP,
};

extern const char *ASTNodeExprFlatType_identifier[];
extern const char *ASTNodeIncludeType_identifier[];
extern const char *ASTNodeVariableModifier_humanize[];
extern const char *NodeExprIfType_humanize[];

void ast_free(struct ASTNode *);
struct ASTNode *ast_node_new(struct Mempool *, enum ASTNodeType, struct ASTNodeLineRange *, void *);
struct ASTNode *ast_node_clone(struct Mempool *, struct ASTNode *);
struct Array *ast_node_siblings(struct ASTNode *);
void ast_node_parent_append_sibling(struct ASTNode *, struct ASTNode *, int);
void ast_node_parent_insert_before_sibling(struct ASTNode *, struct ASTNode *);
void ast_node_print(struct ASTNode *, FILE *);

#define AST_WALK_RECUR(x) \
	if ((x) == AST_WALK_STOP) { \
		return AST_WALK_STOP; \
	}

#define AST_WALK_DEFAULT(f, node, ...) \
switch (node->type) { \
case AST_NODE_ROOT: \
	ARRAY_FOREACH(node->root.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_NODE_EXPR_FOR: \
	ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_NODE_EXPR_IF: \
	ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_NODE_INCLUDE: \
	ARRAY_FOREACH(node->include.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_NODE_TARGET: \
	ARRAY_FOREACH(node->target.body, struct ASTNode *, child) { \
		AST_WALK_RECUR(f(child, ##__VA_ARGS__)); \
	} \
	break; \
case AST_NODE_DELETED: \
case AST_NODE_COMMENT: \
case AST_NODE_EXPR_FLAT: \
case AST_NODE_TARGET_COMMAND: \
case AST_NODE_VARIABLE: \
	break; \
}
