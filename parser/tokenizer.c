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
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "astbuilder.h"
#include "astbuilder/enum.h"
#include "astbuilder/conditional.h"
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
	bool continued;
	bool in_target;
	bool finished;
};

struct ParserTokenizeData {
	struct ParserTokenizer *tokenizer;
	int32_t dollar;
	int32_t escape;
	size_t i;
	size_t start;
	const char *line;
	enum ParserASTBuilderTokenType type;
};

// Prototypes
static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct ParserTokenizeData *, size_t, char, char, bool);
static size_t consume_var(const char *);
static bool is_empty_line(const char *);
static const char *parser_tokenize_conditional(struct ParserTokenizeData *);
static void parser_tokenize_helper(struct ParserTokenizeData *);
static void parser_tokenize(struct ParserTokenizer *, const char *, enum ParserASTBuilderTokenType, size_t);
static void parser_tokenizer_create_token(struct ParserTokenizer *, enum ParserASTBuilderTokenType, const char *);
static void parser_tokenizer_read_internal(struct ParserTokenizer *);

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

void
parser_tokenizer_create_token(struct ParserTokenizer *tokenizer, enum ParserASTBuilderTokenType type, const char *token)
{
	parser_astbuilder_append_token(tokenizer->builder, type, token);
}

size_t
consume_comment(const char *buf)
{
	for (const char *bufp = buf; *bufp != 0; bufp++) {
		if (*bufp == '#') {
			return strlen(buf);
		} else if (!isspace((unsigned char)*bufp)) {
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
		"if", "else", "elifdef", "elifndef", "elifmake", "elifnmake",
		"elif", "endif", "sinclude",
	};

	size_t pos = 0;
	if (*buf == '.') {
		pos++;
		for (; isspace((unsigned char)buf[pos]); pos++);
		for (size_t i = 0; i < nitems(conditionals); i++) {
			if (str_startswith(buf + pos, conditionals[i])) {
				pos += strlen(conditionals[i]);
				size_t origpos = pos;
				for (; isspace((unsigned char)buf[pos]); pos++);
				if (buf[pos] == 0 || pos > origpos) {
					return pos;
				} else if (buf[pos] == '(' || buf[pos] == '<' || buf[pos] == '!') {
					return pos;
				}
			}
		}
	} else if (str_startswith(buf, "include")) {
		pos += strlen("include");
		bool space = false;
		for (; isspace((unsigned char)buf[pos]); pos++, space = true);
		if (space) {
			return pos;
		}
	} else if (str_startswith(buf, "-include") || str_startswith(buf, "sinclude")) {
		pos += strlen("-include");
		bool space = false;
		for (; isspace((unsigned char)buf[pos]); pos++, space = true);
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
consume_token(struct ParserTokenizeData *this, size_t pos, char startchar, char endchar, bool eol_ok)
{
	int counter = 0;
	bool escape = false;
	size_t i = pos;
	for (; i < strlen(this->line); i++) {
		char c = this->line[i];
		if (escape) {
			escape = false;
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
				escape = true;
			}
		} else {
			if (c == startchar) {
				counter++;
			} else if (c == endchar && counter == 1) {
				return i;
			} else if (c == endchar) {
				counter--;
			} else if (c == '\\') {
				escape = true;
			}
		}
	}
	if (!eol_ok) {
		SCOPE_MEMPOOL(pool);
		parser_set_error(this->tokenizer->parser, PARSER_ERROR_EXPECTED_CHAR, str_printf(pool, "%c", endchar));
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
	for (i = pos; i < len && !(isspace((unsigned char)buf[i]) || buf[i] == '='); i++);
	if (pos == i) {
		return 0;
	}
	pos = i;

	// [[:space:]]*
	for (; pos < len && isspace((unsigned char)buf[pos]); pos++);

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

bool
is_empty_line(const char *buf)
{
	for (const char *p = buf; *p != 0; p++) {
		if (!isspace((unsigned char)*p)) {
			return false;
		}
	}

	return true;
}

static void
consume_expansion(struct ParserTokenizeData *this)
{
	SCOPE_MEMPOOL(pool);
	panic_unless(this->dollar, "not in '$' state");
	char c = this->line[this->i];
	if (this->dollar > 1) {
		if (c == '(') {
			this->i = consume_token(this, this->i - 2, '(', ')', false);
			if (*this->tokenizer->parser_error != PARSER_ERROR_OK) {
				return;
			}
			this->dollar = 0;
		} else if (c == '$') {
			this->dollar++;
		} else if (c == ' ' || c == '\t') {
			const char *token = str_trim(pool, str_slice(pool, this->line, this->start, this->i));
			if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
				parser_tokenizer_create_token(this->tokenizer, this->type, token);
			}
			this->start = this->i;
			this->dollar = 0;
		} else {
			this->dollar = 0;
		}
	} else if (c == '{') {
		this->i = consume_token(this, this->i, '{', '}', false);
		this->dollar = 0;
	} else if (c == '(') {
		this->i = consume_token(this, this->i, '(', ')', false);
		this->dollar = 0;
	} else if (isalnum((unsigned char)c) || c == '@' || c == '<' || c == '>' || c == '/' ||
		   c == '?' || c == '*' || c == '^' || c == '-' || c == '_' ||
		   c == ')') {
		this->dollar = 0;
	} else if (c == ' ' || c == '\\') {
		/* '$ ' or '$\' are ignored by make for some reason instead of making
		 * it an error, so we do the same...
		 */
		this->dollar = 0;
		this->i--;
	} else if (c == 1) {
		this->dollar = 0;
	} else if (c == '$') {
		this->dollar++;
	} else {
		parser_set_error(this->tokenizer->parser, PARSER_ERROR_EXPECTED_CHAR, "$");
	}
}

static const char *
parser_tokenize_conditional(struct ParserTokenizeData *this)
{
	unless (this->type == PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN && this->tokenizer->builder->condname) {
		return NULL;
	}

	size_t indent = 0;
	switch (parse_conditional(this->tokenizer->builder->condname, &indent)) {
	case PARSER_AST_BUILDER_CONDITIONAL_ELIF:
	case PARSER_AST_BUILDER_CONDITIONAL_IF:
		break;
	default:
		return NULL;
	}

	static const char *condtokens[] = {
		"commands(",
		"defined(",
		"empty(",
		"exists(",
		"make(",
		"target(",
		"==",
		"!=",
		"<=",
		">=",
		"<",
		">",
		"&&",
		"||",
		"!",
		"(",
		")",
	};

	for (size_t i = 0; i < nitems(condtokens); i++) {
		if (str_startswith(this->line + this->i, condtokens[i])) {
			return condtokens[i];
		}
	}

	return NULL;
}

void
parser_tokenize_helper(struct ParserTokenizeData *this)
{
	SCOPE_MEMPOOL(pool);

	for (; this->i < strlen(this->line); this->i++) {
		panic_if(this->i < this->start, "index went before start");
		char c = this->line[this->i];
		if (this->escape) {
			this->escape = 0;
			if (c == '#' || c == '"' || c == '\'' || c == '\\' || c == '$' || isspace((unsigned char)c)) {
				continue;
			}
		}
		const char *condtoken = NULL;
		if (this->dollar) {
			consume_expansion(this);
		} else if (c == ' ' || c == '\t') {
			const char *token = str_trim(pool, str_slice(pool, this->line, this->start, this->i));
			if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
				parser_tokenizer_create_token(this->tokenizer, this->type, token);
			}
			this->start = this->i;
		} else if (c == '"') {
			this->i = consume_token(this, this->i, '"', '"', true);
		} else if (c == '\'') {
			this->i = consume_token(this, this->i, '\'', '\'', true);
		} else if (c == '`') {
			this->i = consume_token(this, this->i, '`', '`', true);
		} else if (c == '$') {
			this->dollar++;
		} else if (c == '\\') {
			this->escape = 1;
		} else if (c == '#') {
			const char *token = str_trim(pool, str_slice(pool, this->line, this->start, this->i));
			if (strcmp(token, "") != 0) {
				parser_tokenizer_create_token(this->tokenizer, this->type, token);
			}
			token = str_trim(pool, str_slice(pool, this->line, this->i, -1));
			parser_tokenizer_create_token(this->tokenizer, this->type, token);
			parser_set_error(this->tokenizer->parser, PARSER_ERROR_OK, NULL);
			return;
		} else if ((condtoken = parser_tokenize_conditional(this))) {
			const char *token = str_trim(pool, str_slice(pool, this->line, this->start, this->i));
			if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
				parser_tokenizer_create_token(this->tokenizer, this->type, token);
			}
			parser_tokenizer_create_token(this->tokenizer, this->type, condtoken);
			this->start = this->i + strlen(condtoken);
			this->i += strlen(condtoken) - 1;
		}
		if (*this->tokenizer->parser_error != PARSER_ERROR_OK) {
			return;
		}
	}

	const char *token = str_trim(pool, str_slice(pool, this->line, this->start, this->i));
	if (strcmp(token, "") != 0) {
		parser_tokenizer_create_token(this->tokenizer, this->type, token);
	}
	parser_set_error(this->tokenizer->parser, PARSER_ERROR_OK, NULL);
}

void
parser_tokenize(struct ParserTokenizer *tokenizer, const char *line, enum ParserASTBuilderTokenType type, size_t start)
{
	parser_tokenize_helper(&(struct ParserTokenizeData){
		.tokenizer = tokenizer,
		.dollar = 0,
		.escape = 0,
		.i = start,
		.start = start,
		.line = line,
		.type = type,
	});
}

void
parser_tokenizer_feed_line(struct ParserTokenizer *tokenizer, const char *inputline, const size_t linelen)
{
	SCOPE_MEMPOOL(pool);
	panic_if(tokenizer->finished, "tokenizer is in finished state");

	if (*tokenizer->parser_error != PARSER_ERROR_OK) {
		return;
	}

	tokenizer->builder->lines.b++;

	char *line = str_ndup(pool, inputline, linelen);
	if (linelen != strlen(line)) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_IO, "input not a Makefile?"); // 0 byte before \n ?
		return;
	}
	bool will_continue = linelen > 0 && line[linelen - 1] == '\\' && (linelen == 1 || line[linelen - 2] != '\\');
	if (will_continue) {
 		if (linelen > 2 && line[linelen - 2] == '$' && line[linelen - 3] != '$') {
			/* Hack to "handle" things like $\ in variable values */
			line[linelen - 1] = 1;
		} else if (linelen > 1 && !isspace((unsigned char)line[linelen - 2])) {
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
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_COMMENT, buf);
		goto next;
	} else if (is_empty_line(buf)) {
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_COMMENT, buf);
		goto next;
	}

	if (tokenizer->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			tokenizer->builder->condname = str_trimr(tokenizer->builder->pool, str_ndup(tokenizer->builder->pool, buf, pos));
			parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, tokenizer->builder->condname);
			parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, tokenizer->builder->condname);
			parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, pos);
			parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, tokenizer->builder->condname);
			goto next;
		}
		if (consume_var(buf) == 0 && consume_target(buf) == 0 &&
		    *buf != 0 && *buf == '\t') {
			parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START, NULL);
			parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN, 0);
			parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END, NULL);
			goto next;
		}
		if (consume_var(buf) > 0) {
			goto var;
		}
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_TARGET_END, NULL);
		tokenizer->in_target = false;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		tokenizer->builder->condname = str_trimr(tokenizer->builder->pool, str_ndup(tokenizer->builder->pool, buf, pos));
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START, tokenizer->builder->condname);
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, tokenizer->builder->condname);
		parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN, pos);
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END, tokenizer->builder->condname);
		goto next;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		tokenizer->in_target = true;
		tokenizer->builder->targetname = str_dup(tokenizer->builder->pool, buf);
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_TARGET_START, buf);
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
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_VARIABLE_START, NULL);
	}
	parser_tokenize(tokenizer, buf, PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN, pos);
	if (tokenizer->builder->varname == NULL) {
		parser_set_error(tokenizer->parser, PARSER_ERROR_UNSPECIFIED, NULL);
	}
next:
	if (tokenizer->builder->varname) {
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_VARIABLE_END, NULL);
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
		parser_tokenizer_create_token(tokenizer, PARSER_AST_BUILDER_TOKEN_TARGET_END, NULL);
	}

	tokenizer->finished = true;

	return PARSER_ERROR_OK;
}
