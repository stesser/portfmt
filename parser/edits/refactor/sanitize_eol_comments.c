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
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/flow.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "ast.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"

static int
preserve_eol_comment(const char *word)
{
	SCOPE_MEMPOOL(pool);

	if (!is_comment(word)) {
		return 1;
	}

	/* Remove all whitespace from the comment first to cover more cases */
	char *token = mempool_alloc(pool, strlen(word) + 1);
	const char *datap = word;
	for (char *tokenp = token; *datap != 0; datap++) {
		if (!isspace(*datap)) {
			*tokenp++ = *datap;
		}
	}
	return strcmp(token, "#") == 0 || strcmp(token, "#empty") == 0 || strcmp(token, "#none") == 0;
}

static enum ASTWalkState
refactor_sanitize_eol_comments_walker(struct ASTNode *node)
{
	switch (node->type) {
	/* Try to push end of line comments out of the way above
	 * the variable as a way to preserve them.  They clash badly
	 * with sorting tokens in variables.  We could add more
	 * special cases for this, but often having them at the top
	 * is just as good.
	 */
	case AST_NODE_VARIABLE: {
		if (preserve_eol_comment(node->variable.comment)) {
			return AST_WALK_CONTINUE;
		}
		struct ASTNode *comment = ast_node_new(node->pool, AST_NODE_COMMENT, &node->line_start, &(struct ASTNodeComment){
			.type = AST_NODE_COMMENT_LINE,
		});
		array_append(comment->comment.lines, node->variable.comment);
		node->variable.comment = NULL;
		node->edited = 1;
		comment->edited = 1;
		ast_node_parent_insert_before_sibling(node, comment);
		break;
	} default:
		break;
	}

	AST_WALK_DEFAULT(refactor_sanitize_eol_comments_walker, node);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(refactor_sanitize_eol_comments)
{
	if (userdata != NULL) {
		parser_set_error(parser, PARSER_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}

	refactor_sanitize_eol_comments_walker(root);

	return 1;
}
