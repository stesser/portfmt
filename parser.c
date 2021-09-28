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
#include <sys/types.h>
#include <sys/uio.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libias/array.h>
#include <libias/color.h>
#include <libias/diff.h>
#include <libias/diffutil.h>
#include <libias/flow.h>
#include <libias/io.h>
#include <libias/mempool/file.h>
#include <libias/map.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/path.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "constants.h"
#include "io/file.h"
#include "parser.h"
#include "parser/astbuilder.h"
#include "parser/edits.h"
#include "parser/tokenizer.h"
#include "rules.h"

struct Parser {
	struct ParserSettings settings;
	enum ParserError error;
	char *error_msg;

	struct Array *rawlines;
	struct ParserTokenizer *tokenizer;
	struct ParserASTBuilder *builder;

	struct Mempool *pool;
	struct AST *ast;
	struct Array *result;
	void *metadata[PARSER_METADATA_USES + 1];
	int metadata_valid[PARSER_METADATA_USES + 1];

	int read_finished;
};

struct ParserFindGoalcolsState {
	struct Parser *parser;
	int moving_goalcol;
	struct Array *nodes;
};

static void parser_find_goalcols(struct Parser *);
static enum ParserError parser_load_includes(struct Parser *);
static void parser_meta_values(struct Parser *, const char *, struct Set *);
static void parser_metadata_alloc(struct Parser *);
static void parser_metadata_free(struct Parser *);
static void parser_metadata_port_options(struct Parser *);
static void parser_output_dump_tokens(struct Parser *);
static void parser_output_prepare(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct ASTLineRange *);
static void parser_output_print_target_command(struct Parser *, struct AST *);
static void parser_output_print_variable(struct Parser *, struct Mempool *, struct AST *);
static struct Array *parser_output_sort_opt_use(struct Parser *, struct Mempool *, struct ASTVariable *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_output_diff(struct Parser *);
static void parser_propagate_goalcol(struct ParserFindGoalcolsState *);
static void print_newline_array(struct Parser *, struct AST *, struct Array *);
static void print_token_array(struct Parser *, struct AST *, struct Array *);

static int
parser_is_category_makefile(struct AST *node, struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK || !parser->read_finished) {
		return 0;
	}

	switch (node->type) {
	case AST_INCLUDE:
		if (node->include.type == AST_INCLUDE_BMAKE &&
		    node->include.sys && node->include.path &&
		    strcmp(node->include.path, "bsd.port.subdir.mk") == 0) {
			return AST_WALK_STOP;
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(parser_is_category_makefile, node, parser);

	return 0;
}

void
parser_init_settings(struct ParserSettings *settings)
{
	settings->filename = NULL;
	settings->portsdir = -1;
	settings->behavior = PARSER_DEFAULT;
	settings->diff_context = 3;
	settings->target_command_format_threshold = 8;
	settings->target_command_format_wrapcol = 65;
	settings->wrapcol = 80;
	settings->debug_level = 0;
}

struct Parser *
parser_new(struct Mempool *extpool, struct ParserSettings *settings)
{
	struct Parser *parser = xmalloc(sizeof(struct Parser));

	parser->pool = mempool_new();
	parser->rawlines = array_new();
	parser->result = array_new();
	parser_metadata_alloc(parser);
	parser->error = PARSER_ERROR_OK;
	parser->error_msg = NULL;
	parser->settings = *settings;
	if (settings->filename) {
		parser->settings.filename = path_normalize(parser->pool, settings->filename, NULL);
	} else {
		parser->settings.filename = str_dup(parser->pool, "/dev/stdin");
	}

	if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser->settings.behavior &= ~PARSER_COLLAPSE_ADJACENT_VARIABLES;
	}

	if ((settings->behavior & PARSER_OUTPUT_DUMP_TOKENS) ||
	    (settings->behavior & PARSER_OUTPUT_DIFF) ||
	    (settings->behavior & PARSER_OUTPUT_RAWLINES)) {
		settings->behavior &= ~PARSER_OUTPUT_INPLACE;
	}

	parser->builder = parser_astbuilder_new(parser);
	parser->tokenizer = parser_tokenizer_new(parser, &parser->error, parser->builder);

	return mempool_add(extpool, parser, parser_free);
}

void
parser_free(struct Parser *parser)
{
	if (parser == NULL) {
		return;
	}

	ARRAY_FOREACH(parser->result, void *, x) {
		free(x);
	}
	array_free(parser->result);

	ARRAY_FOREACH(parser->rawlines, char *, line) {
		free(line);
	}
	array_free(parser->rawlines);

	mempool_free(parser->pool);
	parser_metadata_free(parser);
	free(parser->error_msg);

	parser_tokenizer_free(parser->tokenizer);
	parser_astbuilder_free(parser->builder);
	ast_free(parser->ast);
	free(parser);
}

void
parser_set_error(struct Parser *parser, enum ParserError error, const char *msg)
{
	free(parser->error_msg);
	if (msg) {
		parser->error_msg = str_dup(NULL, msg);
	} else {
		parser->error_msg = NULL;
	}
	parser->error = error;
}

char *
parser_error_tostring(struct Parser *parser, struct Mempool *extpool)
{
	SCOPE_MEMPOOL(pool);

	char *lines = ast_line_range_tostring(&parser->builder->lines, 1, pool);
	switch (parser->error) {
	case PARSER_ERROR_OK:
		return str_printf(extpool, "%s: no error", lines);
	case PARSER_ERROR_DIFFERENCES_FOUND:
		return str_printf(extpool, "differences found");
	case PARSER_ERROR_EDIT_FAILED:
		if (parser->error_msg) {
			return str_printf(extpool, "%s", parser->error_msg);
		} else {
			return str_printf(extpool, "%s: edit failed", lines);
		}
	case PARSER_ERROR_EXPECTED_CHAR:
		if (parser->error_msg) {
			return str_printf(extpool, "%s: expected char: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "%s: expected char", lines);
		}
	case PARSER_ERROR_EXPECTED_INT:
		if (parser->error_msg) {
			return str_printf(extpool, "%s: expected integer: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "%s: expected integer", lines);
		}
	case PARSER_ERROR_EXPECTED_TOKEN:
		if (parser->error_msg) {
			return str_printf(extpool, "%s: expected %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "%s: expected token", lines);
		}
	case PARSER_ERROR_INVALID_ARGUMENT:
		if (parser->error_msg) {
			return str_printf(extpool, "invalid argument: %s", parser->error_msg);
		} else {
			return str_printf(extpool, "invalid argument");
		}
	case PARSER_ERROR_IO:
		if (parser->error_msg) {
			return str_printf(extpool, "%s: IO error: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "%s: IO error", lines);
		}
	case PARSER_ERROR_AST_BUILD_FAILED:
		if (parser->error_msg) {
			return str_printf(extpool, "error building AST: %s", parser->error_msg);
		} else {
			return str_printf(extpool, "%s: error building AST", lines);
		}
		break;
	case PARSER_ERROR_UNSPECIFIED:
		if (parser->error_msg) {
			return str_printf(extpool, "%s: parse error: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "%s: parse error", lines);
		}
	}
	panic("unhandled parser error: %d", parser->error);
}

void
parser_enqueue_output(struct Parser *parser, const char *s)
{
	panic_unless(s, "parser_enqueue_output() is not NULL-safe");
	array_append(parser->result, str_dup(NULL, s));
}

static void
parser_propagate_goalcol(struct ParserFindGoalcolsState *this)
{
	this->moving_goalcol = MAX(16, this->moving_goalcol);
	ARRAY_FOREACH(this->nodes, struct AST *, node) {
		node->meta.goalcol = this->moving_goalcol;
	}

	this->moving_goalcol = 0;
	array_truncate(this->nodes);
}

static enum ASTWalkState
parser_find_goalcols_walker(struct AST *node, struct ParserFindGoalcolsState *this)
{
	if (this->parser->error != PARSER_ERROR_OK) {
		return AST_WALK_STOP;
	}

	switch (node->type) {
	case AST_COMMENT:
		/* Ignore comments in between variables and
		 * treat variables after them as part of the
		 * same block, i.e., indent them the same way.
		 */
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			if (!is_comment(line) && array_len(this->nodes) > 0) {
				parser_propagate_goalcol(this);
			}
		}
		break;
	case AST_VARIABLE:
		if (array_len(node->variable.words) > 0) {
			if (skip_goalcol(this->parser, node->variable.name)) {
				node->meta.goalcol = indent_goalcol(node->variable.name, node->variable.modifier);
			} else {
				array_append(this->nodes, node);
				this->moving_goalcol = MAX(indent_goalcol(node->variable.name, node->variable.modifier), this->moving_goalcol);
			}
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(parser_find_goalcols_walker, node, this);
	return AST_WALK_CONTINUE;
}

void
parser_find_goalcols(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);
	struct ParserFindGoalcolsState this = {
		.parser = parser,
		.moving_goalcol = 0,
		.nodes = mempool_array(pool),
	};
	parser_find_goalcols_walker(parser->ast, &this);
	parser_propagate_goalcol(&this);
}

void
print_newline_array(struct Parser *parser, struct AST *node, struct Array *arr)
{
	SCOPE_MEMPOOL(pool);

	size_t startlen = strlen(node->variable.name);
	parser_enqueue_output(parser, node->variable.name);
	if (str_endswith(node->variable.name, "+")) {
		startlen++;
		parser_enqueue_output(parser, " ");
	}
	parser_enqueue_output(parser, ASTVariableModifier_humanize[node->variable.modifier]);
	startlen += strlen(ASTVariableModifier_humanize[node->variable.modifier]);

	size_t ntabs;
	if (startlen > MAX(16, node->meta.goalcol)) {
		ntabs = ceil((startlen - MAX(16, node->meta.goalcol)) / 8.0);
	} else {
		ntabs = ceil((MAX(16, node->meta.goalcol) - startlen) / 8.0);
	}
	char *sep = str_repeat(pool, "\t", ntabs);

	if (array_len(arr) == 0) {
		if (node->variable.comment && strlen(node->variable.comment) > 0) {
			parser_enqueue_output(parser, sep);
			parser_enqueue_output(parser, node->variable.comment);
		}
		parser_enqueue_output(parser, "\n");
		parser_set_error(parser, PARSER_ERROR_OK, NULL);
		return;
	}

	const char *end = " \\\n";
	ARRAY_FOREACH(arr, const char *, line) {
		if (!line || strlen(line) == 0) {
			continue;
		}
		if (line_index == array_len(arr) - 1) {
			end = "";
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		if (line_index == 0) {
			size_t ntabs = ceil(MAX(16, node->meta.goalcol) / 8.0);
			sep = str_repeat(pool, "\t", ntabs);
		}
	}

	if (node->variable.comment && strlen(node->variable.comment) > 0) {
		parser_enqueue_output(parser, " ");
		parser_enqueue_output(parser, node->variable.comment);
	}
	parser_enqueue_output(parser, "\n");
}

void
print_token_array(struct Parser *parser, struct AST *node, struct Array *tokens)
{
	SCOPE_MEMPOOL(pool);

	if (array_len(tokens) < 2) {
		print_newline_array(parser, node, tokens);
		return;
	}

	struct Array *arr = mempool_array(pool);
	size_t wrapcol;
	if (ignore_wrap_col(parser, node->variable.name, node->variable.modifier)) {
		wrapcol = 99999999;
	} else {
		/* Minus ' \' at end of line */
		wrapcol = parser->settings.wrapcol - node->meta.goalcol - 2;
	}

	struct Array *row = mempool_array(pool);
	size_t rowlen = 0;
	const char *token = NULL;
	ARRAY_FOREACH(tokens, const char *, t) {
		token = t;
		size_t tokenlen = strlen(token);
		if (tokenlen == 0) {
			continue;
		}
		if ((rowlen + tokenlen) > wrapcol) {
			if (rowlen == 0) {
				array_append(arr, token);
				continue;
			} else {
				array_append(arr, str_join(pool, row, ""));
				array_truncate(row);
				rowlen = 0;
			}
		}
		if (rowlen > 0) {
			array_append(row, " ");
			rowlen++;
		}
		array_append(row, token);
		rowlen += tokenlen;
	}
	if (token && rowlen > 0 && array_len(arr) < array_len(tokens)) {
		array_append(arr, str_join(pool, row, ""));
	}
	print_newline_array(parser, node, arr);
}

void
parser_output_print_rawlines(struct Parser *parser, struct ASTLineRange *lines)
{
	for (size_t i = lines->a; i < lines->b; i++) {
		parser_enqueue_output(parser, array_get(parser->rawlines, i - 1));
		parser_enqueue_output(parser, "\n");
	}
}

void
parser_output_print_target_command(struct Parser *parser, struct AST *node)
{
	if (array_len(node->targetcommand.words) == 0) {
		return;
	}

	SCOPE_MEMPOOL(pool);
	struct Array *commands = mempool_array(pool);
	struct Array *merge = mempool_array(pool);
	const char *command = NULL;
	int wrap_after = 0;
	ARRAY_FOREACH(node->targetcommand.words, const char *, word) {
		panic_unless(word && strlen(word) != 0, "target command token is empty");

		if (command == NULL) {
			command = word;
			if (*command == '@') {
				command++;
			}
		}
		if (target_command_should_wrap(word)) {
			command = NULL;
		}

		if (command &&
		    (strcmp(command, "${SED}") == 0 ||
		     strcmp(command, "${REINPLACE_CMD}") == 0)) {
			if (strcmp(word, "-e") == 0 || strcmp(word, "-i") == 0) {
				array_append(merge, word);
				wrap_after = 1;
				continue;
			}
		}

		array_append(merge, word);
		array_append(commands, str_join(pool, merge, " "));
		if (wrap_after) {
			// An empty string is abused as a "wrap line here" marker
			array_append(commands, str_dup(pool, ""));
			wrap_after = 0;
		}
		array_truncate(merge);
	}
	if (array_len(merge) > 0) {
		array_append(commands, str_join(pool, merge, " "));
		if (wrap_after) {
			// An empty string is abused as a "wrap line here" marker
			array_append(commands, str_dup(pool, ""));
			wrap_after = 0;
		}
	}
	merge = NULL;

	const char *endnext = "\\\n";
	const char *endword = " ";
	const char *startlv0 = "";
	const char *startlv1 = "\t";
	const char *startlv2 = "\t\t";
	const char *start = startlv0;

	// Find the places we need to wrap to the next line.
	struct Set *wraps = mempool_set(pool, NULL, NULL, NULL);
	size_t column = 8;
	int complexity = 0;
	size_t command_i = 0;
	ARRAY_FOREACH(commands, char *, word) {
		if (command == NULL) {
			command = word;
			command_i = word_index;
		}
		if (target_command_should_wrap(word)) {
			command = NULL;
			command_i = 0;
		}

		for (char *c = word; *c != 0; c++) {
			switch (*c) {
			case '`':
			case '(':
			case ')':
			case '[':
			case ']':
			case ';':
				complexity++;
				break;
			}
		}

		if (start == startlv1 || start == startlv2) {
			start = startlv0;
		}

		column += strlen(start) * 8 + strlen(word);
		if (column > parser->settings.target_command_format_wrapcol ||
		    strcmp(word, "") == 0 || target_command_should_wrap(word) ||
		    (command && word_index > command_i && target_command_wrap_after_each_token(command))) {
			if (word_index + 1 < array_len(commands)) {
				char *next = array_get(commands, word_index + 1);
				if (strcmp(next, "") == 0 || target_command_should_wrap(next)) {
					continue;
				}
			}
			start = startlv2;
			column = 16;
			set_add(wraps, (void *)word_index);
		}
	}

	if (!(parser->settings.behavior & PARSER_FORMAT_TARGET_COMMANDS) ||
	    complexity > parser->settings.target_command_format_threshold) {
		if (!node->edited) {
			struct ASTLineRange range = { .a = node->line_start.a, .b = node->line_end.b };
			parser_output_print_rawlines(parser, &range);
			return;
		}
	}

	parser_enqueue_output(parser, startlv1);
	int wrapped = 0;
	ARRAY_FOREACH(commands, const char *, word) {
		if (wrapped) {
			parser_enqueue_output(parser, startlv2);
		}
		wrapped = set_contains(wraps, (void *)word_index);

		parser_enqueue_output(parser, word);
		if (wrapped) {
			if (word_index < array_len(node->targetcommand.words) - 1) {
				if (strcmp(word, "") != 0) {
					parser_enqueue_output(parser, endword);
				}
				parser_enqueue_output(parser, endnext);
			}
		} else if (word_index < array_len(node->targetcommand.words) - 1) {
			parser_enqueue_output(parser, endword);
		}
	}

	if (node->targetcommand.comment && strlen(node->targetcommand.comment) > 0) {
		parser_enqueue_output(parser, " ");
		parser_enqueue_output(parser, node->targetcommand.comment);
	}
	parser_enqueue_output(parser, "\n");
}

void
parser_output_prepare(struct Parser *parser)
{
	if (!parser->read_finished) {
		parser_read_finish(parser);
	}

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	if (parser->settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_RAWLINES) {
		/* no-op */
	} else if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser_output_reformatted(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}

	if (parser->settings.behavior & PARSER_OUTPUT_DIFF) {
		parser_output_diff(parser);
	}
}

static int
matches_opt_use_prefix_helper(char c)
{
	return isupper(c) || islower(c) || isdigit(c) || c == '-' || c == '_';
}

static int
matches_opt_use_prefix(const char *s)
{
	// ^([-_[:upper:][:lower:][:digit:]]+)
	if (!matches_opt_use_prefix_helper(*s)) {
		return 0;
	}
	size_t len = strlen(s);
	size_t i;
	for (i = 1; i < len && matches_opt_use_prefix_helper(s[i]); i++);

	// \+?
	if (s[i] == '+') {
		i++;
	}

	// =
	if (s[i] == '=') {
		return 1;
	}

	return 0;
}

struct Array *
parser_output_sort_opt_use(struct Parser *parser, struct Mempool *pool, struct ASTVariable *var, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return arr;
	}

	int opt_use = 0;
	char *helper = NULL;
	if (is_options_helper(pool, parser, var->name, NULL, &helper, NULL)) {
		if (strcmp(helper, "USE") == 0 || strcmp(helper, "USE_OFF") == 0)  {
			opt_use = 1;
		} else if (strcmp(helper, "VARS") == 0 || strcmp(helper, "VARS_OFF") == 0) {
			opt_use = 0;
		} else {
			return arr;
		}
	} else {
		return arr;
	}

	struct Array *up = mempool_array(pool);
	ARRAY_FOREACH(arr, const char *, t) {
		if (!matches_opt_use_prefix(t)) {
			array_append(up, t);
			continue;
		}
		char *suffix = strchr(t, '=');
		if (suffix == NULL) {
			array_append(up, t);
			continue;
		}
		suffix++;

		char *prefix = str_map(pool, t, suffix - t, toupper);
		enum ASTVariableModifier mod = AST_VARIABLE_MODIFIER_ASSIGN;
		if ((suffix - t) >= 1 && prefix[suffix - t - 1] == '=') {
			prefix[suffix - t - 1] = 0;
		}
		if ((suffix - t) >= 2 && prefix[suffix - t - 2] == '+') {
			mod = AST_VARIABLE_MODIFIER_APPEND;
			prefix[suffix - t - 2] = 0;
		}
		struct Array *buf = mempool_array(pool);
		if (opt_use) {
			char *var = str_printf(pool, "USE_%s", prefix);
			array_append(buf, prefix);
			array_append(buf, ASTVariableModifier_humanize[mod]);
			struct CompareTokensData data = {
				.parser = parser,
				.var = var,
			};
			struct Array *values = str_split(pool, suffix, ",");
			array_sort(values, compare_tokens, &data);
			ARRAY_FOREACH(values, const char *, t2) {
				array_append(buf, t2);
				if (t2_index < array_len(values) - 1) {
					array_append(buf, ",");
				}
			}
		} else {
			array_append(buf, prefix);
			array_append(buf, ASTVariableModifier_humanize[mod]);
			array_append(buf, suffix);
		}

		array_append(up, str_join(pool, buf, ""));
	}
	return up;
}

void
parser_output_print_variable(struct Parser *parser, struct Mempool *pool, struct AST *node)
{
	panic_unless(node->type == AST_VARIABLE, "expected AST_VARIABLE");
	struct Array *words = node->variable.words;

	/* Leave variables unformatted that have $\ in them. */
	struct ASTLineRange range = { .a = node->line_start.a, .b = node->line_end.b };
	if ((array_len(words) == 1 && strstr(array_get(words, 0), "$\001") != NULL) ||
	    (leave_unformatted(parser, node->variable.name) &&
	     !node->edited)) {
		parser_output_print_rawlines(parser, &range);
		return;
	}

	if (!node->edited &&
	    (parser->settings.behavior & PARSER_OUTPUT_EDITED)) {
		parser_output_print_rawlines(parser, &range);
		return;
	}

	if (!(parser->settings.behavior & PARSER_UNSORTED_VARIABLES) &&
	    should_sort(parser, node->variable.name, node->variable.modifier)) {
		words = parser_output_sort_opt_use(parser, pool, &node->variable, words);
		struct CompareTokensData data = {
			.parser = parser,
			.var = node->variable.name,
		};
		array_sort(words, compare_tokens, &data);
	}

	if (print_as_newlines(parser, node->variable.name)) {
		print_newline_array(parser, node, words);
	} else {
		print_token_array(parser, node, words);
	}
}

static void
parser_output_category_makefile_reformatted(struct Parser *parser, struct AST *node)
{
	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	SCOPE_MEMPOOL(pool);
	// Category Makefiles have a strict layout so we can simply
	// dump everything out but also verify everything when doing so.
	// We do not support editing/formatting the top level Makefile.
	const char *indent = "    ";

	switch (node->type) {
	case AST_ROOT:
		ARRAY_FOREACH(node->root.body, struct AST *, child) {
			parser_output_category_makefile_reformatted(parser, child);
		}
		return;
	case AST_DELETED:
		break;
	case AST_INCLUDE:
		if (node->include.type == AST_INCLUDE_BMAKE &&
		    node->include.sys && node->include.path &&
		    strcmp(node->include.path, "bsd.port.subdir.mk") == 0) {
			parser_enqueue_output(parser, ".include <bsd.port.subdir.mk>\n");
			return;
		}
		break;
	case AST_EXPR:
	case AST_IF:
	case AST_FOR:
	case AST_TARGET:
	case AST_TARGET_COMMAND:
		parser_set_error(parser, PARSER_ERROR_UNSPECIFIED,
				 "unsupported node type in category Makefile"); // TODO
		return;
	case AST_COMMENT:
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			parser_enqueue_output(parser, line);
			parser_enqueue_output(parser, "\n");
		}
		return;
	case AST_VARIABLE:
		if (strcmp(node->variable.name, "COMMENT") == 0) {
			parser_enqueue_output(parser, indent);
			parser_enqueue_output(parser, "COMMENT = ");
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				parser_enqueue_output(parser, word);
				if ((word_index + 1) < array_len(node->variable.words)) {
					parser_enqueue_output(parser, " ");
				}
			}
			parser_enqueue_output(parser, "\n");
		} else if (strcmp(node->variable.name, "SUBDIR") == 0) {
			array_sort(node->variable.words, str_compare, NULL);
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				parser_enqueue_output(parser, indent);
				parser_enqueue_output(parser, "SUBDIR += ");
				parser_enqueue_output(parser, word);
				parser_enqueue_output(parser, "\n");
			}
		} else {
			parser_set_error(parser, PARSER_ERROR_UNSPECIFIED,
					 str_printf(pool, "unsupported variable in category Makefile: %s", node->variable.name));
			return;
		}
		return;
	}
}

static enum ASTWalkState
parser_output_reformatted_walker(struct Parser *parser, struct AST *node)
{
	SCOPE_MEMPOOL(pool);

	int edited = node->edited || (!(parser->settings.behavior & PARSER_OUTPUT_EDITED) && (parser->settings.behavior & PARSER_OUTPUT_REFORMAT));
	switch (node->type) {
	case AST_ROOT:
		ARRAY_FOREACH(node->root.body, struct AST *, child) {
			AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
		}
		break;
	case AST_DELETED:
		break;
	case AST_COMMENT:
		if (node->edited) { // Ignore PARSER_OUTPUT_REFORMAT
			ARRAY_FOREACH(node->comment.lines, const char *, line) {
				parser_enqueue_output(parser, line);
				parser_enqueue_output(parser, "\n");
			}
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
		}
		break;
	case AST_INCLUDE:
		if (edited) {
			const char *dot;
			switch (node->include.type) {
			case AST_INCLUDE_POSIX:
				dot = "";
				break;
			default:
				dot = ".";
				break;
			}
			const char *name = ASTIncludeType_identifier[node->include.type];
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_printf(pool, "%s%s%s", dot, str_repeat(pool, " ", node->include.indent), name));
			if (node->include.path) {
				if (node->include.type == AST_INCLUDE_POSIX) {
					parser_enqueue_output(parser, " ");
					parser_enqueue_output(parser, node->include.path);
				} else if (node->include.sys) {
					parser_enqueue_output(parser, str_printf(pool, " <%s>", node->include.path));
				} else {
					parser_enqueue_output(parser, str_printf(pool, " \"%s\"", node->include.path));
				}
			} else if (node->include.type != AST_INCLUDE_POSIX) {
				parser_enqueue_output(parser, " \"\"");
			}
			if (node->include.comment && strlen(node->include.comment) > 0) {
				parser_enqueue_output(parser, str_printf(pool, " %s", node->include.comment));
			}
			parser_enqueue_output(parser, "\n");
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
		}
		break;
	case AST_EXPR:
		if (edited) {
			const char *name = ASTExprType_identifier[node->expr.type];
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_printf(pool, ".%s%s %s",
				str_repeat(pool, " ", node->expr.indent), name, str_join(pool, node->expr.words, " ")));
			if (node->expr.comment && strlen(node->expr.comment) > 0) {
				parser_enqueue_output(parser, " ");
				parser_enqueue_output(parser, node->expr.comment);
			}
			parser_enqueue_output(parser, "\n");
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
		}
		break;
	case AST_FOR:
		if (edited) {
			const char *indent = str_repeat(pool, " ", node->forexpr.indent);
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_printf(pool, ".%sfor %s in %s",
				indent,
				str_join(pool, node->forexpr.bindings, " "),
				str_join(pool, node->forexpr.words, " ")));
			if (node->forexpr.comment && strlen(node->forexpr.comment) > 0) {
				parser_enqueue_output(parser, " ");
				parser_enqueue_output(parser, node->forexpr.comment);
			}
			parser_enqueue_output(parser, "\n");
			ARRAY_FOREACH(node->forexpr.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			parser_enqueue_output(parser, str_printf(pool, ".%sendfor", indent));
			if (node->forexpr.end_comment && strlen(node->forexpr.end_comment) > 0) {
				parser_enqueue_output(parser, " ");
				parser_enqueue_output(parser, node->forexpr.end_comment);
			}
			parser_enqueue_output(parser, "\n");
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->forexpr.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			parser_output_print_rawlines(parser, &node->line_end);
		}
		break;
	case AST_IF:
		if (edited) {
			const char *prefix = "";
			if (node->ifexpr.ifparent) {
				prefix = "el";
			}
			parser_enqueue_output(parser, str_printf(pool, ".%s%s%s ", str_repeat(pool, " ", node->ifexpr.indent),
				prefix, ASTIfType_humanize[node->ifexpr.type]));
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_join(pool, node->ifexpr.test, " "));
			if (node->ifexpr.comment && strlen(node->ifexpr.comment) > 0) {
				parser_enqueue_output(parser, " ");
				parser_enqueue_output(parser, node->ifexpr.comment);
			}
			parser_enqueue_output(parser, "\n");
			ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			if (array_len(node->ifexpr.orelse) > 0) {
				struct AST *next = array_get(node->ifexpr.orelse, 0);
				if (next && next->type == AST_IF && next->ifexpr.type == AST_IF_ELSE) {
					parser_output_print_rawlines(parser, &next->line_start); // .else
					ARRAY_FOREACH(next->ifexpr.body, struct AST *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				} else {
					ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				}
			}
			unless (node->ifexpr.ifparent) { // .endif
				parser_enqueue_output(parser, str_printf(pool, ".%sendif", str_repeat(pool, " ", node->ifexpr.indent)));
				if (node->ifexpr.end_comment && strlen(node->ifexpr.end_comment) > 0) {
					parser_enqueue_output(parser, " ");
					parser_enqueue_output(parser, node->ifexpr.end_comment);
				}
				parser_enqueue_output(parser, "\n");
			}
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->ifexpr.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			if (array_len(node->ifexpr.orelse) > 0) {
				struct AST *next = array_get(node->ifexpr.orelse, 0);
				if (next && next->type == AST_IF && next->ifexpr.type == AST_IF_ELSE) {
					parser_output_print_rawlines(parser, &next->line_start); // .else
					ARRAY_FOREACH(next->ifexpr.body, struct AST *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				} else {
					ARRAY_FOREACH(node->ifexpr.orelse, struct AST *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				}
			}
			unless (node->ifexpr.ifparent) { // .endif
				parser_output_print_rawlines(parser, &node->line_end);
			}
		}
		break;
	case AST_TARGET:
		if (edited) {
			const char *sep = "";
			if (array_len(node->target.dependencies) > 0) {
				sep = " ";
			}
			parser_enqueue_output(parser, str_printf(pool, "%s:%s%s",
				str_join(pool, node->target.sources, " "),
				sep,
				str_join(pool, node->target.dependencies, " ")));
			if (node->target.comment && strlen(node->target.comment) > 0) {
				parser_enqueue_output(parser, " ");
				parser_enqueue_output(parser, node->target.comment);
			}
			parser_enqueue_output(parser, "\n");
			ARRAY_FOREACH(node->target.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->target.body, struct AST *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
		}
		break;
	case AST_TARGET_COMMAND:
		parser_output_print_target_command(parser, node);
		break;
	case AST_VARIABLE:
		parser_output_print_variable(parser, pool, node);
		break;
	}

	return AST_WALK_CONTINUE;
}

void
parser_output_reformatted(struct Parser *parser)
{
	parser_find_goalcols(parser);
	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	if (parser_is_category_makefile(parser->ast, parser)) {
		parser_output_category_makefile_reformatted(parser, parser->ast);
	} else {
		parser_output_reformatted_walker(parser, parser->ast);
	}
}

void
parser_output_diff(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	// Normalize result: one element = one line like parser->rawlines
	struct Array *lines = str_split(pool, str_join(pool, parser->result, ""), "\n");
	array_pop(lines);

	struct diff *p = array_diff(parser->rawlines, lines, pool, str_compare, NULL);
	if (p == NULL) {
		parser_set_error(parser, PARSER_ERROR_UNSPECIFIED,
				 str_printf(pool, "could not create diff"));
		return;
	}

	ARRAY_FOREACH(parser->result, char *, line) {
		free(line);
	}
	array_truncate(parser->result);

	if (p->editdist > 0) {
		const char *filename = parser->settings.filename;
		if (filename == NULL) {
			filename = "Makefile";
		}
		const char *color_add = ANSI_COLOR_GREEN;
		const char *color_delete = ANSI_COLOR_RED;
		const char *color_reset = ANSI_COLOR_RESET;
		int nocolor = parser->settings.behavior & PARSER_OUTPUT_NO_COLOR;
		if (nocolor) {
			color_add = "";
			color_delete = "";
			color_reset = "";
		}
		char *buf = str_printf(NULL, "%s--- %s\n%s+++ %s%s\n", color_delete, filename, color_add, filename, color_reset);
		array_append(parser->result, buf);
		array_append(parser->result, diff_to_patch(p, NULL, NULL, NULL, parser->settings.diff_context, !nocolor));
		parser_set_error(parser, PARSER_ERROR_DIFFERENCES_FOUND, NULL);
	}
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);
	size_t len = 0;
	char *buf = NULL;

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	if (parser->settings.debug_level >= 2) {
		struct ParserASTBuilder *builder = mempool_add(pool, parser_astbuilder_from_ast(parser, parser->ast), parser_astbuilder_free);
		FILE *f = open_memstream(&buf, &len);
		panic_unless(f, "open_memstream: %s", strerror(errno));
		parser_astbuilder_print_token_stream(builder, f);
		fclose(f);
		parser_enqueue_output(parser, buf);
		free(buf);
	} else if (parser->settings.debug_level == 1) {
		FILE *f = open_memstream(&buf, &len);
		panic_unless(f, "open_memstream: %s", strerror(errno));
		ast_print(parser->ast, f);
		fclose(f);
		parser_enqueue_output(parser, buf);
		free(buf);
	}
}

enum ParserError
parser_read_from_file(struct Parser *parser, FILE *fp)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	LINE_FOREACH(fp, line) {
		parser_tokenizer_feed_line(parser->tokenizer, line, line_len);
		if (parser->error != PARSER_ERROR_OK) {
			return parser->error;
		}
		array_append(parser->rawlines, str_ndup(NULL, line, line_len));
	}

	return PARSER_ERROR_OK;
}

enum ParserError
parser_read_finish(struct Parser *parser)
{
	panic_if(parser->read_finished, "parser_read_finish() called multiple times");

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	if (parser_tokenizer_finish(parser->tokenizer) != PARSER_ERROR_OK) {
		return parser->error;
	}

	for (size_t i = 0; i <= PARSER_METADATA_USES; i++) {
		parser->metadata_valid[i] = 0;
	}

	parser->read_finished = 1;
	ast_free(parser->ast);
	parser->ast = parser_astbuilder_finish(parser->builder);
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}
	parser_tokenizer_free(parser->tokenizer);
	parser->tokenizer = NULL;

	if (parser->settings.behavior & PARSER_LOAD_LOCAL_INCLUDES &&
	    PARSER_ERROR_OK != parser_load_includes(parser)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_SANITIZE_COMMENTS &&
	    PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_sanitize_comments, NULL)) {
		return parser->error;
	}

	if (PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_sanitize_cmake_args, NULL)) {
		return parser->error;
	}

	// To properly support editing category Makefiles always
	// collapse all the SUBDIR into one assignment regardless
	// of settings.
	if ((parser_is_category_makefile(parser->ast, parser) ||
	     parser->settings.behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES) &&
	    PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_collapse_adjacent_variables, NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_SANITIZE_APPEND &&
	    PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_sanitize_append_modifier, NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_DEDUP_TOKENS &&
	    PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_dedup_tokens, NULL)) {
		return parser->error;
	}

	if (PARSER_ERROR_OK != parser_edit(parser, NULL, refactor_remove_consecutive_empty_lines, NULL)) {
		return parser->error;
	}

	return parser->error;
}

struct AST *
parser_ast(struct Parser *parser)
{
	panic_unless(parser->read_finished, "parser_ast() called before parser_read_finish()");
	if (parser->error == PARSER_ERROR_OK) {
		return parser->ast;
	} else {
		return NULL;
	}
}

enum ParserError
parser_output_write_to_file(struct Parser *parser, FILE *fp)
{
	SCOPE_MEMPOOL(pool);

	parser_output_prepare(parser);
	if (fp == NULL ||
	    (parser->error != PARSER_ERROR_OK &&
	     parser->error != PARSER_ERROR_DIFFERENCES_FOUND)) {
		return parser->error;
	}

	int fd = fileno(fp);
	if (parser->settings.behavior & PARSER_OUTPUT_INPLACE) {
		if (lseek(fd, 0, SEEK_SET) < 0) {
			parser_set_error(parser, PARSER_ERROR_IO,
					 str_printf(pool, "lseek: %s", strerror(errno)));
			return parser->error;
		}
		if (ftruncate(fd, 0) < 0) {
			parser_set_error(parser, PARSER_ERROR_IO,
					 str_printf(pool, "ftruncate: %s", strerror(errno)));
			return parser->error;
		}
	}

	size_t len = array_len(parser->result);
	if (len == 0) {
		return parser->error;
	}

	size_t iov_len = MIN(len, IOV_MAX);
	struct iovec *iov = mempool_take(pool, xrecallocarray(NULL, 0, iov_len, sizeof(struct iovec)));
	for (size_t i = 0; i < len;) {
		size_t j = 0;
		for (; i < len && j < iov_len; j++) {
			char *s = array_get(parser->result, i++);
			iov[j].iov_base = s;
			iov[j].iov_len = strlen(s);
		}
		if (writev(fd, iov, j) < 0) {
			parser_set_error(parser, PARSER_ERROR_IO,
					 str_printf(pool, "writev: %s", strerror(errno)));
			return parser->error;
		}
	}

	/* Collect garbage */
	ARRAY_FOREACH(parser->result, char *, line) {
		free(line);
	}
	array_truncate(parser->result);

	return parser->error;
}

enum ParserError
parser_read_from_buffer(struct Parser *parser, const char *input, size_t len)
{
	SCOPE_MEMPOOL(pool);

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	ARRAY_FOREACH(str_nsplit(pool, input, len, "\n"), const char *, line) {
		array_append(parser->rawlines, str_dup(NULL, line));
		parser_tokenizer_feed_line(parser->tokenizer, line, strlen(line));
		if (parser->error != PARSER_ERROR_OK) {
			break;
		}
	}

	return parser->error;
}

static const char *
process_include(struct Parser *parser, struct Mempool *extpool, const char *curdir, const char *filename)
{
	SCOPE_MEMPOOL(pool);

	if (str_startswith(filename, "${MASTERDIR}/")) {
		filename += strlen("${MASTERDIR}/");
		const char *masterdir = parser_metadata(parser, PARSER_METADATA_MASTERDIR);
		unless (masterdir) {
			masterdir = ".";
		}
		filename = str_printf(pool, "%s/%s", masterdir, filename);
	}

	struct Array *path = mempool_array(pool);
	if (str_startswith(filename, "${.PARSEDIR}/")) {
		array_append(path, curdir);
		array_append(path, filename + strlen("${.PARSEDIR}/"));
	} else if (str_startswith(filename, "${.CURDIR}/")) {
		array_append(path, curdir);
		array_append(path, filename + strlen("${.CURDIR}/"));
	} else if (str_startswith(filename, "${.CURDIR:H}/")) {
		array_append(path, curdir);
		array_append(path, "..");
		array_append(path, filename + strlen("${.CURDIR:H}/"));
	} else if (str_startswith(filename, "${.CURDIR:H:H}/")) {
		array_append(path, curdir);
		array_append(path, "..");
		array_append(path, "..");
		array_append(path, filename + strlen("${.CURDIR:H:H}/"));
	} else if (str_startswith(filename, "${PORTSDIR}/")) {
		array_append(path, filename + strlen("${PORTSDIR}/"));
	} else if (str_startswith(filename, "${FILESDIR}/")) {
		array_append(path, curdir);
		array_append(path, "files");
		array_append(path, filename + strlen("${FILESDIR}/"));
	} else {
		array_append(path, curdir);
		array_append(path, filename);
	}

	return path_join(extpool, path);
}

static enum ASTWalkState
parser_load_includes_walker(struct AST *node, struct Parser *parser, int portsdir)
{
	SCOPE_MEMPOOL(pool);

	switch (node->type) {
	case AST_INCLUDE:
		if (node->include.type == AST_INCLUDE_BMAKE && !node->include.loaded && !node->include.sys) {
			struct Array *components = path_split(pool, parser->settings.filename);
			if (array_len(components) > 0) {
				array_truncate_at(components, array_len(components) - 1);
			}
			const char *curdir = path_join(pool, components);
			const char *path = process_include(parser, pool, curdir, node->include.path);
			unless (path) {
				parser_set_error(parser, PARSER_ERROR_IO, str_printf(pool, "cannot open include: %s", node->include.path));
				return AST_WALK_STOP;
			}
			FILE *f = fileopenat(pool, portsdir, path);
			if (f == NULL) {
				parser_set_error(parser, PARSER_ERROR_IO, str_printf(pool, "cannot open include: %s: %s", path, strerror(errno)));
				return AST_WALK_STOP;
			}
			struct ParserSettings settings = parser->settings;
			settings.behavior &= ~PARSER_LOAD_LOCAL_INCLUDES;
			settings.filename = path;
			struct Parser *incparser = parser_new(pool, &settings);
			if (PARSER_ERROR_OK != parser_read_from_file(incparser, f)) {
				parser_set_error(parser, PARSER_ERROR_IO, str_printf(pool, "cannot open include: %s: %s", path, strerror(errno)));
				return AST_WALK_STOP;
			}
			if (PARSER_ERROR_OK != parser_read_finish(incparser)) {
				parser_set_error(parser, PARSER_ERROR_IO, parser_error_tostring(incparser, pool));
				return AST_WALK_STOP;
			}

			struct AST *incroot = incparser->ast;
			// take ownership of incparser's AST
			incparser->ast = NULL;
			mempool_add(node->pool, incroot, ast_free);
			panic_unless(incroot->type == AST_ROOT, "incroot != AST_ROOT");
			ARRAY_FOREACH(incroot->root.body, struct AST *, child) {
				child->parent = node;
				array_append(node->include.body, child);
			}
			node->edited = 1;
			node->include.loaded = 1;
		}
		return AST_WALK_CONTINUE;
	case AST_FOR:
	case AST_IF:
	case AST_TARGET:
		// Shorten the walk; we only care about top level includes for now
		return AST_WALK_CONTINUE;
	case AST_ROOT:
	case AST_COMMENT:
	case AST_DELETED:
	case AST_EXPR:
	case AST_TARGET_COMMAND:
	case AST_VARIABLE:
		break;
	}

	AST_WALK_DEFAULT(parser_load_includes_walker, node, parser, portsdir);
	return AST_WALK_CONTINUE;
}

enum ParserError
parser_load_includes(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);
	panic_unless(parser->read_finished, "parser_load_includes() called before parser_read_finish()");

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	if (parser->settings.portsdir < 0) {
		parser_set_error(parser, PARSER_ERROR_IO, str_printf(pool, "invalid portsdir"));
		return parser->error;
	}

	parser_load_includes_walker(parser->ast, parser, parser->settings.portsdir);
	return parser->error;
}

enum ParserError
parser_edit(struct Parser *parser, struct Mempool *extpool, ParserEditFn f, void *userdata)
{
	SCOPE_MEMPOOL(pool);
	panic_unless(parser->read_finished, "parser_edit() called before parser_read_finish()");

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	f(parser, parser->ast, extpool, userdata);
	if (parser->error != PARSER_ERROR_OK) {
		parser_set_error(parser, PARSER_ERROR_EDIT_FAILED, parser_error_tostring(parser, pool));
	}

	ast_balance(parser->ast);

	return parser->error;
}

struct ParserSettings parser_settings(struct Parser *parser)
{
	return parser->settings;
}

static void
parser_meta_values_helper(struct Set *set, const char *var, char *value)
{
	if (strcmp(var, "USES") == 0) {
		char *buf = strchr(value, ':');
		if (buf != NULL) {
			char *val = str_ndup(NULL, value, buf - value);
			if (set_contains(set, val)) {
				free(val);
			} else {
				set_add(set, val);
			}
			return;
		}
	}

	if (!set_contains(set, value)) {
		set_add(set, str_dup(NULL, value));
	}
}

void
parser_meta_values(struct Parser *parser, const char *var, struct Set *set)
{
	SCOPE_MEMPOOL(pool);

	struct Array *tmp = NULL;
	if (parser_lookup_variable(parser, var, PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL)) {
		ARRAY_FOREACH(tmp, char *, value) {
			parser_meta_values_helper(set, var, value);
		}
	}

	struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
	SET_FOREACH(options, const char *, opt) {
		char *buf = str_printf(pool, "%s_VARS", opt);
		if (parser_lookup_variable(parser, buf, PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL)) {
			ARRAY_FOREACH(tmp, char *, value) {
				char *buf = str_printf(pool, "%s+=", var);
				if (str_startswith(value, buf)) {
					value += strlen(buf);
				} else {
					buf = str_printf(pool, "%s=", var);
					if (str_startswith(value, buf)) {
						value += strlen(buf);
					} else {
						continue;
					}
				}
				parser_meta_values_helper(set, var, value);
			}
		}

		buf = str_printf(pool, "%s_VARS_OFF", opt);
		if (parser_lookup_variable(parser, buf, PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL)) {
			ARRAY_FOREACH(tmp, char *, value) {
				char *buf = str_printf(pool, "%s+=", var);
				if (str_startswith(value, buf)) {
					value += strlen(buf);
				} else {
					buf = str_printf(pool, "%s=", var);
					if (str_startswith(value, buf)) {
						value += strlen(buf);
					} else {
						continue;
					}
				}
				parser_meta_values_helper(set, var, value);
			}
		}

#if PORTFMT_SUBPACKAGES
		if (strcmp(var, "USES") == 0 || strcmp(var, "SUBPACKAGES") == 0) {
#else
		if (strcmp(var, "USES") == 0) {
#endif
			buf = str_printf(pool, "%s_%s", opt, var);
			if (parser_lookup_variable(parser, buf, PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL)) {
				ARRAY_FOREACH(tmp, char *, value) {
					parser_meta_values_helper(set, var, value);
				}
			}

			buf = str_printf(pool, "%s_%s_OFF", opt, var);
			if (parser_lookup_variable(parser, buf, PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL)) {
				ARRAY_FOREACH(tmp, char *, value) {
					parser_meta_values_helper(set, var, value);
				}
			}
		}
	}
}

static void
parser_port_options_add_from_group(struct Parser *parser, const char *groupname)
{
	SCOPE_MEMPOOL(pool);

	struct Array *optmulti = NULL;
	if (parser_lookup_variable(parser, groupname, PARSER_LOOKUP_DEFAULT, pool, &optmulti, NULL)) {
		ARRAY_FOREACH(optmulti, char *, optgroupname) {
			if (!set_contains(parser->metadata[PARSER_METADATA_OPTION_GROUPS], optgroupname)) {
				set_add(parser->metadata[PARSER_METADATA_OPTION_GROUPS], str_dup(NULL, optgroupname));
			}
			char *optgroupvar = str_printf(pool, "%s_%s", groupname, optgroupname);
			struct Array *opts = NULL;
			if (parser_lookup_variable(parser, optgroupvar, PARSER_LOOKUP_DEFAULT, pool, &opts, NULL)) {
				ARRAY_FOREACH(opts, char *, opt) {
					if (!set_contains(parser->metadata[PARSER_METADATA_OPTIONS], opt)) {
						set_add(parser->metadata[PARSER_METADATA_OPTIONS], str_dup(NULL, opt));
					}
				}
			}
		}
	}
}

static void
parser_port_options_add_from_var(struct Parser *parser, const char *var)
{
	SCOPE_MEMPOOL(pool);

	struct Array *optdefine = NULL;
	if (parser_lookup_variable(parser, var, PARSER_LOOKUP_DEFAULT, pool, &optdefine, NULL)) {
		ARRAY_FOREACH(optdefine, char *, opt) {
			if (!set_contains(parser->metadata[PARSER_METADATA_OPTIONS], opt)) {
				set_add(parser->metadata[PARSER_METADATA_OPTIONS], str_dup(NULL, opt));
			}
		}
	}
}

void
parser_metadata_port_options(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);

	if (parser->metadata_valid[PARSER_METADATA_OPTIONS]) {
		return;
	}

	parser->metadata_valid[PARSER_METADATA_OPTION_DESCRIPTIONS] = 1;
	parser->metadata_valid[PARSER_METADATA_OPTION_GROUPS] = 1;
	parser->metadata_valid[PARSER_METADATA_OPTIONS] = 1;

#define FOR_EACH_ARCH(f, var) \
	for (size_t i = 0; i < known_architectures_len; i++) { \
		char *buf = str_printf(pool, "%s_%s", var, known_architectures[i]); \
		f(parser, buf); \
	}

	parser_port_options_add_from_var(parser, "OPTIONS_DEFINE");
	FOR_EACH_ARCH(parser_port_options_add_from_var, "OPTIONS_DEFINE");

	parser_port_options_add_from_group(parser, "OPTIONS_GROUP");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_GROUP");

	parser_port_options_add_from_group(parser, "OPTIONS_MULTI");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_MULTI");

	parser_port_options_add_from_group(parser, "OPTIONS_RADIO");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_RADIO");

	parser_port_options_add_from_group(parser, "OPTIONS_SINGLE");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_SINGLE");

#undef FOR_EACH_ARCH

	struct Set *opts[] = { parser->metadata[PARSER_METADATA_OPTIONS], parser->metadata[PARSER_METADATA_OPTION_GROUPS] };
	for (size_t i = 0; i < nitems(opts); i++) {
		if (opts[i]) SET_FOREACH(opts[i], const char *, opt) {
			char *var = str_printf(pool, "%s_DESC", opt);
			if (!map_contains(parser->metadata[PARSER_METADATA_OPTION_DESCRIPTIONS], var)) {
				char *desc;
				if (parser_lookup_variable_str(parser, var, PARSER_LOOKUP_FIRST, pool, &desc, NULL)) {
					map_add(parser->metadata[PARSER_METADATA_OPTION_DESCRIPTIONS], str_dup(NULL, var), str_dup(NULL, desc));
				}
			}
		}
	}
}

void
parser_metadata_alloc(struct Parser *parser)
{
	for (enum ParserMetadata meta = 0; meta <= PARSER_METADATA_USES; meta++) {
		switch (meta) {
		case PARSER_METADATA_OPTION_DESCRIPTIONS:
			parser->metadata[meta] = map_new(str_compare, NULL, free, free);
			break;
		case PARSER_METADATA_MASTERDIR:
			parser->metadata[meta] = NULL;
			break;
		default:
			parser->metadata[meta] = set_new(str_compare, NULL, free);
			break;
		}
	}
}

void
parser_metadata_free(struct Parser *parser)
{
	for (enum ParserMetadata i = 0; i <= PARSER_METADATA_USES; i++) {
		switch (i) {
		case PARSER_METADATA_MASTERDIR:
			free(parser->metadata[i]);
			break;
		case PARSER_METADATA_OPTION_DESCRIPTIONS:
			map_free(parser->metadata[i]);
			break;
		default:
			set_free(parser->metadata[i]);
			break;
		}
		parser->metadata[i] = NULL;
	}
}

void *
parser_metadata(struct Parser *parser, enum ParserMetadata meta)
{
	SCOPE_MEMPOOL(pool);

	if (!parser->metadata_valid[meta]) {
		switch (meta) {
		case PARSER_METADATA_CABAL_EXECUTABLES: {
			struct Set *uses = parser_metadata(parser, PARSER_METADATA_USES);
			if (set_contains(uses, "cabal")) {
				parser_meta_values(parser, "EXECUTABLES", parser->metadata[PARSER_METADATA_CABAL_EXECUTABLES]);
				if (set_len(parser->metadata[PARSER_METADATA_CABAL_EXECUTABLES]) == 0) {
					char *portname;
					if (parser_lookup_variable_str(parser, "PORTNAME", PARSER_LOOKUP_FIRST, pool, &portname, NULL)) {
						if (!set_contains(parser->metadata[PARSER_METADATA_CABAL_EXECUTABLES], portname)) {
							set_add(parser->metadata[PARSER_METADATA_CABAL_EXECUTABLES], str_dup(NULL, portname));
						}
					}
				}
			}
			break;
		} case PARSER_METADATA_FLAVORS: {
			parser_meta_values(parser, "FLAVORS", parser->metadata[PARSER_METADATA_FLAVORS]);
			struct Set *uses = parser_metadata(parser, PARSER_METADATA_USES);
			// XXX: Does not take into account USE_PYTHON=noflavors etc.
			for (size_t i = 0; i < static_flavors_len; i++) {
				if (set_contains(uses, (void*)static_flavors[i].uses) &&
				    !set_contains(parser->metadata[PARSER_METADATA_FLAVORS], (void*)static_flavors[i].flavor)) {
					set_add(parser->metadata[PARSER_METADATA_FLAVORS], str_dup(NULL, static_flavors[i].flavor));
				}
			}
			break;
		} case PARSER_METADATA_LICENSES:
			parser_meta_values(parser, "LICENSE", parser->metadata[PARSER_METADATA_LICENSES]);
			break;
		case PARSER_METADATA_MASTERDIR: {
			struct Array *tokens = NULL;
			if (parser_lookup_variable(parser, "MASTERDIR", PARSER_LOOKUP_FIRST | PARSER_LOOKUP_IGNORE_VARIABLES_IN_CONDITIIONALS, pool, &tokens, NULL)) {
				free(parser->metadata[meta]);
				parser->metadata[meta] = str_join(NULL, tokens, " ");
			}
			break;
		} case PARSER_METADATA_SHEBANG_LANGS:
			parser_meta_values(parser, "SHEBANG_LANG", parser->metadata[PARSER_METADATA_SHEBANG_LANGS]);
			break;
		case PARSER_METADATA_OPTION_DESCRIPTIONS:
		case PARSER_METADATA_OPTION_GROUPS:
		case PARSER_METADATA_OPTIONS:
			parser_metadata_port_options(parser);
			break;
		case PARSER_METADATA_POST_PLIST_TARGETS:
			parser_meta_values(parser, "POST_PLIST", parser->metadata[PARSER_METADATA_POST_PLIST_TARGETS]);
			break;
#if PORTFMT_SUBPACKAGES
		case PARSER_METADATA_SUBPACKAGES:
			if (!set_contains(parser->metadata[PARSER_METADATA_SUBPACKAGES], "main")) {
				// There is always a main subpackage
				set_add(parser->metadata[PARSER_METADATA_SUBPACKAGES], str_dup(NULL, "main"));
			}
			parser_meta_values(parser, "SUBPACKAGES", parser->metadata[PARSER_METADATA_SUBPACKAGES]);
			break;
#endif
		case PARSER_METADATA_USES:
			parser_meta_values(parser, "USES", parser->metadata[PARSER_METADATA_USES]);
			break;
		}
		parser->metadata_valid[meta] = 1;
	}

	return parser->metadata[meta];
}

static enum ASTWalkState
parser_lookup_target_walker(struct AST *node, const char *name, struct AST **retval)
{
	switch (node->type) {
	case AST_TARGET:
		ARRAY_FOREACH(node->target.sources, char *, src) {
			if (strcmp(src, name) == 0) {
				*retval = node;
				return AST_WALK_STOP;
			}
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(parser_lookup_target_walker, node, name, retval);

	return AST_WALK_CONTINUE;
}

struct AST *
parser_lookup_target(struct Parser *parser, const char *name)
{
	struct AST *node = NULL;
	parser_lookup_target_walker(parser->ast, name, &node);
	return node;
}

static enum ASTWalkState
parser_lookup_variable_walker(struct AST *node, struct Mempool *pool, const char *name, enum ParserLookupVariableBehavior behavior, struct Array *tokens, struct Array *comments, struct AST **retval)
{
	switch (node->type) {
	case AST_VARIABLE:
		if (strcmp(node->variable.name, name) == 0) {
			*retval = node;
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				array_append(tokens, str_dup(pool, word));
			}
			if (node->variable.comment && strlen(node->variable.comment) > 0) {
				array_append(comments, str_dup(pool, node->variable.comment));
			}
			if (behavior & PARSER_LOOKUP_FIRST) {
				return AST_WALK_STOP;
			}
		}
		break;
	case AST_FOR:
	case AST_IF:
	case AST_INCLUDE:
		if (behavior & PARSER_LOOKUP_IGNORE_VARIABLES_IN_CONDITIIONALS) {
			return AST_WALK_CONTINUE;
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(parser_lookup_variable_walker, node, pool, name, behavior, tokens, comments, retval);

	return AST_WALK_CONTINUE;
}

struct AST *
parser_lookup_variable(struct Parser *parser, const char *name, enum ParserLookupVariableBehavior behavior, struct Mempool *extpool, struct Array **retval, struct Array **comment)
{
	SCOPE_MEMPOOL(pool);
	struct Array *tokens = mempool_array(pool);
	struct Array *comments = mempool_array(pool);
	struct AST *node = NULL;
	parser_lookup_variable_walker(parser->ast, pool, name, behavior, tokens, comments, &node);
	if (node) {
		mempool_inherit(extpool, pool);
		if (comment) {
			*comment = comments;
		}
		if (retval) {
			*retval = tokens;
		}
		return node;
	} else {
		if (comment) {
			*comment = NULL;
		}
		if (retval) {
			*retval = NULL;
		}
		return NULL;
	}
}

struct AST *
parser_lookup_variable_str(struct Parser *parser, const char *name, enum ParserLookupVariableBehavior behavior, struct Mempool *extpool, char **retval, char **comment)
{
	SCOPE_MEMPOOL(pool);

	struct Array *comments;
	struct Array *words;
	struct AST *node = parser_lookup_variable(parser, name, behavior, pool, &words, &comments);
	if (node) {
		if (comment) {
			*comment = str_join(extpool, comments, " ");
		}

		if (retval) {
			*retval = str_join(extpool, words, " ");
		}
		return node;
	} else {
		return NULL;
	}
}

enum ParserError
parser_merge(struct Parser *parser, struct Parser *subparser, enum ParserMergeBehavior settings)
{
	SCOPE_MEMPOOL(pool);
	if (parser_is_category_makefile(parser->ast, parser)) {
		settings &= ~PARSER_MERGE_AFTER_LAST_IN_GROUP;
	}
	struct ParserEdit params = { subparser, NULL, settings };
	enum ParserError error = parser_edit(parser, NULL, edit_merge, &params);

	if (error == PARSER_ERROR_OK &&
	    parser->settings.behavior & PARSER_DEDUP_TOKENS) {
		error = parser_edit(parser, pool, refactor_dedup_tokens, NULL);
	}

	if (error == PARSER_ERROR_OK) {
		error = parser_edit(parser, pool, refactor_remove_consecutive_empty_lines, NULL);
	}

	return error;
}
