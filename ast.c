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
#include <libias/mempool.h>
#include <libias/stack.h>
#include <libias/str.h>

#include "ast.h"
#include "conditional.h"
#include "target.h"
#include "token.h"
#include "variable.h"

static enum ASTNodeExprFlatType conditional_to_flatexpr[] = {
	[COND_DINCLUDE] = AST_NODE_EXPR_DINCLUDE,
	[COND_ERROR] = AST_NODE_EXPR_ERROR,
	[COND_EXPORT_ENV] = AST_NODE_EXPR_EXPORT_ENV,
	[COND_EXPORT_LITERAL] = AST_NODE_EXPR_EXPORT_LITERAL,
	[COND_EXPORT] = AST_NODE_EXPR_EXPORT,
	[COND_INCLUDE_POSIX] = AST_NODE_EXPR_INCLUDE_POSIX,
	[COND_INCLUDE] = AST_NODE_EXPR_INCLUDE,
	[COND_INFO] = AST_NODE_EXPR_INFO,
	[COND_SINCLUDE] = AST_NODE_EXPR_SINCLUDE,
	[COND_UNDEF] = AST_NODE_EXPR_UNDEF,
	[COND_UNEXPORT_ENV] = AST_NODE_EXPR_UNEXPORT_ENV,
	[COND_UNEXPORT] = AST_NODE_EXPR_UNEXPORT,
	[COND_WARNING] = AST_NODE_EXPR_WARNING,
};

const char *ASTNodeExprFlatType_identifier[] = {
	[AST_NODE_EXPR_DINCLUDE] = "dinclude",
	[AST_NODE_EXPR_ERROR] = "error",
	[AST_NODE_EXPR_EXPORT_ENV] = "export-env",
	[AST_NODE_EXPR_EXPORT_LITERAL] = "export-literal",
	[AST_NODE_EXPR_EXPORT] = "export",
	[AST_NODE_EXPR_INCLUDE_POSIX] = "include",
	[AST_NODE_EXPR_INCLUDE] = "include",
	[AST_NODE_EXPR_INFO] = "info",
	[AST_NODE_EXPR_SINCLUDE] = "sinclude",
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

static const char *NodeExprIfType_tostring_human[] = {
	[AST_NODE_EXPR_IF_IF] = "if",
	[AST_NODE_EXPR_IF_ELSE] = "else",
	[AST_NODE_EXPR_IF_DEF] = "ifdef",
	[AST_NODE_EXPR_IF_MAKE] = "ifmake",
	[AST_NODE_EXPR_IF_NDEF] = "ifndef",
	[AST_NODE_EXPR_IF_NMAKE] = "ifnmake",
};

static enum ASTNodeExprIfType ConditionalType_to_ASTNodeExprIfType[] = {
	[COND_IF] = AST_NODE_EXPR_IF_IF,
	[COND_IFDEF] = AST_NODE_EXPR_IF_DEF,
	[COND_IFMAKE] = AST_NODE_EXPR_IF_MAKE,
	[COND_IFNDEF] = AST_NODE_EXPR_IF_NDEF,
	[COND_IFNMAKE] = AST_NODE_EXPR_IF_NMAKE,
	[COND_ELIF] = AST_NODE_EXPR_IF_IF,
	[COND_ELIFDEF] = AST_NODE_EXPR_IF_DEF,
	[COND_ELIFMAKE] = AST_NODE_EXPR_IF_MAKE,
	[COND_ELIFNDEF] = AST_NODE_EXPR_IF_NDEF,
	[COND_ELSE] = AST_NODE_EXPR_IF_ELSE,
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

static void ast_node_print_helper(struct ASTNode *, FILE *, size_t);

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
	} case AST_NODE_TARGET:
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

void
ast_node_print_helper(struct ASTNode *node, FILE *f, size_t level)
{
	SCOPE_MEMPOOL(pool);
	const char *indent = str_repeat(pool, "\t", level);
	const char *line_start = str_printf(pool, "[%zu,%zu)", node->line_start.a, node->line_start.b);
	const char *line_end = str_printf(pool, "[%zu,%zu)", node->line_end.a, node->line_end.b);
	switch(node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			ast_node_print_helper(child, f, level);
		}
		break;
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
	case AST_NODE_EXPR_FOR:
		fprintf(f, "%s{ .type = AST_NODE_EXPR_FOR, .line_start = %s, .line_end = %s, .indent = %zu, .bindings = { %s }, .words = { %s } }\n",
			indent,
			line_start,
			line_end,
			node->forexpr.indent,
			str_join(pool, node->forexpr.bindings, ", "),
			str_join(pool, node->forexpr.words, ", "));
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			ast_node_print_helper(child, f, level + 1);
		}
		break;
	case AST_NODE_EXPR_IF:
		fprintf(f, "%s{ .type = AST_NODE_EXPR_IF, .line_start = %s, .line_end = %s, .iftype = %s, .indent = %zu, .test = { %s }, .elseif = %d }\n",
			indent,
			line_start,
			line_end,
			NodeExprIfType_tostring[node->ifexpr.type],
			node->ifexpr.indent,
			str_join(pool, node->ifexpr.test, ", "),
			node->ifexpr.ifparent != NULL);
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
		break;
	case AST_NODE_TARGET:
		fprintf(f, "%s{ .type = AST_NODE_TARGET, .line_start = %s, .line_end = %s, .type = %s, .sources = { %s }, .dependencies = { %s } }\n",
			indent,
			line_start,
			line_end,
			ASTNodeTargetType_tostring[node->target.type],
			str_join(pool, node->target.sources, ", "),
			str_join(pool, node->target.dependencies, ", "));
		if (array_len(node->target.body) > 0) {
			ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
				ast_node_print_helper(child, f, level + 1);
			}
		}
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
	}
}

void
ast_node_print(struct ASTNode *node, FILE *f)
{
	ast_node_print_helper(node, f, 0);
}

static size_t
cond_indent(const char *word)
{
	word++; // Skip .
	size_t indent = 0;
	for (; isspace(*word); word++, indent++);
	return indent;
}

static void
ast_from_token_stream_flush_comments(struct ASTNode *parent, struct Array *comments)
{
	if (array_len(comments) == 0) {
		return;
	}

	struct ASTNode *node = ast_node_new(parent->pool, AST_NODE_COMMENT, (struct ASTNodeLineRange *)token_lines(array_get(comments, 0)), &(struct ASTNodeComment){
		.type = AST_NODE_COMMENT_LINE,
	});
	ast_node_parent_append_sibling(parent, node, 0);

	ARRAY_FOREACH(comments, struct Token *, t) {
		node->edited = node->edited || token_edited(t);
		array_append(node->comment.lines, str_dup(node->pool, token_data(t)));
		struct Range *range = token_lines(t);
		node->line_start.b = range->end;
	}

	array_truncate(comments);
}

struct ASTNode *
ast_from_token_stream(struct Array *tokens)
{
	SCOPE_MEMPOOL(pool);

	struct ASTNode *root = ast_node_new(mempool_new(), AST_NODE_ROOT, NULL, NULL);
	struct Array *current_cond = mempool_array(pool);
	struct Array *current_comments = mempool_array(pool);
	struct Array *current_target_cmds = mempool_array(pool);
	struct Array *current_var = mempool_array(pool);
	struct Stack *ifstack = mempool_stack(pool);
	struct Stack *nodestack = mempool_stack(pool);
	stack_push(nodestack, root);

	ARRAY_FOREACH(tokens, struct Token *, t) {
		if (stack_len(nodestack) == 0) {
			panic("node stack exhausted at token on input line %zu-%zu", token_lines(t)->start, token_lines(t)->end);
		}
		if (token_type(t) != COMMENT) {
			ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);
		}
		switch (token_type(t)) {
		case CONDITIONAL_START:
			array_truncate(current_cond);
			break;
		case CONDITIONAL_TOKEN:
			array_append(current_cond, t);
			break;
		case CONDITIONAL_END: {
			enum ConditionalType condtype = conditional_type(token_conditional(t));
			switch (condtype) {
			case COND_DINCLUDE:
			case COND_ERROR:
			case COND_EXPORT_ENV:
			case COND_EXPORT_LITERAL:
			case COND_EXPORT:
			case COND_INCLUDE_POSIX:
			case COND_INCLUDE:
			case COND_INFO:
			case COND_SINCLUDE:
			case COND_UNDEF:
			case COND_UNEXPORT_ENV:
			case COND_UNEXPORT:
			case COND_WARNING: {
				struct ASTNode *node = ast_node_new(root->pool, AST_NODE_EXPR_FLAT, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeExprFlat){
					.type = conditional_to_flatexpr[condtype],
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
				});
				ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
				if (token_edited(t)) {
					node->edited = 1;
				}
				ARRAY_FOREACH_SLICE(current_cond, 1, -1, struct Token *, t) {
					if (token_edited(t)) {
						node->edited = 1;
					}
					array_append(node->flatexpr.words, str_dup(node->pool, token_data(t)));
				}
				break;
			} case COND_FOR: {
				struct ASTNode *node = ast_node_new(root->pool, AST_NODE_EXPR_FOR, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeExprFor){
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
				});
				ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
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
				ARRAY_FOREACH_SLICE(current_cond, word_start, -1, struct Token *, t) {
					if (token_edited(t)) {
						node->edited = 1;
					}
				 	array_append(node->forexpr.words, str_dup(node->pool, token_data(t)));
				}
				stack_push(nodestack, node);
				break;
			} case COND_ENDFOR: {
				struct ASTNode *node = stack_pop(nodestack);
				node->line_end = *((struct ASTNodeLineRange *)token_lines(t));
				break;
			} case COND_IF:
			case COND_IFDEF:
			case COND_IFMAKE:
			case COND_IFNDEF:
			case COND_IFNMAKE:
			case COND_ELIF:
			case COND_ELIFDEF:
			case COND_ELIFMAKE:
			case COND_ELIFNDEF:
			case COND_ELSE: {
				struct ASTNode *parent = stack_peek(nodestack);
				struct ASTNode *ifparent = NULL;
				switch (condtype) {
				case COND_ELIF:
				case COND_ELIFDEF:
				case COND_ELIFMAKE:
				case COND_ELIFNDEF:
				case COND_ELSE:
					ifparent = stack_peek(ifstack);
					break;
				default:
					ifparent = NULL;
					break;
				}
				if (ifparent) {
					parent = stack_peek(ifstack);
				}
				struct ASTNode *node = ast_node_new(root->pool, AST_NODE_EXPR_IF, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeExprIf){
					.type = ConditionalType_to_ASTNodeExprIfType[condtype],
					.indent = cond_indent(token_data(array_get(current_cond, 0))),
					.ifparent = ifparent,
				});
				ast_node_parent_append_sibling(parent, node, ifparent != NULL);
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
			} case COND_ENDIF: {
				struct ASTNode *ifnode = stack_pop(ifstack);
				while (ifnode && ifnode->ifexpr.ifparent && (ifnode = stack_pop(ifstack)));
				if (ifnode) {
					ifnode->line_end = *((struct ASTNodeLineRange *)token_lines(t));
				}
				struct ASTNode *node = NULL;
				while ((node = stack_pop(nodestack)) && node != ifnode);
				break;
			} }
			break;
		}

		case TARGET_START: {
			// When a new target starts the old one if
			// any has ended.  Also see TARGET_END.
			struct ASTNode *node = stack_peek(nodestack);
			if (node->type == AST_NODE_TARGET) {
				stack_pop(nodestack);
			}

			node = ast_node_new(root->pool, AST_NODE_TARGET, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeTarget){
				.type = AST_NODE_TARGET_NAMED,
			});
			ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			struct Target *target = token_target(t);
			ARRAY_FOREACH(target_names(target), const char *, source) {
				array_append(node->target.sources, str_dup(node->pool, source));
			}
			ARRAY_FOREACH(target_dependencies(target), const char *, dependency) {
				array_append(node->target.dependencies, str_dup(node->pool, dependency));
			}
			stack_push(nodestack, node);
			break;
		} case TARGET_END: {
			// We might have a token stream like this,
			// so we need to be careful what we pop
			// from the nodestack.
			// target-start                8 bar bar:
			// conditional-start           9 .endif -
			// conditional-token           9 .endif .endif
			// conditional-end             9 .endif -
			// target-end                 10 - -
			struct ASTNode *node = stack_peek(nodestack);
			if (node->type == AST_NODE_TARGET) {
				stack_pop(nodestack);
			}
			break;
		}

		case TARGET_COMMAND_START:
			array_truncate(current_target_cmds);
			break;
		case TARGET_COMMAND_TOKEN:
			array_append(current_target_cmds, t);
			break;
		case TARGET_COMMAND_END: {
			struct ASTNodeTarget *target = NULL;
			// Find associated target if any
			for (struct ASTNode *cur = stack_peek(nodestack); cur && cur != root; cur = cur->parent) {
				if (cur->type == AST_NODE_TARGET) {
					target = &cur->target;
					break;
				}
			}
			unless (target) { // Inject new unassociated target
				struct ASTNode *node = ast_node_new(root->pool, AST_NODE_TARGET, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeTarget){
					.type = AST_NODE_TARGET_UNASSOCIATED,
				});
				ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
				node->edited = token_edited(t);
				stack_push(nodestack, node);
				target = &node->target;
			}

			//panic_unless(target, "unassociated target command on input line %zu-%zu", token_lines(t)->start, token_lines(t)->end);
			struct ASTNode *node = ast_node_new(root->pool, AST_NODE_TARGET_COMMAND, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeTargetCommand){
				.target = target,
			});
			ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			node->line_end = node->line_start;
			ARRAY_FOREACH(current_target_cmds, struct Token *, t) {
				if (t_index == 0) {
					node->line_start = *((struct ASTNodeLineRange *)token_lines(t));
				}
				if (token_edited(t)) {
					node->edited = 1;
				}
				array_append(node->targetcommand.words, str_dup(node->pool, token_data(t)));
			}
			break;
		}

		case VARIABLE_START:
			array_truncate(current_var);
			array_append(current_var, t); // XXX: Hack to make the old refactor_collapse_adjacent_variables work correctly...
			break;
		case VARIABLE_TOKEN:
			array_append(current_var, t);
			break;
		case VARIABLE_END: {
			struct ASTNodeLineRange *range = (struct ASTNodeLineRange *)array_get(current_var, 0);
			unless (range) {
				range = (struct ASTNodeLineRange *)token_lines(t);
			}
			struct ASTNode *node = ast_node_new(root->pool, AST_NODE_VARIABLE, (struct ASTNodeLineRange *)token_lines(t), &(struct ASTNodeVariable){
				.name = variable_name(token_variable(t)),
				.modifier = variable_modifier(token_variable(array_get(current_var, 0))),
			});
			ast_node_parent_append_sibling(stack_peek(nodestack), node, 0);
			node->edited = token_edited(t);
			node->line_end = node->line_start;
			ARRAY_FOREACH_SLICE(current_var, 1, -1, struct Token *, t) {
				if (t_index == 0) {
					node->line_start = *((struct ASTNodeLineRange *)token_lines(t));
				}
				if (token_edited(t)) {
					node->edited = 1;
				}
				array_append(node->variable.words, str_dup(node->pool, token_data(t)));
			}
			break;
		} case COMMENT:
			array_append(current_comments, t);
			break;
		}
	}

	ast_from_token_stream_flush_comments(stack_peek(nodestack), current_comments);

	return root;
}

void
ast_free(struct ASTNode *node) {
	if (node) {
		mempool_free(node->pool);
	}
}


static void
token_to_stream(struct Mempool *pool, struct Array *tokens, enum TokenType type, int edited, struct ASTNodeLineRange *lines, const char *data, const char *varname, const char *condname, const char *targetname)
{
	struct Token *t = token_new(type, (struct Range *)lines, data, varname, condname, targetname);
	panic_unless(t, "null token?");
	if (t) {
		if (edited) {
			token_mark_edited(t);
		}
		array_append(tokens, mempool_add(pool, t, token_free));
	}
}

static const char *
get_targetname(struct Mempool *pool, struct ASTNodeTarget *target)
{
	switch (target->type) {
	case AST_NODE_TARGET_NAMED:
		if (array_len(target->dependencies) > 0) {
			return str_printf(pool, "%s: %s",
				str_join(pool, target->sources, " "),
				str_join(pool, target->dependencies, " "));
		} else {
			return str_printf(pool, "%s:", str_join(pool, target->sources, " "));
		}
		break;
	case AST_NODE_TARGET_UNASSOCIATED:
		return "<unassociated>:";
		break;
	}
}

static void
ast_to_token_stream_helper(struct ASTNode *node, struct Mempool *extpool, struct Array *tokens)
{
	SCOPE_MEMPOOL(pool);

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			ast_to_token_stream_helper(child, extpool, tokens);
		}
		break;
	case AST_NODE_COMMENT: {
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			struct Token *t = token_new_comment((struct Range *)&node->line_start, line, NULL);
			if (node->edited) {
				token_mark_edited(t);
			}
			array_append(tokens, mempool_add(extpool, t, token_free));
		}
		break;
	} case AST_NODE_EXPR_FLAT: {
		const char *indent = str_repeat(pool, " ", node->flatexpr.indent);
		const char *data = str_printf(pool, ".%s%s", indent, ASTNodeExprFlatType_identifier[node->flatexpr.type]);
		const char *exprname = str_printf(pool, ".%s", ASTNodeExprFlatType_identifier[node->flatexpr.type]);
		token_to_stream(extpool, tokens, CONDITIONAL_START, node->edited, &node->line_start, data, NULL, exprname, NULL);
		token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, exprname, NULL);
		ARRAY_FOREACH(node->flatexpr.words, const char *, word) {
			token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, exprname, NULL);
		}
		token_to_stream(extpool, tokens, CONDITIONAL_END, node->edited, &node->line_start, data, NULL, exprname, NULL);
		break;
	} case AST_NODE_EXPR_IF: {
		const char *indent = str_repeat(pool, " ", node->ifexpr.indent);
		const char *prefix  = "";
		if (node->ifexpr.ifparent && node->ifexpr.type != AST_NODE_EXPR_IF_ELSE) {
			prefix = "el";
		}
		const char *ifname = str_printf(pool, "%s%s", prefix, NodeExprIfType_tostring_human[node->ifexpr.type]);
		const char *ifnamedot = str_printf(pool, ".%s", ifname);
		const char *data = str_printf(pool, ".%s%s", indent, ifname);
		token_to_stream(extpool, tokens, CONDITIONAL_START, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);
		token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);
		ARRAY_FOREACH(node->ifexpr.test, const char *, word) {
			token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, ifnamedot, NULL);
		}
		token_to_stream(extpool, tokens, CONDITIONAL_END, node->edited, &node->line_start, data, NULL, ifnamedot, NULL);

		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			ast_to_token_stream_helper(child, extpool, tokens);
		}

		if (array_len(node->ifexpr.orelse) > 0) {
			struct ASTNode *next = array_get(node->ifexpr.orelse, 0);
			if (next && next->type == AST_NODE_EXPR_IF && next->ifexpr.type == AST_NODE_EXPR_IF_ELSE) {
				data = str_printf(pool, ".%selse", indent);
				token_to_stream(extpool, tokens, CONDITIONAL_START, next->edited, &next->line_start, data, NULL, ".else", NULL);
				token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, next->edited, &next->line_start, data, NULL, ".else", NULL);
				token_to_stream(extpool, tokens, CONDITIONAL_END, next->edited, &next->line_start, data, NULL, ".else", NULL);
				ARRAY_FOREACH(next->ifexpr.body, struct ASTNode *, child) {
					ast_to_token_stream_helper(child, extpool, tokens);
				}
			} else {
				ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
					ast_to_token_stream_helper(child, extpool, tokens);
				}
			}
		}

		unless (node->ifexpr.ifparent) {
			data = str_printf(pool, ".%sendif", indent);
			token_to_stream(extpool, tokens, CONDITIONAL_START, node->edited, &node->line_end, data, NULL, ".endif", NULL);
			token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_end, data, NULL, ".endif", NULL);
			token_to_stream(extpool, tokens, CONDITIONAL_END, node->edited, &node->line_end, data, NULL, ".endif", NULL);
		}
		break;
	} case AST_NODE_EXPR_FOR: {
		const char *indent = str_repeat(pool, " ", node->forexpr.indent);
		const char *data = str_printf(pool, ".%sfor", indent);
		token_to_stream(extpool, tokens, CONDITIONAL_START, node->edited, &node->line_start, data, NULL, ".for", NULL);
		token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, data, NULL, ".for", NULL);
		ARRAY_FOREACH(node->forexpr.bindings, const char *, binding) {
			token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, binding, NULL, ".for", NULL);
		}
		token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, "in", NULL, ".for", NULL);
		ARRAY_FOREACH(node->forexpr.words, const char *, word) {
			token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_start, word, NULL, ".for", NULL);
		}
		token_to_stream(extpool, tokens, CONDITIONAL_END, node->edited, &node->line_start, data, NULL, ".for", NULL);

		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			ast_to_token_stream_helper(child, extpool, tokens);
		}

		data = str_printf(pool, ".%sendfor", indent);
		token_to_stream(extpool, tokens, CONDITIONAL_START, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		token_to_stream(extpool, tokens, CONDITIONAL_TOKEN, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		token_to_stream(extpool, tokens, CONDITIONAL_END, node->edited, &node->line_end, data, NULL, ".endfor", NULL);
		break;
	} case AST_NODE_TARGET: {
		const char *targetname = get_targetname(pool, &node->target);
		token_to_stream(extpool, tokens, TARGET_START, node->edited, &node->line_start, targetname, NULL, NULL, targetname);
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			ast_to_token_stream_helper(child, extpool, tokens);
		}
		token_to_stream(extpool, tokens, TARGET_END, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		break;
	} case AST_NODE_TARGET_COMMAND: {
		const char *targetname = get_targetname(pool, node->targetcommand.target);
		token_to_stream(extpool, tokens, TARGET_COMMAND_START, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		ARRAY_FOREACH(node->targetcommand.words, const char *, word) {
			token_to_stream(extpool, tokens, TARGET_COMMAND_TOKEN, node->edited, &node->line_start, word, NULL, NULL, targetname);
		}
		token_to_stream(extpool, tokens, TARGET_COMMAND_END, node->edited, &node->line_start, NULL, NULL, NULL, targetname);
		break;
	} case AST_NODE_VARIABLE: {
		const char *space = "";
		if (str_endswith(node->variable.name, "+")) {
			space = " ";
		}
		const char *varname = str_printf(pool, "%s%s%s", node->variable.name, space, ASTNodeVariableModifier_humanize[node->variable.modifier]);
		token_to_stream(extpool, tokens, VARIABLE_START, node->edited, &node->line_start, NULL, varname, NULL, NULL);
		ARRAY_FOREACH(node->variable.words, const char *, word) {
			token_to_stream(extpool, tokens, VARIABLE_TOKEN, node->edited, &node->line_start, word, varname, NULL, NULL);
		}
		token_to_stream(extpool, tokens, VARIABLE_END, node->edited, &node->line_end, NULL, varname, NULL, NULL);
		break;
	} }
}

struct Array *
ast_to_token_stream(struct ASTNode *node, struct Mempool *pool)
{
	struct Array *tokens = array_new();
	ast_to_token_stream_helper(node, pool, tokens);
	return tokens;
}
