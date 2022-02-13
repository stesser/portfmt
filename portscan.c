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
#include <sys/stat.h>
#include <dirent.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <regex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <libias/array.h>
#include <libias/diff.h>
#include <libias/flow.h>
#include <libias/io.h>
#include <libias/io/dir.h>
#include <libias/map.h>
#include <libias/mem.h>
#include <libias/mempool.h>
#include <libias/mempool/dir.h>
#include <libias/mempool/file.h>
#include <libias/set.h>
#include <libias/str.h>
#include <libias/workqueue.h>

#include "ast.h"
#include "capsicum_helpers.h"
#include "io/dir.h"
#include "io/file.h"
#include "mainutils.h"
#include "parser.h"
#include "parser/edits.h"
#include "portscan/log.h"
#include "portscan/status.h"
#include "regexp.h"

enum ScanFlags {
	SCAN_NOTHING = 0,
	SCAN_CATEGORIES = 1 << 0,
	SCAN_CLONES = 1 << 1,
	SCAN_OPTION_DEFAULT_DESCRIPTIONS = 1 << 2,
	SCAN_OPTIONS = 1 << 3,
	SCAN_UNKNOWN_TARGETS = 1 << 4,
	SCAN_UNKNOWN_VARIABLES = 1 << 5,
	SCAN_VARIABLE_VALUES = 1 << 6,
	SCAN_PARTIAL = 1 << 7,
	SCAN_COMMENTS = 1 << 8,
	SCAN_STRICT_VARIABLES = 1 << 9,
};

enum ScanLongopts {
	SCAN_LONGOPT_CATEGORIES,
	SCAN_LONGOPT_CLONES,
	SCAN_LONGOPT_COMMENTS,
	SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS,
	SCAN_LONGOPT_OPTIONS,
	SCAN_LONGOPT_PROGRESS,
	SCAN_LONGOPT_STRICT,
	SCAN_LONGOPT_UNKNOWN_TARGETS,
	SCAN_LONGOPT_UNKNOWN_VARIABLES,
	SCAN_LONGOPT_VARIABLE_VALUES,
	SCAN_LONGOPT__N
};

struct ScanLongoptsState {
	int flag;
	const char *optarg;
};

struct CategoryReaderState {
	// Input
	const char *category;
	int portsdir;
	enum ScanFlags flags;

	// Output
	struct Mempool *pool;
	struct Array *error_origins;
	struct Array *error_msgs;
	struct Array *nonexistent;
	struct Array *origins;
	struct Array *unhooked;
	struct Array *unsorted;
};

struct PortReaderState {
	// Input
	int portsdir;
	const char *origin;
	struct Regexp *keyquery;
	struct Regexp *query;
	ssize_t editdist;
	enum ScanFlags flags;
	struct Map *default_option_descriptions;

	// Output
	struct Mempool *pool;
	const char *path;
	struct Set *comments;
	struct Set *errors;
	struct Set *unknown_variables;
	struct Set *unknown_targets;
	struct Set *clones;
	struct Set *option_default_descriptions;
	struct Set *option_groups;
	struct Set *options;
	struct Set *variable_values;
};

// Prototypes
static void add_error(struct Set *, char *);
static void lookup_subdirs(int, const char *, const char *, enum ScanFlags, struct Mempool *, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *);
static bool variable_value_filter(struct Parser *, const char *, void *);
static bool unknown_targets_filter(struct Parser *, const char *, void *);
static bool unknown_variables_filter(struct Parser *, const char *, void *);
static int char_cmp(const void *, const void *, void *);
static ssize_t edit_distance(const char *, const char *);
static void collect_output_unknowns(struct Mempool *, const char *, const char *, const char *, void *);
static void collect_output_variable_values(struct Mempool *, const char *, const char *, const char *, void *);
static void scan_port_worker(int, void *);
static void lookup_origins_worker(int, void *);
static struct Array *lookup_origins(struct Mempool *, struct Workqueue *, int, enum ScanFlags, struct PortscanLog *);
static enum ASTWalkState get_default_option_descriptions_walker(struct AST *, struct Map *, struct Mempool *);
static PARSER_EDIT(get_default_option_descriptions);
static void scan_ports(struct Workqueue *, int, struct Array *, enum ScanFlags, struct Regexp *, struct Regexp *, ssize_t, struct PortscanLog *);
static void usage(void);

// Constants
static const uint32_t DEFAULT_PROGRESSINTERVAL = 1;
static struct option longopts[SCAN_LONGOPT__N + 1] = {
	[SCAN_LONGOPT_CATEGORIES] = { "categories", no_argument, NULL, 1 },
	[SCAN_LONGOPT_CLONES] = { "clones", no_argument, NULL, 1 },
	[SCAN_LONGOPT_COMMENTS] = { "comments", no_argument, NULL, 1 },
	[SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS] = { "option-default-descriptions", optional_argument, NULL, 1 },
	[SCAN_LONGOPT_OPTIONS] = { "options", no_argument, NULL, 1 },
	[SCAN_LONGOPT_PROGRESS] = { "progress", optional_argument, NULL, 1 },
	[SCAN_LONGOPT_STRICT] = { "strict", no_argument, NULL, 1 },
	[SCAN_LONGOPT_UNKNOWN_TARGETS] = { "unknown-targets", no_argument, NULL, 1 },
	[SCAN_LONGOPT_UNKNOWN_VARIABLES] = { "unknown-variables", no_argument, NULL, 1 },
	[SCAN_LONGOPT_VARIABLE_VALUES] = { "variable-values", optional_argument, NULL, 1 },
};

void
add_error(struct Set *errors, char *msg)
{
	if (!set_contains(errors, msg)) {
		set_add(errors, str_dup(NULL, msg));
	}
}

void
lookup_subdirs(int portsdir, const char *category, const char *path, enum ScanFlags flags, struct Mempool *extpool, struct Array *subdirs, struct Array *nonexistent, struct Array *unhooked, struct Array *unsorted, struct Array *error_origins, struct Array *error_msgs)
{
	SCOPE_MEMPOOL(pool);

	FILE *in = fileopenat(pool, portsdir, path);
	if (in == NULL) {
		array_append(error_origins, str_dup(extpool, path));
		array_append(error_msgs, str_printf(extpool, "fileopenat: %s", strerror(errno)));
		return;
	}

	struct ParserSettings settings;
	parser_init_settings(&settings);
	if (flags & SCAN_CATEGORIES) {
		settings.behavior |= PARSER_OUTPUT_REFORMAT | PARSER_OUTPUT_DIFF;
	}

	struct Parser *parser = parser_new(pool, &settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		array_append(error_origins, str_dup(extpool, path));
		array_append(error_msgs, parser_error_tostring(parser, extpool));
		return;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		array_append(error_origins, str_dup(extpool, path));
		array_append(error_msgs, parser_error_tostring(parser, extpool));
		return;
	}

	struct Array *tmp;
	if (parser_lookup_variable(parser, "SUBDIR", PARSER_LOOKUP_DEFAULT, pool, &tmp, NULL) == NULL) {
		return;
	}

	if (unhooked && (flags & SCAN_CATEGORIES)) {
		DIR *dir = diropenat(pool, portsdir, category);
		if (dir == NULL) {
			array_append(error_origins, str_dup(extpool, category));
			array_append(error_msgs, str_printf(extpool, "diropenat: %s", strerror(errno)));
		} else {
			DIR_FOREACH(dir, dp) {
				if (dp->d_name[0] == '.') {
					continue;
				}
				char *path = str_printf(pool, "%s/%s", category, dp->d_name);
				struct stat sb;
				if (fstatat(portsdir, path, &sb, 0) == -1 ||
				    !S_ISDIR(sb.st_mode)) {
					continue;
				}
				if (array_find(tmp, dp->d_name, str_compare) == -1) {
					array_append(unhooked, str_dup(extpool, path));
				}
			}
		}
	}

	ARRAY_FOREACH(tmp, char *, port) {
		char *origin;
		if (flags != SCAN_NOTHING) {
			origin = str_printf(pool, "%s/%s", category, port);
		} else {
			origin = port;
		}
		if (flags & SCAN_CATEGORIES) {
			struct stat sb;
			if (nonexistent &&
			    (fstatat(portsdir, origin, &sb, 0) == -1 ||
			     !S_ISDIR(sb.st_mode))) {
				array_append(nonexistent, str_dup(extpool, origin));
			}
		}
		array_append(subdirs, str_dup(extpool, origin));
	}

	if ((flags & SCAN_CATEGORIES) && unsorted &&
	    parser_output_write_to_file(parser, NULL) == PARSER_ERROR_DIFFERENCES_FOUND) {
		array_append(unsorted, str_dup(extpool, category));
	}
}

bool
variable_value_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

bool
unknown_targets_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

bool
unknown_variables_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

int
char_cmp(const void *ap, const void *bp, void *userdata)
{
	char a = *(char *)ap;
	char b = *(char *)bp;
	if (a < b) {
		return -1;
	} else if (a > b) {
		return 1;
	} else {
		return 0;
	}
}

ssize_t
edit_distance(const char *a, const char *b)
{
	if (!a || !b) {
		return -1;
	}

	ssize_t editdist = -1;
	struct diff d;
	if (diff(&d, char_cmp, NULL, sizeof(char), a, strlen(a), b, strlen(b)) > 0) {
		editdist = d.editdist;
		free(d.ses);
		free(d.lcs);
	}

	return editdist;
}

void
collect_output_unknowns(struct Mempool *extpool, const char *key, const char *value, const char *hint, void *userdata)
{
	if (!set_contains(userdata, key)) {
		set_add(userdata, str_dup(NULL, key));
	}
}

void
collect_output_variable_values(struct Mempool *extpool, const char *key, const char *value, const char *hint, void *userdata)
{
	SCOPE_MEMPOOL(pool);

	char *buf = str_printf(pool, "%-30s\t%s", key, value);
	if (!set_contains(userdata, buf)) {
		set_add(userdata, str_dup(NULL, buf));
	}
}

void
scan_port_worker(int tid, void *userdata)
{
	SCOPE_MEMPOOL(pool);

	struct PortReaderState *this = userdata;
	this->pool = mempool_new();
	this->origin = str_dup(this->pool, this->origin);
	portscan_status_print(this->origin);
	this->path = str_printf(this->pool, "%s/Makefile", this->origin);

	this->comments = mempool_set(this->pool, str_compare);
	this->errors = mempool_set(this->pool, str_compare);
	this->option_default_descriptions = mempool_set(this->pool, str_compare);
	this->option_groups = mempool_set(this->pool, str_compare);
	this->options = mempool_set(this->pool, str_compare);
	this->unknown_variables = mempool_set(this->pool, str_compare);
	this->unknown_targets = mempool_set(this->pool, str_compare);
	this->variable_values = mempool_set(this->pool, str_compare);

	struct ParserSettings settings;
	parser_init_settings(&settings);
	settings.behavior = PARSER_OUTPUT_RAWLINES | PARSER_LOAD_LOCAL_INCLUDES;
	settings.filename = this->path;
	settings.portsdir = this->portsdir;

	if (!(this->flags & SCAN_STRICT_VARIABLES)) {
		settings.behavior |= PARSER_CHECK_VARIABLE_REFERENCES;
	}

	FILE *in = fileopenat(pool, this->portsdir, this->path);
	if (in == NULL) {
		add_error(this->errors, str_printf(pool, "fileopenat: %s", strerror(errno)));
		portscan_status_inc();
		return;
	}

	struct Parser *parser = parser_new(pool, &settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		add_error(this->errors, parser_error_tostring(parser, pool));
		portscan_status_inc();
		return;
	}

	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		add_error(this->errors, parser_error_tostring(parser, pool));
		portscan_status_inc();
		return;
	}

	if (this->flags & SCAN_PARTIAL) {
		error = parser_edit(parser, pool, lint_bsd_port, NULL);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, parser_error_tostring(parser, pool));
			portscan_status_inc();
			return;
		}
	}

	if (this->flags & SCAN_UNKNOWN_VARIABLES) {
		struct ParserEditOutput param = { unknown_variables_filter, this->query, NULL, NULL, collect_output_unknowns, this->unknown_variables, 0 };
		error = parser_edit(parser, pool, output_unknown_variables, &param);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, str_printf(pool, "output.unknown-variables: %s", parser_error_tostring(parser, pool)));
			portscan_status_inc();
			return;
		}
	}

	if (this->flags & SCAN_UNKNOWN_TARGETS) {
		struct ParserEditOutput param = { unknown_targets_filter, this->query, NULL, NULL, collect_output_unknowns, this->unknown_targets, 0 };
		error = parser_edit(parser, pool, output_unknown_targets, &param);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, str_printf(pool, "output.unknown-targets: %s", parser_error_tostring(parser, pool)));
			portscan_status_inc();
			return;
		}
	}

	if (this->flags & SCAN_CLONES) {
		// XXX: Limit by query?
		error = parser_edit(parser, this->pool, lint_clones, &this->clones);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, str_printf(pool, "lint.clones: %s", parser_error_tostring(parser, pool)));
			portscan_status_inc();
			return;
		}
	}

	if (this->flags & SCAN_OPTION_DEFAULT_DESCRIPTIONS) {
		struct Map *descs = parser_metadata(parser, PARSER_METADATA_OPTION_DESCRIPTIONS);
		MAP_FOREACH(descs, char *, var, char *, desc) {
			char *default_desc = map_get(this->default_option_descriptions, var);
			if (!default_desc) {
				continue;
			}
			if (!set_contains(this->option_default_descriptions, var)) {
				ssize_t editdist = edit_distance(default_desc, desc);
				if (strcasecmp(default_desc, desc) == 0 || (editdist > 0 && editdist <= this->editdist)) {
					set_add(this->option_default_descriptions, str_dup(this->pool, var));
				}
			}
		}
	}

	if (this->flags & SCAN_OPTIONS) {
		struct Set *groups = parser_metadata(parser, PARSER_METADATA_OPTION_GROUPS);
		SET_FOREACH(groups, char *, group) {
			if (!set_contains(this->option_groups, group) &&
			    (this->query == NULL || regexp_exec(this->query, group) == 0)) {
				set_add(this->option_groups, str_dup(this->pool, group));
			}
		}
		struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
		SET_FOREACH(options, char *, option) {
			if (!set_contains(this->options, option) &&
			    (this->query == NULL || regexp_exec(this->query, option) == 0)) {
				set_add(this->options, str_dup(this->pool, option));
			}
		}
	}

	if (this->flags & SCAN_VARIABLE_VALUES) {
		struct ParserEditOutput param = { variable_value_filter, this->keyquery, variable_value_filter, this->query, collect_output_variable_values, this->variable_values, 0 };
		error = parser_edit(parser, pool, output_variable_value, &param);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, str_printf(pool, "output.variable-value: %s", parser_error_tostring(parser, pool)));
			portscan_status_inc();
			return;
		}
	}

	if (this->flags & SCAN_COMMENTS) {
		struct Set *commented_portrevision = NULL;
		error = parser_edit(parser, pool, lint_commented_portrevision, &commented_portrevision);
		if (error != PARSER_ERROR_OK) {
			add_error(this->errors, str_printf(pool, "lint.commented-portrevision: %s", parser_error_tostring(parser, pool)));
			portscan_status_inc();
			return;
		}
		SET_FOREACH(commented_portrevision, char *, comment) {
			char *msg = str_printf(pool, "commented revision or epoch: %s", comment);
			if (!set_contains(this->comments, msg)) {
				set_add(this->comments, str_dup(this->pool, msg));
			}
		}
	}

	portscan_status_inc();
}

void
lookup_origins_worker(int tid, void *userdata)
{
	struct CategoryReaderState *this = userdata;
	struct Mempool *pool = mempool_new();
	this->pool = pool;
	this->error_origins = mempool_array(pool);
	this->error_msgs = mempool_array(pool);
	this->nonexistent = mempool_array(pool);
	this->unhooked = mempool_array(pool);
	this->unsorted = mempool_array(pool);
	this->origins = mempool_array(pool);

	portscan_status_print(this->category);
	char *path = str_printf(pool, "%s/Makefile", this->category);
	lookup_subdirs(this->portsdir, this->category, path, this->flags, this->pool, this->origins, this->nonexistent, this->unhooked, this->unsorted, this->error_origins, this->error_msgs);
	portscan_status_inc();
}

struct Array *
lookup_origins(struct Mempool *extpool, struct Workqueue *workqueue, int portsdir, enum ScanFlags flags, struct PortscanLog *log)
{
	SCOPE_MEMPOOL(pool);
	struct Array *retval = mempool_array(extpool);

	struct Array *categories = mempool_array(pool);
	struct Array *error_origins = mempool_array(pool);
	struct Array *error_msgs = mempool_array(pool);
	lookup_subdirs(portsdir, "", "Makefile", SCAN_NOTHING, pool, categories, NULL, NULL, NULL, error_origins, error_msgs);

	ARRAY_FOREACH(error_origins, char *, origin) {
		char *msg = array_get(error_msgs, origin_index);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_ERROR, origin, msg);
	}

	portscan_status_reset(PORTSCAN_STATUS_CATEGORIES, array_len(categories));
	struct Array *results = mempool_array(pool);
	ARRAY_FOREACH(categories, char *, category) {
		struct CategoryReaderState *this = mempool_alloc(pool, sizeof(struct CategoryReaderState));
		this->category = category;
		this->portsdir = portsdir;
		this->flags = flags;
		workqueue_push(workqueue, lookup_origins_worker, this);
		array_append(results, this);
	}
	workqueue_wait(workqueue);
	ARRAY_FOREACH(results, struct CategoryReaderState *, result) {
		ARRAY_FOREACH(result->error_origins, char *, origin) {
			char *msg = array_get(result->error_msgs, origin_index);
			portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_ERROR, origin, msg);
		}
		ARRAY_FOREACH(result->nonexistent, char *, origin) {
			portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_NONEXISTENT_PORT, origin, "entry without existing directory");
		}
		ARRAY_FOREACH(result->unhooked, char *, origin) {
			portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNHOOKED_PORT, origin, "unhooked port");
		}
		ARRAY_FOREACH(result->unsorted, char *, origin) {
			portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNSORTED, origin, "unsorted category or other formatting issues");
		}
		ARRAY_FOREACH(result->origins, char *, origin) {
			array_append(retval, mempool_move(result->pool, origin, extpool));
		}
		mempool_free(result->pool);
	}

	// Get consistent output even when category Makefiles are
	// not sorted correctly
	array_sort(retval, str_compare);

	return retval;
}

enum ASTWalkState
get_default_option_descriptions_walker(struct AST *node, struct Map *this, struct Mempool *pool)
{
	switch (node->type) {
	case AST_VARIABLE:
		if (str_endswith(node->variable.name, "_DESC") &&
		    !map_contains(this, node->variable.name)) {
			map_add(this, str_dup(pool, node->variable.name), str_join(pool, node->variable.words, " "));
		}
		break;
	default:
		break;
	}

	AST_WALK_DEFAULT(get_default_option_descriptions_walker, node, this, pool);
	return AST_WALK_CONTINUE;
}

PARSER_EDIT(get_default_option_descriptions)
{
	struct Map *default_option_descriptions = mempool_map(extpool, str_compare);
	get_default_option_descriptions_walker(root, default_option_descriptions, extpool);
	struct Map **retval = (struct Map **)userdata;
	*retval = default_option_descriptions;
}

void
scan_ports(struct Workqueue *workqueue, int portsdir, struct Array *origins, enum ScanFlags flags, struct Regexp *keyquery, struct Regexp *query, ssize_t editdist, struct PortscanLog *retval)
{
	SCOPE_MEMPOOL(pool);

	if (!(flags & (SCAN_CLONES |
		       SCAN_COMMENTS |
		       SCAN_OPTION_DEFAULT_DESCRIPTIONS |
		       SCAN_OPTIONS |
		       SCAN_UNKNOWN_TARGETS |
		       SCAN_UNKNOWN_VARIABLES |
		       SCAN_VARIABLE_VALUES))) {
		return;
	}

	FILE *in = fileopenat(pool, portsdir, "Mk/bsd.options.desc.mk");
	if (in == NULL) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk",
			str_printf(pool, "fileopenat: %s", strerror(errno)));
		return;
	}

	struct ParserSettings settings;
	parser_init_settings(&settings);
	struct Parser *parser = parser_new(pool, &settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser, pool));
		return;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser, pool));
		return;
	}

	struct Map *default_option_descriptions = NULL;
	if (parser_edit(parser, pool, get_default_option_descriptions, &default_option_descriptions) != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser, pool));
		return;
	}
	panic_unless(default_option_descriptions, "no default option descriptions found");

	struct Array *results = mempool_array(pool);
	ARRAY_FOREACH(origins, const char *, origin) {
		struct PortReaderState *this = mempool_alloc(pool, sizeof(struct PortReaderState));
		this->portsdir = portsdir;
		this->origin = origin;
		this->keyquery = keyquery;
		this->query = query;
		this->editdist = editdist;
		this->flags = flags;
		this->default_option_descriptions = default_option_descriptions;
		workqueue_push(workqueue, scan_port_worker, this);
		array_append(results, this);
	}
	workqueue_wait(workqueue);
	ARRAY_FOREACH(results, struct PortReaderState *, this) {
		portscan_status_print(NULL);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_ERROR, this->origin, this->errors);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_VAR, this->origin, this->unknown_variables);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET, this->origin, this->unknown_targets);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_DUPLICATE_VAR, this->origin, this->clones);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION_DEFAULT_DESCRIPTION, this->origin, this->option_default_descriptions);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION_GROUP, this->origin, this->option_groups);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION, this->origin, this->options);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_VARIABLE_VALUE, this->origin, this->variable_values);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_COMMENT, this->origin, this->comments);
		mempool_free(this->pool);
	}
}

void
usage()
{
	fprintf(stderr, "usage: portscan [-l <logdir>] [-p <portsdir>] [-q <regexp>] [--<check> ...] [<origin1> ...]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	SCOPE_MEMPOOL(pool);

	const char *portsdir_path = getenv("PORTSDIR");
	const char *logdir_path = NULL;
	const char *keyquery = NULL;
	const char *query = NULL;
	uint32_t progressinterval = 0;

	struct ScanLongoptsState opts[SCAN_LONGOPT__N] = {};
	for (enum ScanLongopts i = 0; i < SCAN_LONGOPT__N; i++) {
		longopts[i].flag = &opts[i].flag;
	}

	enum ScanFlags flags = SCAN_NOTHING;
	int ch;
	int longindex;
	while ((ch = getopt_long(argc, argv, "l:q:o:p:", longopts, &longindex)) != -1) {
		switch (ch) {
		case 'l':
			logdir_path = optarg;
			break;
		case 'q':
			query = optarg;
			break;
		case 'o': {
			bool found = false;
			const char *name = NULL;
			for (enum ScanLongopts i = 0; !found && i < SCAN_LONGOPT__N; i++) {
				name = longopts[i].name;
				if (strcasecmp(optarg, name) == 0) {
					opts[i].flag = 1;
					opts[i].optarg = NULL;
					found = true;
				} else if (longopts[i].has_arg != no_argument) {
					char *buf = str_printf(pool, "%s=", name);
					if (strncasecmp(optarg, buf, strlen(buf)) == 0) {
						opts[i].flag = 1;
						opts[i].optarg = optarg + strlen(buf);
						found = true;
					}
				}
			}
			if (found) {
				warnx("`-o %s' is deprecated; use `--%s' instead", optarg, optarg);
			} else {
				warnx("unrecognized option `--%s'", optarg);
				usage();
			}
			break;
		} case 'p':
			portsdir_path = optarg;
			break;
		case 0:
			opts[longindex].flag = 1;
			opts[longindex].optarg = optarg;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	bool strict_variables = false;
	for (enum ScanLongopts i = 0; i < SCAN_LONGOPT__N; i++) {
		if (!opts[i].flag) {
			continue;
		}
		switch (i) {
		case SCAN_LONGOPT_CATEGORIES:
			flags |= SCAN_CATEGORIES;
			break;
		case SCAN_LONGOPT_CLONES:
			flags |= SCAN_CLONES;
			break;
		case SCAN_LONGOPT_COMMENTS:
			flags |= SCAN_COMMENTS;
			break;
		case SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS:
			flags |= SCAN_OPTION_DEFAULT_DESCRIPTIONS;
			break;
		case SCAN_LONGOPT_OPTIONS:
			flags |= SCAN_OPTIONS;
			break;
		case SCAN_LONGOPT_PROGRESS:
			progressinterval = DEFAULT_PROGRESSINTERVAL;
			break;
		case SCAN_LONGOPT_STRICT:
			strict_variables = true;
			break;
		case SCAN_LONGOPT_UNKNOWN_TARGETS:
			flags |= SCAN_UNKNOWN_TARGETS;
			break;
		case SCAN_LONGOPT_UNKNOWN_VARIABLES:
			flags |= SCAN_UNKNOWN_VARIABLES;
			break;
		case SCAN_LONGOPT_VARIABLE_VALUES:
			flags |= SCAN_VARIABLE_VALUES;
			keyquery = opts[i].optarg;
			break;
		case SCAN_LONGOPT__N:
			break;
		}
	}

	if (flags == SCAN_NOTHING) {
		flags = SCAN_CATEGORIES | SCAN_CLONES | SCAN_COMMENTS |
			SCAN_OPTION_DEFAULT_DESCRIPTIONS | SCAN_UNKNOWN_TARGETS |
			SCAN_UNKNOWN_VARIABLES;
	}
	if (strict_variables) {
		flags |= SCAN_STRICT_VARIABLES;
	}

	if (portsdir_path == NULL) {
		portsdir_path = "/usr/ports";
	}

	if (isatty(STDERR_FILENO)) {
		progressinterval = DEFAULT_PROGRESSINTERVAL;
	}

#if HAVE_CAPSICUM
	if (caph_limit_stdio() < 0) {
		err(1, "caph_limit_stdio");
	}

	closefrom(STDERR_FILENO + 1);
	close(STDIN_FILENO);
#endif

	int portsdir = open(portsdir_path, O_DIRECTORY);
	if (portsdir == -1) {
		err(1, "open: %s", portsdir_path);
	}

	struct PortscanLogDir *logdir = NULL;
	FILE *out = stdout;
	if (logdir_path != NULL) {
		logdir = portscan_log_dir_open(pool, logdir_path, portsdir);
		if (logdir == NULL) {
			err(1, "portscan_log_dir_open: %s", logdir_path);
		}
		fclose(out);
		out = NULL;
	}

#if HAVE_CAPSICUM
	if (caph_limit_stream(portsdir, CAPH_LOOKUP | CAPH_READ | CAPH_READDIR) < 0) {
		err(1, "caph_limit_stream");
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif

	if (opts[SCAN_LONGOPT_PROGRESS].optarg) {
		const char *error;
		progressinterval = strtonum(opts[SCAN_LONGOPT_PROGRESS].optarg, 0, 100000000, &error);
		if (error) {
			errx(1, "--progress=%s is %s (must be >=1)", opts[SCAN_LONGOPT_PROGRESS].optarg, error);
		}
	}
	portscan_status_init(progressinterval);

	struct Regexp *keyquery_regexp = NULL;
	if (keyquery) {
		keyquery_regexp = regexp_new_from_str(pool, keyquery, REG_EXTENDED);
		if (keyquery_regexp == NULL) {
			errx(1, "invalid regexp");
		}
	}
	struct Regexp *query_regexp = NULL;
	if (query) {
		query_regexp = regexp_new_from_str(pool, query, REG_EXTENDED);
		if (query_regexp == NULL) {
			errx(1, "invalid regexp");
		}
	}

	ssize_t editdist = 3;
	if (opts[SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS].optarg) {
		const char *error;
		editdist = strtonum(opts[SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS].optarg, 0, INT_MAX, &error);
		if (error) {
			errx(1, "--option-default-descriptions=%s is %s (must be >=0)", opts[SCAN_LONGOPT_OPTION_DEFAULT_DESCRIPTIONS].optarg, error);
		}
	}

	struct Workqueue *workqueue = mempool_workqueue(pool, 0);
	struct PortscanLog *result = portscan_log_new(pool);
	struct Array *origins = NULL;
	if (argc == 0) {
		origins = lookup_origins(pool, workqueue, portsdir, flags, result);
	} else {
		flags |= SCAN_PARTIAL;
		origins = mempool_array(pool);
		for (int i = 0; i < argc; i++) {
			array_append(origins, str_dup(pool, argv[i]));
		}
	}

	portscan_status_reset(PORTSCAN_STATUS_PORTS, array_len(origins));
	scan_ports(workqueue, portsdir, origins, flags, keyquery_regexp, query_regexp, editdist, result);
	if (portscan_log_len(result) > 0) {
		if (logdir != NULL) {
			struct PortscanLog *prev_result = portscan_log_read_all(pool, logdir, PORTSCAN_LOG_LATEST);
			if (portscan_log_compare(prev_result, result)) {
				if (progressinterval) {
					portscan_status_reset(PORTSCAN_STATUS_FINISHED, 0);
					portscan_status_print(NULL);
				}
				warnx("no changes compared to previous result");
				return 2;
			}
			if (progressinterval) {
				portscan_status_reset(PORTSCAN_STATUS_FINISHED, 0);
				portscan_status_print(NULL);
			}
			if (!portscan_log_serialize_to_dir(result, logdir)) {
				err(1, "portscan_log_serialize_to_dir");
			}
		} else {
			if (progressinterval) {
				portscan_status_reset(PORTSCAN_STATUS_FINISHED, 0);
				portscan_status_print(NULL);
			}
			if (!portscan_log_serialize_to_file(result, out)) {
				err(1, "portscan_log_serialize");
			}
		}
	} else if (progressinterval) {
		portscan_status_reset(PORTSCAN_STATUS_FINISHED, 0);
		portscan_status_print(NULL);
	}

	return 0;
}
