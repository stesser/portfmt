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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mem.h>
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
	struct Set *vars;
};

struct UnknownVariable {
	char *name;
	char *hint;
};

// Prototypes
static struct UnknownVariable *var_new(const char *, const char *);
static void var_free(struct UnknownVariable *);
static int var_compare(const void *, const void *, void *);
static void check_opthelper(struct WalkerData *, const char *, int, int);
static enum ASTWalkState output_unknown_variables_walker(struct AST *, struct WalkerData *);

struct UnknownVariable *
var_new(const char *name, const char *hint)
{
	struct UnknownVariable *var = xmalloc(sizeof(struct UnknownVariable));
	var->name = str_dup(NULL, name);
	if (hint) {
		var->hint = str_dup(NULL, hint);
	}
	return var;
}

void
var_free(struct UnknownVariable *var)
{
	if (var) {
		free(var->name);
		free(var->hint);
		free(var);
	}
}

int
var_compare(const void *ap, const void *bp, void *userdata)
{
	struct UnknownVariable *a = *(struct UnknownVariable **)ap;
	struct UnknownVariable *b = *(struct UnknownVariable **)bp;
	int retval = strcmp(a->name, b->name);
	if (retval == 0) {
		if (a->hint && b->hint) {
			return strcmp(a->hint, b->hint);
		} else if (a->hint) {
			return -1;
		} else {
			return 1;
		}
	} else {
		return retval;
	}
}

void
check_opthelper(struct WalkerData *this, const char *option, int optuse, int optoff)
{
	SCOPE_MEMPOOL(pool);

	const char *suffix;
	if (optoff) {
		suffix = "_OFF";
	} else {
		suffix = "";
	}
	char *var;
	if (optuse) {
		var = str_printf(pool, "%s_USE%s", option, suffix);
	} else {
		var = str_printf(pool, "%s_VARS%s", option, suffix);
	}
	struct Array *optvars;
	if (!parser_lookup_variable(this->parser, var, PARSER_LOOKUP_DEFAULT, pool, &optvars, NULL)) {
		return;
	}

	ARRAY_FOREACH(optvars, const char *, token) {
		char *suffix = strchr(token, '+');
		if (!suffix) {
			suffix = strchr(token, '=');
			if (!suffix) {
				continue;
			}
		} else if (*(suffix + 1) != '=') {
			continue;
		}
		char *name = str_map(pool, token, suffix - token, toupper);
		if (optuse) {
			name = str_printf(pool, "USE_%s", name);
		}
		struct UnknownVariable varskey = { .name = name, .hint = var };
		if (variable_order_block(this->parser, name, NULL, NULL) == BLOCK_UNKNOWN &&
		    !is_referenced_var(this->parser, name) &&
		    !set_contains(this->vars, &varskey) &&
		    (this->param->keyfilter == NULL || this->param->keyfilter(this->parser, name, this->param->keyuserdata))) {
			set_add(this->vars, var_new(name, var));
			if (this->param->callback) {
				this->param->callback(this->pool, name, name, var, this->param->callbackuserdata);
			}
		}
	}
}

enum ASTWalkState
output_unknown_variables_walker(struct AST *node, struct WalkerData *this)
{
	switch (node->type) {
	case AST_VARIABLE: {
		const char *name = node->variable.name;
		struct UnknownVariable varskey = { .name = (char *)name, .hint = NULL };
		if (variable_order_block(this->parser, name, NULL, NULL) == BLOCK_UNKNOWN &&
		    !is_referenced_var(this->parser, name) &&
		    !set_contains(this->vars, &varskey) &&
		    (this->param->keyfilter == NULL || this->param->keyfilter(this->parser, name, this->param->keyuserdata))) {
			set_add(this->vars, var_new(name, NULL));
			this->param->found = 1;
			if (this->param->callback) {
				this->param->callback(this->pool, name, name, NULL, this->param->callbackuserdata);
			}
		}
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(output_unknown_variables_walker, node, this);
	return AST_WALK_CONTINUE;
}


PARSER_EDIT(output_unknown_variables)
{
	SCOPE_MEMPOOL(pool);

	struct ParserEditOutput *param = userdata;
	if (param == NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, "missing parameter");
		return;
	}

	param->found = 0;
	struct WalkerData this = {
		.parser = parser,
		.pool = extpool,
		.param = param,
		.vars = mempool_set(pool, var_compare, NULL, var_free),
	};
	output_unknown_variables_walker(root, &this);

	struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
	SET_FOREACH (options, const char *, option) {
		check_opthelper(&this, option, 1, 0);
		check_opthelper(&this, option, 0, 0);
		check_opthelper(&this, option, 1, 1);
		check_opthelper(&this, option, 0, 1);
	}
}

