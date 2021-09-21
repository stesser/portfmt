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
#include <regex.h>
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

static enum ASTExprType conditional_to_expr[] = {
	[PARSER_AST_BUILDER_CONDITIONAL_ERROR] = AST_EXPR_ERROR,
	[PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV] = AST_EXPR_EXPORT_ENV,
	[PARSER_AST_BUILDER_CONDITIONAL_EXPORT_LITERAL] = AST_EXPR_EXPORT_LITERAL,
	[PARSER_AST_BUILDER_CONDITIONAL_EXPORT] = AST_EXPR_EXPORT,
	[PARSER_AST_BUILDER_CONDITIONAL_INFO] = AST_EXPR_INFO,
	[PARSER_AST_BUILDER_CONDITIONAL_UNDEF] = AST_EXPR_UNDEF,
	[PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT_ENV] = AST_EXPR_UNEXPORT_ENV,
	[PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT] = AST_EXPR_UNEXPORT,
	[PARSER_AST_BUILDER_CONDITIONAL_WARNING] = AST_EXPR_WARNING,
};

static enum ASTIncludeType conditional_to_include[] = {
	[PARSER_AST_BUILDER_CONDITIONAL_DINCLUDE] = AST_INCLUDE_D,
	[PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX] = AST_INCLUDE_POSIX,
	[PARSER_AST_BUILDER_CONDITIONAL_INCLUDE] = AST_INCLUDE_BMAKE,
	[PARSER_AST_BUILDER_CONDITIONAL_SINCLUDE] = AST_INCLUDE_S,
};

static enum ASTIfType ConditionalType_to_ASTIfType[] = {
	[PARSER_AST_BUILDER_CONDITIONAL_IF] = AST_IF_IF,
	[PARSER_AST_BUILDER_CONDITIONAL_IFDEF] = AST_IF_DEF,
	[PARSER_AST_BUILDER_CONDITIONAL_IFMAKE] = AST_IF_MAKE,
	[PARSER_AST_BUILDER_CONDITIONAL_IFNDEF] = AST_IF_NDEF,
	[PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE] = AST_IF_NMAKE,
	[PARSER_AST_BUILDER_CONDITIONAL_ELIF] = AST_IF_IF,
	[PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF] = AST_IF_DEF,
	[PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE] = AST_IF_MAKE,
	[PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF] = AST_IF_NDEF,
	[PARSER_AST_BUILDER_CONDITIONAL_ELSE] = AST_IF_ELSE,
};

static void ast_to_token_stream(struct AST *, struct Mempool *, struct Array *);

static size_t
cond_indent(const char *word)
{
	word++; // Skip .
	size_t indent = 0;
	for (; isspace(*word); word++, indent++);
	return indent;
}

static char *
range_tostring(struct Mempool *pool, struct ASTLineRange *range)
{
	panic_unless(range, "range_tostring() is not NULL-safe");
	panic_unless(range->a < range->b, "range is inverted");

	if (range->a == range->b - 1) {
		return str_printf(pool, "%zu", range->a);
	} else {
		return str_printf(pool, "%zu-%zu", range->a, range->b - 1);
	}
}

static char *
split_off_comment(struct Mempool *extpool, struct Array *tokens, ssize_t a, ssize_t b, struct Array *words)
{
	SCOPE_MEMPOOL(pool);

	struct Array *commentwords = mempool_array(pool);
	int comments = 0;
	ARRAY_FOREACH_SLICE(tokens, a, b, struct Token *, t) {
		if (comments || is_comment(token_data(t))) {
			comments = 1;
			array_append(commentwords, token_data(t));
		} else if (words) {
			array_append(words, str_dup(extpool, token_data(t)));
		}
	}

	if (array_len(commentwords) > 0) {
		return str_join(extpool, commentwords, "");
	} else {
		return NULL;
	}
}

static void
token_to_stream(struct Mempool *pool, struct Array *tokens, enum ParserASTBuilderTokenType type, int edited, struct ASTLineRange *lines, const char *data, const char *varname, const char *condname, const char *targetname)
{
	struct Token *t = token_new(type, lines, data, varname, condname, targetname);
	panic_unless(t, "null token?");
	if (t) {
		if (edited) {
			token_mark_edited(t);
		}
		array_append(tokens, mempool_add(pool, t, token_free));
	}
}

static const char *
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
		break;
	}
}

struct ParserASTBuilder *
parser_astbuilder_new(struct Parser *parser)
{
	struct Mempool *pool = mempool_new();
	struct ParserASTBuilder *builder = mempool_alloc(pool, sizeof(struct ParserASTBuilder));
	builder->parser = parser;
	builder->tokens = mempool_array(pool);
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
	}
}

void
parser_astbuilder_append_token(struct ParserASTBuilder *builder, enum ParserASTBuilderTokenType type, const char *data)
{
	panic_unless(builder->tokens, "AST was already built");
	struct Token *t = token_new(type, &builder->lines, data, builder->varname, builder->condname, builder->targetname);
	if (t == NULL) {
		if (builder->parser) {
			parser_set_error(builder->parser, PARSER_ERROR_EXPECTED_TOKEN, ParserASTBuilderTokenType_humanize[type]);
		}
		return;
	}
	mempool_add(builder->pool, t, token_free);
	array_append(builder->tokens, t);
}

struct AST *
parser_astbuilder_finish(struct ParserASTBuilder *builder)
{
	panic_unless(builder->tokens, "AST was already built");
	struct AST *root = ast_from_token_stream(builder->tokens);
	mempool_release_all(builder->pool);
	builder->tokens = NULL;
	return root;
}

static void
ast_from_token_stream_flush_comments(struct AST *parent, struct Array *comments)
{
	if (array_len(comments) == 0) {
		return;
	}

	struct AST *node = ast_new(parent->pool, AST_COMMENT, token_lines(array_get(comments, 0)), &(struct ASTComment){
		.type = AST_COMMENT_LINE,
	});
	ast_parent_append_sibling(parent, node, 0);

	ARRAY_FOREACH(comments, struct Token *, t) {
		node->edited = node->edited || token_edited(t);
		array_append(node->comment.lines, str_dup(node->pool, token_data(t)));
		struct ASTLineRange *range = token_lines(t);
		node->line_start.b = range->b;
	}

	array_truncate(comments);
}

struct AST *
ast_from_token_stream(struct Array *tokens)
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

	ARRAY_FOREACH(tokens, struct Token *, t) {
		if (stack_len(nodestack) == 0) {
			panic("node stack exhausted at token on input line %zu-%zu", token_lines(t)->a, token_lines(t)->b);
		}
		if (token_type(t) != PARSER_AST_BUILDER_TOKEN_COMMENT) {
			ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);
		}
		switch (token_type(t)) {
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START:
			array_truncate(current_cond);
			break;
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN:
			array_append(current_cond, t);
			break;
		case PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END: {
			enum ParserASTBuilderConditionalType condtype = token_conditional(t);
			switch (condtype) {
			case PARSER_AST_BUILDER_CONDITIONAL_INVALID:
				panic("got invalid conditional");
			case PARSER_AST_BUILDER_CONDITIONAL_DINCLUDE:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX:
			case PARSER_AST_BUILDER_CONDITIONAL_INCLUDE:
			case PARSER_AST_BUILDER_CONDITIONAL_SINCLUDE: {
				struct AST *node = ast_new(root->pool, AST_INCLUDE, token_lines(t), &(struct ASTInclude){
					.type = conditional_to_include[condtype],
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = token_edited(t);
				struct Array *pathwords = mempool_array(pool);
				node->include.comment = split_off_comment(node->pool, current_cond, 1, -1, pathwords);
				if (array_len(pathwords) > 0) {
					char *path = str_trim(node->pool, str_join(pool, pathwords, " "));
					if (*path == '<') {
						node->include.sys = 1;
						path++;
						if (str_endswith(path, ">")) {
							path[strlen(path) - 1] = 0;
						}
					} else if (*path == '"') {
						path++;
						if (str_endswith(path, "\"")) {
							path[strlen(path) - 1] = 0;
						}
					}
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
				struct AST *node = ast_new(root->pool, AST_EXPR, token_lines(t), &(struct ASTExpr){
					.type = conditional_to_expr[condtype],
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = token_edited(t);
				node->expr.comment = split_off_comment(node->pool, current_cond, 1, -1, node->expr.words);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_FOR: {
				struct AST *node = ast_new(root->pool, AST_FOR, token_lines(t), &(struct ASTFor){
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = token_edited(t);
				size_t word_start = 1;
				ARRAY_FOREACH_SLICE(current_cond, 1, -1, struct Token *, t) {
					if (token_edited(t)) {
						node->edited = 1;
					}
					if (strcmp(token_data(t), "in") == 0) {
						word_start = t_index + 1;
						break;
					} else {
						array_append(node->forexpr.bindings, str_dup(node->pool, token_data(t)));
					}
				}
				node->forexpr.comment = split_off_comment(node->pool, current_cond, word_start, -1, node->forexpr.words);
				stack_push(nodestack, node);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_ENDFOR: {
				struct AST *node = stack_pop(nodestack);
				node->line_end = *token_lines(t);
				node->forexpr.end_comment = split_off_comment(node->pool, current_cond, 1, -1, NULL);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_IF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_IFNDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE:
			case PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF:
			case PARSER_AST_BUILDER_CONDITIONAL_ELSE: {
				struct AST *parent = stack_peek(nodestack);
				struct AST *ifparent = NULL;
				switch (condtype) {
				case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE:
				case PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF:
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
				struct AST *node = ast_new(root->pool, AST_IF, token_lines(t), &(struct ASTIf){
					.type = ConditionalType_to_ASTIfType[condtype],
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
					.ifparent = ifparent,
				});
				ast_parent_append_sibling(parent, node, ifparent != NULL);
				node->edited = token_edited(t);
				ARRAY_FOREACH_SLICE(current_cond, 1, -1, struct Token *, t) {
					if (token_edited(t)) {
						node->edited = 1;
					}
					array_append(node->ifexpr.test, str_dup(node->pool, token_data(t)));
				}
				stack_push(ifstack, node);
				stack_push(nodestack, node);
				break;
			} case PARSER_AST_BUILDER_CONDITIONAL_ENDIF: {
				struct AST *ifnode = stack_pop(ifstack);
				while (ifnode && ifnode->ifexpr.ifparent && (ifnode = stack_pop(ifstack)));
				if (ifnode) {
					ifnode->line_end = *token_lines(t);
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

			node = ast_new(root->pool, AST_TARGET, token_lines(t), &(struct ASTTarget){
				.type = AST_TARGET_NAMED,
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			struct Target *target = token_target(t);
			if (target_comment(target)) {
				node->target.comment = str_dup(node->pool, target_comment(target));
			}
			ARRAY_FOREACH(target_names(target), const char *, source) {
				array_append(node->target.sources, str_dup(node->pool, source));
			}
			ARRAY_FOREACH(target_dependencies(target), const char *, dependency) {
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
				struct AST *node = ast_new(root->pool, AST_TARGET, token_lines(t), &(struct ASTTarget){
					.type = AST_TARGET_UNASSOCIATED,
				});
				ast_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = token_edited(t);
				stack_push(nodestack, node);
				target = &node->target;
			}

			//panic_unless(target, "unassociated target command on input line %zu-%zu", token_lines(t)->start, token_lines(t)->end);
			struct AST *node = ast_new(root->pool, AST_TARGET_COMMAND, token_lines(t), &(struct ASTTargetCommand){
				.target = target,
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			node->line_end = node->line_start;
			node->targetcommand.comment = split_off_comment(node->pool, current_target_cmds, 0, -1, node->targetcommand.words);
			if (array_len(current_target_cmds) > 1) {
				node->line_start = *token_lines(array_get(current_target_cmds, 1));
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
			struct ASTLineRange *range = array_get(current_var, 0);
			unless (range) {
				range = token_lines(t);
			}
			struct AST *node = ast_new(root->pool, AST_VARIABLE, token_lines(t), &(struct ASTVariable){
				.name = variable_name(token_variable(t)),
				.modifier = variable_modifier(token_variable(array_get(current_var, 0))),
			});
			ast_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			node->line_end = node->line_start;
			node->variable.comment = split_off_comment(node->pool, current_var, 1, -1, node->variable.words);
			if (array_len(current_var) > 1) {
				node->line_start = *token_lines(array_get(current_var, 1));
			}
			break;
		} case PARSER_AST_BUILDER_TOKEN_COMMENT:
			array_append(current_comments, t);
			break;
		}
	}

	ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);

	return root;
}

static void
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
			struct Token *t = token_new_comment(&node->line_start, line, PARSER_AST_BUILDER_CONDITIONAL_INVALID);
			if (node->edited) {
				token_mark_edited(t);
			}
			array_append(tokens, mempool_add(extpool, t, token_free));
		}
		break;
	} case AST_EXPR: {
		const char *indent = str_repeat(pool, " ", node->expr.indent);
		const char *data = str_printf(pool, ".%s%s", indent, ASTExprType_identifier[node->expr.type]);
		const char *exprname = str_printf(pool, ".%s", ASTExprType_identifier[node->expr.type]);
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
		const char *ifname = str_printf(pool, "%s%s", prefix, ASTIfType_humanize[node->ifexpr.type]);
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
		const char *dot;
		switch (node->include.type) {
		case AST_INCLUDE_POSIX:
			dot = "";
			break;
		default:
			dot = ".";
			break;
		}
		const char *indent = str_repeat(pool, " ", node->include.indent);
		const char *data = str_printf(pool, "%s%s%s", dot, indent, ASTIncludeType_identifier[node->include.type]);
		const char *exprname = str_printf(pool, "%s%s", dot, ASTIncludeType_identifier[node->include.type]);
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
		ARRAY_FOREACH(node->targetcommand.words, const char *, word) {
			token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN, node->edited, &node->line_start, word, NULL, NULL, targetname);
		}
		token_to_stream(extpool, tokens, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		break;
	} case AST_VARIABLE: {
		const char *space = "";
		if (str_endswith(node->variable.name, "+")) {
			space = " ";
		}
		const char *varname = str_printf(pool, "%s%s%s", node->variable.name, space, ASTVariableModifier_humanize[node->variable.modifier]);
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
	ARRAY_FOREACH(tokens, struct Token *, o) {
		if (token_type(o) == PARSER_AST_BUILDER_TOKEN_VARIABLE_START && token_variable(o)) {
			maxvarlen = MAX(maxvarlen, strlen(variable_tostring(token_variable(o), pool)));
		}
	}
	struct Array *vars = mempool_array(pool);
	ARRAY_FOREACH(tokens, struct Token *, t) {
		const char *type = ParserASTBuilderTokenType_humanize[token_type(t)];
		if (token_variable(t) &&
		    (token_type(t) == PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN ||
		     token_type(t) == PARSER_AST_BUILDER_TOKEN_VARIABLE_START ||
		     token_type(t) == PARSER_AST_BUILDER_TOKEN_VARIABLE_END)) {
			array_append(vars, variable_tostring(token_variable(t), pool));
		} else if (token_conditional(t) &&
			   (token_type(t) == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN)) {
			array_append(vars, ParserASTBuilderConditionalType_humanize[token_conditional(t)]);
		} else if (token_target(t) && token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_START) {
			ARRAY_FOREACH(target_names(token_target(t)), char *, name) {
				array_append(vars, str_dup(pool, name));
			}
			ARRAY_FOREACH(target_dependencies(token_target(t)), char *, dep) {
				array_append(vars, str_printf(pool, "->%s", dep));
			}
		} else if (token_target(t) &&
			   (token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_START ||
			    token_type(t) == PARSER_AST_BUILDER_TOKEN_TARGET_END)) {
			array_append(vars, str_dup(pool, "-"));
		} else {
			array_append(vars, str_dup(pool, "-"));
		}

		ARRAY_FOREACH(vars, char *, var) {
			ssize_t len = maxvarlen - strlen(var);
			const char *range = range_tostring(pool, token_lines(t));
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

			if (token_data(t) &&
			    (token_type(t) != PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START &&
			     token_type(t) != PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END)) {
				fputs(token_data(t), f);
			} else {
				fputs("-", f);
			}
			fprintf(f, "\n");
		}
		array_truncate(vars);
	}
}
