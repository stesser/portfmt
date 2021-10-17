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
#pragma once

enum ParserASTBuilderTokenType {
	PARSER_AST_BUILDER_TOKEN_COMMENT,		// human:"comment"
	PARSER_AST_BUILDER_TOKEN_CONDITIONAL_END,	// human:"conditional-end"
	PARSER_AST_BUILDER_TOKEN_CONDITIONAL_TOKEN,	// human:"conditional-token"
	PARSER_AST_BUILDER_TOKEN_CONDITIONAL_START,	// human:"conditional-start"
	PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_END,	// human:"target-command-end"
	PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_START,	// human:"target-command-start"
	PARSER_AST_BUILDER_TOKEN_TARGET_COMMAND_TOKEN,	// human:"target-command-token"
	PARSER_AST_BUILDER_TOKEN_TARGET_END,		// human:"target-end"
	PARSER_AST_BUILDER_TOKEN_TARGET_START,		// human:"target-start"
	PARSER_AST_BUILDER_TOKEN_VARIABLE_END,		// human:"variable-end"
	PARSER_AST_BUILDER_TOKEN_VARIABLE_START,	// human:"variable-start"
	PARSER_AST_BUILDER_TOKEN_VARIABLE_TOKEN,	// human:"variable-token"
};

const char *ParserASTBuilderTokenType_human(enum ParserASTBuilderTokenType);
const char *ParserASTBuilderTokenType_tostring(enum ParserASTBuilderTokenType);

enum ParserASTBuilderConditionalType {
	PARSER_AST_BUILDER_CONDITIONAL_INVALID,				// human:"<invalid>"
	PARSER_AST_BUILDER_CONDITIONAL_ELIF,				// human:".elif"
	PARSER_AST_BUILDER_CONDITIONAL_ELIFDEF,				// human:".elifdef"
	PARSER_AST_BUILDER_CONDITIONAL_ELIFNDEF,			// human:".elifndef"
	PARSER_AST_BUILDER_CONDITIONAL_ELIFMAKE,			// human:".elifmake"
	PARSER_AST_BUILDER_CONDITIONAL_ELIFNMAKE,			// human:".elifnmake"
	PARSER_AST_BUILDER_CONDITIONAL_ELSE,				// human:".else"
	PARSER_AST_BUILDER_CONDITIONAL_ENDFOR,				// human:".endfor"
	PARSER_AST_BUILDER_CONDITIONAL_ENDIF,				// human:".endif"
	PARSER_AST_BUILDER_CONDITIONAL_ERROR,				// human:".error"
	PARSER_AST_BUILDER_CONDITIONAL_EXPORT_ENV,			// human:".export-env"
	PARSER_AST_BUILDER_CONDITIONAL_EXPORT_LITERAL,			// human:".export-literal"
	PARSER_AST_BUILDER_CONDITIONAL_EXPORT,				// human:".export"
	PARSER_AST_BUILDER_CONDITIONAL_FOR,				// human:".for"
	PARSER_AST_BUILDER_CONDITIONAL_IF,				// human:".if"
	PARSER_AST_BUILDER_CONDITIONAL_IFDEF,				// human:".ifdef"
	PARSER_AST_BUILDER_CONDITIONAL_IFMAKE,				// human:".ifmake"
	PARSER_AST_BUILDER_CONDITIONAL_IFNDEF,				// human:".ifndef"
	PARSER_AST_BUILDER_CONDITIONAL_IFNMAKE,				// human:".ifnmake"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE,				// human:".include"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL,		// human:".-include"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_D,		// human:".dinclude"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_OPTIONAL_S,		// human:".sinclude"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX,			// human:"include"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL,		// human:"-include"
	PARSER_AST_BUILDER_CONDITIONAL_INCLUDE_POSIX_OPTIONAL_S,	// human:"sinclude"
	PARSER_AST_BUILDER_CONDITIONAL_INFO,				// human:".info"
	PARSER_AST_BUILDER_CONDITIONAL_UNDEF,				// human:".undef"
	PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT_ENV,			// human:".unexport-env"
	PARSER_AST_BUILDER_CONDITIONAL_UNEXPORT,			// human:".unexport"
	PARSER_AST_BUILDER_CONDITIONAL_WARNING,				// human:".warning"
};

const char *ParserASTBuilderConditionalType_human(enum ParserASTBuilderConditionalType);
const char *ParserASTBuilderConditionalType_tostring(enum ParserASTBuilderConditionalType);
