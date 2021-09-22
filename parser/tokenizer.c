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
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "astbuilder.h"
#include "astbuilder/enum.h"
#include "parser.h"
#include "tokenizer.h"

struct ParserTokenizer {
	struct Parser *parser;
	struct ParserASTBuilder *builder;

	const enum ParserError *parser_error;
	struct {
		char *buf;
		size_t len;
		FILE *stream;
	} inbuf;
	int continued;
	int in_target;
	int skip;
	int finished;
};

static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct ParserTokenizer *, const char *, size_t, char, char, int);
static size_t consume_var(const char *);
static int is_empty_line(const char *);
static void parser_tokenizer_read_internal(struct ParserTokenizer *);
static void parser_tokenize(struct ParserTokenizer *, const char *, enum ParserASTBuilderTokenType, size_t);

struct ParserTokenizer *
parser_tokenizer_new(struct Parser *parser, const enum ParserError *error, struct ParserASTBuilder *builder)
{
	struct ParserTokenizer *tokenizer = xmalloc(sizeof(struct ParserTokenizer));
	tokenizer->parser = parser;
	tokenizer->builder = builder;
	tokenizer->inbuf.stream = open_memstream(&tokenizer->inbuf.buf, &tokenizer->inbuf.len);
	tokenizer->parser_error = error;
	panic_unless(tokenizer->inbuf.stream, "open_memstream failed");
	return tokenizer;
}

void
parser_tokenizer_free(struct ParserTokenizer *tokenizer)
{
	if (tokenizer) {
		fclose(tokenizer->inbuf.stream);
		free(tokenizer->inbuf.buf);
		free(tokenizer);
	}
}

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
	static const char *conditionals[] = {
		"error", "export-env", "export.env", "export-literal", "export",
		"unexport-env", "unexport", "undef", "info", "for", "endfor",
		"warning", "ifdef", "ifndef", "include", "ifmake", "ifnmake",
		"if", "else", "elifndef", "elifmake", "elifdef", "elif", "endif",
		"sinclude",
	};

	size_t pos = 0;
	if (*buf == '.') {
		pos++;
		for (; isspace(buf[pos]); pos++);
		for (size_t i = 0; i < nitems(conditionals); i++) {
			if (str_startswith(buf + pos, conditionals[i])) {
				pos += strlen(conditionals[i]);
				size_t origpos = pos;
				for (; isspace(buf[pos]); pos++);
				if (buf[pos] == 0 || pos > origpos) {
					return pos;
				} else if (buf[pos] == '(' || buf[pos] == '<' || buf[pos] == '!') {
					return pos;
				}
			}
		}
	} else if (str_startswith(buf, "include")) {
		pos += strlen("include");
		int space = 0;
		for (; isspace(buf[pos]); pos++, space = 1);
		if (space) {
			return pos;
		}
	}

	return 0;
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
consume_token(struct ParserTokenizer *tokenizer, const char *line, size_t pos,
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
		parser_set_error(tokenizer->parser, PARSER_ERROR_EXPECTED_CHAR, str_printf(pool, "%c", endchar));
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

void
parser_tokenize(struct ParserTokenizer *tokenizer, const char *line, enum ParserASTBuilderTokenType type, size_t start)
{
	SCOPE_MEMPOOL(pool);
	struct Parser *parser = tokenizer->parser;

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
					i = consume_token(tokenizer, line, i - 2, '(', ')', 0);
					if (*tokenizer->parser_error != PARSER_ERROR_OK) {
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
				i = consume_token(tokenizer, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '(') {
				i = consume_token(tokenizer, line, i, '(', ')', 0);
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
			if (*tokenizer->parser_error != PARSER_ERROR_OK) {
				return;
			}
		} else {
			if (c == ' ' || c == '\t') {
				token = str_trim(pool, str_slice(pool, line, start, i));
				if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
					parser_astbuilder_append_token(tokenizer->builder, type, token);
				}
				token = NULL;
				start = i;
			} else if (c == '"') {
				i = consume_token(tokenizer, line, i, '"', '"', 1);
			} else if (c == '\'') {
				i = consume_token(tokenizer, line, i, '\'', '\'', 1);
			} else if (c == '`') {
				i = consume_token(tokenizer, line, i, '`', '`', 1);
			} else if (c == '$') {
				dollar++;
			} else if (c == '\\') {
				escape = 1;
			} else if (c == '#') {
				token = str_trim(pool, str_slice(pool, line, i, -1));
				parser_astbuilder_append_token(tokenizer->builder, type, token);
				token = NULL;
				parser_set_error(tokenizer->parser, PARSER_ERROR_OK, NULL);
				return;
			}
			if (*tokenizer->parser_error != PARSER_ERROR_OK) {
				return;
			}
		}
	}

	token = str_trim(pool, str_slice(pool, line, start, i));
	if (strcmp(token, "") != 0) {
		parser_astbuilder_append_token(tokenizer->builder, type, token);
	}
	parser_set_error(parser, PARSER_ERROR_OK, NULL);
}

void
parser_tokenizer_feed_line(struct ParserTokenizer *tokenizer, char *line, size_t linelen)
{
	SCOPE_MEMPOOL(pool);
	panic_if(tokenizer->finished, "tokenizer is in finished state");

	if (*tokenizer->parser_error != PARSER_ERROR_OK) {
		return;
	}

	tokenizer->builder->lines.b++;

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

	if (tokenizer->continued) {
		/* Replace all whitespace at the beginning with a single
		 * space which is what make seems to do.
		 */
		for (;isblank(*line); line++);
		if (strlen(line) < 1) {
			if (fputc(' ', tokenizer->inbuf.stream) != ' ') {
				parser_set_error(tokenizer->parser, PARSER_ERROR_IO,
					 str_printf(pool, "fputc: %s", strerror(errno)));
				return;
			}
		}
	}

	fwrite(line, 1, strlen(line), tokenizer->inbuf.stream);
	if (ferror(tokenizer->inbuf.stream)) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_IO,
				 str_printf(pool, "fwrite: %s", strerror(errno)));
		return;
	}

	if (!will_continue) {
		parser_tokenizer_read_internal(tokenizer);
		if (*tokenizer->parser_error != PARSER_ERROR_OK) {
			return;
		}
		tokenizer->builder->lines.a = tokenizer->builder->lines.b;
		fclose(tokenizer->inbuf.stream);
		free(tokenizer->inbuf.buf);
		tokenizer->inbuf.buf = NULL;
		tokenizer->inbuf.stream = open_memstream(&tokenizer->inbuf.buf, &tokenizer->inbuf.len);
		panic_unless(tokenizer->inbuf.stream, "open_memstream failed");
	}

	tokenizer->continued = will_continue;
}

void
parser_tokenizer_read_internal(struct ParserTokenizer *tokenizer)
{
	SCOPE_MEMPOOL(pool);

	if (*tokenizer->parser_error != PARSER_ERROR_OK) {
		return;
	}

	if (fflush(tokenizer->inbuf.stream) != 0) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_IO,
				 str_printf(pool, "fflush: %s", strerror(errno)));
		return;
	}

	char *buf = str_trimr(pool, tokenizer->inbuf.buf);
	size_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_COMMENT, buf);
		goto next;
	} else if (is_empty_line(buf)) {
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_COMMENT, buf);
		goto next;
	}

	if (tokenizer->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			tokenizer->builder->condname = str_trimr(tokenizer->builder->pool, str_ndup(tokenizer->builder->pool, buf, pos));
			parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, tokenizer->builder->condname);
			parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, tokenizer->builder->condname);
			parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, pos);
			parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, tokenizer->builder->condname);
			goto next;
		}
		if (consume_var(buf) == 0 && consume_target(buf) == 0 &&
		    *buf != 0 && *buf == '\t') {
			parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START, NULL);
			parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN, 0);
			parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END, NULL);
			goto next;
		}
		if (consume_var(buf) > 0) {
			goto var;
		}
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_TARGET_END, NULL);
		tokenizer->in_target = 0;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		tokenizer->builder->condname = str_trimr(tokenizer->builder->pool, str_ndup(tokenizer->builder->pool, buf, pos));
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, tokenizer->builder->condname);
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, tokenizer->builder->condname);
		parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, pos);
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, tokenizer->builder->condname);
		goto next;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		tokenizer->in_target = 1;
		tokenizer->builder->targetname = str_dup(tokenizer->builder->pool, buf);
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_TARGET_START, buf);
		goto next;
	}

var:
	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > strlen(buf)) {
			parser_set_error(tokenizer->parser, PARSER_ERROR_UNSPECIFIED, "inbuf overflow");
			goto next;
		}
		tokenizer->builder->varname = str_trim(tokenizer->builder->pool, str_ndup(tokenizer->builder->pool, buf, pos));
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_VARIABLE_START, NULL);
	}
	parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN, pos);
	if (tokenizer->builder->varname == NULL) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_UNSPECIFIED, NULL);
	}
next:
	if (tokenizer->builder->varname) {
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_VARIABLE_END, NULL);
		tokenizer->builder->varname = NULL;
	}
}

enum ParserError
parser_tokenizer_finish(struct ParserTokenizer *tokenizer)
{
	SCOPE_MEMPOOL(pool);
	panic_if(tokenizer->finished, "tokenizer is in finished state");

	if (!tokenizer->continued) {
		tokenizer->builder->lines.b++;
	}

	if (fflush(tokenizer->inbuf.stream) != 0) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_IO,
				 str_printf(pool, "fflush: %s", strerror(errno)));
		return *tokenizer->parser_error;
	}

	if (tokenizer->inbuf.len > 0) {
		parser_tokenizer_read_internal(tokenizer);
		if (*tokenizer->parser_error != PARSER_ERROR_OK) {
			return *tokenizer->parser_error;
		}
	}

	if (tokenizer->in_target) {
		parser_astbuilder_append_token(tokenizer->builder, PARSER_AST_BUILDER_TOKEN_TARGET_END, NULL);
	}

	tokenizer->finished = 1;

	return PARSER_ERROR_OK;
}
