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

#include <regex.h>
#include <string.h>

#include <libias/mempool.h>
#include <libias/str.h>

#include "enum.h"
#include "conditional.h"
#include "regexp.h"
#include "rules.h"

enum ParserASTBuilderConditionalType
parse_conditional(const char *s)
{
	SCOPE_MEMPOOL(pool);

	struct Regexp *re = regexp_new(pool, regex(RE_CONDITIONAL));
	if (regexp_exec(re, s) != 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_INVALID;
	}

	char *tmp = regexp_substr(re, pool, 0);
	if (strlen(tmp) < 2) {
		return PARSER_AST_BUILDER_CONDITIONAL_INVALID;
	}

	char *type;
	if (tmp[0] == '.') {
		char *tmp2 = str_trim(pool, tmp + 1);
		type = str_printf(pool, ".%s", tmp2);
	} else {
		type = str_trim(pool, tmp);
	}

	if (strcmp(type, "include") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX;
	} else if (strcmp(type, ".include") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_INCLUDE;
	} else if (strcmp(type, ".error") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ERROR;
	} else if (strcmp(type, ".export") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_EXPORT;
	} else if (strcmp(type, ".export-env") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV;
	} else if (strcmp(type, ".export.env") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV;
	} else if (strcmp(type, ".export-literal") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_EXPORT_LITERAL;
	} else if (strcmp(type, ".info") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_INFO;
	} else if (strcmp(type, ".undef") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_UNDEF;
	} else if (strcmp(type, ".unexport") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT;
	} else if (strcmp(type, ".for") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_FOR;
	} else if (strcmp(type, ".endfor") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ENDFOR;
	} else if (strcmp(type, ".unexport-env") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT_ENV;
	} else if (strcmp(type, ".warning") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_WARNING;
	} else if (strcmp(type, ".if") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_IF;
	} else if (strcmp(type, ".ifdef") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_IFDEF;
	} else if (strcmp(type, ".ifndef") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_IFNDEF;
	} else if (strcmp(type, ".ifmake") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_IFMAKE;
	} else if (strcmp(type, ".ifnmake") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE;
	} else if (strcmp(type, ".else") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ELSE;
	} else if (strcmp(type, ".elif") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ELIF;
	} else if (strcmp(type, ".elifdef") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF;
	} else if (strcmp(type, ".elifndef") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF;
	} else if (strcmp(type, ".elifmake") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE;
	} else if (strcmp(type, ".endif") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_ENDIF;
	} else if (strcmp(type, ".dinclude") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_DINCLUDE;
	} else if (strcmp(type, ".sinclude") == 0 || strcmp(type, ".-include") == 0) {
		return PARSER_AST_BUILDER_CONDITIONAL_SINCLUDE;
	} else {
		return PARSER_AST_BUILDER_CONDITIONAL_INVALID;
	}
}
