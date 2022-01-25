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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/set.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

struct WalkerData {
	struct Parser *parser;
	struct Mempool *pool;
	struct ParserEditOutput *param;
	struct Set *targets;
	struct Set *deps;
	struct Set *post_plist_targets;
};

// Prototypes
static void check_target(struct WalkerData *, const char *, bool);
static enum ASTWalkState output_unknown_targets_walker(struct AST *, struct WalkerData *);

void
check_target(struct WalkerData *this, const char *name, bool deps)
{
	if (deps) {
		if (is_special_source(name)) {
			return;
		}
		if (is_known_target(this->parser, name)) {
			return;
		}
		if (set_contains(this->targets, name)) {
			return;
		}
		if (set_contains(this->post_plist_targets, name)) {
			return;
		}
	} else {
		if (is_special_target(name)) {
			return;
		}
		if (is_known_target(this->parser, name)) {
			return;
		}
		if (set_contains(this->deps, name)) {
			return;
		}
		if (set_contains(this->post_plist_targets, name)) {
			return;
		}
	}
	if ((this->param->keyfilter == NULL || this->param->keyfilter(this->parser, name, this->param->keyuserdata))) {
		this->param->found = true;
		if (this->param->callback) {
			// XXX: provide option as hint for opthelper targets?
			this->param->callback(this->pool, name, name, NULL, this->param->callbackuserdata);
		}
	}
}

enum ASTWalkState
output_unknown_targets_walker(struct AST *node, struct WalkerData *this) 
{
	switch (node->type) {
	case AST_TARGET: {
		bool skip_deps = false;
		ARRAY_FOREACH(node->target.sources, const char *, name) {
			if (is_special_target(name)) {
				skip_deps = true;
			}
			set_add(this->targets, name);
		}
		unless (skip_deps) {
			ARRAY_FOREACH(node->target.dependencies, const char *, name) {
				set_add(this->deps, name);
			}
		}
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(output_unknown_targets_walker, node, this);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(output_unknown_targets)
{
	SCOPE_MEMPOOL(pool);

	struct ParserEditOutput *param = userdata;
	if (param == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, "missing parameter");
		return;
	}

	param->found = true;
	struct WalkerData this = {
		.parser = parser,
		.pool = extpool,
		.param = param,
		.targets = mempool_set(pool, str_compare),
		.deps = mempool_set(pool, str_compare),
		.post_plist_targets = parser_metadata(parser, PARSER_METADATA_POST_PLIST_TARGETS),
	};
	output_unknown_targets_walker(root, &this);

	SET_FOREACH(this.targets, const char *, name) {
		check_target(&this, name, false);
	}
	SET_FOREACH(this.deps, const char *, name) {
		check_target(&this, name, true);
	}
}
