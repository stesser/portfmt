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
	struct Set *post_plist_targets;
};

static int
add_target(struct WalkerData *this, const char *name, int deps)
{
	if (deps && is_special_source(name)) {
		return 0;
	}
	if (is_special_target(name)) {
		return 1;
	}
	if (!is_known_target(this->parser, name) &&
	    !set_contains(this->post_plist_targets, name) &&
	    !set_contains(this->targets, name) &&
	    (this->param->keyfilter == NULL || this->param->keyfilter(this->parser, name, this->param->keyuserdata))) {
		set_add(this->targets, name);
		this->param->found = 1;
		if (this->param->callback) {
			// XXX: provide option as hint for opthelper targets?
			this->param->callback(this->pool, name, name, NULL, this->param->callbackuserdata);
		}
	}
	return 0;
}

static enum ASTWalkState
output_unknown_targets_walker(struct ASTNode *node, struct WalkerData *this) 
{
	switch (node->type) {
	case AST_NODE_TARGET: {
		int skip_deps = 0;
		ARRAY_FOREACH(node->target.sources, const char *, name) {
			if (add_target(this, name, 0)) {
				skip_deps = 1;
			}
		}
		if (!skip_deps) {
			ARRAY_FOREACH(node->target.dependencies, const char *, name) {
				add_target(this, name, 1);
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
		return 0;
	}

	param->found = 0;
	output_unknown_targets_walker(root, &(struct WalkerData){
		.parser = parser,
		.pool = extpool,
		.param = param,
		.targets = mempool_set(pool, str_compare, NULL, NULL),
		.post_plist_targets = parser_metadata(parser, PARSER_METADATA_POST_PLIST_TARGETS),
	});

	return 0;
}

