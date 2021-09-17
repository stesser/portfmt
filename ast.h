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
	AST_NODE_COMMENT,
	AST_NODE_EXPR_FLAT,
	AST_NODE_EXPR_IF,
	AST_NODE_EXPR_FOR,
	AST_NODE_TARGET,
	AST_NODE_TARGET_COMMAND,
	AST_NODE_VARIABLE,
};

enum ASTNodeCommentType {
	AST_NODE_COMMENT_LINE,
	// TODO: AST_NODE_COMMENT_EOL,
};

enum ASTNodeExprFlatType {
	AST_NODE_EXPR_DINCLUDE,
	AST_NODE_EXPR_ERROR,
	AST_NODE_EXPR_EXPORT_ENV,
	AST_NODE_EXPR_EXPORT_LITERAL,
	AST_NODE_EXPR_EXPORT,
	AST_NODE_EXPR_INCLUDE_POSIX,
	AST_NODE_EXPR_INCLUDE,
	AST_NODE_EXPR_INFO,
	AST_NODE_EXPR_SINCLUDE,
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
	size_t indent;
	struct ASTNode *ifparent;
};

struct ASTNodeExprFlat {
	enum ASTNodeExprFlatType type;
	struct Array *words;
	size_t indent;
};

struct ASTNodeTarget {
	enum ASTNodeTargetType type;
	struct Array *sources;
	struct Array *dependencies;
	struct Array *body;
};

struct ASTNodeTargetCommand {
	struct ASTNodeTarget *target;
	struct Array *words;
};

struct ASTNodeVariable {
	const char *name;
	enum ASTNodeVariableModifier modifier;
	struct Array *words;
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
		int goalcol;
	} meta;
	union {
		struct ASTNodeRoot root;
		struct ASTNodeComment comment;
		struct ASTNodeExprFlat flatexpr;
		struct ASTNodeExprIf ifexpr;
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
extern const char *ASTNodeVariableModifier_humanize[];

struct ASTNode *ast_from_token_stream(struct Array *);
struct Array *ast_to_token_stream(struct ASTNode *, struct Mempool *);
void ast_free(struct ASTNode *);
struct ASTNode *ast_node_new(struct ASTNode *, enum ASTNodeType, struct ASTNodeLineRange *, int, void *);
void ast_node_print(struct ASTNode *, FILE *);

#define AST_WALK_RECUR(x) \
	if ((x) == AST_WALK_STOP) { \
		return AST_WALK_STOP; \
	}
