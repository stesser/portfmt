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

#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#if HAVE_SBUF
# include <sys/sbuf.h>
#endif
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "conditional.h"
#include "parser.h"
#include "rules.h"
#include "target.h"
#include "util.h"
#include "variable.h"

enum TokenType {
	COMMENT = 0,
	CONDITIONAL_END,
	CONDITIONAL_TOKEN,
	CONDITIONAL_START,
	EMPTY,
	INLINE_COMMENT,
	PORT_MK,
	PORT_OPTIONS_MK,
	PORT_PRE_MK,
	TARGET_COMMAND_END,
	TARGET_COMMAND_START,
	TARGET_COMMAND_TOKEN,
	TARGET_END,
	TARGET_START,
	VARIABLE_END,
	VARIABLE_START,
	VARIABLE_TOKEN,
};

struct Range {
	size_t start;
	size_t end;
};

struct Token {
	enum TokenType type;
	struct sbuf *data;
	struct Conditional *cond;
	struct Variable *var;
	struct Target *target;
	int goalcol;
	struct Range lines;
	int ignore;
};

struct Parser {
	struct ParserSettings settings;
	int continued;
	int in_target;
	struct Range lines;
	int skip;
	struct sbuf *inbuf;
	struct sbuf *condname;
	struct sbuf *targetname;
	struct sbuf *varname;

	struct Array *tokens;
	struct Array *result;
	struct Array *rawlines;
};

static size_t consume_comment(struct sbuf *);
static size_t consume_conditional(struct sbuf *);
static size_t consume_target(struct sbuf *);
static size_t consume_token(struct Parser *, struct sbuf *, size_t, char, char, int);
static size_t consume_var(struct sbuf *);
static void parser_append_token(struct Parser *, enum TokenType, struct sbuf *);
static void parser_collapse_adjacent_variables(struct Parser *);
static void parser_enqueue_output(struct Parser *, struct sbuf *);
static void parser_find_goalcols(struct Parser *);
static void parser_output_dump_tokens(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct Range *);
static void parser_output_print_target_command(struct Parser *, struct Array *);
static void parser_output_reformatted_helper(struct Parser *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read_internal(struct Parser *, struct sbuf *);
static void parser_sanitize_append_modifier(struct Parser *);
static void parser_tokenize(struct Parser *, struct sbuf *, enum TokenType, ssize_t);
static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static struct sbuf *range_tostring(struct Range *);
static int tokcompare(const void *, const void *);
static struct Token *parser_get_token(struct Parser *, size_t);

size_t
consume_comment(struct sbuf *buf)
{
	size_t pos = 0;
	if (sbuf_startswith(buf, "#")) {
		pos = sbuf_len(buf);
	}
	return pos;
}

size_t
consume_conditional(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_CONDITIONAL, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_target(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_TARGET, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_token(struct Parser *parser, struct sbuf *line, size_t pos,
	      char startchar, char endchar, int eol_ok)
{
	char *linep = sbuf_data(line);
	int counter = 0;
	int escape = 0;
	ssize_t i = pos;
	for (; i < sbuf_len(line); i++) {
		char c = linep[i];
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
		errx(1, "tokenizer: %s: expected %c", sbuf_data(range_tostring(&parser->lines)), endchar);
	} else {
		return i;
	}
}

size_t
consume_var(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_VAR, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

struct sbuf *
range_tostring(struct Range *range)
{
	assert(range);
	assert(range->start < range->end);

	struct sbuf *s = sbuf_dup(NULL);
	if (range->start == range->end - 1) {
		sbuf_printf(s, "%zu", range->start);
	} else {
		sbuf_printf(s, "%zu-%zu", range->start, range->end - 1);
	}
	sbuf_finishx(s);

	return s;
}

void
parser_init_settings(struct ParserSettings *settings)
{
	settings->behavior = 0;
	settings->target_command_format_threshold = 8;
	settings->target_command_format_wrapcol = 65;
	settings->wrapcol = 80;
}

struct Parser *
parser_new(struct ParserSettings *settings)
{
	struct Parser *parser = calloc(1, sizeof(struct Parser));
	if (parser == NULL) {
		err(1, "calloc");
	}

	parser->rawlines = array_new(sizeof(struct sbuf *));
	parser->result = array_new(sizeof(struct sbuf *));
	parser->tokens = array_new(sizeof(struct Token *));
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = sbuf_dupstr(NULL);
	parser->settings = *settings;

	compile_regular_expressions();

	return parser;
}

void
parser_free(struct Parser *parser)
{
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (t->data) {
			sbuf_delete(t->data);
		}
		if (t->var) {
			variable_free(t->var);
		}
		if (t->target) {
			target_free(t->target);
		}
		free(t);
	}
	array_free(parser->tokens);

	for (size_t i = 0; i < array_len(parser->result); i++) {
		struct sbuf *s = array_get(parser->result, i);
		sbuf_delete(s);
	}
	array_free(parser->result);


	for (size_t i = 0; i < array_len(parser->rawlines); i++) {
		struct sbuf *s = array_get(parser->rawlines, i);
		sbuf_delete(s);
	}
	array_free(parser->rawlines);

	sbuf_delete(parser->inbuf);
	free(parser);
}

void
parser_append_token(struct Parser *parser, enum TokenType type, struct sbuf *v)
{
	struct Target *target = NULL;
	if (parser->targetname) {
		target = target_new(parser->targetname);
	}

	struct Conditional *cond = NULL;
	if (parser->condname) {
		cond = conditional_new(parser->condname);
	}

	struct Variable *var = NULL;
	if (parser->varname) {
		var = variable_new(parser->varname);
	}

	struct sbuf *data = NULL;
	if (v) {
		data = sbuf_dup(v);
		sbuf_finishx(data);
	}

	struct Token *o = xmalloc(sizeof(struct Token));
	o->type = type;
	o->data = data;
	o->cond = cond;
	o->target = target;
	o->var = var;
	o->goalcol = 0;
	o->lines = parser->lines;
	o->ignore = 0;
	array_append(parser->tokens, o);
}

void
parser_enqueue_output(struct Parser *parser, struct sbuf *s)
{
	if (!sbuf_done(s)) {
		sbuf_finishx(s);
	}
	struct sbuf *tmp = sbuf_dup(s);
	sbuf_finishx(tmp);
	array_append(parser->result, tmp);
}

struct Token *
parser_get_token(struct Parser *parser, size_t i)
{
	return array_get(parser->tokens, i);
}

void
parser_tokenize(struct Parser *parser, struct sbuf *line, enum TokenType type, ssize_t start)
{
	int dollar = 0;
	int escape = 0;
	char *linep = sbuf_data(line);
	struct sbuf *token = NULL;
	ssize_t i = start;
	size_t queued_tokens = 0;
	for (; i < sbuf_len(line); i++) {
		assert(i >= start);
		char c = linep[i];
		if (escape) {
			escape = 0;
			if (c == '#' || c == '\\' || c == '$') {
				continue;
			}
		}
		if (dollar) {
			if (dollar == 2) {
				if (c == '(') {
					i = consume_token(parser, line, i - 2, '(', ')', 0);
					continue;
				} else {
					dollar = 0;
				}
			} else if (c == '{') {
				i = consume_token(parser, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '$') {
				dollar++;
			} else {
				fprintf(stderr, "%s\n", linep);
				errx(1, "tokenizer: %s: expected {", sbuf_data(range_tostring(&parser->lines)));
			}
		} else {
			if (c == ' ' || c == '\t') {
				struct sbuf *tmp = sbuf_substr_dup(line, start, i);
				sbuf_finishx(tmp);
				token = sbuf_strip_dup(tmp);
				sbuf_finishx(token);
				sbuf_delete(tmp);
				if (sbuf_strcmp(token, "") != 0 && sbuf_strcmp(token, "\\") != 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				}
				sbuf_delete(token);
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
				/* Try to push end of line comments out of the way above
				 * the variable as a way to preserve them.  They clash badly
				 * with sorting tokens in variables.  We could add more
				 * special cases for this, but often having them at the top
				 * is just as good.
				 */
				struct sbuf *tmp = sbuf_substr_dup(line, i, sbuf_len(line));
				sbuf_finishx(tmp);
				token = sbuf_strip_dup(tmp);
				sbuf_finishx(token);
				sbuf_delete(tmp);
				if (sbuf_strcmp(token, "#") == 0 ||
				    sbuf_strcmp(token, "# empty") == 0 ||
				    sbuf_strcmp(token, "#none") == 0 ||
				    sbuf_strcmp(token, "# none") == 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				} else {
					parser_append_token(parser, INLINE_COMMENT, token);
				}

				sbuf_delete(token);
				token = NULL;
				goto cleanup;
			}
		}
	}
	struct sbuf *tmp = sbuf_substr_dup(line, start, i);
	sbuf_finishx(tmp);
	token = sbuf_strip_dup(tmp);
	sbuf_finishx(token);
	sbuf_delete(tmp);
	if (sbuf_strcmp(token, "") != 0) {
		parser_append_token(parser, type, token);
		queued_tokens++;
	}

	sbuf_delete(token);
	token = NULL;
cleanup:
	if (queued_tokens == 0 && type == VARIABLE_TOKEN) {
		parser_append_token(parser, EMPTY, NULL);
	}
}

void
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end,
			 int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Token *o = parser_get_token(parser, k);
		if (!o->ignore && o->var && !skip_goalcol(o->var)) {
			o->goalcol = moving_goalcol;
		}
	}
}

void
parser_find_goalcols(struct Parser *parser)
{
	int moving_goalcol = 0;
	int last = 0;
	ssize_t tokens_start = -1;
	ssize_t tokens_end = -1;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = parser_get_token(parser, i);
		if (o->ignore) {
			continue;
		}
		switch(o->type) {
		case VARIABLE_END:
		case VARIABLE_START:
			break;
		case VARIABLE_TOKEN:
			if (tokens_start == -1) {
				tokens_start = i;
			}
			tokens_end = i;

			if (o->var && skip_goalcol(o->var)) {
				o->goalcol = indent_goalcol(o->var);
			} else {
				moving_goalcol = MAX(indent_goalcol(o->var), moving_goalcol);
			}
			break;
		case TARGET_END:
		case TARGET_START:
		case CONDITIONAL_END:
		case CONDITIONAL_START:
		case TARGET_COMMAND_END:
		case TARGET_COMMAND_START:
		case TARGET_COMMAND_TOKEN:
			break;
		case COMMENT:
		case CONDITIONAL_TOKEN:
		case PORT_MK:
		case PORT_OPTIONS_MK:
		case PORT_PRE_MK:
			/* Ignore comments in between variables and
			 * treat variables after them as part of the
			 * same block, i.e., indent them the same way.
			 */
			if (sbuf_startswith(o->data, "#")) {
				continue;
			}
			if (tokens_start != -1) {
				parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
				moving_goalcol = 0;
				last = i;
				tokens_start = -1;
			}
			break;
		case EMPTY:
		case INLINE_COMMENT:
			break;
		default:
			errx(1, "Unhandled token type: %i", o->type);
		}
	}
	if (tokens_start != -1) {
		parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
	}
}

void
print_newline_array(struct Parser *parser, struct Array *arr)
{
	struct Token *o = array_get(arr, 0);
	assert(o && o->data != NULL);
	assert(sbuf_len(o->data) != 0);
	struct sbuf *sep;
	switch (o->type) {
	case VARIABLE_TOKEN: {
		struct sbuf *start = variable_tostring(o->var);
		size_t ntabs = ceil((MAX(16, o->goalcol) - sbuf_len(start)) / 8.0);
		sep = sbuf_dup(start);
		sbuf_cat(sep, repeat('\t', ntabs));
		break;
	} case CONDITIONAL_TOKEN:
		sep = sbuf_dupstr(NULL);
		break;
	case TARGET_COMMAND_TOKEN:
		sep = sbuf_dupstr("\t");
		break;
	default:
		errx(1, "unhandled token type: %i", o->type);
	}

	struct sbuf *end = sbuf_dupstr(" \\\n");
	for (size_t i = 0; i < array_len(arr); i++) {
		struct Token *o = array_get(arr, i);
		struct sbuf *line = o->data;
		if (!line || sbuf_len(line) == 0) {
			continue;
		}
		if (i == array_len(arr) - 1) {
			end = sbuf_dupstr("\n");
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		switch (o->type) {
		case VARIABLE_TOKEN:
			if (i == 0) {
				size_t ntabs = ceil(MAX(16, o->goalcol) / 8.0);
				sep = sbuf_dupstr(repeat('\t', ntabs));
			}
			break;
		case CONDITIONAL_TOKEN:
			sep = sbuf_dupstr("\t");
			break;
		case TARGET_COMMAND_TOKEN:
			sep = sbuf_dupstr(repeat('\t', 2));
			break;
		default:
			errx(1, "unhandled token type: %i", o->type);
		}
	}
}

int
tokcompare(const void *a, const void *b)
{
	struct Token *ao = *(struct Token**)a;
	struct Token *bo = *(struct Token**)b;
	if (variable_cmp(ao->var, bo->var) == 0) {
		return compare_tokens(ao->var, ao->data, bo->data);
	}
	return strcasecmp(sbuf_data(ao->data), sbuf_data(bo->data));
#if 0
	# Hack to treat something like ${PYTHON_PKGNAMEPREFIX} or
	# ${RUST_DEFAULT} as if they were PYTHON_PKGNAMEPREFIX or
	# RUST_DEFAULT for the sake of approximately sorting them
	# correctly in *_DEPENDS.
	gsub(/[\$\{\}]/, "", a)
	gsub(/[\$\{\}]/, "", b)
#endif
}

void
print_token_array(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) < 2) {
		print_newline_array(parser, tokens);
		return;
	}

	struct Array *arr = array_new(sizeof(struct Token *));
	struct Token *o = array_get(tokens, 0);
	int wrapcol;
	if (o->var && ignore_wrap_col(o->var)) {
		wrapcol = 99999999;
	} else {
		/* Minus ' \' at end of line */
		wrapcol = parser->settings.wrapcol - o->goalcol - 2;
	}

	struct sbuf *row = sbuf_dupstr(NULL);
	sbuf_finishx(row);

	struct Token *token = NULL;
	for (size_t i = 0; i < array_len(tokens); i++) {
		token = array_get(tokens, i);
		if (sbuf_len(token->data) == 0) {
			continue;
		}
		if ((sbuf_len(row) + sbuf_len(token->data)) > wrapcol) {
			if (sbuf_len(row) == 0) {
				array_append(arr, token);
				continue;
			} else {
				struct Token *o = xmalloc(sizeof(struct Token));
				memcpy(o, token, sizeof(struct Token));
				o->data = row;
				array_append(arr, o);
				row = sbuf_dupstr(NULL);
				//sbuf_finishx(row);
			}
		}
		if (sbuf_len(row) == 0) {
			row = token->data;
		} else {
			struct sbuf *s = sbuf_dup(row);
			sbuf_putc(s, ' ');
			sbuf_cat(s, sbuf_data(token->data));
			sbuf_finishx(s);
			row = s;
		}
	}
	if (token && sbuf_len(row) > 0 && array_len(arr) < array_len(tokens)) {
		struct Token *o = xmalloc(sizeof(struct Token));
		memcpy(o, token, sizeof(struct Token));
		o->data = row;
		array_append(arr, o);
	}
	print_newline_array(parser, arr);
	array_free(arr);
}

void
parser_output_print_rawlines(struct Parser *parser, struct Range *lines)
{
	struct sbuf *endline = sbuf_dupstr("\n");
	for (size_t i = lines->start; i < lines->end; i++) {
		parser_enqueue_output(parser, array_get(parser->rawlines, i - 1));
		parser_enqueue_output(parser, endline);
	}
	sbuf_delete(endline);
}

void
parser_output_print_target_command(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) == 0) {
		return;
	}

	struct sbuf *endline = sbuf_dupstr("\n");
	struct sbuf *endnext = sbuf_dupstr(" \\\n");
	struct sbuf *endword = sbuf_dupstr(" ");
	struct sbuf *startlv0 = sbuf_dupstr("");
	struct sbuf *startlv1 = sbuf_dupstr("\t");
	struct sbuf *startlv2 = sbuf_dupstr("\t\t");
	struct sbuf *start = startlv0;

	/* Find the places we need to wrap to the next line.
	 * TODO: This is broken as wrapping changes the next place we need to wrap
	 */
	struct Array *wraps = array_new(sizeof(int));
	int column = 8;
	int complexity = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		struct sbuf *word = t->data;

		assert(t->type == TARGET_COMMAND_TOKEN);
		assert(sbuf_len(word) != 0);

		for (char *c = sbuf_data(word); *c != 0; c++) {
			switch (*c) {
			case '`':
			case '(':
			case ')':
			case ';':
				complexity++;
				break;
			}
		}

		if (start == startlv1 || start == startlv2) {
			start = startlv0;
		}

		column += sbuf_len(start) * 8 + sbuf_len(word);
		if (column > parser->settings.target_command_format_wrapcol ||
		    target_command_should_wrap(word)) {
			if (i + 1 < array_len(tokens)) {
				struct Token *next = array_get(tokens, i + 1);
				if (target_command_should_wrap(next->data)) {
					continue;
				}
			}
			start = startlv2;
			column = 16;
			array_append(wraps, (void*)i);
		}
	}

	if (!(parser->settings.behavior & PARSER_FORMAT_TARGET_COMMANDS) ||
	    complexity > parser->settings.target_command_format_threshold) {
		struct Token *t = array_get(tokens, 0);
		parser_output_print_rawlines(parser, &t->lines);
		goto cleanup;
	}

	parser_enqueue_output(parser, startlv1);
	int wrapped = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		struct sbuf *word = t->data;

		if (wrapped) {
			if (i == 0) {
				parser_enqueue_output(parser, startlv1);
			} else {
				parser_enqueue_output(parser, startlv2);
			}
		}
		wrapped = array_find(wraps, (void*)i) > -1;

		parser_enqueue_output(parser, word);
		if (wrapped) {
			if (i == array_len(tokens) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				parser_enqueue_output(parser, endnext);
			}
		} else {
			if (i == array_len(tokens) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				parser_enqueue_output(parser, endword);
			}
		}
	}

cleanup:
	array_free(wraps);
	sbuf_delete(endline);
	sbuf_delete(endnext);
	sbuf_delete(endword);
	sbuf_delete(startlv1);
	sbuf_delete(startlv2);
}

void
parser_output_prepare(struct Parser *parser)
{
	if (parser->settings.behavior & PARSER_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}
}

void
parser_output_reformatted_helper(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return;
	}
	struct Token *arr0 = array_get(arr, 0);
	if (!(parser->settings.behavior & PARSER_UNSORTED_VARIABLES) && !leave_unsorted(arr0->var)) {
		array_sort(arr, tokcompare);
	}
	if (print_as_newlines(arr0->var)) {
		print_newline_array(parser, arr);
	} else {
		print_token_array(parser, arr);
	}
	array_truncate(arr);
}

void
parser_output_reformatted(struct Parser *parser)
{
	struct sbuf *endline = sbuf_dupstr("\n");
	parser_find_goalcols(parser);

	struct Array *target_arr = array_new(sizeof(struct Token *));
	struct Array *variable_arr = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		switch (o->type) {
		case CONDITIONAL_END:
			parser_output_print_rawlines(parser, &o->lines);
			break;
		case CONDITIONAL_START:
		case CONDITIONAL_TOKEN:
			break;
		case VARIABLE_END:
			parser_output_reformatted_helper(parser, variable_arr);
			break;
		case VARIABLE_START:
			array_truncate(variable_arr);
			break;
		case VARIABLE_TOKEN:
			array_append(variable_arr, o);
			break;
		case TARGET_COMMAND_END:
			parser_output_print_target_command(parser, target_arr);
			array_truncate(target_arr);
			break;
		case TARGET_COMMAND_START:
			array_truncate(target_arr);
			break;
		case TARGET_COMMAND_TOKEN:
			array_append(target_arr, o);
			break;
		case TARGET_END:
			break;
		case COMMENT:
		case PORT_OPTIONS_MK:
		case PORT_MK:
		case PORT_PRE_MK:
		case TARGET_START:
			parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, &o->lines);
			break;
		case EMPTY: {
			struct sbuf *v = variable_tostring(o->var);
			sbuf_putc(v, '\n');
			sbuf_finishx(v);
			parser_enqueue_output(parser, v);
			break;
		} case INLINE_COMMENT:
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, endline);
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	parser_output_reformatted_helper(parser, variable_arr);
	array_free(target_arr);
	array_free(variable_arr);
	sbuf_delete(endline);
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	ssize_t maxvarlen = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		if (o->type == VARIABLE_START && o->var) {
			struct sbuf *var = variable_tostring(o->var);
			maxvarlen = MAX(maxvarlen, sbuf_len(var));
			sbuf_delete(var);
		}
	}

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		const char *type;
		switch (o->type) {
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
		case EMPTY:
			type = "empty";
			break;
		case INLINE_COMMENT:
			type = "inline-comment";
			break;
		case COMMENT:
			type = "comment";
			break;
		case PORT_MK:
			type = "port-mk";
			break;
		case PORT_PRE_MK:
			type = "port-pre-mk";
			break;
		case PORT_OPTIONS_MK:
			type = "port-options-mk";
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
		struct sbuf *var = NULL;
		ssize_t len = maxvarlen;
		if (o->var &&
		    (o->type == VARIABLE_TOKEN ||
		     o->type == VARIABLE_START ||
		     o->type == VARIABLE_END)) {
			var = variable_tostring(o->var);
			len = maxvarlen - sbuf_len(var);
		} else if (o->cond &&
			   (o->type == CONDITIONAL_END ||
			    o->type == CONDITIONAL_START ||
			    o->type == CONDITIONAL_TOKEN)) {
			var = conditional_tostring(o->cond);
			len = maxvarlen - sbuf_len(var);
		} else if (o->target &&
			   (o->type == TARGET_COMMAND_END ||
			    o->type == TARGET_COMMAND_START ||
			    o->type == TARGET_COMMAND_TOKEN ||
			    o->type == TARGET_START ||
			    o->type == TARGET_END)) {
			var = sbuf_dup(target_name(o->target));
			len = maxvarlen - sbuf_len(var);
		} else {
			len = maxvarlen - 1;
		}
		struct sbuf *range = range_tostring(&o->lines);
		struct sbuf *out = sbuf_dup(NULL);
		sbuf_printf(out, "%-20s %8s ", type, sbuf_data(range));
		sbuf_delete(range);
		if (var) {
			sbuf_cat(out, sbuf_data(var));
		} else {
			sbuf_putc(out, '-');
		}
		for (ssize_t j = 0; j < len; j++) {
			sbuf_putc(out, ' ');
		}
		sbuf_putc(out, ' ');
		if (o->data) {
			sbuf_cat(out, sbuf_data(o->data));
		} else {
			sbuf_putc(out, '-');
		}
		sbuf_putc(out, '\n');
		sbuf_finishx(out);
		parser_enqueue_output(parser, out);
		sbuf_delete(out);
		if (var) {
			sbuf_delete(var);
		}
	}
}

void
parser_read(struct Parser *parser, char *line)
{
	size_t linelen = strlen(line);
	struct sbuf *buf = sbuf_dupstr(line);
	sbuf_finishx(buf);

	parser->lines.end++;

	int will_continue = matches(RE_CONTINUE_LINE, buf, NULL);
	if (will_continue) {
		line[linelen - 1] = 0;
	}

	if (parser->continued) {
		/* Replace all whitespace at the beginning with a single
		 * space which is what make seems to do.
		*/
		for (;isblank(*line); line++);
		if (strlen(line) < 1) {
			sbuf_putc(parser->inbuf, ' ');
		}
	}

	sbuf_cat(parser->inbuf, line);

	if (!will_continue) {
		sbuf_trim(parser->inbuf);
		sbuf_finishx(parser->inbuf);
		parser_read_internal(parser, parser->inbuf);
		parser->lines.start = parser->lines.end;
		sbuf_delete(parser->inbuf);
		parser->inbuf = sbuf_dupstr(NULL);
	}

	parser->continued = will_continue;

	array_append(parser->rawlines, buf);
}

void
parser_read_internal(struct Parser *parser, struct sbuf *buf)
{
	ssize_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	} else if (matches(RE_EMPTY_LINE, buf, NULL)) {
		if (parser->in_target) {
			parser_append_token(parser, TARGET_END, NULL);
			parser_append_token(parser, COMMENT, buf);
			parser->in_target = 0;
			goto next;
		} else {
			parser_append_token(parser, COMMENT, buf);
			goto next;
		}
	}

	if (parser->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			if (parser->condname) {
				sbuf_delete(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = sbuf_substr_dup(buf, 0, pos);
			sbuf_trim(parser->condname);
			sbuf_finishx(parser->condname);

			parser_append_token(parser, CONDITIONAL_START, NULL);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, NULL);
			goto next;
		}
		pos = consume_var(buf);
		if (pos == 0) {
			parser_append_token(parser, TARGET_COMMAND_START, NULL);
			parser_tokenize(parser, buf, TARGET_COMMAND_TOKEN, pos);
			parser_append_token(parser, TARGET_COMMAND_END, NULL);
			goto next;
		}
		parser_append_token(parser, TARGET_END, NULL);
		parser->in_target = 0;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		parser->in_target = 1;
		if (parser->targetname) {
			sbuf_delete(parser->targetname);
			parser->targetname = NULL;
		}
		parser->targetname = sbuf_dup(buf);
		sbuf_finishx(parser->targetname);
		parser_append_token(parser, TARGET_START, buf);
		goto next;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		if (sbuf_endswith(buf, "<bsd.port.options.mk>")) {
			parser_append_token(parser, PORT_OPTIONS_MK, buf);
			goto next;
		} else if (sbuf_endswith(buf, "<bsd.port.pre.mk>")) {
			parser_append_token(parser, PORT_PRE_MK, buf);
			goto next;
		} else if (sbuf_endswith(buf, "<bsd.port.post.mk>") ||
			   sbuf_endswith(buf, "<bsd.port.mk>")) {
			parser_append_token(parser, PORT_MK, buf);
			goto next;
		} else {
			if (parser->condname) {
				sbuf_delete(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = sbuf_substr_dup(buf, 0, pos);
			sbuf_trim(parser->condname);
			sbuf_finishx(parser->condname);

			parser_append_token(parser, CONDITIONAL_START, NULL);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, NULL);
		}
		goto next;
	}

	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > sbuf_len(buf)) {
			errx(1, "parser->varname too small");
		}
		parser->varname = sbuf_substr_dup(buf, 0, pos);
		sbuf_finishx(parser->varname);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		errx(1, "parser error on line %s", sbuf_data(range_tostring(&parser->lines)));
	}
next:
	if (parser->varname) {
		parser_append_token(parser, VARIABLE_END, NULL);
		sbuf_delete(parser->varname);
		parser->varname = NULL;
	}
}

void
parser_read_finish(struct Parser *parser)
{
	parser->lines.end++;

	if (sbuf_len(parser->inbuf) > 0) {
		sbuf_trim(parser->inbuf);
		sbuf_finishx(parser->inbuf);
		parser_read_internal(parser, parser->inbuf);
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	if (parser->settings.behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES) {
		parser_collapse_adjacent_variables(parser);
	}

	if (parser->settings.behavior & PARSER_SANITIZE_APPEND) {
		parser_sanitize_append_modifier(parser);
	}
}

void
parser_collapse_adjacent_variables(struct Parser *parser)
{
	struct Variable *last_var = NULL;
	struct Token *last = NULL;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		switch (o->type) {
		case VARIABLE_START:
			if (last_var != NULL && variable_cmp(o->var, last_var) == 0 &&
			    variable_modifier(last_var) != MODIFIER_EXPAND &&
			    variable_modifier(o->var) != MODIFIER_EXPAND) {
				o->ignore = 1;
				if (last) {
					last->ignore = 1;
					last = NULL;
				}
			}
			break;
		case VARIABLE_END:
			last = o;
			break;
		default:
			last_var = o->var;
			break;
		}
	}
}

void
parser_sanitize_append_modifier(struct Parser *parser)
{
	/* Sanitize += before bsd.options.mk */
	ssize_t start = -1;
	struct Array *seen = array_new(sizeof(struct Variable *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (t->ignore) {
			continue;
		}
		switch(t->type) {
		case VARIABLE_START:
			start = i;
			break;
		case VARIABLE_END: {
			if (start < 0) {
				continue;
			}
			int found = 0;
			for (size_t j = 0; j < array_len(seen); j++) {
				struct Variable *var = array_get(seen, j);
				if (variable_cmp(t->var, var) == 0) {
					found = 1;
					break;
				}
			}
			if (found) {
				start = -1;
				continue;
			} else {
				array_append(seen, t->var);
			}
			for (size_t j = start; j <= i; j++) {
				struct Token *o = array_get(parser->tokens, j);
				if (sbuf_strcmp(variable_name(o->var), "CXXFLAGS") != 0 &&
				    sbuf_strcmp(variable_name(o->var), "CFLAGS") != 0 &&
				    sbuf_strcmp(variable_name(o->var), "LDFLAGS") != 0 &&
				    variable_modifier(o->var) == MODIFIER_APPEND) {
					variable_set_modifier(o->var, MODIFIER_ASSIGN);
				}
			}
			start = -1;
			break;
		} case PORT_OPTIONS_MK:
		case PORT_PRE_MK:
		case PORT_MK:
			goto end;
		default:
			break;
		}
	}
end:
	array_free(seen);
}

void
parser_output_write(struct Parser *parser, int fd)
{
	size_t len = array_len(parser->result);
	if (len == 0) {
		return;
	}

	size_t iov_len = MIN(len, IOV_MAX);
	struct iovec *iov = reallocarray(NULL, iov_len, sizeof(struct iovec));
	if (iov == NULL) {
		err(1, "reallocarray");
	}

	for (size_t i = 0; i < len;) {
		size_t j = 0;
		for (; i < len && j < iov_len; j++) {
			struct sbuf *s = array_get(parser->result, i++);
			iov[j].iov_base = sbuf_data(s);
			iov[j].iov_len = sbuf_len(s);
		}
		if (writev(fd, iov, j) < 0) {
			err(1, "writev");
		}
	}

	/* Collect garbage */
	for (size_t i = 0; i < array_len(parser->result); i++) {
		sbuf_delete((struct sbuf *)array_get(parser->result, i));
	}
	array_truncate(parser->result);
	free(iov);
}
