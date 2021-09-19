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

/* Order is significant here and should match variable_order_ in rules.c */
enum BlockType {
	BLOCK_PORTNAME,
	BLOCK_PATCHFILES,
	BLOCK_MAINTAINER,
	BLOCK_LICENSE,
	BLOCK_LICENSE_OLD,
	BLOCK_BROKEN,
	BLOCK_DEPENDS,
	BLOCK_FLAVORS,
	BLOCK_FLAVORS_HELPER,
#if PORTFMT_SUBPACKAGES
	BLOCK_SUBPACKAGES,
#endif
	BLOCK_USES,
	BLOCK_SHEBANGFIX,
	BLOCK_UNIQUEFILES,
	BLOCK_APACHE,
	BLOCK_ELIXIR,
	BLOCK_EMACS,
	BLOCK_ERLANG,
	BLOCK_CMAKE,
	BLOCK_CONFIGURE,
	BLOCK_QMAKE,
	BLOCK_MESON,
	BLOCK_SCONS,
	BLOCK_CABAL,
	BLOCK_CARGO,
	BLOCK_GO,
	BLOCK_LAZARUS,
	BLOCK_LINUX,
	BLOCK_NUGET,
	BLOCK_MAKE,
	BLOCK_CFLAGS,
	BLOCK_CONFLICTS,
	BLOCK_STANDARD,
	BLOCK_WRKSRC,
	BLOCK_USERS,
	BLOCK_PLIST,
	BLOCK_OPTDEF,
	BLOCK_OPTDESC,
	BLOCK_OPTHELPER,
	BLOCK_UNKNOWN,
};

enum RegularExpression {
	RE_CONDITIONAL = 0,
};

struct CompareTokensData {
	struct Parser *parser;
	const char *var;
};

struct Mempool;
struct Parser;
struct Set;
enum ASTNodeVariableModifier;
struct ASTNode;

const char *blocktype_tostring(enum BlockType);
int compare_order(const void *, const void *, void *);
int compare_target_order(const void *, const void *, void *);
int compare_tokens(const void *, const void *, void *);
int ignore_wrap_col(struct Parser *, const char *, enum ASTNodeVariableModifier);
int indent_goalcol(const char *, enum ASTNodeVariableModifier);
int is_comment(const char *);
int is_referenced_var(struct Parser *, const char *);
int is_include_bsd_port_mk(struct ASTNode *);
int is_known_target(struct Parser *, const char *);
int is_special_source(const char *);
int is_special_target(const char *);
int is_options_helper(struct Mempool *, struct Parser *, const char *, char **, char **, char **);
int leave_unformatted(struct Parser *, const char *);
regex_t *regex(enum RegularExpression);
int matches(enum RegularExpression, const char *);
int print_as_newlines(struct Parser *, const char *);
void rules_init(void);
int should_sort(struct Parser *, const char *, enum ASTNodeVariableModifier);
int skip_dedup(struct Parser *, const char *, enum ASTNodeVariableModifier);
int skip_goalcol(struct Parser *, const char *);
int target_command_wrap_after_each_token(const char *);
int target_command_should_wrap(const char *);
enum BlockType variable_order_block(struct Parser *, const char *, struct Mempool *, struct Set **);
