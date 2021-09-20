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
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/color.h>
#include <libias/diff.h>
#include <libias/flow.h>
#include <libias/map.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct GetVariablesWalkerData {
	struct Parser *parser;
	struct Array *vars;
};

struct TargetListWalkData {
	struct Array *targets;
};

struct Row {
	char *name;
	char *hint;
};

static int check_target_order(struct Parser *, struct AST *, int, int);
static int check_variable_order(struct Parser *, struct AST *, int);
static int output_diff(struct Parser *, struct Array *, struct Array *, int);
static void output_row(struct Parser *, struct Row *, size_t);

static void
row_free(struct Row *row)
{
	if (row) {
		free(row->name);
		free(row->hint);
		free(row);
	}
}

static void
row(struct Mempool *pool, struct Array *output, const char *name, const char *hint)
{
	struct Row *row = xmalloc(sizeof(struct Row));
	row->name = str_dup(NULL, name);
	if (hint) {
		row->hint = str_dup(NULL, hint);
	}
	mempool_add(pool, row, row_free);
	array_append(output, row);
}

static int
row_compare(const void *ap, const void *bp, void *userdata)
{
	struct Row *a = *(struct Row **)ap;
	struct Row *b = *(struct Row **)bp;
	return strcmp(a->name, b->name);
}

static enum ASTWalkState
get_variables(struct AST *node, struct GetVariablesWalkerData *this)
{
	switch (node->type) {
	case AST_IF:
		if (array_len(node->ifexpr.test) == 1) {
			const char *word = array_get(node->ifexpr.test, 0);
			if (strcmp(word, "defined(DEVELOPER)") == 0 ||
			    strcmp(word, "defined(MAINTAINER_MODE)") == 0 ||
			    strcmp(word, "make(makesum)") == 0) {
				return AST_WALK_CONTINUE;
			}
		}
		break;
	case AST_INCLUDE:
		if (is_include_bsd_port_mk(node)) {
			return AST_WALK_STOP;
		} else {
			// XXX: Should we recurse down into includes?
			return AST_WALK_CONTINUE;
		}
		break;
	case AST_VARIABLE:
		// Ignore port local variables that start with an _
		if (node->variable.name[0] != '_' && array_find(this->vars, node->variable.name, str_compare, NULL) == -1 &&
		    !is_referenced_var(this->parser, node->variable.name)) {
			array_append(this->vars, node->variable.name);
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(get_variables, node, this);
	return AST_WALK_CONTINUE;
}

static int
get_all_unknown_variables_row_compare(const void *ap, const void *bp, void *userdata)
{
	struct Row *a = *(struct Row **)ap;
	struct Row *b = *(struct Row **)bp;
	int retval = strcmp(a->name, b->name);
	if (retval == 0) {
		if (a->hint && b->hint) {
			return strcmp(a->hint, b->hint);
		} else if (a->hint) {
			return -1;
		} else {
			return 1;
		}
	} else {
		return retval;
	}
}

static void
get_all_unknown_variables_helper(struct Mempool *extpool, const char *key, const char *val, const char *hint, void *userdata)
{
	struct Set *unknowns = userdata;
	struct Row rowkey = { .name = (char *)key, .hint = (char *)hint };
	if (key && hint && !set_contains(unknowns, &rowkey)) {
		struct Row *row = xmalloc(sizeof(struct Row));
		row->name = str_dup(NULL, key);
		row->hint = str_dup(NULL, hint);
		set_add(unknowns, row);
	}
}

static int
get_all_unknown_variables_filter(struct Parser *parser, const char *key, void *userdata)
{
	return *key != '_';
}

static struct Set *
get_all_unknown_variables(struct Mempool *pool, struct Parser *parser)
{
	struct Set *unknowns = mempool_set(pool, get_all_unknown_variables_row_compare, NULL, row_free);
	struct ParserEditOutput param = { get_all_unknown_variables_filter, NULL, NULL, NULL, get_all_unknown_variables_helper, unknowns, 0 };
	if (parser_edit(parser, pool, output_unknown_variables, &param) != PARSER_ERROR_OK) {
		return unknowns;
	}
	return unknowns;
}

static char *
get_hint(struct Mempool *pool, struct Parser *parser, const char *var, enum BlockType block, struct Set *uses_candidates)
{
	char *hint = NULL;
	if (uses_candidates) {
		struct Array *uses = set_values(uses_candidates, pool);
		char *buf = str_join(pool, uses, " ");
		if (set_len(uses_candidates) > 1) {
			hint = str_printf(pool, "missing one of USES=%s ?", buf);
		} else {
			hint = str_printf(pool, "missing USES=%s ?", buf);
		}
	} else if (block == BLOCK_UNKNOWN) {
		char *uppervar = str_map(pool, var, strlen(var), toupper);
		if (variable_order_block(parser, uppervar, NULL, NULL) != BLOCK_UNKNOWN) {
			hint = str_printf(pool, "did you mean %s ?", uppervar);
		}
	}

	return hint;
}

static struct Array *
variable_list(struct Mempool *pool, struct Parser *parser, struct AST *root)
{
	struct Array *output = mempool_array(pool);
	struct Array *vars = mempool_array(pool);
	get_variables(root, &(struct GetVariablesWalkerData){
		.parser = parser,
		.vars = vars,
	});

	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	ARRAY_FOREACH(vars, char *, var) {
		struct Set *uses_candidates = NULL;
		block = variable_order_block(parser, var, pool, &uses_candidates);
		if (block != last_block) {
			if (flag && block != last_block) {
				row(pool, output, "", NULL);
			}
			row(pool, output, str_printf(pool, "# %s", blocktype_tostring(block)), NULL);
		}
		flag = 1;
		char *hint = get_hint(pool, parser, var, block, uses_candidates);
		row(pool, output, var, hint);
		last_block = block;
	}

	return output;
}

static enum ASTWalkState
target_list(struct AST *node, struct TargetListWalkData *this)
{
	switch (node->type) {
	case AST_IF:
		if (array_len(node->ifexpr.test) == 1) {
			const char *word = array_get(node->ifexpr.test, 0);
			if (strcmp(word, "defined(DEVELOPER)") == 0 ||
			    strcmp(word, "defined(MAINTAINER_MODE)") == 0 ||
			    strcmp(word, "make(makesum)") == 0) {
				return AST_WALK_CONTINUE;
			}
		}
		break;
	case AST_INCLUDE:
		if (is_include_bsd_port_mk(node)) {
			return AST_WALK_STOP;
		} else {
			// XXX: Should we recurse down into includes?
			return AST_WALK_CONTINUE;
		}
		break;
	case AST_TARGET:
		ARRAY_FOREACH(node->target.sources, const char *, target) {
			// Ignore port local targets that start with an _
			if (target[0] != '_' && !is_special_target(target) &&
			    array_find(this->targets, target, str_compare, NULL) == -1) {
				array_append(this->targets, target);
			}
		}
	default:
		break;
	}

	AST_WALK_DEFAULT(target_list, node, this);
	return AST_WALK_CONTINUE;
}

int
check_variable_order(struct Parser *parser, struct AST *root, int no_color)
{
	SCOPE_MEMPOOL(pool);
	struct Array *origin = variable_list(pool, parser, root);

	struct Array *vars = mempool_array(pool);
	get_variables(root, &(struct GetVariablesWalkerData){
		.parser = parser,
		.vars = vars,
	});
	array_sort(vars, compare_order, parser);

	struct Set *uses_candidates = NULL;
	struct Array *target = mempool_array(pool);
	struct Array *unknowns = mempool_array(pool);
	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	ARRAY_FOREACH(vars, char *, var) {
		if ((block = variable_order_block(parser, var, pool, &uses_candidates)) != BLOCK_UNKNOWN) {
			if (block != last_block) {
				if (flag && block != last_block) {
					row(pool, target, "", NULL);
				}
				row(pool, target, str_printf(pool, "# %s", blocktype_tostring(block)), NULL);
			}
			flag = 1;
			row(pool, target, var, NULL);
			last_block = block;
		} else {
			array_append(unknowns, var);
			last_block = BLOCK_UNKNOWN;
		}
	}

	array_sort(unknowns, str_compare, NULL);

	struct Set *all_unknown_variables = get_all_unknown_variables(pool, parser);
	ARRAY_FOREACH(unknowns, char *, var) {
		struct Row key = { .name = var, .hint = NULL };
		set_remove(all_unknown_variables, &key);
	}

	if (array_len(vars) > 0 && (array_len(unknowns) > 0 || set_len(all_unknown_variables) > 0)) {
		row(pool, target, "", NULL);
		row(pool, target, str_printf(pool, "# %s", blocktype_tostring(BLOCK_UNKNOWN)), NULL);
		row(pool, target, "# WARNING:", NULL);
		row(pool, target, "# The following variables were not recognized.", NULL);
		row(pool, target, "# They could just be typos or Portclippy needs to be made aware of them.", NULL);
		row(pool, target, "# Please double check them.", NULL);
		row(pool, target, "#", NULL);
		row(pool, target, "# Prefix them with an _ to tell Portclippy to ignore them.", NULL);
		row(pool, target, "#", NULL);
		row(pool, target, "# If in doubt please report this on portfmt's bug tracker:", NULL);
		row(pool, target, "# https://github.com/t6/portfmt/issues", NULL);
	}
	ARRAY_FOREACH(unknowns, char *, var) {
		struct Set *uses_candidates = NULL;
		enum BlockType block = variable_order_block(parser, var, pool, &uses_candidates);
		char *hint = get_hint(pool, parser, var, block, uses_candidates);
		row(pool, target, var, hint);
	}

	int retval = output_diff(parser, origin, target, no_color);

	if (array_len(vars) > 0 && set_len(all_unknown_variables) > 0) {
		struct Map *group = mempool_map(pool, str_compare, NULL, NULL, NULL);
		size_t maxlen = 0;
		SET_FOREACH(all_unknown_variables, struct Row *, var) {
			struct Array *hints = map_get(group, var->name);
			maxlen = MAX(maxlen, strlen(var->name));
			if (!hints) {
				hints = mempool_array(pool);
				map_add(group, var->name, hints);
			}
			if (var->hint) {
				array_append(hints, str_printf(pool, "in %s", var->hint));
			}
		}
		parser_enqueue_output(parser, "\n");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_CYAN);
		}
		parser_enqueue_output(parser, "# Unknown variables in options helpers\n");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
		MAP_FOREACH(group, char *, name, struct Array *, hints) {
			struct Set *uses_candidates = NULL;
			variable_order_block(parser, name, pool, &uses_candidates);
			if (uses_candidates) {
				struct Array *uses = set_values(uses_candidates, pool);
				char *buf = str_join(pool, uses, " ");
				char *hint = NULL;
				if (set_len(uses_candidates) > 1) {
					hint = str_printf(pool, "missing one of USES=%s ?", buf);
				} else {
					hint = str_printf(pool, "missing USES=%s ?", buf);
				}
				array_append(hints, hint);
			}
			if (array_len(hints) > 0) {
				struct Row row = { .name = name, .hint = array_get(hints, 0) };
				output_row(parser, &row, maxlen + 1);
				ARRAY_FOREACH(hints, char *, hint) {
					if (hint_index > 0) {
						struct Row row = { .name = (char *)"", .hint = hint };
						output_row(parser, &row, maxlen + 1);
					}
				}
			} else {
				parser_enqueue_output(parser, name);
				parser_enqueue_output(parser, "\n");
			}
		}
	}

	return retval;
}

int
check_target_order(struct Parser *parser, struct AST *root, int no_color, int status_var)
{
	SCOPE_MEMPOOL(pool);

	struct Array *targets = mempool_array(pool);
	target_list(root, &(struct TargetListWalkData){
		.targets = targets,
	});

	struct Array *origin = mempool_array(pool);
	if (status_var) {
		row(pool, origin, "", NULL);
	}
	row(pool, origin, "# Out of order targets", NULL);
	ARRAY_FOREACH(targets, char *, name) {
		if (is_known_target(parser, name)) {
			row(pool, origin, str_printf(pool, "%s:", name), NULL);
		}
	}

	array_sort(targets, compare_target_order, parser);

	struct Array *target = mempool_array(pool);
	if (status_var) {
		row(pool, target, str_dup(NULL, ""), NULL);
	}
	row(pool, target, "# Out of order targets", NULL);
	ARRAY_FOREACH(targets, char *, name) {
		if (is_known_target(parser, name)) {
			row(pool, target, str_printf(pool, "%s:", name), NULL);
		}
	}

	struct Array *unknowns = mempool_array(pool);
	ARRAY_FOREACH(targets, char *, name) {
		if (!is_known_target(parser, name) && name[0] != '_') {
			array_append(unknowns, str_printf(pool, "%s:", name));
		}
	}

	int status_target = 0;
	if ((status_target = output_diff(parser, origin, target, no_color)) == -1) {
		return status_target;
	}

	if (array_len(unknowns) > 0) {
		if (status_var || status_target) {
			parser_enqueue_output(parser, "\n");
		}
		status_target = 1;
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_CYAN);
		}
		parser_enqueue_output(parser, "# Unknown targets");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
		parser_enqueue_output(parser, "\n");
		ARRAY_FOREACH(unknowns, char *, name) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
		}
	}

	return status_target;
}

static void
output_row(struct Parser *parser, struct Row *row, size_t maxlen)
{
	SCOPE_MEMPOOL(pool);

	parser_enqueue_output(parser, row->name);
	if (row->hint && maxlen > 0) {
		size_t len = maxlen - strlen(row->name);
		char *spaces = str_repeat(pool, " ", len + 4);
		parser_enqueue_output(parser, spaces);
		parser_enqueue_output(parser, row->hint);
	}
	parser_enqueue_output(parser, "\n");
}

static int
output_diff(struct Parser *parser, struct Array *origin, struct Array *target, int no_color)
{
	SCOPE_MEMPOOL(pool);

	struct diff *p = array_diff(origin, target, pool, row_compare, NULL);
	if (p == NULL) {
		return -1;
	}

	size_t edits = 0;
	for (size_t i = 0; i < p->sessz; i++) {
		switch (p->ses[i].type) {
		case DIFF_ADD:
		case DIFF_DELETE:
			edits++;
			break;
		default:
			break;
		}
	}
	if (edits == 0) {
		return 0;
	}

	size_t maxlen = 0;
	ARRAY_FOREACH(origin, struct Row *, row) {
		if (row->name[0] != '#') {
			maxlen = MAX(maxlen, strlen(row->name));
		}
	}

	for (size_t i = 0; i < p->sessz; i++) {
		struct Row *row = *(struct Row **)p->ses[i].e;
		if (strlen(row->name) == 0) {
			parser_enqueue_output(parser, "\n");
			continue;
		} else if (row->name[0] == '#') {
			if (p->ses[i].type != DIFF_DELETE) {
				if (!no_color) {
					parser_enqueue_output(parser, ANSI_COLOR_CYAN);
				}
				output_row(parser, row, 0);
				if (!no_color) {
					parser_enqueue_output(parser, ANSI_COLOR_RESET);
				}
			}
			continue;
		}
		switch (p->ses[i].type) {
		case DIFF_ADD:
			if (!no_color) {
				parser_enqueue_output(parser, ANSI_COLOR_GREEN);
			}
			parser_enqueue_output(parser, "+");
			output_row(parser, row, maxlen);
			break;
		case DIFF_DELETE:
			if (!no_color) {
				parser_enqueue_output(parser, ANSI_COLOR_RED);
			}
			parser_enqueue_output(parser, "-");
			output_row(parser, row, 0);
			break;
		default:
			output_row(parser, row, maxlen + 1);
			break;
		}
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
	}

	return 1;
}

PARSER_EDIT(lint_order)
{
	int *status = userdata;
	struct ParserSettings settings = parser_settings(parser);
	if (!(settings.behavior & PARSER_OUTPUT_RAWLINES)) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, "needs PARSER_OUTPUT_RAWLINES");
		return 0;
	}
	int no_color = settings.behavior & PARSER_OUTPUT_NO_COLOR;

	int status_var;
	if ((status_var = check_variable_order(parser, root, no_color)) == -1) {
		parser_set_error(parser, PARSER_ERROR_EDIT_FAILED, "lint_order: cannot compute difference");
		return 0;
	}

	int status_target;
	if ((status_target = check_target_order(parser, root, no_color, status_var)) == -1) {
		parser_set_error(parser, PARSER_ERROR_EDIT_FAILED, "lint_order: cannot compute difference");
		return 0;
	}

	if (status != NULL && (status_var > 0 || status_target > 0)) {
		*status = 1;
	}

	return 1;
}

