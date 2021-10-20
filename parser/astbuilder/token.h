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

enum ParserASTBuilderTokenType;
struct ASTLineRange;
struct Mempool;

struct ParserASTBuilderToken {
	enum ParserASTBuilderTokenType type;
	struct Mempool *pool;
	char *data;
	struct {
		enum ParserASTBuilderConditionalType type;
		size_t indent;
	} conditional;
	struct {
		char *name;
		enum ASTVariableModifier modifier;
	} variable;
	struct {
		struct Array *sources;
		struct Array *dependencies;
		const char *comment;
	} target;
	bool edited;
	struct ASTLineRange lines;
};

struct ParserASTBuilderToken *parser_astbuilder_token_new(enum ParserASTBuilderTokenType, struct ASTLineRange *, const char *, const char *, const char *, const char *);
struct ParserASTBuilderToken *parser_astbuilder_token_new_comment(struct ASTLineRange *, const char *, enum ParserASTBuilderConditionalType);
void parser_astbuilder_token_free(struct ParserASTBuilderToken *);
