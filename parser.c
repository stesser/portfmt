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
#include <regex.h>
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
#include <libias/map.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "constants.h"
#include "parser.h"
#include "parser/astbuilder/conditional.h"
#include "parser/astbuilder/target.h"
#include "parser/astbuilder/token.h"
#include "parser/astbuilder/variable.h"
#include "parser/edits.h"
#include "regexp.h"
#include "rules.h"

struct Parser {
	struct ParserSettings settings;
	int continued;
	int in_target;
	struct ASTNodeLineRange lines;
	int skip;
	enum ParserError error;
	char *error_msg;
	struct {
		char *buf;
		size_t len;
		FILE *stream;
	} inbuf;
	char *condname;
	char *targetname;
	char *varname;

	struct Mempool *pool;
	struct Mempool *tokengc;
	struct ASTNode *ast;
	struct Array *tokens;
	struct Array *result;
	struct Array *rawlines;
	void *metadata[PARSER_METADATA_USES + 1];
	int metadata_valid[PARSER_METADATA_USES + 1];

	int read_finished;
};

struct ParserFindGoalcolsState {
	int moving_goalcol;
	struct Array *nodes;
};

static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct Parser *, const char *, size_t, char, char, int);
static size_t consume_var(const char *);
static int is_empty_line(const char *);
static void parser_append_token(struct Parser *, enum TokenType, const char *);
static void parser_find_goalcols(struct Parser *);
static void parser_meta_values(struct Parser *, const char *, struct Set *);
static void parser_metadata_alloc(struct Parser *);
static void parser_metadata_free(struct Parser *);
static void parser_metadata_port_options(struct Parser *);
static void parser_output_dump_tokens(struct Parser *);
static void parser_output_prepare(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct ASTNodeLineRange *);
static void parser_output_print_target_command(struct Parser *, struct ASTNode *);
static void parser_output_print_variable(struct Parser *, struct Mempool *, struct ASTNode *);
static struct Array *parser_output_sort_opt_use(struct Parser *, struct Mempool *, struct ASTNodeVariable *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_output_diff(struct Parser *);
static void parser_propagate_goalcol(struct ParserFindGoalcolsState *);
static void parser_read_internal(struct Parser *);
static void parser_read_line(struct Parser *, char *, size_t);
static void parser_tokenize(struct Parser *, const char *, enum TokenType, size_t);
static void print_newline_array(struct Parser *, struct ASTNode *, struct Array *);
static void print_token_array(struct Parser *, struct ASTNode *, struct Array *);
static char *range_tostring(struct Mempool *, struct ASTNodeLineRange *);

size_t
consume_comment(const char *buf)
{
	for (const char *bufp = buf; *bufp != 0; bufp++) {
		if (*bufp == '#') {
			return strlen(buf);
		} else if (!isspace(*bufp)) {
			break;
		}
	}
	return 0;
}

size_t
consume_conditional(const char *buf)
{
	SCOPE_MEMPOOL(pool);

	size_t pos = 0;
	struct Regexp *re = regexp_new(pool, regex(RE_CONDITIONAL));
	if (regexp_exec(re, buf) == 0) {
		pos = regexp_length(re, 0);
	}

	if(pos > 0 && (buf[pos - 1] == '(' || buf[pos - 1] == '!')) {
		pos--;
	}

	return pos;
}

size_t
consume_target(const char *buf)
{
	// Variable assignments are prioritized and can be ambigious
	// due to :=, so check for it first.  Targets can also not
	// start with a tab which implies a conditional.
	if (consume_var(buf) > 0 || *buf == '\t') {
		return 0;
	}

	// ^[^:]+(::?|!)
	// We are more strict than make(1) and do not accept something
	// like just ":".
	size_t len = strlen(buf);
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == ':' || buf[i] == '!') {
			if (i == 0) {
				return 0;
			}
			// Consume the next ':' too if any
			if (buf[i] == ':' && i < len - 1 && buf[i + 1] == ':') {
				i++;
			}
			return i;
		}
	}

	return 0;
}

size_t
consume_token(struct Parser *parser, const char *line, size_t pos,
	      char startchar, char endchar, int eol_ok)
{
	int counter = 0;
	int escape = 0;
	size_t i = pos;
	for (; i < strlen(line); i++) {
		char c = line[i];
		if (escape) {
			escape = 0;
			continue;
		}
		if (startchar == endchar) {
			if (c == startchar) {
				if (counter == 1) {
					return i;
				} else {
					counter++;
				}
			} else if (c == '\\') {
				escape = 1;
			}
		} else {
			if (c == startchar) {
				counter++;
			} else if (c == endchar && counter == 1) {
				return i;
			} else if (c == endchar) {
				counter--;
			} else if (c == '\\') {
				escape = 1;
			}
		}
	}
	if (!eol_ok) {
		SCOPE_MEMPOOL(pool);
		parser_set_error(parser, PARSER_ERROR_EXPECTED_CHAR, str_printf(pool, "%c", endchar));
		return 0;
	} else {
		return i;
	}
}

size_t
consume_var(const char *buf)
{
	size_t len = strlen(buf);

	// ^ *
	size_t pos;
	for (pos = 0; pos < len && buf[pos] == ' '; pos++);

	// [^[:space:]=]+
	size_t i;
	for (i = pos; i < len && !(isspace(buf[i]) || buf[i] == '='); i++);
	if (pos == i) {
		return 0;
	}
	pos = i;

	// [[:space:]]*
	for (; pos < len && isspace(buf[pos]); pos++);

	// [+!?:]?
	switch (buf[pos]) {
	case '+':
	case '!':
	case '?':
	case ':':
		pos++;
		break;
	case '=':
		return pos + 1;
	default:
		return 0;
	}

	// =
	if (buf[pos] != '=') {
		return 0;
	}
	return pos + 1;
}

int
is_empty_line(const char *buf)
{
	for (const char *p = buf; *p != 0; p++) {
		if (!isspace(*p)) {
			return 0;
		}
	}

	return 1;
}

char *
range_tostring(struct Mempool *pool, struct ASTNodeLineRange *range)
{
	panic_unless(range, "range_tostring() is not NULL-safe");
	panic_unless(range->a < range->b, "range is inverted");

	if (range->a == range->b - 1) {
		return str_printf(pool, "%zu", range->a);
	} else {
		return str_printf(pool, "%zu-%zu", range->a, range->b - 1);
	}
}

static int
parser_is_category_makefile(struct Parser *parser, struct ASTNode *node)
{
	if (parser->error != PARSER_ERROR_OK || !parser->read_finished) {
		return 0;
	}

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			if (parser_is_category_makefile(parser, child)) {
				return 1;
			}
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			if (parser_is_category_makefile(parser, child)) {
				return 1;
			}
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			if (parser_is_category_makefile(parser, child)) {
				return 1;
			}
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			if (parser_is_category_makefile(parser, child)) {
				return 1;
			}
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			if (parser_is_category_makefile(parser, child)) {
				return 1;
			}
		}
		break;
	case AST_NODE_EXPR_FLAT:
		if (node->flatexpr.type == AST_NODE_EXPR_INCLUDE &&
		    array_len(node->flatexpr.words) > 0 &&
		    strcmp(array_get(node->flatexpr.words, 0), "<bsd.port.subdir.mk>") == 0) {
			return 1;
		}
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_VARIABLE:
		break;
	}

	return 0;
}

void
parser_init_settings(struct ParserSettings *settings)
{
	settings->filename = NULL;
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
	rules_init();

	struct Parser *parser = xmalloc(sizeof(struct Parser));

	parser->pool = mempool_new();
	parser->tokengc = mempool_new_unique();
	parser->rawlines = array_new();
	parser->result = array_new();
	parser->tokens = array_new();
	parser_metadata_alloc(parser);
	parser->error = PARSER_ERROR_OK;
	parser->error_msg = NULL;
	parser->lines.a = 1;
	parser->lines.b = 1;
	parser->inbuf.stream = open_memstream(&parser->inbuf.buf, &parser->inbuf.len);
	panic_unless(parser->inbuf.stream, "open_memstream failed");
	parser->settings = *settings;
	if (settings->filename) {
		parser->settings.filename = str_dup(NULL, settings->filename);
	} else {
		parser->settings.filename = str_dup(NULL, "/dev/stdin");
	}

	if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser->settings.behavior &= ~PARSER_COLLAPSE_ADJACENT_VARIABLES;
	}

	if ((settings->behavior & PARSER_OUTPUT_DUMP_TOKENS) ||
	    (settings->behavior & PARSER_OUTPUT_DIFF) ||
	    (settings->behavior & PARSER_OUTPUT_RAWLINES)) {
		settings->behavior &= ~PARSER_OUTPUT_INPLACE;
	}

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
	mempool_free(parser->tokengc);
	parser_metadata_free(parser);
	array_free(parser->tokens);

	free(parser->condname);
	free(parser->targetname);
	free(parser->varname);
	free(parser->settings.filename);
	free(parser->error_msg);
	fclose(parser->inbuf.stream);
	free(parser->inbuf.buf);
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

	char *lines = range_tostring(pool, &parser->lines);
	switch (parser->error) {
	case PARSER_ERROR_OK:
		return str_printf(extpool, "line %s: no error", lines);
	case PARSER_ERROR_DIFFERENCES_FOUND:
		return str_printf(extpool, "differences found");
	case PARSER_ERROR_EDIT_FAILED:
		if (parser->error_msg) {
			return str_printf(extpool, "%s", parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: edit failed", lines);
		}
	case PARSER_ERROR_EXPECTED_CHAR:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: expected char: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: expected char", lines);
		}
	case PARSER_ERROR_EXPECTED_INT:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: expected integer: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: expected integer", lines);
		}
	case PARSER_ERROR_EXPECTED_TOKEN:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: expected %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: expected token", lines);
		}
	case PARSER_ERROR_INVALID_ARGUMENT:
		if (parser->error_msg) {
			return str_printf(extpool, "invalid argument: %s", parser->error_msg);
		} else {
			return str_printf(extpool, "invalid argument");
		}
	case PARSER_ERROR_INVALID_REGEXP:
		if (parser->error_msg) {
			return str_printf(extpool, "invalid regexp: %s", parser->error_msg);
		} else {
			return str_printf(extpool, "invalid regexp");
		}
	case PARSER_ERROR_IO:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: IO error: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: IO error", lines);
		}
	case PARSER_ERROR_UNHANDLED_TOKEN_TYPE:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: unhandled token type: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: unhandled token type", lines);
		}
	case PARSER_ERROR_UNSPECIFIED:
		if (parser->error_msg) {
			return str_printf(extpool, "line %s: parse error: %s", lines, parser->error_msg);
		} else {
			return str_printf(extpool, "line %s: parse error", lines);
		}
	}
	panic("unhandled parser error: %d", parser->error);
}

void
parser_append_token(struct Parser *parser, enum TokenType type, const char *data)
{
	struct Token *t = token_new(type, &parser->lines, data, parser->varname,
				    parser->condname, parser->targetname);
	if (t == NULL) {
		parser_set_error(parser, PARSER_ERROR_EXPECTED_TOKEN, token_type_tostring(type));
		return;
	}
	mempool_add(parser->tokengc, t, token_free);
	array_append(parser->tokens, t);
}

void
parser_enqueue_output(struct Parser *parser, const char *s)
{
	panic_unless(s, "parser_enqueue_output() is not NULL-safe");
	array_append(parser->result, str_dup(NULL, s));
}

void
parser_tokenize(struct Parser *parser, const char *line, enum TokenType type, size_t start)
{
	SCOPE_MEMPOOL(pool);

	int dollar = 0;
	int escape = 0;
	char *token = NULL;
	size_t i = start;
	for (; i < strlen(line); i++) {
		panic_if(i < start, "index went before start");
		char c = line[i];
		if (escape) {
			escape = 0;
			if (c == '#' || c == '\\' || c == '$' || isspace(c)) {
				continue;
			}
		}
		if (dollar) {
			if (dollar > 1) {
				if (c == '(') {
					i = consume_token(parser, line, i - 2, '(', ')', 0);
					if (parser->error != PARSER_ERROR_OK) {
						return;
					}
					dollar = 0;
					continue;
				} else if (c == '$') {
					dollar++;
				} else {
					dollar = 0;
				}
			} else if (c == '{') {
				i = consume_token(parser, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '(') {
				i = consume_token(parser, line, i, '(', ')', 0);
				dollar = 0;
			} else if (isalnum(c) || c == '@' || c == '<' || c == '>' || c == '/' ||
				   c == '?' || c == '*' || c == '^' || c == '-' || c == '_' ||
				   c == ')') {
				dollar = 0;
			} else if (c == ' ' || c == '\\') {
				/* '$ ' or '$\' are ignored by make for some reason instead of making
				 * it an error, so we do the same...
				 */
				dollar = 0;
				i--;
			} else if (c == 1) {
				dollar = 0;
			} else if (c == '$') {
				dollar++;
			} else {
				parser_set_error(parser, PARSER_ERROR_EXPECTED_CHAR, "$");
			}
			if (parser->error != PARSER_ERROR_OK) {
				return;
			}
		} else {
			if (c == ' ' || c == '\t') {
				token = str_trim(pool, str_slice(pool, line, start, i));
				if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
					parser_append_token(parser, type, token);
				}
				token = NULL;
				start = i;
			} else if (c == '"') {
				i = consume_token(parser, line, i, '"', '"', 1);
			} else if (c == '\'') {
				i = consume_token(parser, line, i, '\'', '\'', 1);
			} else if (c == '`') {
				i = consume_token(parser, line, i, '`', '`', 1);
			} else if (c == '$') {
				dollar++;
			} else if (c == '\\') {
				escape = 1;
			} else if (c == '#') {
				token = str_trim(pool, str_slice(pool, line, i, -1));
				parser_append_token(parser, type, token);
				token = NULL;
				parser_set_error(parser, PARSER_ERROR_OK, NULL);
				return;
			}
			if (parser->error != PARSER_ERROR_OK) {
				return;
			}
		}
	}

	token = str_trim(pool, str_slice(pool, line, start, i));
	if (strcmp(token, "") != 0) {
		parser_append_token(parser, type, token);
	}
	parser_set_error(parser, PARSER_ERROR_OK, NULL);
}


static void
parser_propagate_goalcol(struct ParserFindGoalcolsState *this)
{
	this->moving_goalcol = MAX(16, this->moving_goalcol);
	ARRAY_FOREACH(this->nodes, struct ASTNode *, node) {
		node->meta.goalcol = this->moving_goalcol;
	}

	this->moving_goalcol = 0;
	array_truncate(this->nodes);
}

static enum ASTWalkState
parser_find_goalcols_walker(struct Parser *parser, struct ASTNode *node, struct ParserFindGoalcolsState *this)
{
	if (parser->error != PARSER_ERROR_OK) {
		return AST_WALK_STOP;
	}

	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_find_goalcols_walker(parser, child, this));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_find_goalcols_walker(parser, child, this));
		}
		ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_find_goalcols_walker(parser, child, this));
		}
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_find_goalcols_walker(parser, child, this));
		}
		break;
	case AST_NODE_TARGET:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_EXPR_FLAT:
		break;
	case AST_NODE_COMMENT:
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
	case AST_NODE_VARIABLE:
		if (array_len(node->variable.words) > 0) {
			if (skip_goalcol(parser, node->variable.name)) {
				node->meta.goalcol = indent_goalcol(node->variable.name, node->variable.modifier);
			} else {
				array_append(this->nodes, node);
				this->moving_goalcol = MAX(indent_goalcol(node->variable.name, node->variable.modifier), this->moving_goalcol);
			}
		}
		break;
	}

	return AST_WALK_CONTINUE;
}

void
parser_find_goalcols(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);
	struct ParserFindGoalcolsState this = {
		.moving_goalcol = 0,
		.nodes = mempool_array(pool),
	};
	parser_find_goalcols_walker(parser, parser->ast, &this);
	parser_propagate_goalcol(&this);
}

void
print_newline_array(struct Parser *parser, struct ASTNode *node, struct Array *arr)
{
	SCOPE_MEMPOOL(pool);

	size_t startlen = strlen(node->variable.name);
	parser_enqueue_output(parser, node->variable.name);
	if (str_endswith(node->variable.name, "+")) {
		startlen++;
		parser_enqueue_output(parser, " ");
	}
	parser_enqueue_output(parser, ASTNodeVariableModifier_humanize[node->variable.modifier]);
	startlen += strlen(ASTNodeVariableModifier_humanize[node->variable.modifier]);

	if (array_len(arr) == 0) {
		parser_enqueue_output(parser, "\n");
		parser_set_error(parser, PARSER_ERROR_OK, NULL);
		return;
	}

	size_t ntabs = ceil((MAX(16, node->meta.goalcol) - startlen) / 8.0);
	char *sep = str_repeat(pool, "\t", ntabs);
	const char *end = " \\\n";
	ARRAY_FOREACH(arr, const char *, line) {
		const char *next = array_get(arr, line_index + 1);
		if (!line || strlen(line) == 0) {
			continue;
		}
		if (line_index == array_len(arr) - 1) {
			end = "\n";
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		// Do not wrap end of line comments
		if (next && is_comment(next)) {
			sep = str_dup(pool, " ");
			continue;
		}
		parser_enqueue_output(parser, end);
		if (line_index == 0) {
			size_t ntabs = ceil(MAX(16, node->meta.goalcol) / 8.0);
			sep = str_repeat(pool, "\t", ntabs);
		}
	}
}

void
print_token_array(struct Parser *parser, struct ASTNode *node, struct Array *tokens)
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
parser_output_print_rawlines(struct Parser *parser, struct ASTNodeLineRange *lines)
{
	for (size_t i = lines->a; i < lines->b; i++) {
		parser_enqueue_output(parser, array_get(parser->rawlines, i - 1));
		parser_enqueue_output(parser, "\n");
	}
}

void
parser_output_print_target_command(struct Parser *parser, struct ASTNode *node)
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

	const char *endline = "\n";
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
			struct ASTNodeLineRange range = { .a = node->line_start.a, .b = node->line_end.b };
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
			if (word_index == array_len(node->targetcommand.words) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				if (strcmp(word, "") != 0) {
					parser_enqueue_output(parser, endword);
				}
				parser_enqueue_output(parser, endnext);
			}
		} else {
			if (word_index == array_len(node->targetcommand.words) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				parser_enqueue_output(parser, endword);
			}
		}
	}
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
parser_output_sort_opt_use(struct Parser *parser, struct Mempool *pool, struct ASTNodeVariable *var, struct Array *arr)
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
		enum ASTNodeVariableModifier mod = AST_NODE_VARIABLE_MODIFIER_ASSIGN;
		if ((suffix - t) >= 1 && prefix[suffix - t - 1] == '=') {
			prefix[suffix - t - 1] = 0;
		}
		if ((suffix - t) >= 2 && prefix[suffix - t - 2] == '+') {
			mod = AST_NODE_VARIABLE_MODIFIER_APPEND;
			prefix[suffix - t - 2] = 0;
		}
		struct Array *buf = mempool_array(pool);
		if (opt_use) {
			struct Array *values = mempool_array(pool);
			char *var = str_printf(pool, "USE_%s", prefix);
			array_append(buf, prefix);
			array_append(buf, ASTNodeVariableModifier_humanize[mod]);
			char *s, *token;
			s = str_dup(pool, suffix);
			while ((token = strsep(&s, ",")) != NULL) {
				array_append(values, str_dup(pool, token));
			}
			struct CompareTokensData data = {
				.parser = parser,
				.var = var,
			};
			array_sort(values, compare_tokens, &data);
			ARRAY_FOREACH(values, const char *, t2) {
				array_append(buf, t2);
				if (t2_index < array_len(values) - 1) {
					array_append(buf, ",");
				}
			}
		} else {
			array_append(buf, prefix);
			array_append(buf, ASTNodeVariableModifier_humanize[mod]);
			array_append(buf, suffix);
		}

		array_append(up, str_join(pool, buf, ""));
	}
	return up;
}

void
parser_output_print_variable(struct Parser *parser, struct Mempool *pool, struct ASTNode *node)
{
	panic_unless(node->type == AST_NODE_VARIABLE, "expected AST_NODE_VARIABLE");
	struct Array *words = node->variable.words;

	/* Leave variables unformatted that have $\ in them. */
	struct ASTNodeLineRange range = { .a = node->line_start.a, .b = node->line_end.b };
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
parser_output_category_makefile_reformatted(struct Parser *parser, struct ASTNode *node)
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
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			parser_output_category_makefile_reformatted(parser, child);
		}
		return;
	case AST_NODE_EXPR_FLAT:
		if (node->flatexpr.type == AST_NODE_EXPR_INCLUDE &&
		    array_len(node->flatexpr.words) > 0 &&
		    strcmp(array_get(node->flatexpr.words, 0), "<bsd.port.subdir.mk>") == 0) {
			parser_enqueue_output(parser, ".include <bsd.port.subdir.mk>\n");
			return;
		}
	case AST_NODE_EXPR_IF:
	case AST_NODE_EXPR_FOR:
	case AST_NODE_TARGET:
	case AST_NODE_TARGET_COMMAND:
		parser_set_error(parser, PARSER_ERROR_UNSPECIFIED,
				 "unsupported node type in category Makefile"); // TODO
		return;
	case AST_NODE_COMMENT:
		ARRAY_FOREACH(node->comment.lines, const char *, line) {
			parser_enqueue_output(parser, line);
			parser_enqueue_output(parser, "\n");
		}
		return;
	case AST_NODE_VARIABLE:
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
parser_output_reformatted_walker(struct Parser *parser, struct ASTNode *node)
{
	SCOPE_MEMPOOL(pool);

	int edited = node->edited;
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
		}
		break;
	case AST_NODE_COMMENT:
		if (edited) {
			ARRAY_FOREACH(node->comment.lines, const char *, line) {
				parser_enqueue_output(parser, line);
				parser_enqueue_output(parser, "\n");
			}
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
		}
		break;
	case AST_NODE_EXPR_FLAT:
		if (edited) {
			const char *name = ASTNodeExprFlatType_identifier[node->flatexpr.type];
			const char *dot;
			switch (node->flatexpr.type) {
			case AST_NODE_EXPR_INCLUDE_POSIX:
				dot = "";
				break;
			default:
				dot = ".";
				break;
			}
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_printf(pool, "%s%s%s %s\n",
				dot, str_repeat(pool, " ", node->flatexpr.indent), name, str_join(pool, node->flatexpr.words, " ")));
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
		}
		break;
	case AST_NODE_EXPR_FOR:
		if (edited) {
			const char *indent = str_repeat(pool, " ", node->forexpr.indent);
			// TODO: Apply some formatting like line breaks instead of just one long forever line???
			parser_enqueue_output(parser, str_printf(pool, ".%sfor %s in %s\n",
				indent,
				str_join(pool, node->forexpr.bindings, " "),
				str_join(pool, node->forexpr.words, " ")));
			ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			parser_enqueue_output(parser, str_printf(pool, ".%sendfor\n", indent));
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			parser_output_print_rawlines(parser, &node->line_end);
		}
		break;
	case AST_NODE_EXPR_IF:
		if (edited) {
			// TODO
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
			if (array_len(node->ifexpr.orelse) > 0) {
				struct ASTNode *next = array_get(node->ifexpr.orelse, 0);
				if (next && next->type == AST_NODE_EXPR_IF && next->ifexpr.type == AST_NODE_EXPR_IF_ELSE) {
					parser_output_print_rawlines(parser, &next->line_start); // .else
					ARRAY_FOREACH(next->ifexpr.body, struct ASTNode *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				} else {
					ARRAY_FOREACH(node->ifexpr.orelse, struct ASTNode *, child) {
						AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
					}
				}
			}
			unless (node->ifexpr.ifparent) { // .endif
				parser_output_print_rawlines(parser, &node->line_end);
			}
		}
		break;
	case AST_NODE_TARGET:
		if (edited) {
			const char *sep = "";
			if (array_len(node->target.dependencies) > 0) {
				sep = " ";
			}
			parser_enqueue_output(parser, str_printf(pool, "%s:%s%s\n",
				str_join(pool, node->target.sources, " "),
				sep,
				str_join(pool, node->target.dependencies, " ")));
			ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
		} else {
			parser_output_print_rawlines(parser, &node->line_start);
			ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
				AST_WALK_RECUR(parser_output_reformatted_walker(parser, child));
			}
		}
		break;
	case AST_NODE_TARGET_COMMAND:
		parser_output_print_target_command(parser, node);
		break;
	case AST_NODE_VARIABLE:
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

	if (parser_is_category_makefile(parser, parser->ast)) {
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
	struct Array *lines = mempool_array(pool);
	char *lines_buf = str_join(pool, parser->result, "");
	char *token;
	while ((token = strsep(&lines_buf, "\n")) != NULL) {
		array_append(lines, token);
	}
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

static void
parser_output_dump_tokens_helper(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	struct Array *tokens = mempool_add(pool, ast_to_token_stream(parser->ast, pool), array_free);
	size_t maxvarlen = 0;
	ARRAY_FOREACH(tokens, struct Token *, o) {
		if (token_type(o) == VARIABLE_START && token_variable(o)) {
			maxvarlen = MAX(maxvarlen, strlen(variable_tostring(token_variable(o), pool)));
		}
	}

	struct Array *vars = mempool_array(pool);
	ARRAY_FOREACH(tokens, struct Token *, t) {
		const char *type;
		switch (token_type(t)) {
		case VARIABLE_END:
			type = "variable-end";
			break;
		case VARIABLE_START:
			type = "variable-start";
			break;
		case VARIABLE_TOKEN:
			type = "variable-token";
			break;
		case TARGET_COMMAND_END:
			type = "target-command-end";
			break;
		case TARGET_COMMAND_START:
			type = "target-command-start";
			break;
		case TARGET_COMMAND_TOKEN:
			type = "target-command-token";
			break;
		case TARGET_END:
			type = "target-end";
			break;
		case TARGET_START:
			type = "target-start";
			break;
		case CONDITIONAL_END:
			type = "conditional-end";
			break;
		case CONDITIONAL_START:
			type = "conditional-start";
			break;
		case CONDITIONAL_TOKEN:
			type = "conditional-token";
			break;
		case COMMENT:
			type = "comment";
			break;
		default:
			parser_set_error(parser, PARSER_ERROR_UNHANDLED_TOKEN_TYPE, NULL);
			return;
		}
		if (token_variable(t) &&
		    (token_type(t) == VARIABLE_TOKEN ||
		     token_type(t) == VARIABLE_START ||
		     token_type(t) == VARIABLE_END)) {
			array_append(vars, variable_tostring(token_variable(t), pool));
		} else if (token_conditional(t) &&
			   (token_type(t) == CONDITIONAL_END ||
			    token_type(t) == CONDITIONAL_START ||
			    token_type(t) == CONDITIONAL_TOKEN)) {
			array_append(vars, conditional_tostring(token_conditional(t), pool));
		} else if (token_target(t) && token_type(t) == TARGET_START) {
			ARRAY_FOREACH(target_names(token_target(t)), char *, name) {
				array_append(vars, str_dup(pool, name));
			}
			ARRAY_FOREACH(target_dependencies(token_target(t)), char *, dep) {
				array_append(vars, str_printf(pool, "->%s", dep));
			}
		} else if (token_target(t) &&
			   (token_type(t) == TARGET_COMMAND_END ||
			    token_type(t) == TARGET_COMMAND_START ||
			    token_type(t) == TARGET_COMMAND_TOKEN ||
			    token_type(t) == TARGET_START ||
			    token_type(t) == TARGET_END)) {
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
			parser_enqueue_output(parser, str_printf(pool, "%-20s %8s ", tokentype, range));
			parser_enqueue_output(parser, var);

			if (len > 0) {
				parser_enqueue_output(parser, str_repeat(pool, " ", len));
			}
			parser_enqueue_output(parser, " ");

			if (token_data(t) &&
			    (token_type(t) != CONDITIONAL_START &&
			     token_type(t) != CONDITIONAL_END)) {
				parser_enqueue_output(parser, token_data(t));
			} else {
				parser_enqueue_output(parser, "-");
			}
			parser_enqueue_output(parser, "\n");
		}
		array_truncate(vars);
	}

	parser_set_error(parser, PARSER_ERROR_OK, NULL);
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);

	if (parser->settings.debug_level == 1) {
		parser_output_dump_tokens_helper(parser);
	} else if (parser->settings.debug_level == 2) {
		struct ASTNode *root = ast_from_token_stream(parser->tokens);
		mempool_add(pool, root, ast_free);
		size_t len = 0;
		char *buf = NULL;
		FILE *f = open_memstream(&buf, &len);
		panic_unless(f, "open_memstream: %s", strerror(errno));
		ast_node_print(root, f);
		fclose(f);
		parser_enqueue_output(parser, buf);
		free(buf);
	} else {
		struct ASTNode *root = ast_from_token_stream(parser->tokens);
		mempool_add(pool, root, ast_free);
		struct Array *tokens = ast_to_token_stream(root, parser->tokengc);
		array_free(parser->tokens);
		parser->tokens = tokens;
		parser_output_dump_tokens_helper(parser);
	}
}

void
parser_read_line(struct Parser *parser, char *line, size_t linelen)
{
	SCOPE_MEMPOOL(pool);

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	array_append(parser->rawlines, str_ndup(NULL, line, linelen));

	parser->lines.b++;

	int will_continue = linelen > 0 && line[linelen - 1] == '\\' && (linelen == 1 || line[linelen - 2] != '\\');
	if (will_continue) {
 		if (linelen > 2 && line[linelen - 2] == '$' && line[linelen - 3] != '$') {
			/* Hack to "handle" things like $\ in variable values */
			line[linelen - 1] = 1;
		} else if (linelen > 1 && !isspace(line[linelen - 2])) {
			/* "Handle" lines that end without a preceding space before '\'. */
			line[linelen - 1] = ' ';
		} else {
			line[linelen - 1] = 0;
		}
	}

	if (parser->continued) {
		/* Replace all whitespace at the beginning with a single
		 * space which is what make seems to do.
		 */
		for (;isblank(*line); line++);
		if (strlen(line) < 1) {
			if (fputc(' ', parser->inbuf.stream) != ' ') {
				parser_set_error(parser, PARSER_ERROR_IO,
					 str_printf(pool, "fputc: %s", strerror(errno)));
				return;
			}
		}
	}

	fwrite(line, 1, strlen(line), parser->inbuf.stream);
	if (ferror(parser->inbuf.stream)) {
		parser_set_error(parser, PARSER_ERROR_IO,
				 str_printf(pool, "fwrite: %s", strerror(errno)));
		return;
	}

	if (!will_continue) {
		parser_read_internal(parser);
		if (parser->error != PARSER_ERROR_OK) {
			return;
		}
		parser->lines.a = parser->lines.b;
		fclose(parser->inbuf.stream);
		free(parser->inbuf.buf);
		parser->inbuf.buf = NULL;
		parser->inbuf.stream = open_memstream(&parser->inbuf.buf, &parser->inbuf.len);
		panic_unless(parser->inbuf.stream, "open_memstream failed");
	}

	parser->continued = will_continue;
}

enum ParserError
parser_read_from_file(struct Parser *parser, FILE *fp)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	LINE_FOREACH(fp, line) {
		parser_read_line(parser, line, line_len);
		if (parser->error != PARSER_ERROR_OK) {
			return parser->error;
		}
	}

	return PARSER_ERROR_OK;
}

void
parser_read_internal(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	if (fflush(parser->inbuf.stream) != 0) {
		parser_set_error(parser, PARSER_ERROR_IO,
				 str_printf(pool, "fflush: %s", strerror(errno)));
		return;
	}

	char *buf = str_trimr(pool, parser->inbuf.buf);
	size_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	} else if (is_empty_line(buf)) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	}

	if (parser->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			free(parser->condname);
			char *tmp = str_ndup(NULL, buf, pos);
			parser->condname = str_trimr(NULL, tmp);
			free(tmp);

			parser_append_token(parser, CONDITIONAL_START, parser->condname);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, parser->condname);
			goto next;
		}
		if (consume_var(buf) == 0 && consume_target(buf) == 0 &&
		    *buf != 0 && *buf == '\t') {
			parser_append_token(parser, TARGET_COMMAND_START, NULL);
			parser_tokenize(parser, buf, TARGET_COMMAND_TOKEN, 0);
			parser_append_token(parser, TARGET_COMMAND_END, NULL);
			goto next;
		}
		if (consume_var(buf) > 0) {
			goto var;
		}
		parser_append_token(parser, TARGET_END, NULL);
		parser->in_target = 0;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		free(parser->condname);
		char *tmp = str_ndup(NULL, buf, pos);
		parser->condname = str_trimr(NULL, tmp);
		free(tmp);

		parser_append_token(parser, CONDITIONAL_START, parser->condname);
		parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
		parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
		parser_append_token(parser, CONDITIONAL_END, parser->condname);
		goto next;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		parser->in_target = 1;
		free(parser->targetname);
		parser->targetname = str_dup(NULL, buf);
		parser_append_token(parser, TARGET_START, buf);
		goto next;
	}

var:
	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > strlen(buf)) {
			parser_set_error(parser, PARSER_ERROR_UNSPECIFIED, "inbuf overflow");
			goto next;
		}
		char *tmp = str_ndup(NULL, buf, pos);
		parser->varname = str_trim(NULL, tmp);
		free(tmp);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		parser_set_error(parser, PARSER_ERROR_UNSPECIFIED, NULL);
	}
next:
	if (parser->varname) {
		parser_append_token(parser, VARIABLE_END, NULL);
		free(parser->varname);
		parser->varname = NULL;
	}
}

enum ParserError
parser_read_finish(struct Parser *parser)
{
	SCOPE_MEMPOOL(pool);
	//TODO: uncomment when process_include() mess in portscan is fixed
	//panic_if(parser->read_finished, "parser_read_finish() called multiple times");

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	for (size_t i = 0; i <= PARSER_METADATA_USES; i++) {
		parser->metadata_valid[i] = 0;
	}

	if (!parser->continued) {
		parser->lines.b++;
	}

	if (fflush(parser->inbuf.stream) != 0) {
		parser_set_error(parser, PARSER_ERROR_IO,
				 str_printf(pool, "fflush: %s", strerror(errno)));
		return parser->error;
	}

	if (parser->inbuf.len > 0) {
		parser_read_internal(parser);
		if (parser->error != PARSER_ERROR_OK) {
			return parser->error;
		}
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	parser->read_finished = 1;
	ast_free(parser->ast);
	parser->ast = ast_from_token_stream(parser->tokens);
	// TODO: see above
	// array_free(parser->tokens);
	// parser->tokens = NULL;
	// mempool_release_all(parser->tokengc);

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
	if ((parser_is_category_makefile(parser, parser->ast) ||
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
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	char *buf, *bufp, *line;
	buf = bufp = str_ndup(NULL, input, len);
	while ((line = strsep(&bufp, "\n")) != NULL) {
		parser_read_line(parser, line, strlen(line));
		if (parser->error != PARSER_ERROR_OK) {
			break;
		}
	}
	free(buf);

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

	struct Array *tokens = NULL;
	if (f(parser, parser->ast, extpool, userdata, &tokens)) {
		// do nothing
	} else if (tokens) {
		ast_free(parser->ast);
		parser->ast = ast_from_token_stream(tokens);
	}

	if (parser->error != PARSER_ERROR_OK) {
		parser_set_error(parser, PARSER_ERROR_EDIT_FAILED, parser_error_tostring(parser, pool));
	}

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
			if (parser_lookup_variable(parser, "MASTERDIR", PARSER_LOOKUP_FIRST, pool, &tokens, NULL)) {
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
parser_lookup_target_walker(struct ASTNode *node, const char *name, struct ASTNode **retval)
{
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_target_walker(child, name, retval));
		}
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_EXPR_FLAT:
	case AST_NODE_TARGET_COMMAND:
	case AST_NODE_VARIABLE:
		break;
	case AST_NODE_EXPR_FOR:
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_target_walker(child, name, retval));
		}
		break;
	case AST_NODE_EXPR_IF:
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_target_walker(child, name, retval));
		}
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_target_walker(child, name, retval));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.sources, char *, src) {
			if (strcmp(src, name) == 0) {
				*retval = node;
				return AST_WALK_STOP;
			}
		}
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_target_walker(child, name, retval)); // XXX: Really needed?
		}
		break;
	}

	return AST_WALK_CONTINUE;
}

struct ASTNode *
parser_lookup_target(struct Parser *parser, const char *name)
{
	struct ASTNode *node = NULL;
	parser_lookup_target_walker(parser->ast, name, &node);
	return node;
}

static enum ASTWalkState
parser_lookup_variable_walker(struct ASTNode *node, struct Mempool *pool, const char *name, enum ParserLookupVariableBehavior behavior, struct Array *tokens, struct Array *comments, struct ASTNode **retval)
{
	switch (node->type) {
	case AST_NODE_ROOT:
		ARRAY_FOREACH(node->root.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_variable_walker(child, pool, name, behavior, tokens, comments, retval));
		}
		break;
	case AST_NODE_COMMENT:
	case AST_NODE_EXPR_FLAT:
	case AST_NODE_TARGET_COMMAND:
		break;
	case AST_NODE_VARIABLE:
		if (strcmp(node->variable.name, name) == 0) {
			*retval = node;
			ARRAY_FOREACH(node->variable.words, const char *, word) {
				if (is_comment(word)) {
					array_append(comments, str_dup(pool, word));
				} else {
					array_append(tokens, str_dup(pool, word));
				}
			}
			if (behavior & PARSER_LOOKUP_FIRST) {
				return AST_WALK_STOP;
			}
		}
		break;
	case AST_NODE_EXPR_FOR:
		if (behavior & PARSER_LOOKUP_IGNORE_VARIABLES_IN_CONDITIIONALS) {
			return AST_WALK_CONTINUE;
		}
		ARRAY_FOREACH(node->forexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_variable_walker(child, pool, name, behavior, tokens, comments, retval));
		}
		break;
	case AST_NODE_EXPR_IF:
		if (behavior & PARSER_LOOKUP_IGNORE_VARIABLES_IN_CONDITIIONALS) {
			return AST_WALK_CONTINUE;
		}
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_variable_walker(child, pool, name, behavior, tokens, comments, retval));
		}
		ARRAY_FOREACH(node->ifexpr.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_variable_walker(child, pool, name, behavior, tokens, comments, retval));
		}
		break;
	case AST_NODE_TARGET:
		ARRAY_FOREACH(node->target.body, struct ASTNode *, child) {
			AST_WALK_RECUR(parser_lookup_variable_walker(child, pool, name, behavior, tokens, comments, retval));
		}
		break;
	}

	return AST_WALK_CONTINUE;
}

struct ASTNode *
parser_lookup_variable(struct Parser *parser, const char *name, enum ParserLookupVariableBehavior behavior, struct Mempool *extpool, struct Array **retval, struct Array **comment)
{
	SCOPE_MEMPOOL(pool);
	struct Array *tokens = mempool_array(pool);
	struct Array *comments = mempool_array(pool);
	struct ASTNode *node = NULL;
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

struct ASTNode *
parser_lookup_variable_str(struct Parser *parser, const char *name, enum ParserLookupVariableBehavior behavior, struct Mempool *extpool, char **retval, char **comment)
{
	SCOPE_MEMPOOL(pool);

	struct Array *comments;
	struct Array *words;
	struct ASTNode *node = parser_lookup_variable(parser, name, behavior, pool, &words, &comments);
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
	if (parser_is_category_makefile(parser, parser->ast)) {
		settings &= ~PARSER_MERGE_AFTER_LAST_IN_GROUP;
	}
	struct ParserEdit params = { subparser, NULL, settings };
	enum ParserError error = parser_edit(parser, NULL, edit_merge, &params);

	if (error == PARSER_ERROR_OK &&
	    parser->settings.behavior & PARSER_DEDUP_TOKENS) {
		error = parser_edit(parser, NULL, refactor_dedup_tokens, NULL);
	}

	if (error == PARSER_ERROR_OK) {
		error = parser_edit(parser, NULL, refactor_remove_consecutive_empty_lines, NULL);
	}

	return error;
}
