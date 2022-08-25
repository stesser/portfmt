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
	BLOCK_PORTNAME,		// human:"PORTNAME block"
	BLOCK_PATCHFILES,	// human:"Patch files"
	BLOCK_MAINTAINER,	// human:"Maintainer block"
	BLOCK_WWW,		// human:"Project website"
	BLOCK_LICENSE,		// human:"License block"
	BLOCK_LICENSE_OLD,	// human:"Old-school license block (please replace with LICENSE)"
	BLOCK_BROKEN,		// human:"BROKEN/IGNORE/DEPRECATED messages"
	BLOCK_DEPENDS,		// human:"Dependencies"
	BLOCK_FLAVORS,		// human:"Flavors"
	BLOCK_FLAVORS_HELPER,	// human:"Flavors helpers"
#if PORTFMT_SUBPACKAGES
	BLOCK_SUBPACKAGES,	// human:"Subpackages block"
#endif
	BLOCK_USES,		// human:"USES block"
	BLOCK_SHEBANGFIX,	// human:"USES=shebangfix related variables"
	BLOCK_UNIQUEFILES,	// human:"USES=uniquefiles block"
	BLOCK_APACHE,		// human:"USES=apache related variables"
	BLOCK_ELIXIR,		// human:"USES=elixir related variables"
	BLOCK_EMACS,		// human:"USES=emacs related variables"
	BLOCK_ERLANG,		// human:"USES=erlang related variables"
	BLOCK_CMAKE,		// human:"USES=cmake related variables"
	BLOCK_CONFIGURE,	// human:"Configure block"
	BLOCK_QMAKE,		// human:"USES=qmake related variables"
	BLOCK_MESON,		// human:"USES=meson related variables"
	BLOCK_SCONS,		// human:"USES=scons related variables"
	BLOCK_CABAL,		// human:"USES=cabal related variables"
	BLOCK_CARGO,		// human:"USES=cargo related variables"
	BLOCK_GO,		// human:"USES=go related variables"
	BLOCK_LAZARUS,		// human:"USES=lazarus related variables"
	BLOCK_LINUX,		// human:"USES=linux related variables"
	BLOCK_NUGET,		// human:"USES=mono related variables"
	BLOCK_MAKE,		// human:"Make block"
	BLOCK_CFLAGS,		// human:"CFLAGS/CXXFLAGS/LDFLAGS block"
	BLOCK_CONFLICTS,	// human:"Conflicts"
	BLOCK_STANDARD,		// human:"Standard bsd.port.mk variables"
	BLOCK_WRKSRC,		// human:"WRKSRC block"
	BLOCK_USERS,		// human:"Users and groups block"
	BLOCK_PLIST,		// human:"Packaging list block"
	BLOCK_OPTDEF,		// human:"Options definitions"
	BLOCK_OPTDESC,		// human:"Options descriptions"
	BLOCK_OPTHELPER,	// human:"Options helpers"
	BLOCK_UNKNOWN,		// human:"Unknown variables"
};

const char *BlockType_human(enum BlockType);
const char *BlockType_tostring(enum BlockType);

struct CompareTokensData {
	struct Parser *parser;
	const char *var;
};

struct Mempool;
struct Parser;
struct Set;
enum ASTVariableModifier;
struct AST;

int compare_order(const void *, const void *, void *);
int compare_target_order(const void *, const void *, void *);
int compare_tokens(const void *, const void *, void *);
bool ignore_wrap_col(struct Parser *, const char *, enum ASTVariableModifier);
uint32_t indent_goalcol(const char *, enum ASTVariableModifier);
bool is_comment(const char *);
bool is_referenced_var(struct Parser *, const char *);
bool is_include_bsd_port_mk(struct AST *);
bool is_known_target(struct Parser *, const char *);
bool is_special_source(const char *);
bool is_special_target(const char *);
bool is_options_helper(struct Mempool *, struct Parser *, const char *, char **, char **, char **);
bool leave_unformatted(struct Parser *, const char *);
bool print_as_newlines(struct Parser *, const char *);
bool should_sort(struct Parser *, const char *, enum ASTVariableModifier);
bool skip_dedup(struct Parser *, const char *, enum ASTVariableModifier);
bool skip_goalcol(struct Parser *, const char *);
bool target_command_wrap_after_each_token(const char *);
bool target_command_should_wrap(const char *);
enum BlockType variable_order_block(struct Parser *, const char *, struct Mempool *, struct Set **);
