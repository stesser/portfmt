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

#include <sys/param.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/stack.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/astbuilder.h"
#include "parser/astbuilder/conditional.h"
#include "parser/astbuilder/enum.h"
#include "parser/astbuilder/target.h"
#include "parser/astbuilder/token.h"
#include "parser/astbuilder/variable.h"
#include "rules.h"

// Prototypes
static int ParserASTBuilderConditionalType_to_ASTExprType(enum ParserASTBuilderConditionalType, enum ASTExprType *);
static int ParserASTBuilderConditionalType_to_ASTIncludeType(enum ParserASTBuilderConditionalType, enum ASTIncludeType *);
static int ParserASTBuilderConditionalType_to_ASTIfType(enum ParserASTBuilderConditionalType, enum ASTIfType *);
static char *split_off_comment(struct Mempool *, struct Array *, ssize_t, ssize_t, struct Array *);
static void token_to_stream(struct Mempool *, struct Array *, enum ParserASTBuilderTokenType, int, struct ASTLineRange *, const char *, const char *, const char *, const char *);
static const char *get_targetname(struct Mempool *, struct ASTTarget *);
static void ast_from_token_stream_flush_comments(struct AST *, struct Array *);
static struct AST *ast_from_token_stream(struct Parser *, struct Array *);
static void ast_to_token_stream(struct AST *, struct Mempool *, struct Array *);

int
ParserASTBuilderConditionalType_to_ASTExprType(enum ParserASTBuilderConditionalType value, enum ASTExprType *retval)
{
	switch (value) {
	case PARSER_AST_BUILDER_CONDITIONAL_ERROR:
		*retval = AST_EXPR_ERROR;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV:
		*retval = AST_EXPR_EXPORT_ENV;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_EXPORT_LITERAL:
		*retval = AST_EXPR_EXPORT_LITERAL;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_EXPORT:
		*retval = AST_EXPR_EXPORT;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INFO:
		*retval = AST_EXPR_INFO;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_UNDEF:
		*retval = AST_EXPR_UNDEF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT_ENV:
		*retval = AST_EXPR_UNEXPORT_ENV;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT:
		*retval = AST_EXPR_UNEXPORT;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_WARNING:
		*retval = AST_EXPR_WARNING;
		return 1;
	default:
		return 0;
	}
}

int
ParserASTBuilderConditionalType_to_ASTIncludeType(enum ParserASTBuilderConditionalType value, enum ASTIncludeType *retval)
{
	switch (value) {
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE:
		*retval = AST_INCLUDE_BMAKE;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL:
		*retval = AST_INCLUDE_OPTIONAL;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_D:
		*retval = AST_INCLUDE_OPTIONAL_D;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_S:
		*retval = AST_INCLUDE_OPTIONAL_S;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX:
		*retval = AST_INCLUDE_POSIX;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL:
		*retval = AST_INCLUDE_POSIX_OPTIONAL;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL_S:
		*retval = AST_INCLUDE_POSIX_OPTIONAL_S;
		return 1;
	default:
		return 0;
	}
}

int
ParserASTBuilderConditionalType_to_ASTIfType(enum ParserASTBuilderConditionalType value, enum ASTIfType *retval)
{
	switch (value) {
	case PARSER_AST_BUILDER_CONDITIONAL_IF:
		*retval = AST_IF_IF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_IFDEF:
		*retval = AST_IF_DEF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_IFMAKE:
		*retval = AST_IF_MAKE;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_IFNDEF:
		*retval = AST_IF_NDEF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE:
		*retval = AST_IF_NMAKE;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
		*retval = AST_IF_IF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF:
		*retval = AST_IF_DEF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF:
		*retval = AST_IF_NDEF;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE:
		*retval = AST_IF_MAKE;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELIFNMAKE:
		*retval = AST_IF_NMAKE;
		return 1;
	case PARSER_AST_BUILDER_CONDITIONAL_ELSE:
		*retval = AST_IF_ELSE;
		return 1;
	default:
		return 0;
	}
}

char *
split_off_comment(struct Mempool *extpool, struct Array *tokens, ssize_t a, ssize_t b, struct Array *words)
{
	SCOPE_MEMPOOL(pool);

	struct Array *commentwords = mempool_array(pool);
	int comments = 0;
	ARRAY_FOREACH_SLICE(tokens, a, b, struct ParserASTBuilderToken *, t) {
		if (comments || is_comment(t->data)) {
			comments = 1;
			array_append(commentwords, t->data);
		} else if (words) {
			array_append(words, str_dup(extpool, t->data));
		}
	}

	if (array_len(commentwords) > 0) {
		return str_join(extpool, commentwords, "");
	} else {
		return NULL;
	}
}

void
token_to_stream(struct Mempool *pool, struct Array *tokens, enum ParserASTBuilderTokenType type, int edited, struct ASTLineRange *lines, const char *data, const char *varname, const char *condname, const char *targetname)
{
	struct ParserASTBuilderToken *t = parser_astbuilder_token_new(type, lines, data, varname, condname, targetname);
	panic_unless(t, "null token?");
	if (t) {
		if (edited) {
			t->edited = 1;
		}
		array_append(tokens, mempool_add(pool, t, parser_astbuilder_token_free));
	}
}

const char *
get_targetname(struct Mempool *pool, struct ASTTarget *target)
{
	switch (target->type) {
	case AST_TARGET_NAMED:
		if (array_len(target->dependencies) > 0) {
			return str_printf(pool, "%s: %s",
				str_join(pool, target->sources, " "),
				str_join(pool, target->dependencies, " "));
		} else {
			return str_printf(pool, "%s:", str_join(pool, target->sources, " "));
		}
		break;
	case AST_TARGET_UNASSOCIATED:
		return "<unassociated>:";
	}

	panic("never reached");
}

struct ParserASTBuilder *
parser_astbuilder_new(struct Parser *parser)
{
	struct ParserASTBuilder *builder = xmalloc(sizeof(struct ParserASTBuilder));
	builder->pool = mempool_new();
	builder->parser = parser;
	builder->tokens = mempool_array(builder->pool);
	builder->lines.a = 1;
	builder->lines.b = 1;
	return builder;
}

struct ParserASTBuilder *
parser_astbuilder_from_ast(struct Parser *parser, struct AST *node)
{
	struct ParserASTBuilder *builder = parser_astbuilder_new(parser);
	ast_to_token_stream(node, builder->pool, builder->tokens);
	return builder;
}

void
parser_astbuilder_free(struct ParserASTBuilder *builder)
{
	if (builder) {
		mempool_free(builder->pool);
		free(builder);
	}
}

void
parser_astbuilder_append_token(struct ParserASTBuilder *builder, enum ParserASTBuilderTokenType type, const char *data)
{
	panic_unless(builder->tokens, "AST was already built");
	struct ParserASTBuilderToken *t = parser_astbuilder_token_new(type, &builder->lines, data, builder->varname, builder->condname, builder->targetname);
	if (t == NULL) {
		if (builder->parser) {
			parser_set_error(builder->parser, PARSER_ERROR_EXPECTED_TOKEN, ParserASTBuilderTokenType_human(type));
		}
		return;
	}
	mempool_add(builder->pool, t, parser_astbuilder_token_free);
	array_append(builder->tokens, t);
}

struct AST *
parser_astbuilder_finish(struct ParserASTBuilder *builder)
{
	panic_unless(builder->tokens, "AST was already built");
	struct AST *root = ast_from_token_stream(builder->parser, builder->tokens);
	mempool_release_all(builder->pool);
	builder->tokens = NULL;
	return root;
}

void
ast_from_token_stream_flush_comments(struct AST *parent, struct Array *comments)
{
	if (array_len(comments) == 0) {
		return;
	}

	struct AST *node = ast_new(parent->pool, AST_COMMENT, &((struct ParserASTBuilderToken *)array_get(comments, 0))->lines, &(struct ASTComment){
		.type = AST_COMMENT_LINE,
	});
	ast_parent_append_sibling(parent, node, 0);

	ARRAY_FOREACH(comments, struct ParserASTBuilderToken *, t) {
		node->edited = node->edited || t->edited;
		array_append(node->comment.lines, str_dup(node->pool, t->data));
		node->line_start.b = t->lines.b;
	}

	array_truncate(comments);
}

struct AST *
ast_from_token_stream(struct Parser *parser, struct Array *tokens)
{
	SCOPE_MEMPOOL(pool);

	struct AST *root = ast_new(mempool_new(), AST_ROOT, NULL, NULL);
	struct Array *current_cond = mempool_array(pool);
	struct Array *current_comments = mempool_array(pool);
	struct Array *current_target_cmds = mempool_array(pool);
	struct Array *current_var = mempool_array(pool);
	struct Stack *ifstack = mempool_stack(pool);
	struct Stack *nodestack = mempool_stack(pool);
	stack_push(nodestack, root);

	ARRAY_FOREACH(tokens, struct ParserASTBuilderToken *, t) {
		if (stack_len(nodestack) == 0) {
			parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
					 str_printf(pool, "node exhausted on line %zu-%zu", t->lines.a, t->lines.b));
			return NULL;
		}
		if (t->type != PARSER_AST_BUILDER_TOKEN_COMMENT) {
			ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);
		}
		switch (t->type) {
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START:
			array_truncate(current_cond);
			break;
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN:
			array_append(current_cond, t);
			break;
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END: {
			enum ParserASTBuilderConditionalType condtype = t->conditional.type;
			switch (condtype) {
			case PARSER_AST_BUILDER_CONDITIONAL_INVALID:
				panic("got invalid conditional");
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_D:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_S:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL_S: {
				unless (array_len(current_cond) > 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "%s has no tokens",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				enum ASTIncludeType type;
				unless (ParserASTBuilderConditionalType_to_ASTIncludeType(condtype, &type)) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "cannot map %s to ASTIncludeType",
								ParserASTBuilderConditionalType_tostring(condtype)));
				}
				struct AST *node = ast_new(root->pool, AST_INCLUDE, &t->lines, &(struct ASTInclude){
					.type = type,
					.indent = ((struct ParserASTBuilderToken *)array_get(current_cond, 0))->conditional.indent,
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = t->edited;
				struct Array *pathwords = mempool_array(pool);
				node->include.comment = split_off_comment(node->pool, current_cond, 1, -1, pathwords);
				if (array_len(pathwords) == 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "missing path for %s on %s",
								ASTIncludeType_identifier(node->include.type),
								ast_line_range_tostring(&t->lines, 1, pool)));
					return NULL;
				}
				char *path = str_trim(node->pool, str_join(pool, pathwords, " "));
				int invalid = 0;
				const char *hint = "";
				if (*ASTIncludeType_identifier(node->include.type) == '.') {
					if (*path == '<') {
						node->include.sys = 1;
						path++;
						if (str_endswith(path, ">")) {
							path[strlen(path) - 1] = 0;
						} else {
							invalid = 1;
							hint = ": missing > at the end";
						}
					} else if (*path == '"') {
						path++;
						if (str_endswith(path, "\"")) {
							path[strlen(path) - 1] = 0;
						} else {
							invalid = 1;
							hint = ": missing \" at the end";
						}
					} else {
						invalid = 1;
						hint = ": must start with < or \"";
					}
				}
				if (strlen(path) == 0 || invalid) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "invalid path for %s on %s%s",
								ASTIncludeType_identifier(node->include.type),
								ast_line_range_tostring(&t->lines, 1, pool),
								hint));
					return NULL;
				} else {
					node->include.path = path;
				}
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_ERROR:
			case PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV:
			case PARSER_AST_BUILDER_CONDITIONAL_EXPORT_LITERAL:
			case PARSER_AST_BUILDER_CONDITIONAL_EXPORT:
			case PARSER_AST_BUILDER_CONDITIONAL_INFO:
			case PARSER_AST_BUILDER_CONDITIONAL_UNDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT_ENV:
			case PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT:
			case PARSER_AST_BUILDER_CONDITIONAL_WARNING: {
				unless (array_len(current_cond) > 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "%s has no tokens",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				enum ASTExprType type;
				unless (ParserASTBuilderConditionalType_to_ASTExprType(condtype, &type)) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "cannot map %s to ASTExprType",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				struct AST *node = ast_new(root->pool, AST_EXPR, &t->lines, &(struct ASTExpr){
					.type = type,
					.indent = ((struct ParserASTBuilderToken *)array_get(current_cond, 0))->conditional.indent,
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = t->edited;
				node->expr.comment = split_off_comment(node->pool, current_cond, 1, -1, node->expr.words);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_FOR: {
				unless (array_len(current_cond) > 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "%s has no tokens",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				struct AST *node = ast_new(root->pool, AST_FOR, &t->lines, &(struct ASTFor){
					.indent = ((struct ParserASTBuilderToken *)array_get(current_cond, 0))->conditional.indent,
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = t->edited;
				size_t word_start = 1;
				ARRAY_FOREACH_SLICE(current_cond, 1, -1, struct ParserASTBuilderToken *, t) {
					if (t->edited) {
						node->edited = 1;
					}
					if (strcmp(t->data, "in") == 0) {
						word_start = t_index + 1;
						break;
					} else {
						array_append(node->forexpr.bindings, str_dup(node->pool, t->data));
					}
				}
				node->forexpr.comment = split_off_comment(node->pool, current_cond, word_start, -1, node->forexpr.words);
				stack_push(nodestack, node);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_ENDFOR: {
				struct AST *node = stack_pop(nodestack);
				if (node->type == AST_TARGET) {
					node = stack_pop(nodestack);
				}
				unless (node->type == AST_FOR) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
						str_printf(pool, "could not find matching .for for .endfor on line %zu-%zu", t->lines.a, t->lines.b));
					return NULL;
				}
				node->line_end = t->lines;
				node->forexpr.end_comment = split_off_comment(node->pool, current_cond, 1, -1, NULL);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_IF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_IFNDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFNMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_ELSE: {
				unless (array_len(current_cond) > 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "%s has no tokens",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				struct AST *parent = stack_peek(nodestack);
				struct AST *ifparent = NULL;
				switch (condtype) {
				case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFNMAKE:
				case PARSER_AST_BUILDER_CONDITIONAL_ELSE:
					ifparent = stack_peek(ifstack);
					break;
				default:
					ifparent = NULL;
					break;
				}
				if (ifparent) {
					parent = stack_peek(ifstack);
				}
				enum ASTIfType type;
				unless (ParserASTBuilderConditionalType_to_ASTIfType(condtype, &type)) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
							str_printf(pool, "cannot map %s to ASTIfType",
								ParserASTBuilderConditionalType_tostring(condtype)));
					return NULL;
				}
				struct AST *node = ast_new(root->pool, AST_IF, &t->lines, &(struct ASTIf){
					.type = type,
					.indent = ((struct ParserASTBuilderToken *)array_get(current_cond, 0))->conditional.indent,
					.ifparent = ifparent,
				});
				ast_parent_append_sibling(parent, node, ifparent != NULL);
				node->edited = t->edited;
				ARRAY_FOREACH_SLICE(current_cond, 1, -1, struct ParserASTBuilderToken *, t) {
					if (t->edited) {
						node->edited = 1;
					}
					array_append(node->ifexpr.test, str_dup(node->pool, t->data));
				}
				switch (type) {
				case AST_IF_IF:
				case AST_IF_DEF:
				case AST_IF_MAKE:
				case AST_IF_NDEF:
				case AST_IF_NMAKE:
					if (array_len(node->ifexpr.test) == 0) {
						parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
								str_printf(pool, "%s with no words in test expression", ASTIfType_human(type)));
						return NULL;
					}
					break;
				case AST_IF_ELSE:
					break;
				}
				stack_push(ifstack, node);
				stack_push(nodestack, node);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_ENDIF: {
				if (stack_len(ifstack) == 0) {
					parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED,
						str_printf(pool, "could not find matching .if for .endif on line %zu-%zu", t->lines.a, t->lines.b));
					return NULL;
				}
				struct AST *ifnode = stack_pop(ifstack);
				while (ifnode && ifnode->ifexpr.ifparent && (ifnode = stack_pop(ifstack)));
				if (ifnode) {
					ifnode->line_end = t->lines;
					if (ifnode->type == AST_IF) {
						ifnode->ifexpr.end_comment = split_off_comment(ifnode->pool, current_cond, 1, -1, NULL);
					}
				}
				struct AST *node = NULL;
				while ((node = stack_pop(nodestack)) && node != ifnode);
				break;
			} }
			break;
		}

		case PARSER_AST_BUILDER_TOKEN_TARGET_START: {
			// When a new target starts the old one if
			// any has ended.  Also see TARGET_END.
			struct AST *node = stack_peek(nodestack);
			if (node->type == AST_TARGET) {
				stack_pop(nodestack);
			}

			node = ast_new(root->pool, AST_TARGET, &t->lines, &(struct ASTTarget){
				.type = AST_TARGET_NAMED,
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = t->edited;
			if (t->target.comment) {
				node->target.comment = str_dup(node->pool, t->target.comment);
			}
			ARRAY_FOREACH(t->target.sources, const char *, source) {
				array_append(node->target.sources, str_dup(node->pool, source));
			}
			ARRAY_FOREACH(t->target.dependencies, const char *, dependency) {
				array_append(node->target.dependencies, str_dup(node->pool, dependency));
			}
			stack_push(nodestack, node);
			break;
		} case PARSER_AST_BUILDER_TOKEN_TARGET_END: {
			// We might have a token stream like this,
			// so we need to be careful what we pop
			// from the nodestack.
			// target-start                8 bar bar:
			// conditional-start           9 .endif -
			// conditional-token           9 .endif .endif
			// conditional-end             9 .endif -
			// target-end                 10 - -
			struct AST *node = stack_peek(nodestack);
			if (node->type == AST_TARGET) {
				stack_pop(nodestack);
			}
			break;
		}

		case PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START:
			array_truncate(current_target_cmds);
			break;
		case PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN:
			array_append(current_target_cmds, t);
			break;
		case PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END: {
			struct ASTTarget *target = NULL;
			// Find associated target if any
			for (struct AST *cur = stack_peek(nodestack); cur && cur != root; cur = cur->parent) {
				if (cur->type == AST_TARGET) {
					target = &cur->target;
					break;
				}
			}
			unless (target) { // Inject new unassociated target
				struct AST *node = ast_new(root->pool, AST_TARGET, &t->lines, &(struct ASTTarget){
					.type = AST_TARGET_UNASSOCIATED,
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = t->edited;
				stack_push(nodestack, node);
				target = &node->target;
			}

			//panic_unless(target, "unassociated target command on input line %zu-%zu", token_lines(t)->start, token_lines(t)->end);
			struct AST *node = ast_new(root->pool, AST_TARGET_COMMAND, &t->lines, &(struct ASTTargetCommand){
				.target = target,
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = t->edited;
			node->line_end = node->line_start;
			size_t start = 0;
			struct ParserASTBuilderToken *first = array_get(current_target_cmds, 0);
			if (first && first->data) {
				for (; *first->data; first->data++) {
					switch (*first->data) {
					case '@':
						node->targetcommand.flags |= AST_TARGET_COMMAND_FLAG_SILENT;
						continue;
					case '-':
						node->targetcommand.flags |= AST_TARGET_COMMAND_FLAG_IGNORE_ERROR;
						continue;
					case '+':
						node->targetcommand.flags |= AST_TARGET_COMMAND_FLAG_ALWAYS_EXECUTE;
						continue;
					}
					break;
				}
				if (strcmp(first->data, "") == 0) {
					start = 1;
				}
			}
			node->targetcommand.comment = split_off_comment(node->pool, current_target_cmds, start, -1, node->targetcommand.words);
			if (array_len(current_target_cmds) > 1) {
				node->line_start = ((struct ParserASTBuilderToken *)array_get(current_target_cmds, 1))->lines;
			}
			break;
		}

		case PARSER_AST_BUILDER_TOKEN_VARIABLE_START:
			array_truncate(current_var);
			array_append(current_var, t); // XXX: Hack to make the old refactor_collapse_adjacent_variables work correctly...
			break;
		case PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN:
			array_append(current_var, t);
			break;
		case PARSER_AST_BUILDER_TOKEN_VARIABLE_END: {
			unless (array_len(current_var) > 0) {
				parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED, "variable has no tokens");
				return NULL;
			}
			struct AST *node = ast_new(root->pool, AST_VARIABLE, &t->lines, &(struct ASTVariable){
				.name = t->variable.name,
				.modifier = ((struct ParserASTBuilderToken *)array_get(current_var, 0))->variable.modifier,
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = t->edited;
			node->line_end = node->line_start;
			node->variable.comment = split_off_comment(node->pool, current_var, 1, -1, node->variable.words);
			if (array_len(current_var) > 1) {
				node->line_start = ((struct ParserASTBuilderToken *)array_get(current_var, 1))->lines;
			}
			break;
		} case PARSER_AST_BUILDER_TOKEN_COMMENT:
			array_append(current_comments, t);
			break;
		}
	}

	ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);

	if (stack_pop(nodestack) != root) {
		parser_set_error(parser, PARSER_ERROR_AST_BUILD_FAILED, str_printf(pool, "node stack not exhausted: missing .endif/.endfor?"));
		return NULL;
	} else {
		return root;
	}
}

void
ast_to_token_stream(struct AST *node, struct Mempool *extpool, struct Array *tokens)
{
	SCOPE_MEMPOOL(pool);

	switch (node->type) {
	case AST_ROOT:
		ARRAY_FOREACH(node->root.body, struct AST *, child) {
			ast_to_token_stream(child, extpool, tokens);
		}
		break;
	case AST_DELETED:
		break;
	case AST_COMMENT: {
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			struct ParserASTBuilderToken *t = parser_astbuilder_token_new_comment(&node->line_start, line, PARSER_AST_BUILDER_CONDITIONAL_INVALID);
			t->edited = node->edited;
			array_append(tokens, mempool_add(extpool, t, parser_astbuilder_token_free));
		}
		break;
	} case AST_EXPR: {
		const char *indent = str_repeat(pool, " ", node->expr.indent);
		const char *data = str_printf(pool, ".%s%s", indent, ASTExprType_identifier(node->expr.type) + 1);
		const char *exprname = ASTExprType_identifier(node->expr.type);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_start, data, NULL, exprname, NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, exprname, NULL);
		ARRAY_FOREACH(node->expr.words, const char *, word) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, exprname, NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_start, data, NULL, exprname, NULL);
		break;
	} case AST_IF: {
		const char *indent = str_repeat(pool, " ", node->ifexpr.indent);
		const char *prefix  = "";
		if (node->ifexpr.ifparent && node->ifexpr.type != AST_IF_ELSE) {
			prefix = "el";
		}
		const char *ifname = str_printf(pool, "%s%s", prefix, ASTIfType_human(node->ifexpr.type));
		const char *ifnamedot = str_printf(pool, ".%s", ifname);
		const char *data = str_printf(pool, ".%s%s", indent, ifname);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);
		ARRAY_FOREACH(node->ifexpr.test, const char *, word) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, ifnamedot, NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);

		ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
			ast_to_token_stream(child, extpool, tokens);
		}

		if (array_len(node->ifexpr.orelse) > 0) {
			struct AST *next = array_get(node->ifexpr.orelse, 0);
			if (next && next->type == AST_IF && next->ifexpr.type == AST_IF_ELSE) {
				data = str_printf(pool, ".%selse", indent);
				token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, next->edited, &next->line_start, data, NULL, ".else", NULL);
				token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, next->edited, &next->line_start, data, NULL, ".else", NULL);
				token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, next->edited, &next->line_start, data, NULL, ".else", NULL);
				ARRAY_FOREACH(next->ifexpr.body, struct AST *, child) {
					ast_to_token_stream(child, extpool, tokens);
				}
			} else {
				ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
					ast_to_token_stream(child, extpool, tokens);
				}
			}
		}

		unless (node->ifexpr.ifparent) {
			data = str_printf(pool, ".%sendif", indent);
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_end, data, NULL, ".endif", NULL);
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_end, data, NULL, ".endif", NULL);
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_end, data, NULL, ".endif", NULL);
		}
		break;
	} case AST_FOR: {
		const char *indent = str_repeat(pool, " ", node->forexpr.indent);
		const char *data = str_printf(pool, ".%sfor", indent);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_start, data, NULL, ".for", NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, ".for", NULL);
		ARRAY_FOREACH(node->forexpr.bindings, const char *, binding) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, binding, NULL, ".for", NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, "in", NULL, ".for", NULL);
		ARRAY_FOREACH(node->forexpr.words, const char *, word) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, ".for", NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_start, data, NULL, ".for", NULL);

		ARRAY_FOREACH(node->forexpr.body, struct AST *, child) {
			ast_to_token_stream(child, extpool, tokens);
		}

		data = str_printf(pool, ".%sendfor", indent);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		break;
	} case AST_INCLUDE: {
		const char *exprname = ASTIncludeType_identifier(node->include.type);
		const char *data = exprname;
		if (*exprname == '.') {
			const char *indent = str_repeat(pool, " ", node->include.indent);
			data = str_printf(pool, ".%s%s", indent, exprname + 1);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, node->edited, &node->line_start, data, NULL, exprname, NULL);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, exprname, NULL);
		if (node->include.path) {
			const char *path;
			if (node->include.sys) {
				path = str_printf(pool, "<%s>", node->include.path);
			} else {
				path = str_printf(pool, "\"%s\"", node->include.path);
			}
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, path, NULL, exprname, NULL);
		}
		if (node->include.comment && strlen(node->include.comment) > 0) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, node->edited, &node->line_start, node->include.comment, NULL, exprname, NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, node->edited, &node->line_start, data, NULL, exprname, NULL);
		break;
	} case AST_TARGET: {
		const char *targetname = get_targetname(pool, &node->target);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_START, node->edited, &node->line_start, targetname, NULL, NULL, targetname);
		ARRAY_FOREACH(node->target.body, struct AST *, child) {
			ast_to_token_stream(child, extpool, tokens);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_END, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		break;
	} case AST_TARGET_COMMAND: {
		const char *targetname = get_targetname(pool, node->targetcommand.target);
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		struct Array *flag_tokens = mempool_array(pool);
		if (node->targetcommand.flags & AST_TARGET_COMMAND_FLAG_SILENT) {
			array_append(flag_tokens, ASTTargetCommandFlag_human(AST_TARGET_COMMAND_FLAG_SILENT));
		}
		if (node->targetcommand.flags & AST_TARGET_COMMAND_FLAG_IGNORE_ERROR) {
			array_append(flag_tokens, ASTTargetCommandFlag_human(AST_TARGET_COMMAND_FLAG_IGNORE_ERROR));
		}
		if (node->targetcommand.flags & AST_TARGET_COMMAND_FLAG_ALWAYS_EXECUTE) {
			array_append(flag_tokens, ASTTargetCommandFlag_human(AST_TARGET_COMMAND_FLAG_ALWAYS_EXECUTE));
		}
		if (array_len(node->targetcommand.words) == 0 && array_len(flag_tokens) > 0) {
			const char *flags = str_join(pool, flag_tokens, "");
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN, node->edited, &node->line_start, flags, NULL, NULL, targetname);
		} else {
			ARRAY_FOREACH(node->targetcommand.words, const char *, word) {
				if (word_index == 0 && array_len(flag_tokens) > 0) {
					array_append(flag_tokens, word);
					word = str_join(pool, flag_tokens, "");
				}
				token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN, node->edited, &node->line_start, word, NULL, NULL, targetname);
			}
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		break;
	} case AST_VARIABLE: {
		const char *space = "";
		if (str_endswith(node->variable.name, "+")) {
			space = " ";
		}
		const char *varname = str_printf(pool, "%s%s%s", node->variable.name, space, ASTVariableModifier_human(node->variable.modifier));
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_VARIABLE_START, node->edited, &node->line_start, NULL, varname, NULL, NULL);
		ARRAY_FOREACH(node->variable.words, const char *, word) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN, node->edited, &node->line_start, word, varname, NULL, NULL);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_VARIABLE_END, node->edited, &node->line_end, NULL, varname, NULL, NULL);
		break;
	} }
}

void
parser_astbuilder_print_token_stream(struct ParserASTBuilder *builder, FILE *f)
{
	SCOPE_MEMPOOL(pool);

	struct Array *tokens = builder->tokens;
	size_t maxvarlen = 0;
	ARRAY_FOREACH(tokens, struct ParserASTBuilderToken *, o) {
		if (o->type == PARSER_AST_BUILDER_TOKEN_VARIABLE_START && o->variable.name) {
			maxvarlen = MAX(maxvarlen, strlen(o->variable.name) + strlen(ASTVariableModifier_human(o->variable.modifier)));
			if (str_endswith(o->variable.name, "+")) {
				maxvarlen += 1;
			}
		}
	}
	struct Array *vars = mempool_array(pool);
	ARRAY_FOREACH(tokens, struct ParserASTBuilderToken *, t) {
		const char *type = ParserASTBuilderTokenType_human(t->type);
		if (t->variable.name &&
		    (t->type == PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN ||
		     t->type == PARSER_AST_BUILDER_TOKEN_VARIABLE_START ||
		     t->type == PARSER_AST_BUILDER_TOKEN_VARIABLE_END)) {
			const char *sep = "";
			if (str_endswith(t->variable.name, "+")) {
				sep = " ";
			}
			array_append(vars, str_printf(pool, "%s%s%s", t->variable.name, sep, ASTVariableModifier_human(t->variable.modifier)));
		} else if (t->conditional.type != PARSER_AST_BUILDER_CONDITIONAL_INVALID &&
			   (t->type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END ||
			    t->type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START ||
			    t->type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN)) {
			array_append(vars, ParserASTBuilderConditionalType_human(t->conditional.type));
		} else if (t->target.sources && t->type == PARSER_AST_BUILDER_TOKEN_TARGET_START) {
			ARRAY_FOREACH(t->target.sources, const char *, name) {
				array_append(vars, str_dup(pool, name));
			}
			ARRAY_FOREACH(t->target.dependencies, const char *, dep) {
				array_append(vars, str_printf(pool, "->%s", dep));
			}
		} else if (t->target.sources &&
			   (t->type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END ||
			    t->type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START ||
			    t->type == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN ||
			    t->type == PARSER_AST_BUILDER_TOKEN_TARGET_START ||
			    t->type == PARSER_AST_BUILDER_TOKEN_TARGET_END)) {
			array_append(vars, str_dup(pool, "-"));
		} else {
			array_append(vars, str_dup(pool, "-"));
		}

		ARRAY_FOREACH(vars, char *, var) {
			ssize_t len = maxvarlen - strlen(var);
			const char *range = ast_line_range_tostring(&t->lines, 0, pool);
			char *tokentype;
			if (array_len(vars) > 1) {
				tokentype = str_printf(pool, "%s#%zu", type, var_index + 1);
			} else {
				tokentype = str_dup(pool, type);
			}
			fprintf(f, "%-20s %8s %s", tokentype, range, var);

			if (len > 0) {
				fputs(str_repeat(pool, " ", len), f);
			}
			fputs(" ", f);

			if (t->data &&
			    (t->type != PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START &&
			     t->type != PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END)) {
				fputs(t->data, f);
			} else {
				fputs("-", f);
			}
			fprintf(f, "\n");
		}
		array_truncate(vars);
	}
}
