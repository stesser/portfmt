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

#include <sys/param.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"

struct ShouldDeleteVariableWalkerData {
	struct AST *previous;
	bool delete_variable;
};

// Prototypes
static bool is_empty_line(const char *);
static enum ASTWalkState should_delete_variable_walker(struct AST *, const char *, struct ShouldDeleteVariableWalkerData *);
static char *get_merge_script(struct Mempool *, struct Parser *, struct AST *, const char *);

bool
is_empty_line(const char *s)
{
	for (const char *p = s; *p != 0; p++) {
		if (!isspace((unsigned char)*p)) {
			return false;
		}
	}
	return true;
}

enum ASTWalkState
should_delete_variable_walker(struct AST *node, const char *variable, struct ShouldDeleteVariableWalkerData *this)
{
	switch (node->type) {
	case AST_VARIABLE:
		if (strcmp(node->variable.name, variable) == 0) {
			if (this->previous && this->previous->type == AST_COMMENT) {
				this->delete_variable = true;
				ARRAY_FOREACH(this->previous->comment.lines, const char *, line) {
					this->delete_variable = this->delete_variable && is_empty_line(line);
					unless (this->delete_variable) {
						break;
					}
				}
			}
			return AST_WALK_STOP;
		}
		break;
	default:
		break;
	}
	this->previous = node;
	AST_WALK_DEFAULT(should_delete_variable_walker, node, variable, this);
	return AST_WALK_CONTINUE;
}

char *
get_merge_script(struct Mempool *extpool, struct Parser *parser, struct AST *root, const char *variable)
{
	SCOPE_MEMPOOL(pool);
	struct Array *script = mempool_array(pool);

	struct AST *var;
	if (strcmp(variable, "PORTEPOCH") == 0) {
		if ((var = parser_lookup_variable(parser, "PORTREVISION", PARSER_LOOKUP_FIRST, pool, NULL, NULL)) &&
		    var->variable.modifier == AST_VARIABLE_MODIFIER_OPTIONAL) {
			array_append(script, "PORTREVISION=0\n");
		} else {
			array_append(script, "PORTREVISION!=\n");
		}
	}

	char *comment;
	char *current_revision;
	if ((var = parser_lookup_variable_str(parser, variable, PARSER_LOOKUP_FIRST, pool, &current_revision, &comment)) != NULL) {
		const char *errstr = NULL;
		uint32_t rev = strtonum(current_revision, 0, INT_MAX, &errstr);
		if (errstr == NULL) {
			rev++;
		} else {
			parser_set_error(parser, PARSER_ERROR_EXPECTED_INT, str_printf(pool, "%s %s", errstr, variable));
			return NULL;
		}
		if (parser_lookup_variable(parser, "MASTERDIR", PARSER_LOOKUP_FIRST, pool, NULL, NULL) == NULL) {
			// In slave ports we do not delete the variable first since
			// they have a non-uniform structure and edit_merge will probably
			// insert it into a non-optimal position.

			// If the variable appears after a non-empty comment
			// block we do not delete it either since the comment
			// is probably about the variable and it is natural
			// to have the comment above the variable.
			struct ShouldDeleteVariableWalkerData this = { NULL, true };
			should_delete_variable_walker(root, variable, &this);

			// Otherwise we can safely remove it.
			if (this.delete_variable) {
				array_append(script, variable);
				array_append(script, "!=\n");
			}
		}
		array_append(script, str_printf(pool, "%s%s", var->variable.name, ASTVariableModifier_human(var->variable.modifier)));
		array_append(script, str_printf(pool, "%" PRIu32 " %s\n", rev, comment));
	} else {
		array_append(script, variable);
		array_append(script, "=1\n");
	}

	return str_join(extpool, script, "");
}

PARSER_EDIT(edit_bump_revision)
{
	SCOPE_MEMPOOL(pool);

	const struct ParserEdit *params = userdata;
	if (params == NULL ||
	    params->subparser != NULL ||
	    params->merge_behavior != PARSER_MERGE_DEFAULT) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return;
	}
	const char *variable = params->arg1;

	if (variable == NULL) {
		variable = "PORTREVISION";
	}

	char *script = get_merge_script(pool, parser, root, variable);
	unless (script) {
		return;
	}
	struct ParserSettings settings = parser_settings(parser);
	struct Parser *subparser = parser_new(pool, &settings);
	enum ParserError error = parser_read_from_buffer(subparser, script, strlen(script));
	if (error != PARSER_ERROR_OK) {
		return;
	}
	error = parser_read_finish(subparser);
	if (error != PARSER_ERROR_OK) {
		return;
	}
	parser_merge(parser, subparser, params->merge_behavior | PARSER_MERGE_SHELL_IS_DELETE | PARSER_MERGE_OPTIONAL_LIKE_ASSIGN);
}
