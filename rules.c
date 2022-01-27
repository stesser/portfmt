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
#include <math.h>
#include <stdbool.h>
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
#include "constants.h"
#include "rules.h"
#include "parser.h"
#include "parser/edits.h"

// Prototypes
static bool variable_has_flag(struct Parser *, const char *, int);
static bool extract_arch_prefix(struct Mempool *, const char *, char **, char **);
static void is_referenced_var_cb(struct Mempool *, const char *, const char *, const char *, void *);
static void add_referenced_var_candidates(struct Mempool *, struct Array *, struct Array *, const char *, const char *);
static bool is_valid_license(struct Parser *, const char *);
static bool matches_license_name(struct Parser *, const char *);
static bool case_sensitive_sort(struct Parser *, const char *);
static int compare_rel(const char *[], size_t, const char *, const char *);
static bool compare_license_perms(struct Parser *, const char *, const char *, const char *, int *);
static char *remove_plist_keyword(const char *, struct Mempool *);
static bool compare_plist_files(struct Parser *, const char *, const char *, const char *, int *);
static bool compare_use_gnome(const char *, const char *, const char *, int *);
static bool compare_use_kde(const char *, const char *, const char *, int *);
static bool compare_use_pyqt(const char *, const char *, const char *, int *);
static bool compare_use_qt(const char *, const char *, const char *, int *);
static bool is_flavors_helper(struct Mempool *, struct Parser *, const char *, char **, char **);
static char *extract_subpkg(struct Mempool *, struct Parser *, const char *, char **);
static bool matches_options_group(struct Mempool *, struct Parser *, const char *, char **);
static bool is_cabal_datadir_vars_helper(struct Mempool *, const char *, const char *, char **, char **);
static bool is_cabal_datadir_vars(struct Mempool *, struct Parser *, const char *, char **, char **);
static bool is_shebang_lang_helper(struct Mempool *, const char *, const char *, char **, char **);
static bool is_shebang_lang(struct Mempool *, struct Parser *, const char *, char **, char **);
static void target_extract_opt(struct Mempool *, struct Parser *, const char *, char **, char **, bool *);

// Constants
static const char *license_perms_rel[] = {
	"dist-mirror",
	"no-dist-mirror",
	"dist-sell",
	"no-dist-sell",
	"pkg-mirror",
	"no-pkg-mirror",
	"pkg-sell",
	"no-pkg-sell",
	"auto-accept",
	"no-auto-accept",
	"none",
};

static const char *target_command_wrap_after_each_token_[] = {
	"${INSTALL_DATA}",
	"${INSTALL_LIB}",
	"${INSTALL_MAN}",
	"${INSTALL_PROGRAM}",
	"${INSTALL_SCRIPT}",
	"${INSTALL}",
	"${MKDIR}",
	"${MV}",
	"${REINPLACE_CMD}",
	"${RMDIR}",
	"${SED}",
	"${STRIP_CMD}",
};

static struct {
	const char *name;
	int opthelper;
} target_order_[] = {
	{ "all", 0 },
	{ "post-chroot", 0 },
	{ "pre-everything", 0 },
	{ "fetch", 0 },
	{ "fetch-list", 0 },
	{ "fetch-recursive-list", 0 },
	{ "fetch-recursive", 0 },
	{ "fetch-required-list", 0 },
	{ "fetch-required", 0 },
	{ "fetch-specials", 0 },
	{ "fetch-url-list-int", 0 },
	{ "fetch-url-list", 0 },
	{ "fetch-urlall-list", 0 },
	{ "pre-fetch", 1 },
	{ "pre-fetch-script", 0 },
	{ "do-fetch", 1 },
	{ "post-fetch", 1 },
	{ "post-fetch-script", 0 },
	{ "checksum", 0 },
	{ "checksum-recursive", 0 },
	{ "extract", 0 },
	{ "pre-extract", 1 },
	{ "pre-extract-script", 0 },
	{ "do-extract", 1 },
	{ "post-extract", 1 },
	{ "post-extract-script", 0 },
	{ "patch", 0 },
	{ "pre-patch", 1 },
	{ "pre-patch-script", 0 },
	{ "do-patch", 1 },
	{ "post-patch", 1 },
	{ "post-patch-script", 0 },
	{ "configure", 0 },
	{ "pre-configure", 1 },
	{ "pre-configure-script", 0 },
	{ "do-configure", 1 },
	{ "post-configure", 1 },
	{ "post-configure-script", 0 },
	{ "build", 0 },
	{ "pre-build", 1 },
	{ "pre-build-script", 0 },
	{ "do-build", 1 },
	{ "post-build", 1 },
	{ "post-build-script", 0 },
	{ "install", 0 },
	{ "install-desktop-entries", 0 },
	{ "install-ldconfig-file", 0 },
	{ "install-mtree", 0 },
	{ "install-package", 0 },
	{ "install-rc-script", 0 },
	{ "pre-install", 1 },
	{ "pre-install-script", 0 },
	{ "pre-su-install", 0 },
	{ "do-install", 1 },
	{ "post-install", 1 },
	{ "post-install-script", 0 },
	{ "stage", 0 },
	{ "post-stage", 1 },
	{ "test", 0 },
	{ "pre-test", 1 },
	{ "do-test", 1 },
	{ "post-test", 1 },
	{ "package-name", 0 },
	{ "package-noinstall", 0 },
	{ "pre-package", 1 },
	{ "pre-package-script", 0 },
	{ "do-package", 1 },
	{ "post-package", 1 },
	{ "post-package-script", 0 },
	{ "pre-pkg-script", 0 },
	{ "pkg", 0 },
	{ "post-pkg-script", 0 },
	{ "clean", 0 },
	{ "pre-clean", 0 },
	{ "do-clean", 0 },
	{ "post-clean", 0 },

	{ "add-plist-data", 0 },
	{ "add-plist-docs", 0 },
	{ "add-plist-examples", 0 },
	{ "add-plist-info", 0 },
	{ "add-plist-post", 0 },
	{ "apply-slist", 0 },
	{ "check-already-installed", 0 },
	{ "check-build-conflicts", 0 },
	{ "check-config", 0 },
	{ "check-conflicts", 0 },
	{ "check-deprecated", 0 },
	{ "check-install-conflicts", 0 },
	{ "check-man", 0 },
	{ "check-orphans", 0 },
	{ "check-plist", 0 },
	{ "check-sanity", 0 },
	{ "check-umask", 0 },
	{ "checkpatch", 0 },
	{ "clean-depends", 0 },
	{ "compress-man", 0 },
	{ "config-conditional", 0 },
	{ "config-recursive", 0 },
	{ "config", 0 },
	{ "create-binary-alias", 0 },
	{ "create-binary-wrappers", 0 },
	{ "create-users-groups", 0 },
	{ "deinstall-all", 0 },
	{ "deinstall-depends", 0 },
	{ "deinstall", 0 },
	{ "delete-distfiles-list", 0 },
	{ "delete-distfiles", 0 },
	{ "delete-package-list", 0 },
	{ "delete-package", 0 },
	{ "depends", 0 },
	{ "describe", 0 },
	{ "distclean", 0 },
	{ "fake-pkg", 0 },
	{ "fix-shebang", 0 },
	{ "fixup-lib-pkgconfig", 0 },
	{ "generate-plist", 0 },
	{ "identify-install-conflicts", 0 },
	{ "limited-clean-depends", 0 },
	{ "maintainer", 0 },
	{ "makepatch", 0 },
	{ "makeplist", 0 },
	{ "makesum", 0 },
	{ "post-check-sanity-script", 0 },
	{ "pre-check-config", 0 },
	{ "pre-check-sanity-script", 0 },
	{ "pre-config", 0 },
	{ "pretty-print-build-depends-list", 0 },
	{ "pretty-print-config", 0 },
	{ "pretty-print-run-depends-list", 0 },
	{ "pretty-print-www-site", 0 },
	{ "readme", 0 },
	{ "readmes", 0 },
	{ "reinstall", 0 },
	{ "repackage", 0 },
	{ "restage", 0 },
	{ "rmconfig-recursive", 0 },
	{ "rmconfig", 0 },
	{ "run-autotools-fixup", 0 },
	{ "sanity-config", 0 },
	{ "security-check", 0 },
	{ "showconfig-recursive", 0 },
	{ "showconfig", 0 },
	{ "stage-dir", 0 },
	{ "stage-qa", 0 },
};

static const char *special_sources_[] = {
	".EXEC",
	".IGNORE",
	".MADE",
	".MAKE",
	".META",
	".NOMETA",
	".NOMETA_CMP",
	".NOPATH",
	".NOTMAIN",
	".OPTIONAL",
	".PHONY",
	".PRECIOUS",
	".SILENT",
	".USE",
	".USEBEFORE",
	".WAIT",
};

static const char *special_targets_[] = {
	".BEGIN",
	".DEFAULT",
	".DELETE_ON_ERROR",
	".END",
	".ERROR",
	".EXEC",
	".IGNORE",
	".INTERRUPT",
	".MADE",
	".MAIN",
	".MAKE",
	".MAKEFLAGS",
	".META",
	".NO_PARALLEL",
	".NOMAIN",
	".NOMETA_CMP",
	".NOMETA",
	".NOPATH",
	".NOTPARALLEL",
	".OBJDIR",
	".OPTIONAL",
	".ORDER",
	".PATH",
	".PHONY",
	".PRECIOUS",
	".RECURSIVE",
	".SHELL",
	".SILENT",
	".STALE",
	".SUFFIXES",
	".USE",
	".USEBEFORE",
	".WAIT",
};

enum VariableOrderFlag {
	VAR_DEFAULT = 0,
	VAR_CASE_SENSITIVE_SORT = 1 << 0,
	// Lines that are best not wrapped to 80 columns
	VAR_IGNORE_WRAPCOL = 1 << 1,
	VAR_LEAVE_UNFORMATTED = 1 << 2,
	VAR_NOT_COMPARABLE = 1 << 3,
	VAR_PRINT_AS_NEWLINES = 1 << 4,
	// Do not indent with the rest of the variables in a paragraph
	VAR_SKIP_GOALCOL = 1 << 5,
	VAR_SORTED = 1 << 6,
	VAR_SUBPKG_HELPER = 1 << 7,
	VAR_DEDUP = 1 << 8,
};

struct VariableOrderEntry {
	enum BlockType block;
	const char *var;
	enum VariableOrderFlag flags;
	const char *uses[2];
};

// Based on: https://www.freebsd.org/doc/en/books/porters-handbook/porting-order.html
static struct VariableOrderEntry variable_order_[] = {
	{ BLOCK_PORTNAME, "PORTNAME", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "PORTVERSION", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "DISTVERSIONPREFIX", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_PORTNAME, "DISTVERSION", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "DISTVERSIONSUFFIX", VAR_SKIP_GOALCOL, {} },
	/* XXX: hack to fix inserting PORTREVISION in aspell ports */
	{ BLOCK_PORTNAME, "SPELLVERSION", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "PORTREVISION", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "PORTEPOCH", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "CATEGORIES", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "MASTER_SITES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_PORTNAME, "MASTER_SITE_SUBDIR", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, {} },
	{ BLOCK_PORTNAME, "PKGNAMEPREFIX", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "PKGNAMESUFFIX", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "DISTNAME", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "DISTNAME_aarch64", VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "DISTNAME_amd64", VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "DISTNAME_i386", VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "EXTRACT_SUFX", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "DISTFILES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_PORTNAME, "DISTFILES_aarch64", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "DISTFILES_amd64", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "DISTFILES_i386", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_PORTNAME, "DIST_SUBDIR", VAR_DEFAULT, {} },
	{ BLOCK_PORTNAME, "EXTRACT_ONLY", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_PORTNAME, "EXTRACT_ONLY_7z", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, {} },

	{ BLOCK_PATCHFILES, "PATCH_SITES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_PATCHFILES, "PATCH_SITE_SUBDIR", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, {} },
	{ BLOCK_PATCHFILES, "PATCHFILES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_PATCHFILES, "PATCH_DIST_STRIP", VAR_SKIP_GOALCOL, {} },

	{ BLOCK_MAINTAINER, "MAINTAINER", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_MAINTAINER, "COMMENT", VAR_IGNORE_WRAPCOL | VAR_SUBPKG_HELPER, {} },

	{ BLOCK_LICENSE, "LICENSE", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_LICENSE, "LICENSE_COMB", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_LICENSE, "LICENSE_GROUPS", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_LICENSE, "LICENSE_NAME", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_LICENSE, "LICENSE_TEXT", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_LICENSE, "LICENSE_FILE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_LICENSE, "LICENSE_PERMS", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_LICENSE, "LICENSE_DISTFILES", VAR_SKIP_GOALCOL, {} },

	{ BLOCK_LICENSE_OLD, "RESTRICTED", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_LICENSE_OLD, "RESTRICTED_FILES", VAR_DEFAULT, {} },
	{ BLOCK_LICENSE_OLD, "NO_CDROM", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_LICENSE_OLD, "NO_PACKAGE", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_LICENSE_OLD, "LEGAL_PACKAGE", VAR_DEFAULT, {} },
	{ BLOCK_LICENSE_OLD, "LEGAL_TEXT", VAR_IGNORE_WRAPCOL, {} },

	{ BLOCK_BROKEN, "DEPRECATED", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_BROKEN, "EXPIRATION_DATE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_BROKEN, "FORBIDDEN", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_BROKEN, "MANUAL_PACKAGE_BUILD", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },

	{ BLOCK_BROKEN, "BROKEN", VAR_IGNORE_WRAPCOL, {} },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "BROKEN_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),
	{ BLOCK_BROKEN, "BROKEN_DragonFly", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },
	VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(BLOCK_BROKEN, "BROKEN_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),
	{ BLOCK_BROKEN, "IGNORE", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "IGNORE_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),
	{ BLOCK_BROKEN, "IGNORE_DragonFly", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },
	VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(BLOCK_BROKEN, "IGNORE_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {} },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, {}),

	{ BLOCK_DEPENDS, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "FETCH_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "EXTRACT_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "PATCH_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "CRAN_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_DEPENDS, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "BUILD_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "LIB_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "RUN_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_DEPENDS, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "TEST_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, {}),
#if PORTFMT_SUBPACKAGES
	{ BLOCK_DEPENDS, "SELF_DEPENDS", VAR_SUBPKG_HELPER | VAR_SORTED, {} },
#endif

	{ BLOCK_FLAVORS, "FLAVORS", VAR_DEFAULT, {} },
	{ BLOCK_FLAVORS, "FLAVOR", VAR_DEFAULT, {} },
	{ BLOCK_FLAVORS, "FLAVORS_SUB", VAR_DEFAULT, {} },

#if PORTFMT_SUBPACKAGES
	{ BLOCK_SUBPACKAGES, "SUBPACKAGES", VAR_SORTED, {} },
#endif

	{ BLOCK_FLAVORS_HELPER, "PKGNAMEPREFIX", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "PKGNAMESUFFIX", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "PKG_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS_BUILD", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS_INSTALL", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "DESCR", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_FLAVORS_HELPER, "PLIST", VAR_NOT_COMPARABLE, {} },

	{ BLOCK_USES, "USES", VAR_SORTED, {} },
	{ BLOCK_USES, "BROKEN_SSL", VAR_IGNORE_WRAPCOL | VAR_SORTED, { "ssl" } },
	{ BLOCK_USES, "BROKEN_SSL_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, { "ssl" } },
	VAR_FOR_EACH_SSL(BLOCK_USES, "BROKEN_SSL_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, { "ssl" }),
	{ BLOCK_USES, "IGNORE_SSL", VAR_IGNORE_WRAPCOL | VAR_SORTED, { "ssl" } },
	{ BLOCK_USES, "IGNORE_SSL_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, { "ssl" } },
	VAR_FOR_EACH_SSL(BLOCK_USES, "IGNORE_SSL_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL, { "ssl" }),
	{ BLOCK_USES, "IGNORE_WITH_MYSQL", VAR_SKIP_GOALCOL | VAR_SORTED, { "mysql" } },
	{ BLOCK_USES, "ANSIBLE_CMD", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_DOC_CMD", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_RUN_DEPENDS", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_DATADIR", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_ETCDIR", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_PLUGINS_PREFIX", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_MODULESDIR", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_PLUGINSDIR", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "ANSIBLE_PLUGIN_TYPE", VAR_SKIP_GOALCOL, { "ansible" } },
	{ BLOCK_USES, "INVALID_BDB_VER", VAR_SKIP_GOALCOL, { "bdb" } },
	{ BLOCK_USES, "OBSOLETE_BDB_VAR", VAR_SKIP_GOALCOL | VAR_SORTED, { "bdb" } },
	{ BLOCK_USES, "WITH_BDB_HIGHEST", VAR_SKIP_GOALCOL, { "bdb" } },
	{ BLOCK_USES, "WITH_BDB6_PERMITTED", VAR_SKIP_GOALCOL, { "bdb" } },
	{ BLOCK_USES, "CHARSETFIX_MAKEFILEIN", VAR_SKIP_GOALCOL, { "charsetfix" } },
	{ BLOCK_USES, "CPE_PART", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_VENDOR", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_PRODUCT", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_VERSION", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_UPDATE", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_EDITION", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_LANG", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_SW_EDITION", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_TARGET_SW", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_TARGET_HW", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "CPE_OTHER", VAR_DEFAULT, { "cpe" } },
	{ BLOCK_USES, "DOS2UNIX_REGEX", VAR_SORTED, { "dos2unix" } },
	{ BLOCK_USES, "DOS2UNIX_FILES", VAR_SORTED, { "dos2unix" } },
	{ BLOCK_USES, "DOS2UNIX_GLOB", VAR_SORTED, { "dos2unix" } },
	{ BLOCK_USES, "DOS2UNIX_WRKSRC", VAR_DEFAULT, { "dos2unix" } },
	{ BLOCK_USES, "FONTNAME", VAR_DEFAULT, { "fonts", "xorg-cat" /* :fonts */ } },
	{ BLOCK_USES, "FONTSDIR", VAR_DEFAULT, { "fonts", "xorg-cat" /* :fonts */ } },
	{ BLOCK_USES, "FONTPATHD", VAR_DEFAULT, { "fonts", "xorg-cat", /* :fonts */ } },
	{ BLOCK_USES, "FONTPATHSPEC", VAR_DEFAULT, { "fonts", "xorg-cat", /* :fonts */ } },
	{ BLOCK_USES, "KMODDIR", VAR_DEFAULT, { "kmod" } },
	{ BLOCK_USES, "KERN_DEBUGDIR", VAR_DEFAULT, { "kmod" } },
	{ BLOCK_USES, "NCURSES_IMPL", VAR_DEFAULT, { "ncurses" } },
	{ BLOCK_USES, "NOFONT", VAR_DEFAULT, { "xorg-cat" } },
	{ BLOCK_USES, "PATHFIX_CMAKELISTSTXT", VAR_SKIP_GOALCOL | VAR_SORTED, { "pathfix" } },
	{ BLOCK_USES, "PATHFIX_MAKEFILEIN", VAR_SKIP_GOALCOL | VAR_SORTED, { "pathfix" } },
	{ BLOCK_USES, "PATHFIX_WRKSRC", VAR_DEFAULT, { "pathfix" } },
	{ BLOCK_USES, "QMAIL_PREFIX", VAR_DEFAULT, { "qmail" } },
	{ BLOCK_USES, "QMAIL_SLAVEPORT", VAR_DEFAULT, { "qmail" } },
	{ BLOCK_USES, "TCL_PKG", VAR_DEFAULT, { "tcl", "tk" } },
	{ BLOCK_USES, "WANT_PGSQL", VAR_SORTED, { "pgsql" } },
	{ BLOCK_USES, "USE_ANT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_ASDF", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_ASDF_FASL", VAR_DEFAULT, {} },
	{ BLOCK_USES, "FASL_BUILD", VAR_DEFAULT, {} },
	{ BLOCK_USES, "ASDF_MODULES", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_BINUTILS", VAR_SORTED, {} },
	{ BLOCK_USES, "DISABLE_BINUTILS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_CLISP", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_CSTD", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_CXXSTD", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_FPC", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_GCC", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_GECKO", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_GENERIC_PKGMESSAGE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_GITHUB", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GH_ACCOUNT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GH_PROJECT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GH_SUBDIR", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GH_TAGNAME", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GH_TUPLE", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_USES, "USE_GITLAB", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_SITE", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_ACCOUNT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_PROJECT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_COMMIT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_SUBDIR", VAR_DEFAULT, {} },
	{ BLOCK_USES, "GL_TUPLE", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_USES, "USE_GL", VAR_SORTED, { "gl" } },
	{ BLOCK_USES, "USE_GNOME", VAR_SORTED, { "gnome" } },
	{ BLOCK_USES, "USE_GNOME_SUBR", VAR_DEFAULT, { "gnome" } },
	{ BLOCK_USES, "GCONF_CONFIG_OPTIONS", VAR_SKIP_GOALCOL, { "gnome" } },
	{ BLOCK_USES, "GCONF_CONFIG_DIRECTORY", VAR_SKIP_GOALCOL, { "gnome" } },
	{ BLOCK_USES, "GCONF_CONFIG_SOURCE", VAR_SKIP_GOALCOL, { "gnome" } },
	{ BLOCK_USES, "GCONF_SCHEMAS", VAR_SORTED, { "gnome" } },
	{ BLOCK_USES, "GLIB_SCHEMAS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "gnome" } },
	{ BLOCK_USES, "GNOME_HTML_DIR", VAR_DEFAULT, { "gnome" } },
	{ BLOCK_USES, "GNOME_LOCALSTATEDIR", VAR_SKIP_GOALCOL, { "gnome" } },
	{ BLOCK_USES, "GNOME_MAKEFILEIN", VAR_SKIP_GOALCOL, { "gnome" } },
	{ BLOCK_USES, "INSTALLS_OMF", VAR_DEFAULT, { "gnome" } },
	{ BLOCK_USES, "USE_GNUSTEP", VAR_SORTED, { "gnustep" } },
	{ BLOCK_USES, "GNUSTEP_PREFIX", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "DEFAULT_LIBVERSION", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_CFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_CPPFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_CXXFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_OBJCCFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_OBJCFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_LDFLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_FLAGS", VAR_DEFAULT, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_INCLUDE_DIRS", VAR_SORTED, { "gnustep" } },
	{ BLOCK_USES, "ADDITIONAL_LIB_DIRS", VAR_SORTED, { "gnustep" } },
	{ BLOCK_USES, "USE_GSTREAMER", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_GSTREAMER1", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_HORDE_BUILD", VAR_SKIP_GOALCOL, { "horde" } },
	{ BLOCK_USES, "USE_HORDE_RUN", VAR_DEFAULT, { "horde" } },
	{ BLOCK_USES, "HORDE_DIR", VAR_DEFAULT, { "horde" } },
	{ BLOCK_USES, "USE_JAVA", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_VERSION", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_OS", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_VENDOR", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_EXTRACT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_BUILD", VAR_DEFAULT, {} },
	{ BLOCK_USES, "JAVA_RUN", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_KDE", VAR_SORTED, { "kde" } },
	{ BLOCK_USES, "KDE_INVENT", VAR_DEFAULT, { "kde" } },
	{ BLOCK_USES, "KDE_PLASMA_VERSION", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_PLASMA_BRANCH", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_FRAMEWORKS_VERSION", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_FRAMEWORKS_BRANCH", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_APPLICATIONS_VERSION", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_APPLICATIONS_SHLIB_VER", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "KDE_APPLICATIONS_BRANCH", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "CALLIGRA_VERSION", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "CALLIGRA_BRANCH", VAR_SKIP_GOALCOL, { "kde" } },
	{ BLOCK_USES, "USE_LDCONFIG", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_LDCONFIG32", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_LINUX", VAR_SORTED, { "linux" } },
	{ BLOCK_USES, "USE_LINUX_PREFIX", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_LINUX_RPM", VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_USES, "USE_LINUX_RPM_BAD_PERMS", VAR_SKIP_GOALCOL, { "linux" } },
	{ BLOCK_USES, "USE_LOCALE", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_LXQT", VAR_SORTED, { "lxqt" } },
	{ BLOCK_USES, "USE_MATE", VAR_SORTED, { "mate" } },
	{ BLOCK_USES, "USE_MOZILLA", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_MYSQL", VAR_DEFAULT, { "mysql" } },
	{ BLOCK_USES, "USE_OCAML", VAR_DEFAULT, {} },
	{ BLOCK_USES, "NO_OCAML_BUILDDEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "NO_OCAML_RUNDEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAML_FINDLIB", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAML_CAMLP4", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAML_TK", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "NO_OCAMLTK_BUILDDEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "NO_OCAMLTK_RUNDEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAML_LDCONFIG", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAMLFIND_PLIST", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OCAML_WASH", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "OCAML_PKGDIRS", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_USES, "OCAML_LDLIBS", VAR_SORTED, {} },
	{ BLOCK_USES, "OCAMLFIND", VAR_DEFAULT, {} },
	{ BLOCK_USES, "OCAMLFIND_DEPEND", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "OCAMLFIND_DESTDIR", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "OCAMLFIND_LDCONF", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "OCAMLFIND_PORT", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_OPENLDAP", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WANT_OPENLDAP_SASL", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "WANT_OPENLDAP_VER", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_PERL5", VAR_SORTED, { "perl5" } },
	{ BLOCK_USES, "PL_BUILD", VAR_DEFAULT, { "perl5" } },
	{ BLOCK_USES, "USE_PHP", VAR_SORTED, { "pear", "php" } },
	{ BLOCK_USES, "IGNORE_WITH_PHP", VAR_SKIP_GOALCOL, { "pear", "php" } },
	{ BLOCK_USES, "PHP_MODNAME", VAR_DEFAULT, { "pear", "php" } },
	{ BLOCK_USES, "PHP_MOD_PRIO", VAR_DEFAULT, { "pear", "php" } },
	{ BLOCK_USES, "PEAR_CHANNEL", VAR_DEFAULT, { "pear" } },
	{ BLOCK_USES, "PEAR_CHANNEL_VER", VAR_SKIP_GOALCOL, { "pear" } },
	{ BLOCK_USES, "USE_PYQT", VAR_SORTED, { "pyqt" } },
	{ BLOCK_USES, "PYQT_DIST", VAR_DEFAULT, { "pyqt" } },
	{ BLOCK_USES, "PYQT_SIPDIR", VAR_DEFAULT, { "pyqt" } },
	{ BLOCK_USES, "USE_PYTHON", VAR_SORTED, { "python", "waf" } },
	{ BLOCK_USES, "PYTHON_NO_DEPENDS", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYTHON_CMD", VAR_DEFAULT, { "python", "waf" } },
	{ BLOCK_USES, "PYSETUP", VAR_DEFAULT, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_SETUP", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_CONFIGURE_TARGET", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_BUILD_TARGET", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_INSTALL_TARGET", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_CONFIGUREARGS", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_BUILDARGS", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_INSTALLARGS", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_INSTALLNOSINGLE", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_PKGNAME", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_PKGVERSION", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_EGGINFO", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "PYDISTUTILS_EGGINFODIR", VAR_SKIP_GOALCOL, { "python", "waf" } },
	{ BLOCK_USES, "USE_QT", VAR_SORTED, { "qt", "qt-dist" } },
	{ BLOCK_USES, "QT_BINARIES", VAR_DEFAULT, { "qt", "qt-dist" } },
	{ BLOCK_USES, "QT_CONFIG", VAR_DEFAULT, { "qt", "qt-dist" } },
	{ BLOCK_USES, "QT_DEFINES", VAR_DEFAULT, { "qt", "qt-dist" } },
	{ BLOCK_USES, "QT5_VERSION", VAR_DEFAULT, { "qt", "qt-dist" } },
	{ BLOCK_USES, "USE_RC_SUBR", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_RUBY", VAR_DEFAULT, {} },
	VAR_BROKEN_RUBY(BLOCK_USES, VAR_IGNORE_WRAPCOL, {}),
	{ BLOCK_USES, "RUBY_MODNAME", VAR_DEFAULT, {} },
	{ BLOCK_USES, "RUBY_MODDOCDIR", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_MODEXAMPLESDIR", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_NO_BUILD_DEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_NO_RUN_DEPENDS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_RUBY_EXTCONF", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_EXTCONF", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_EXTCONF_SUBDIRS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_RUBY_SETUP", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_SETUP", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_RUBY_RDOC", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "RUBY_REQUIRE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "USE_RUBYGEMS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_USES, "GEM_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_USES, "USE_SBCL", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_SDL", VAR_SORTED, { "sdl" } },
	{ BLOCK_USES, "USE_SM_COMPAT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_SUBMAKE", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_TEX", VAR_SORTED, {} },
	{ BLOCK_USES, "USE_WX", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_WX_NOT", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WANT_WX", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WANT_WX_VER", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WANT_WXGTK_VER", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WITH_WX_VER", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WX_COMPS", VAR_SORTED, {} },
	{ BLOCK_USES, "WX_CONF_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_USES, "WX_PREMK", VAR_DEFAULT, {} },
	{ BLOCK_USES, "USE_XFCE", VAR_SORTED, { "xfce" } },
	{ BLOCK_USES, "USE_XORG", VAR_SORTED, { "xorg", "motif" } },
	{ BLOCK_USES, "WAF_CMD", VAR_DEFAULT, { "waf" } },
	{ BLOCK_USES, "WEBPLUGIN_NAME", VAR_SKIP_GOALCOL, { "webplugin" } },
	{ BLOCK_USES, "WEBPLUGIN_FILES", VAR_SKIP_GOALCOL, { "webplugin" } },
	{ BLOCK_USES, "WEBPLUGIN_DIR", VAR_SKIP_GOALCOL, { "webplugin" } },
	{ BLOCK_USES, "XMKMF_ARGS", VAR_DEFAULT, { "imake" } },

	{ BLOCK_SHEBANGFIX, "SHEBANG_FILES", VAR_SORTED, { "shebangfix" } },
	{ BLOCK_SHEBANGFIX, "SHEBANG_GLOB", VAR_SORTED, { "shebangfix" } },
	{ BLOCK_SHEBANGFIX, "SHEBANG_REGEX", VAR_SORTED, { "shebangfix" } },
	{ BLOCK_SHEBANGFIX, "SHEBANG_LANG", VAR_SORTED, { "shebangfix" } },
	{ BLOCK_SHEBANGFIX, "OLD_CMD", VAR_NOT_COMPARABLE, { "shebangfix" } },
	{ BLOCK_SHEBANGFIX, "CMD", VAR_NOT_COMPARABLE, { "shebangfix" } },

	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX", VAR_DEFAULT, { "uniquefiles" } },
	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX_FILES", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, { "uniquefiles" } },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX", VAR_DEFAULT, { "uniquefiles" } },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX_FILES", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, { "uniquefiles" } },

	{ BLOCK_APACHE, "AP_EXTRAS", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "AP_INC", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "AP_LIB", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "AP_FAST_BUILD", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "AP_GENPLIST", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "MODULENAME", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "SHORTMODNAME", VAR_DEFAULT, { "apache" } },
	{ BLOCK_APACHE, "SRC_FILE", VAR_DEFAULT, { "apache" } },

	{ BLOCK_ELIXIR, "ELIXIR_APP_NAME", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "ELIXIR_LIB_ROOT", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "ELIXIR_APP_ROOT", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "ELIXIR_HIDDEN", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "ELIXIR_LOCALE", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_CMD", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_COMPILE", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_REWRITE", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_BUILD_DEPS", VAR_SORTED, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_RUN_DEPS", VAR_SORTED, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_DOC_DIRS", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_DOC_FILES", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_ENV", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_ENV_NAME", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_BUILD_NAME", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_TARGET", VAR_DEFAULT, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_EXTRA_APPS", VAR_SORTED, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_EXTRA_DIRS", VAR_SORTED, { "elixir" } },
	{ BLOCK_ELIXIR, "MIX_EXTRA_FILES", VAR_SORTED, { "elixir" } },

	{ BLOCK_EMACS, "EMACS_FLAVORS_EXCLUDE", VAR_DEFAULT, { "emacs" } },
	{ BLOCK_EMACS, "EMACS_NO_DEPENDS", VAR_DEFAULT, { "emacs" } },

	{ BLOCK_ERLANG, "ERL_APP_NAME", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "ERL_APP_ROOT", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "REBAR_CMD", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "REBAR3_CMD", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "REBAR_PROFILE", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "REBAR_TARGETS", VAR_SORTED, { "erlang" } },
	{ BLOCK_ERLANG, "ERL_BUILD_NAME", VAR_DEFAULT, { "erlang" } },
	{ BLOCK_ERLANG, "ERL_BUILD_DEPS", VAR_SORTED, { "erlang" } },
	{ BLOCK_ERLANG, "ERL_RUN_DEPS", VAR_SORTED, { "erlang" } },
	{ BLOCK_ERLANG, "ERL_DOCS", VAR_DEFAULT, { "erlang" } },

	{ BLOCK_CMAKE, "CMAKE_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_ON", VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_OFF", VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_TESTING_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_TESTING_ON", VAR_SKIP_GOALCOL | VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_TESTING_OFF", VAR_SKIP_GOALCOL | VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_TESTING_TARGET", VAR_SKIP_GOALCOL | VAR_SORTED, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_BUILD_TYPE", VAR_SKIP_GOALCOL, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_INSTALL_PREFIX", VAR_SKIP_GOALCOL, { "cmake" } },
	{ BLOCK_CMAKE, "CMAKE_SOURCE_PATH", VAR_SKIP_GOALCOL, { "cmake" } },

	{ BLOCK_CONFIGURE, "HAS_CONFIGURE", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "GNU_CONFIGURE", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "GNU_CONFIGURE_PREFIX", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_CMD", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_LOG", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_SCRIPT", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_SHELL", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_OUTSOURCE", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "CONFIGURE_TARGET", VAR_DEFAULT, {} },
	{ BLOCK_CONFIGURE, "WITHOUT_FBSD10_FIX", VAR_SKIP_GOALCOL, {} },

	{ BLOCK_QMAKE, "QMAKE_ARGS", VAR_SORTED, { "qmake" } },
	{ BLOCK_QMAKE, "QMAKE_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "qmake" } },
	{ BLOCK_QMAKE, "QMAKE_CONFIGURE_ARGS", VAR_SORTED, { "qmake" } },
	{ BLOCK_QMAKE, "QMAKE_SOURCE_PATH", VAR_DEFAULT, { "qmake" } },

	{ BLOCK_MESON, "MESON_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "meson" } },
	{ BLOCK_MESON, "MESON_BUILD_DIR", VAR_DEFAULT, { "meson" } },

	{ BLOCK_SCONS, "CCFLAGS", VAR_DEFAULT, { "scons" } },
	{ BLOCK_SCONS, "CPPPATH", VAR_SORTED, { "scons" } },
	{ BLOCK_SCONS, "LINKFLAGS", VAR_DEFAULT, { "scons" } },
	{ BLOCK_SCONS, "LIBPATH", VAR_DEFAULT, { "scons" } },

	{ BLOCK_CABAL, "USE_CABAL", VAR_CASE_SENSITIVE_SORT | VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "cabal" } },
	{ BLOCK_CABAL, "CABAL_BOOTSTRAP", VAR_SKIP_GOALCOL, { "cabal" } },
	{ BLOCK_CABAL, "CABAL_FLAGS", VAR_DEFAULT, { "cabal" } },
	{ BLOCK_CABAL, "CABAL_PROJECT", VAR_DEFAULT, { "cabal" } },
	{ BLOCK_CABAL, "EXECUTABLES", VAR_SORTED, { "cabal" } },
	{ BLOCK_CABAL, "DATADIR_VARS", VAR_NOT_COMPARABLE | VAR_SKIP_GOALCOL | VAR_SORTED, { "cabal" } },
	{ BLOCK_CABAL, "SKIP_CABAL_PLIST", VAR_SKIP_GOALCOL | VAR_SORTED, { "cabal" } },

	{ BLOCK_CARGO, "CARGO_CRATES", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_USE_GITHUB", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_USE_GITLAB", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_GIT_SUBDIR", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_CARGOLOCK", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_CARGOTOML", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_FEATURES", VAR_SORTED, { "cargo" } },

	{ BLOCK_CARGO, "CARGO_BUILDDEP", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_BUILD", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_BUILD_ARGS", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_BUILD_TARGET", VAR_SKIP_GOALCOL, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_INSTALL", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_INSTALL_ARGS", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_INSTALL_PATH", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_TEST", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_TEST_ARGS", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_UPDATE_ARGS", VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_CARGO_BIN", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_DIST_SUBDIR", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_TARGET_DIR", VAR_DEFAULT, { "cargo" } },
	{ BLOCK_CARGO, "CARGO_VENDOR_DIR", VAR_DEFAULT, { "cargo" } },

	{ BLOCK_GO, "GO_MODULE", VAR_DEFAULT, { "go" } },
	{ BLOCK_GO, "GO_PKGNAME", VAR_DEFAULT, { "go" } },
	{ BLOCK_GO, "GO_TARGET", VAR_SORTED, { "go" } },
	{ BLOCK_GO, "GO_BUILDFLAGS", VAR_LEAVE_UNFORMATTED, { "go" } },
	{ BLOCK_GO, "GO_TESTTARGET", VAR_DEFAULT, { "go" } },
	{ BLOCK_GO, "GO_TESTFLAGS", VAR_LEAVE_UNFORMATTED, { "go" } },
	{ BLOCK_GO, "CGO_ENABLED", VAR_DEFAULT, { "go" } },
	{ BLOCK_GO, "CGO_CFLAGS", VAR_SORTED, { "go" } },
	{ BLOCK_GO, "CGO_LDFLAGS", VAR_DEFAULT, { "go" } },

	{ BLOCK_LAZARUS, "NO_LAZBUILD", VAR_DEFAULT, { "lazarus" } },
	{ BLOCK_LAZARUS, "LAZARUS_PROJECT_FILES", VAR_DEFAULT, { "lazarus" } },
	{ BLOCK_LAZARUS, "LAZARUS_DIR", VAR_DEFAULT, { "lazarus" } },
	{ BLOCK_LAZARUS, "LAZBUILD_ARGS", VAR_SORTED, { "lazarus" } },
	{ BLOCK_LAZARUS, "LAZARUS_NO_FLAVORS", VAR_DEFAULT, { "lazarus" } },

	{ BLOCK_LINUX, "BIN_DISTNAMES", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "LIB_DISTNAMES", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "LIB_DISTNAMES_aarch64", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "LIB_DISTNAMES_amd64", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "LIB_DISTNAMES_i386", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "SHARE_DISTNAMES", VAR_DEFAULT, { "linux" } },
	{ BLOCK_LINUX, "SRC_DISTFILES", VAR_DEFAULT, { "linux" } },

	{ BLOCK_NUGET, "NUGET_DEPENDS", VAR_SORTED, { "mono" } },
	{ BLOCK_NUGET, "NUGET_PACKAGEDIR", VAR_DEFAULT, { "mono" } },
	{ BLOCK_NUGET, "NUGET_LAYOUT", VAR_DEFAULT, { "mono" } },
	{ BLOCK_NUGET, "NUGET_FEEDS", VAR_DEFAULT, { "mono" } },
	// TODO: These need to be handled specially
	//{ BLOCK_NUGET, "_URL", VAR_DEFAULT, { "mono" } },
	//{ BLOCK_NUGET, "_FILE", VAR_DEFAULT, { "mono" } },
	//{ BLOCK_NUGET, "_DEPENDS", VAR_DEFAULT, { "mono" } },
	{ BLOCK_NUGET, "PAKET_PACKAGEDIR", VAR_DEFAULT, { "mono" } },
	{ BLOCK_NUGET, "PAKET_DEPENDS", VAR_SORTED, { "mono" } },

	{ BLOCK_MAKE, "MAKEFILE", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "MAKE_CMD", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "MAKE_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_MAKE, "MAKE_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_MAKE, "SCRIPTS_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_MAKE, "DESTDIRNAME", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "MAKE_FLAGS", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "MAKE_JOBS_UNSAFE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_MAKE, "ALL_TARGET", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "INSTALL_TARGET", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "LATE_INSTALL_ARGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_MAKE, "TEST_ARGS", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_MAKE, "TEST_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_MAKE, "TEST_TARGET", VAR_DEFAULT, {} },
	{ BLOCK_MAKE, "QA_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_MAKE, "DO_MAKE_BUILD", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_MAKE, "DO_MAKE_TEST", VAR_IGNORE_WRAPCOL, {} },

	{ BLOCK_CFLAGS, "CFLAGS", VAR_DEFAULT, {} },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "CFLAGS_", VAR_DEFAULT, {}),
	{ BLOCK_CFLAGS, "CPPFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "CXXFLAGS", VAR_DEFAULT, {} },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "CXXFLAGS_", VAR_DEFAULT, {}),
	{ BLOCK_CFLAGS, "DEBUG_FLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "DPADD", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "FFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "FCFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "OBJCFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "RUSTFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "LDADD", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "LDFLAGS", VAR_DEFAULT, {} },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "LDFLAGS_", VAR_DEFAULT, {}),
	{ BLOCK_CFLAGS, "LIBS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "LLD_UNSAFE", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "SSP_UNSAFE", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "SSP_CFLAGS", VAR_DEFAULT, {} },
	{ BLOCK_CFLAGS, "WITHOUT_CPU_CFLAGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_CFLAGS, "WITHOUT_NO_STRICT_ALIASING", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_CFLAGS, "WITHOUT_SSP", VAR_DEFAULT, {} },

	{ BLOCK_CONFLICTS, "CONFLICTS", VAR_SORTED, {} },
	{ BLOCK_CONFLICTS, "CONFLICTS_BUILD", VAR_SORTED, {} },
	{ BLOCK_CONFLICTS, "CONFLICTS_INSTALL", VAR_SORTED, {} },

	{ BLOCK_STANDARD, "AR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "AS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "CC", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "CPP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "CXX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "LD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "STRIP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "BINDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "ETCDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "ETCDIR_REL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "DATADIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "DATADIR_REL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "DOCSDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "DOCSDIR_REL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "EXAMPLESDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "FILESDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "LIB_DIRS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MASTERDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MANDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MANDIRS", VAR_SORTED, {} },
	{ BLOCK_STANDARD, "MANPREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN1PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN2PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN3PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN4PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN5PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN6PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN7PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN8PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MAN9PREFIX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "PATCHDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "PKGDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SCRIPTDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "STAGEDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SRC_BASE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "TMPDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "WWWDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "WWWDIR_REL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "BINARY_ALIAS", VAR_SORTED, {} },
	{ BLOCK_STANDARD, "BINARY_WRAPPERS", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	{ BLOCK_STANDARD, "BINOWN", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "BINGRP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "BINMODE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MANMODE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SHAREOWN", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SHAREGRP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "_SHAREMODE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SHAREMODE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "WWWOWN", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "WWWGRP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "BUNDLE_LIBS", VAR_SORTED, {} },
	{ BLOCK_STANDARD, "DESKTOP_ENTRIES", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "DESKTOPDIR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "EXTRA_PATCHES", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "EXTRACT_CMD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "EXTRACT_BEFORE_ARGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "EXTRACT_AFTER_ARGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "FETCH_CMD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "FETCH_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "FETCH_REGET", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "FETCH_ENV", VAR_SORTED, {} },
	{ BLOCK_STANDARD, "FETCH_BEFORE_ARGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "FETCH_AFTER_ARGS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "PATCH_STRIP", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "PATCH_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "PATCH_DIST_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "REINPLACE_CMD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "REINPLACE_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "DISTORIG", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "IA32_BINARY_PORT", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "INSTALL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "IS_INTERACTIVE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_ARCH", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_ARCH_IGNORE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_BUILD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NOCCACHE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_CCACHE", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_STANDARD, "NO_CCACHE_DEPEND", VAR_IGNORE_WRAPCOL, {} },
	{ BLOCK_STANDARD, "NO_CHECKSUM", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_INSTALL", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NO_MTREE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NOT_REPRODUCIBLE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "MASTER_SORT", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MASTER_SORT_REGEX", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MTREE_CMD", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MTREE_ARGS", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "MTREE_FILE", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "NOPRECIOUSMAKEVARS", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "NO_TEST", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "PORTSCOUT", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "SUB_FILES", VAR_SORTED, {} },
	{ BLOCK_STANDARD, "SUB_LIST", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_STANDARD, "TARGET_ORDER_OVERRIDE", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_STANDARD, "UID_FILES", VAR_SORTED, {} },
	// XXX: Really add them here?
	{ BLOCK_STANDARD, "ERROR", VAR_DEFAULT, {} },
	{ BLOCK_STANDARD, "WARNING", VAR_DEFAULT, {} },

	{ BLOCK_WRKSRC, "NO_WRKSUBDIR", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "AUTORECONF_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "BUILD_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "CONFIGURE_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "INSTALL_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "PATCH_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "TEST_WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "WRKDIR", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "WRKSRC", VAR_DEFAULT, {} },
	{ BLOCK_WRKSRC, "WRKSRC_SUBDIR", VAR_DEFAULT, {} },

	{ BLOCK_USERS, "USERS", VAR_SORTED, {} },
	{ BLOCK_USERS, "GROUPS", VAR_SORTED, {} },

	{ BLOCK_PLIST, "DESCR", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "DISTINFO_FILE", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "PKGHELP", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "PKGPREINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGPOSTINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGPREDEINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGDEINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGPOSTDEINSTALL", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKGMESSAGE", VAR_SUBPKG_HELPER, {} },
	{ BLOCK_PLIST, "PKG_DBDIR", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "PKG_SUFX", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "PLIST", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "POST_PLIST", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "TMPPLIST", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "INFO", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "INFO_PATH", VAR_DEFAULT, {} },
	{ BLOCK_PLIST, "PLIST_DIRS", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_PLIST, "PLIST_FILES", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_PLIST, "PLIST_SUB", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_PLIST, "PORTDATA", VAR_CASE_SENSITIVE_SORT | VAR_SORTED, {} },
	{ BLOCK_PLIST, "PORTDOCS", VAR_CASE_SENSITIVE_SORT | VAR_SORTED, {} },
	{ BLOCK_PLIST, "PORTEXAMPLES", VAR_CASE_SENSITIVE_SORT | VAR_SORTED, {} },

	{ BLOCK_OPTDEF, "OPTIONS_DEFINE", VAR_SORTED, {} },
	// These do not exist in the framework but some ports
	// define them themselves
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_DragonFly", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_DEFINE_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_DEFINE_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_DragonFly", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_DEFAULT_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_DEFAULT_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_OPTDEF, "OPTIONS_GROUP", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_MULTI", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_RADIO", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_SINGLE", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_DragonFly", VAR_SKIP_GOALCOL | VAR_SORTED, {} },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_EXCLUDE_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_EXCLUDE_", VAR_SKIP_GOALCOL | VAR_SORTED, {}),
	{ BLOCK_OPTDEF, "OPTIONS_SLAVE", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "OPTIONS_OVERRIDE", VAR_SORTED, {} },
	{ BLOCK_OPTDEF, "NO_OPTIONS_SORT", VAR_SKIP_GOALCOL, {} },
	{ BLOCK_OPTDEF, "OPTIONS_FILE", VAR_DEFAULT, {} },
	{ BLOCK_OPTDEF, "OPTIONS_SUB", VAR_DEFAULT, {} },

	{ BLOCK_OPTDESC, "DESC", VAR_IGNORE_WRAPCOL | VAR_NOT_COMPARABLE, {} },

	{ BLOCK_OPTHELPER, "IMPLIES", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PREVENTS", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PREVENTS_MSG", VAR_NOT_COMPARABLE, {} },
#if PORTFMT_SUBPACKAGES
	{ BLOCK_OPTHELPER, "SUBPACKAGES", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
#endif
	{ BLOCK_OPTHELPER, "CATEGORIES", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CATEGORIES_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "MASTER_SITES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "MASTER_SITES_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "DISTFILES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "DISTFILES_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCH_SITES", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCH_SITES_OFF", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCHFILES", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCHFILES_OFF", VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BROKEN", VAR_IGNORE_WRAPCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BROKEN_OFF", VAR_IGNORE_WRAPCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "IGNORE", VAR_IGNORE_WRAPCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "IGNORE_OFF", VAR_IGNORE_WRAPCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BUILD_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_SUBPKG_HELPER | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "USES", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "USES_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "USE", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "USE_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_PROJECT", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_PROJECT_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_SUBDIR", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_SUBDIR_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_TAGNAME", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_TAGNAME_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_TUPLE", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GH_TUPLE_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_COMMIT", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_COMMIT_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_PROJECT", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_PROJECT_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_SITE", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_SITE_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_SUBDIR", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_SUBDIR_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_TUPLE", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "GL_TUPLE_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CMAKE_BOOL", VAR_SORTED | VAR_NOT_COMPARABLE, { "cmake" } },
	{ BLOCK_OPTHELPER, "CMAKE_BOOL_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, { "cmake" } },
	{ BLOCK_OPTHELPER, "CMAKE_ON", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, { "cmake" } },
	{ BLOCK_OPTHELPER, "CMAKE_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, { "cmake" } },
	{ BLOCK_OPTHELPER, "CONFIGURE_ON", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFIGURE_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENABLE", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFIGURE_WITH", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "QMAKE_ON", VAR_SORTED | VAR_NOT_COMPARABLE, { "qmake" } },
	{ BLOCK_OPTHELPER, "QMAKE_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, { "qmake" } },
	{ BLOCK_OPTHELPER, "MESON_ENABLED", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_DISABLED", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_ON", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_TRUE", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_FALSE", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_YES", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "MESON_NO", VAR_SORTED | VAR_NOT_COMPARABLE, { "meson" } },
	{ BLOCK_OPTHELPER, "USE_CABAL", VAR_CASE_SENSITIVE_SORT | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_SORTED | VAR_NOT_COMPARABLE, { "cabal" } },
	{ BLOCK_OPTHELPER, "CABAL_FLAGS", VAR_NOT_COMPARABLE, { "cabal" } },
	{ BLOCK_OPTHELPER, "EXECUTABLES", VAR_SORTED | VAR_NOT_COMPARABLE, { "cabal" } },
	{ BLOCK_OPTHELPER, "MAKE_ARGS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "MAKE_ARGS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "MAKE_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "MAKE_ENV_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "ALL_TARGET", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "ALL_TARGET_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "TEST_TARGET", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "TEST_TARGET_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CFLAGS", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CFLAGS_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CPPFLAGS", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CPPFLAGS_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CXXFLAGS", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CXXFLAGS_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LDFLAGS", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LDFLAGS_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LIBS", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "LIBS_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES_OFF", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES_OFF", VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "SUB_FILES", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "SUB_FILES_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "SUB_LIST", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "SUB_LIST_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "INFO", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "INFO_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_DIRS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_DIRS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_FILES", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_FILES_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_SUB", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PLIST_SUB_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PORTDOCS", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PORTDOCS_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES_OFF", VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "VARS", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
	{ BLOCK_OPTHELPER, "VARS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SORTED | VAR_NOT_COMPARABLE, {} },
};

// Variables that are somewhere in the ports framework but that
// ports do not usually set.  Portclippy will flag them as "unknown".
// We can set special formatting rules for them here instead of in
// variable_order_.
static struct VariableOrderEntry special_variables_[] = {
	{ BLOCK_UNKNOWN, "_DISABLE_TESTS", VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "_IPXE_BUILDCFG", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "_PARFETCH_ENV", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "_SRHT_TUPLE", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "CARGO_CARGO_RUN", VAR_IGNORE_WRAPCOL, { "cargo" } },
	{ BLOCK_UNKNOWN, "CFLAGS_clang", VAR_DEFAULT, {} },
	{ BLOCK_UNKNOWN, "CFLAGS_gcc", VAR_DEFAULT, {} },
	{ BLOCK_UNKNOWN, "CPPFLAGS_clang", VAR_DEFAULT, {} },
	{ BLOCK_UNKNOWN, "CPPFLAGS_gcc", VAR_DEFAULT, {} },
	VAR_FOR_EACH_ARCH(BLOCK_UNKNOWN, "CONFIGURE_ARGS_", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {}),
	VAR_FOR_EACH_ARCH(BLOCK_UNKNOWN, "CONFIGURE_ENV_", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {}),
	{ BLOCK_UNKNOWN, "CXXFLAGS_clang", VAR_DEFAULT, {} },
	{ BLOCK_UNKNOWN, "CXXFLAGS_gcc", VAR_DEFAULT, {} },
	{ BLOCK_UNKNOWN, "CO_ENV", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "D4P_ENV", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "DEV_ERROR", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "DEV_WARNING", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES, {} },
	VAR_FOR_EACH_ARCH(BLOCK_UNKNOWN, "EXTRA_PATCHES_", VAR_DEFAULT, {}),
	{ BLOCK_UNKNOWN, "GN_ARGS", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "GO_ENV", VAR_PRINT_AS_NEWLINES, { "go" } },
	{ BLOCK_UNKNOWN, "IPXE_BUILDCFG", VAR_PRINT_AS_NEWLINES, {} },
	VAR_FOR_EACH_ARCH(BLOCK_UNKNOWN, "MAKE_ARGS_", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {}),
	{ BLOCK_UNKNOWN, "MAKE_ARGS_clang", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "MAKE_ARGS_gcc", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	VAR_FOR_EACH_ARCH(BLOCK_UNKNOWN, "MAKE_ENV_", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {}),
	{ BLOCK_UNKNOWN, "MAKE_ENV_clang", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "MAKE_ENV_gcc", VAR_PRINT_AS_NEWLINES | VAR_SORTED, {} },
	{ BLOCK_UNKNOWN, "MASTER_SITES_ABBREVS", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "MOZ_OPTIONS", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "QA_ENV", VAR_PRINT_AS_NEWLINES, {} },
	{ BLOCK_UNKNOWN, "SUBDIR", VAR_DEDUP | VAR_PRINT_AS_NEWLINES, {} },
};

#undef VAR_FOR_EACH_ARCH
#undef VAR_FOR_EACH_FREEBSD_VERSION
#undef VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH
#undef VAR_FOR_EACH_SSL

bool
variable_has_flag(struct Parser *parser, const char *var, int flag)
{
	SCOPE_MEMPOOL(pool);

	char *helper;
	if (is_options_helper(pool, parser, var, NULL, &helper, NULL)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if ((variable_order_[i].block == BLOCK_OPTHELPER ||
			     variable_order_[i].block == BLOCK_OPTDESC) &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(helper, variable_order_[i].var) == 0) {
				return true;
			}
		}
	}

	if (is_flavors_helper(pool, parser, var, NULL, &helper)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block == BLOCK_FLAVORS_HELPER &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(helper, variable_order_[i].var) == 0) {
				return true;
			}
		}
	}

	char *suffix;
	if (is_shebang_lang(pool, parser, var, NULL, &suffix)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block == BLOCK_SHEBANGFIX &&
			    (variable_order_[i].flags & VAR_NOT_COMPARABLE) &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(suffix, variable_order_[i].var) == 0) {
				return true;
			}
		}
	}

	if (is_cabal_datadir_vars(pool, parser, var, NULL, &suffix)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block == BLOCK_CABAL &&
			    (variable_order_[i].flags & VAR_NOT_COMPARABLE) &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(suffix, variable_order_[i].var) == 0) {
				return true;
			}
		}
	}

	char *prefix;
	if (matches_options_group(pool, parser, var, &prefix)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block == BLOCK_OPTDEF &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(prefix, variable_order_[i].var) == 0) {
				return true;
			}
		}
	}

	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if ((!(variable_order_[i].flags & VAR_NOT_COMPARABLE)) &&
		    (variable_order_[i].flags & flag) &&
		    strcmp(var, variable_order_[i].var) == 0) {
			return true;
		}
	}

	for (size_t i = 0; i < nitems(special_variables_); i++) {
		if ((special_variables_[i].flags & flag) &&
		    strcmp(var, special_variables_[i].var) == 0) {
			return true;
		}
	}

	return false;
}

bool
extract_arch_prefix(struct Mempool *pool, const char *var, char **prefix_without_arch, char **prefix_without_arch_osrel)
{
	for (size_t i = 0; i < known_architectures_len; i++) {
		char *suffix = str_printf(pool, "_%s", known_architectures[i]);
		if (str_endswith(var, suffix)) {
			*prefix_without_arch = str_ndup(pool, var, strlen(var) - strlen(suffix));
			*prefix_without_arch_osrel = NULL;
			return true;
		}
	}
	for (size_t i = 0; i < known_architectures_len; i++) {
		for (size_t j = 0; j < freebsd_versions_len; j++) {
			char *suffix = str_printf(pool, "_%s_%" PRIu32, known_architectures[i], freebsd_versions[j]);
			if (str_endswith(var, suffix)) {
				*prefix_without_arch = str_ndup(pool, var, strlen(var) - strlen(suffix));
				*prefix_without_arch_osrel = str_ndup(pool, var, strlen(var) - strlen(suffix) + strlen(known_architectures[i]) + 1);
				return true;
			}
		}
	}
	return false;
}

void
is_referenced_var_cb(struct Mempool *extpool, const char *key, const char *value, const char *hint, void *userdata)
{
	struct Array *tokens = userdata;
	array_append(tokens, str_dup(extpool, value));
}

void
add_referenced_var_candidates(struct Mempool *pool, struct Array *candidates, struct Array *cond_candidates, const char *stem, const char *ref)
{
	array_append(candidates, str_printf(pool, "${%s_${%s}}", stem, ref));
	array_append(candidates, str_printf(pool, "$(%s_${%s})", stem, ref));
	array_append(candidates, str_printf(pool, "${%s_${%s}:", stem, ref));
	array_append(cond_candidates, str_printf(pool, "defined(%s_${%s})", stem, ref));
	array_append(cond_candidates, str_printf(pool, "empty(%s_${%s})", stem, ref));

	array_append(candidates, str_printf(pool, "${${%s}_%s}", ref, stem));
	array_append(candidates, str_printf(pool, "$(${%s}_%s)", ref, stem));
	array_append(candidates, str_printf(pool, "${${%s}_%s:", ref, stem));
	array_append(cond_candidates, str_printf(pool, "defined(${%s}_%s)", ref, stem));
	array_append(cond_candidates, str_printf(pool, "defined(${%s}_%s:", ref, stem));
	array_append(cond_candidates, str_printf(pool, "empty(${%s}_%s)", ref, stem));
	array_append(cond_candidates, str_printf(pool, "empty(${%s}_%s:", ref, stem));
}

bool
is_referenced_var(struct Parser *parser, const char *var)
{
	if (!(parser_settings(parser).behavior & PARSER_CHECK_VARIABLE_REFERENCES)) {
		return false;
	}

	SCOPE_MEMPOOL(pool);

	// TODO: This is broken in many ways but will reduce
	// the number of false positives from portclippy/portscan

	struct Array *candidates = mempool_array(pool);
	struct Array *cond_candidates = mempool_array(pool);
	size_t varlen = strlen(var);

	array_append(candidates, str_printf(pool, "${%s}", var));
	array_append(candidates, str_printf(pool, "$(%s)", var));
	array_append(candidates, str_printf(pool, "${%s:", var));
	array_append(cond_candidates, str_printf(pool, "defined(%s)", var));
	array_append(cond_candidates, str_printf(pool, "defined(%s:", var));
	array_append(cond_candidates, str_printf(pool, "empty(%s)", var));
	array_append(cond_candidates, str_printf(pool, "empty(%s:", var));

	{
		char *var_without_arch = NULL;
		char *var_without_arch_osrel = NULL;
		if (extract_arch_prefix(pool, var, &var_without_arch, &var_without_arch_osrel)) {
			add_referenced_var_candidates(pool, candidates, cond_candidates, var_without_arch, "ARCH");
			if (var_without_arch_osrel) {
				add_referenced_var_candidates(pool, candidates, cond_candidates, var_without_arch, "ARCH}_${OSREL:R");
			}
		}
	}

	SET_FOREACH(parser_metadata(parser, PARSER_METADATA_FLAVORS), const char *, flavor) {
		size_t flavorlen = strlen(flavor);
		char *var_without_flavor;
		if (varlen > flavorlen && str_endswith(var, flavor) && *(var + varlen - flavorlen - 1) == '_') {
			var_without_flavor = str_slice(pool, var, 0, varlen - flavorlen - 1);
		} else if (str_startswith(var, flavor) && *(var + flavorlen) == '_') {
			var_without_flavor = str_slice(pool, var, flavorlen + 1, varlen);
		} else {
			continue;
		}

		add_referenced_var_candidates(pool, candidates, cond_candidates, var_without_flavor, "FLAVOR");
	}

	if ((str_endswith(var, "_clang") || str_endswith(var, "_gcc")) &&
	    set_contains(parser_metadata(parser, PARSER_METADATA_USES), "compiler")) {
		char *var_without_compiler_type;
		if (str_endswith(var, "_clang")) {
			var_without_compiler_type = str_slice(pool, var, 0, varlen - strlen("_clang"));
		} else {
			var_without_compiler_type = str_slice(pool, var, 0, varlen - strlen("_gcc"));
		}
		add_referenced_var_candidates(pool, candidates, cond_candidates, var_without_compiler_type, "CHOSEN_COMPILER_TYPE");
	}

	struct Array *tokens = mempool_array(pool);
	struct ParserEditOutput param = { NULL, NULL, NULL, NULL, is_referenced_var_cb, tokens, 0 };
	parser_edit(parser, pool, output_target_command_token, &param);
	parser_edit(parser, pool, output_variable_value, &param);
	ARRAY_FOREACH(tokens, const char *, token) {
		ARRAY_FOREACH(candidates, const char *, candidate) {
			if (strstr(token, candidate)) {
				return true;
			}
		}
	}

	array_truncate(tokens);
	parser_edit(parser, pool, output_conditional_token, &param);
	ARRAY_FOREACH(tokens, const char *, token) {
		ARRAY_FOREACH(candidates, const char *, candidate) {
			if (strstr(token, candidate)) {
				return true;
			}
		}
		ARRAY_FOREACH(cond_candidates, const char *, candidate) {
			if (strstr(token, candidate)) {
				return true;
			}
		}
	}

	return false;
}

bool
is_valid_license(struct Parser *parser, const char *license)
{
	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		if (strlen(license) == 0) {
			return false;
		}
		size_t i = 0;
		for (; license[i] != 0; i++) {
			char c = license[i];
			switch (c) {
			case '-':
			case '.':
			case '_':
			case '+':
				break;
			default:
				if (!isalnum((unsigned char)c)) {
					return false;
				}
				break;
			}
		}
		return i > 0;
	} else {
		return set_contains(parser_metadata(parser, PARSER_METADATA_LICENSES), license);
	}
}

bool
matches_license_name(struct Parser *parser, const char *var)
{
	if (strcmp(var, "LICENSE_NAME") == 0 ||
	    strcmp(var, "LICENSE_TEXT") == 0) {
		return true;
	}

	if (*var == '_') {
		var++;
	}

	if (!str_startswith(var, "LICENSE_NAME_") &&
	    !str_startswith(var, "LICENSE_TEXT_") &&
	    !str_startswith(var, "LICENSE_FILE_")) {
		return false;
	}

	return is_valid_license(parser, var + strlen("LICENSE_NAME_"));
}

bool
ignore_wrap_col(struct Parser *parser, const char *varname, enum ASTVariableModifier modifier)
{
	if (modifier == AST_VARIABLE_MODIFIER_SHELL ||
	    matches_license_name(parser, varname)) {
		return true;
	}

	return variable_has_flag(parser, varname, VAR_IGNORE_WRAPCOL);
}

uint32_t
indent_goalcol(const char *var, enum ASTVariableModifier modifier)
{
	size_t varlength = strlen(var) + 1;
	if (str_endswith(var, "+")) {
		varlength += 1; // " " before modifier
	}
	switch (modifier) {
	case AST_VARIABLE_MODIFIER_ASSIGN:
		varlength += 1;
		break;
	case AST_VARIABLE_MODIFIER_APPEND:
	case AST_VARIABLE_MODIFIER_EXPAND:
	case AST_VARIABLE_MODIFIER_OPTIONAL:
	case AST_VARIABLE_MODIFIER_SHELL:
		varlength += 2;
		break;
	default:
		panic("unhandled variable modifier: %d", modifier);
	}
	if (((varlength + 1) % 8) == 0) {
		varlength++;
	}
	return ceil(varlength / 8.0) * 8;
}

bool
is_comment(const char *token)
{
	if (token == NULL) {
		return false;
	}

	const char *datap = token;
	for (; *datap != 0 && isspace((unsigned char)*datap); datap++);
	return *datap == '#';
}

bool
is_include_bsd_port_mk(struct AST *node)
{
	if (node->type == AST_INCLUDE &&
	    node->include.type == AST_INCLUDE_BMAKE &&
	    node->include.sys) {
		if (strcmp(node->include.path, "bsd.port.options.mk") == 0 ||
		    strcmp(node->include.path, "bsd.port.pre.mk") == 0 ||
		    strcmp(node->include.path, "bsd.port.post.mk") == 0 ||
		    strcmp(node->include.path, "bsd.port.mk") == 0) {
			return true;
		}
	}

	return false;
}

bool
case_sensitive_sort(struct Parser *parser, const char *var)
{
	return variable_has_flag(parser, var, VAR_CASE_SENSITIVE_SORT);
}

bool
leave_unformatted(struct Parser *parser, const char *var)
{
	return variable_has_flag(parser, var, VAR_LEAVE_UNFORMATTED);
}

bool
should_sort(struct Parser *parser, const char *var, enum ASTVariableModifier modifier)
{
	if (modifier == AST_VARIABLE_MODIFIER_SHELL) {
		return false;
	}
	if ((parser_settings(parser).behavior & PARSER_ALWAYS_SORT_VARIABLES)) {
		return true;
	}
	return variable_has_flag(parser, var, VAR_SORTED);
}

bool
print_as_newlines(struct Parser *parser, const char *var)
{
	return variable_has_flag(parser, var, VAR_PRINT_AS_NEWLINES);
}

bool
skip_dedup(struct Parser *parser, const char *var, enum ASTVariableModifier modifier)
{
	return !should_sort(parser, var, modifier) && !variable_has_flag(parser, var, VAR_DEDUP);
}

bool
skip_goalcol(struct Parser *parser, const char *varname)
{
	if (matches_license_name(parser, varname)) {
		return true;
	}

	return variable_has_flag(parser, varname, VAR_SKIP_GOALCOL);
}

int
compare_rel(const char *rel[], size_t rellen, const char *a, const char *b)
{
	ssize_t ai = -1;
	ssize_t bi = -1;
	for (size_t i = 0; i < rellen; i++) {
		if (ai == -1 && strcmp(a, rel[i]) == 0) {
			ai = i;
		}
		if (bi == -1 && strcmp(b, rel[i]) == 0) {
			bi = i;
		}
		if (ai != -1 && bi != -1) {
			if (bi > ai) {
				return -1;
			} else if (ai > bi) {
				return 1;
			} else {
				return 0;
			}
		}
	}

	return strcasecmp(a, b);
}

int
compare_tokens(const void *ap, const void *bp, void *userdata)
{
	struct CompareTokensData *data = userdata;
	const char *a = *(const char**)ap;
	const char *b = *(const char**)bp;

	int result;
	if (compare_license_perms(data->parser, data->var, a, b, &result) ||
	    compare_plist_files(data->parser, data->var, a, b, &result) ||
	    compare_use_gnome(data->var, a, b, &result) ||
	    compare_use_kde(data->var, a, b, &result) ||
	    compare_use_pyqt(data->var, a, b, &result) ||
	    compare_use_qt(data->var, a, b, &result)) {
		return result;
	}

	if (case_sensitive_sort(data->parser, data->var)) {
		return strcmp(a, b);
	} else {
		return strcasecmp(a, b);
	}
}

bool
compare_license_perms(struct Parser *parser, const char *varname, const char *a, const char *b, int *result)
{
	// ^(_?LICENSE_PERMS_(-|[A-Z0-9\\._+ ])+|_LICENSE_LIST_PERMS|LICENSE_PERMS)
	if (strcmp(varname, "_LICENSE_LIST_PERMS") != 0 &&
	    strcmp(varname, "LICENSE_PERMS") != 0) {
		if (str_startswith(varname, "_LICENSE_PERMS_")) {
			varname++;
		}
		if (!str_startswith(varname, "LICENSE_PERMS_")) {
			return false;
		}
		const char *license = varname + strlen("LICENSE_PERMS_");
		if (!is_valid_license(parser, license)) {
			return false;
		}
	}

	if (result) {
		*result = compare_rel(license_perms_rel, nitems(license_perms_rel), a, b);
	}

	return true;
}

char *
remove_plist_keyword(const char *s, struct Mempool *pool)
{
	if (!str_endswith(s, "\"")) {
		return str_dup(pool, s);
	}

	// "^\"@([a-z]|-)+ "
	const char *ptr = s;
	if (*ptr != '"') {
		return str_dup(pool, s);
	}
	ptr++;
	if (*ptr != '@') {
		return str_dup(pool, s);
	}
	ptr++;

	const char *prev = ptr;
	for (; *ptr != 0 && (islower((unsigned char)*ptr) || *ptr == '-'); ptr++);
	if (*ptr == 0 || prev == ptr || *ptr != ' ') {
		return str_dup(pool, s);
	}
	ptr++;

	return str_ndup(pool, ptr, strlen(ptr) - 1);
}

bool
compare_plist_files(struct Parser *parser, const char *varname, const char *a, const char *b, int *result)
{
	SCOPE_MEMPOOL(pool);

	char *helper = NULL;
	if (is_options_helper(pool, parser, varname, NULL, &helper, NULL)) {
		if (strcmp(helper, "PLIST_FILES_OFF") != 0 &&
		    strcmp(helper, "PLIST_FILES") != 0 &&
		    strcmp(helper, "PLIST_DIRS_OFF") != 0 &&
		    strcmp(helper, "PLIST_DIRS") != 0) {
			return false;
		}
	} else if (strcmp(varname, "PLIST_FILES") != 0 && strcmp(varname, "PLIST_DIRS") != 0) {
		return false;
	}

	/* Ignore plist keywords */
	char *as = remove_plist_keyword(a, pool);
	char *bs = remove_plist_keyword(b, pool);
	if (result) {
		*result = strcasecmp(as, bs);
	}
	return true;
}

bool
compare_use_gnome(const char *var, const char *a, const char *b, int *result)
{
	if (strcmp(var, "USE_GNOME") != 0) {
		return false;
	}

	if (result) {
		*result = compare_rel(use_gnome_rel, use_gnome_rel_len, a, b);
	}
	return true;
}

bool
compare_use_kde(const char *var, const char *a, const char *b, int *result)
{
	if (strcmp(var, "USE_KDE") != 0) {
		return false;
	}

	if (result) {
		*result = compare_rel(use_kde_rel, use_kde_rel_len, a, b);
	}
	return true;
}

bool
compare_use_pyqt(const char *var, const char *a, const char *b, int *result)
{
	if (strcmp(var, "USE_PYQT") != 0) {
		return false;
	}

	if (result) {
		*result = compare_rel(use_pyqt_rel, use_pyqt_rel_len, a, b);
	}

	return true;
}

bool
compare_use_qt(const char *var, const char *a, const char *b, int *result)
{
	if (strcmp(var, "USE_QT") != 0) {
		return false;
	}

	if (result) {
		*result = compare_rel(use_qt_rel, use_qt_rel_len, a, b);
	}

	return true;
}

bool
is_flavors_helper(struct Mempool *pool, struct Parser *parser, const char *var, char **prefix_ret, char **helper_ret)
{
	const char *suffix = NULL;
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		if (str_endswith(var, helper) &&
		    strlen(var) > strlen(helper) &&
		    var[strlen(var) - strlen(helper) - 1] == '_') {
			suffix = helper;
			break;
		}
	}
	if (suffix == NULL) {
		return false;
	}

	// ^[-_[:lower:][:digit:]]+_
	size_t len = strlen(var) - strlen(suffix);
	if (len == 0) {
		return false;
	}
	if (var[len - 1] != '_') {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		char c = var[i];
		if (c != '-' && c != '_' && !islower((unsigned char)c) && !isdigit((unsigned char)c)) {
			return false;
		}
	}

	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		goto done;
	}

	char *prefix = str_ndup(pool, var, len - 1);
	unless (set_contains(parser_metadata(parser, PARSER_METADATA_FLAVORS), prefix)) {
		return false;
	}
done:
	if (prefix_ret) {
		*prefix_ret = str_ndup(pool, var, len);
	}
	if (helper_ret) {
		*helper_ret = str_dup(pool, suffix);
	}

	return true;
}

char *
extract_subpkg(struct Mempool *pool, struct Parser *parser, const char *var_, char **subpkg_ret)
{
	char *var = NULL;
	const char *subpkg = NULL;
	for (ssize_t i = strlen(var_) - 1; i > -1; i--) {
		char c = var_[i];
		if (c != '-' && c != '_' && !islower((unsigned char)c) && !isdigit((unsigned char)c)) {
			if (c == '.') {
				subpkg = var_ + i + 1;
				var = str_ndup(pool, var_, i);
			} else {
				var = str_dup(pool, var_);
			}
			break;
		}
	}

	if (var == NULL) {
		if (subpkg_ret) {
			*subpkg_ret = NULL;
		}
		return NULL;
	}

	if (subpkg && !(parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING)) {
		bool found = false;
#if PORTFMT_SUBPACKAGES
		found = set_contains(parser_metadata(parser, PARSER_METADATA_SUBPACKAGES), subpkg);
#endif
		if (!found) {
			if (subpkg_ret) {
				*subpkg_ret = NULL;
			}
			return NULL;
		}
	}

	if (subpkg_ret) {
		if (subpkg) {
			*subpkg_ret = str_dup(pool, subpkg);
		} else {
			*subpkg_ret = NULL;
		}
	}

	return var;
}

bool
is_options_helper(struct Mempool *pool, struct Parser *parser, const char *var_, char **prefix_ret, char **helper_ret, char **subpkg_ret)
{
	char *subpkg;
	char *var;
	if ((var = extract_subpkg(pool, parser, var_, &subpkg)) == NULL) {
		return false;
	}

	const char *suffix = NULL;
	if (str_endswith(var, "DESC")) {
		suffix = "DESC";
	} else {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTHELPER) {
				continue;
			}
			const char *helper = variable_order_[i].var;
			if (str_endswith(var, helper) &&
			    strlen(var) > strlen(helper) &&
			    var[strlen(var) - strlen(helper) - 1] == '_') {
				suffix = helper;
				break;
			}
		}
	}
	if (suffix == NULL) {
		return false;
	}

	if (subpkg) {
		bool found = false;
#if PORTFMT_SUBPACKAGES
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTHELPER ||
			    !(variable_order_[i].flags & VAR_SUBPKG_HELPER)) {
				continue;
			}
			if (strcmp(variable_order_[i].var, suffix) == 0) {
				found = true;
			}
		}
#endif
		if (!found) {
			return false;
		}
	}


	// ^[-_[:upper:][:digit:]]+_
	size_t len = strlen(var) - strlen(suffix);
	if (len == 0) {
		return false;
	}
	if (var[len - 1] != '_') {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		char c = var[i];
		if (c != '-' && c != '_' && !isupper((unsigned char)c) && !isdigit((unsigned char)c)) {
			return false;
		}
	}

	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		goto done;
	}

	char *prefix = str_ndup(pool, var, len - 1);
	struct Set *groups = parser_metadata(parser, PARSER_METADATA_OPTION_GROUPS);
	struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
	if (strcmp(suffix, "DESC") == 0) {
		if (set_contains(groups, prefix)) {
			goto done;
		}
	}
	unless (set_contains(options, prefix)) {
		return false;
	}

done:
	if (prefix_ret) {
		*prefix_ret = str_ndup(pool, var, len);
	}
	if (helper_ret) {
		*helper_ret = str_dup(pool, suffix);
	}
	if (subpkg_ret) {
		if (subpkg) {
			*subpkg_ret = str_dup(pool, subpkg);
		} else {
			*subpkg_ret = NULL;
		}
	}

	return true;
}

bool
matches_options_group(struct Mempool *pool, struct Parser *parser, const char *s, char **prefix)
{
	size_t i = 0;
	// ^_?
	if (s[i] == '_') {
		i++;
	}

	const char *var = NULL;
	// OPTIONS_(GROUP|MULTI|RADIO|SINGLE)_
	const char *opts[] = {
		"OPTIONS_GROUP_",
		"OPTIONS_MULTI_",
		"OPTIONS_RADIO_",
		"OPTIONS_SINGLE_",
	};
	bool matched = false;
	for (size_t j = 0; j < nitems(opts); j++) {
		if (str_startswith(s + i, opts[j])) {
			matched = true;
			i += strlen(opts[j]);
			var = opts[j];
			break;
		}
	}
	if (!matched) {
		return false;
	}

	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		// [-_[:upper:][:digit:]]+
		if (!(isupper((unsigned char)s[i]) || isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')) {
			return false;
		}
		for (size_t len = strlen(s); i < len; i++) {
			if (!(isupper((unsigned char)s[i]) || isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')) {
				return false;
			}
		}
		if (prefix) {
			*prefix = str_ndup(pool, var, strlen(var) - 1);
		}
		return true;
	} else {
		struct Set *groups = parser_metadata(parser, PARSER_METADATA_OPTION_GROUPS);
		// XXX: This could be stricter by checking the group type too
		if (set_contains(groups, s + i)) {
			if (prefix) {
				*prefix = str_ndup(pool, var, strlen(var) - 1);
			}
			return true;
		} else {
			return false;
		}
	}
}

bool
is_cabal_datadir_vars_helper(struct Mempool *pool, const char *var, const char *exe, char **prefix, char **suffix)
{
	char *buf = str_printf(pool, "%s_DATADIR_VARS", exe);
	if (strcmp(var, buf) == 0) {
		if (prefix) {
			*prefix = str_dup(pool, exe);
		}
		if (suffix) {
			*suffix = str_dup(pool, "DATADIR_VARS");
		}
		return true;
	} else {
		return false;
	}
}

bool
is_cabal_datadir_vars(struct Mempool *pool, struct Parser *parser, const char *var, char **prefix, char **suffix)
{
	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		if (str_endswith(var, "_DATADIR_VARS")) {
			if (prefix) {
				*prefix = str_ndup(pool, var, strlen(var) - strlen("_DATADIR_VARS"));
			}
			if (suffix) {
				*suffix = str_dup(pool, "DATADIR_VARS");
			}
			return true;
		}
	}

	// Do we have USES=cabal?
	if (!set_contains(parser_metadata(parser, PARSER_METADATA_USES), "cabal")) {
		return false;
	}

	SET_FOREACH (parser_metadata(parser, PARSER_METADATA_CABAL_EXECUTABLES), const char *, exe) {
		if (is_cabal_datadir_vars_helper(pool, var, exe, prefix, suffix)) {
			return true;
		}
	}

	return false;
}

bool
is_shebang_lang_helper(struct Mempool *extpool, const char *var, const char *lang, char **prefix, char **suffix)
{
	SCOPE_MEMPOOL(pool);

	char *buf = str_printf(pool, "%s_OLD_CMD", lang);
	if (strcmp(var, buf) == 0) {
		if (prefix) {
			*prefix = str_dup(extpool, lang);
		}
		if (suffix) {
			*suffix = str_dup(extpool, "OLD_CMD");
		}
		return true;
	}

	buf = str_printf(pool, "%s_CMD", lang);
	if (strcmp(var, buf) == 0) {
		if (prefix) {
			*prefix = str_dup(extpool, lang);
		}
		if (suffix) {
			*suffix = str_dup(extpool, "CMD");
		}
		return true;
	}

	return false;
}

bool
is_shebang_lang(struct Mempool *pool, struct Parser *parser, const char *var, char **prefix, char **suffix)
{
	if (parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) {
		if (str_endswith(var, "_OLD_CMD")) {
			if (prefix) {
				*prefix = str_ndup(pool, var, strlen(var) - strlen("_OLD_CMD"));
			}
			if (suffix) {
				*suffix = str_dup(pool, "OLD_CMD");
			}
			return true;
		}
		if (str_endswith(var, "_CMD")) {
			if (prefix) {
				*prefix = str_ndup(pool, var, strlen(var) - strlen("_CMD"));
			}
			if (suffix) {
				*suffix = str_dup(pool, "CMD");
			}
			return true;
		}
	}

	// Do we have USES=shebangfix?
	if (!set_contains(parser_metadata(parser, PARSER_METADATA_USES), "shebangfix")) {
		return false;
	}

	for (size_t i = 0; i < static_shebang_langs_len; i++) {
		const char *lang = static_shebang_langs[i];
		if (is_shebang_lang_helper(pool, var, lang, prefix, suffix)) {
			return true;
		}
	}

	bool ok = false;
	SET_FOREACH (parser_metadata(parser, PARSER_METADATA_SHEBANG_LANGS), const char *, lang) {
		if (is_shebang_lang_helper(pool, var, lang, prefix, suffix)) {
			ok = true;
			break;
		}
	}

	return ok;
}

enum BlockType
variable_order_block(struct Parser *parser, const char *var, struct Mempool *extpool, struct Set **uses_candidates)
{
	SCOPE_MEMPOOL(pool);

	if (uses_candidates) {
		*uses_candidates = NULL;
	}

	if (strcmp(var, "LICENSE") == 0) {
		return BLOCK_LICENSE;
	}
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_LICENSE ||
		    strcmp(variable_order_[i].var, "LICENSE") == 0) {
			continue;
		}
		if (strcmp(variable_order_[i].var, var) == 0) {
			return BLOCK_LICENSE;
		}
		if (str_startswith(var, variable_order_[i].var)) {
			const char *suffix = var + strlen(variable_order_[i].var);
			if (*suffix == '_' && is_valid_license(parser, suffix + 1)) {
				return BLOCK_LICENSE;
			}
		}
	}

	if (is_flavors_helper(pool, parser, var, NULL, NULL)) {
		return BLOCK_FLAVORS_HELPER;
	}

	if (is_shebang_lang(pool, parser, var, NULL, NULL)) {
		return BLOCK_SHEBANGFIX;
	}

	if (is_cabal_datadir_vars(pool, parser, var, NULL, NULL)) {
		return BLOCK_CABAL;
	}

	if (is_options_helper(pool, parser, var, NULL, NULL, NULL)) {
		if (str_endswith(var, "_DESC")) {
			return BLOCK_OPTDESC;
		} else {
			return BLOCK_OPTHELPER;
		}
	}

	if (matches_options_group(pool, parser, var, NULL)) {
		return BLOCK_OPTDEF;
	}

	const char *tmp = var;
	char *var_without_subpkg = extract_subpkg(pool, parser, var, NULL);
	if (var_without_subpkg) {
		tmp = var_without_subpkg;
	}
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		switch (variable_order_[i].block) {
		case BLOCK_FLAVORS_HELPER:
		case BLOCK_OPTHELPER:
		case BLOCK_OPTDESC:
			continue;
		case BLOCK_LICENSE:
		case BLOCK_OPTDEF:
			// RE_LICENSE_*, matches_options_group() do not
			// cover all cases.
		default:
			break;
		}
		if (strcmp(tmp, variable_order_[i].var) == 0) {
			size_t count = 0;
			bool satisfies_uses = true;
			// We skip the USES check if the port is a
			// slave port since often USES only appears
			// in the master.  Since we do not recurse
			// down in the master Makefile we would
			// get many false positives otherwise.
			if (!(parser_settings(parser).behavior & PARSER_ALLOW_FUZZY_MATCHING) &&
			    !parser_metadata(parser, PARSER_METADATA_MASTERDIR)) {
				struct Set *uses = parser_metadata(parser, PARSER_METADATA_USES);
				for (; count < nitems(variable_order_[i].uses) && variable_order_[i].uses[count]; count++);
				if (count > 0) {
					satisfies_uses = false;
					for (size_t j = 0; j < count; j++) {
						const char *requses = variable_order_[i].uses[j];
						if (set_contains(uses, requses)) {
							satisfies_uses = true;
							break;
						}
					}
				}
			}
			if (satisfies_uses) {
				return variable_order_[i].block;
			} else if (count > 0 && uses_candidates) {
				if (*uses_candidates == NULL) {
					*uses_candidates = mempool_set(extpool, str_compare);
				}
				for (size_t j = 0; j < count; j++) {
					set_add(*uses_candidates, variable_order_[i].uses[j]);
				}
			}
		}
	}

	return BLOCK_UNKNOWN;
}

int
compare_order(const void *ap, const void *bp, void *userdata)
{
	SCOPE_MEMPOOL(pool);
	struct Parser *parser = userdata;
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;

	if (strcmp(a, b) == 0) {
		return 0;
	}
	enum BlockType ablock = variable_order_block(parser, a, NULL, NULL);
	enum BlockType bblock = variable_order_block(parser, b, NULL, NULL);
	if (ablock < bblock) {
		return -1;
	} else if (ablock > bblock) {
		return 1;
	}

	if (ablock == BLOCK_LICENSE) {
		int ascore = -1;
		int bscore = -1;
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_LICENSE) {
				continue;
			}
			if (strcmp(variable_order_[i].var, "LICENSE") == 0) {
				continue;
			}
			if (str_startswith(a, variable_order_[i].var)) {
				ascore = i;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = i;
			}
		}
		if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		}
	} else if (ablock == BLOCK_FLAVORS_HELPER) {
		int ascore = -1;
		int bscore = -1;

		char *ahelper = NULL;
		char *aprefix = NULL;
		char *bhelper = NULL;
		char *bprefix = NULL;
		panic_unless(is_flavors_helper(pool, parser, a, &aprefix, &ahelper) &&
			     is_flavors_helper(pool, parser, b, &bprefix, &bhelper),
			     "is_flavors_helper() failed");
		panic_unless(ahelper && aprefix && bhelper && bprefix,
			     "is_flavors_helper() returned invalid values");

		// Only compare if common prefix (helper for the same flavor)
		int prefix_score = strcmp(aprefix, bprefix);
		if (prefix_score == 0) {
			for (size_t i = 0; i < nitems(variable_order_); i++) {
				if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
					continue;
				}
				if (strcmp(ahelper, variable_order_[i].var) == 0) {
					ascore = i;
				}
				if (strcmp(bhelper, variable_order_[i].var) == 0) {
					bscore = i;
				}
			}
		}

		if (prefix_score != 0) {
			return prefix_score;
		} else if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	} else if (ablock == BLOCK_SHEBANGFIX) {
		if (str_endswith(a, "_CMD") && !str_endswith(b, "_CMD")) {
			return 1;
		} else if (!str_endswith(a, "_CMD") && str_endswith(b, "_CMD")) {
			return -1;
		} else if (str_endswith(a, "_CMD") && str_endswith(b, "_CMD")) {
			char *alang = NULL;
			char *asuffix = NULL;
			char *blang = NULL;
			char *bsuffix = NULL;
			is_shebang_lang(pool, parser, a, &alang, &asuffix);
			is_shebang_lang(pool, parser, b, &blang, &bsuffix);
			panic_unless(alang && asuffix && blang && bsuffix,
				     "is_shebang_lang() returned invalid values");

			ssize_t ascore = -1;
			ssize_t bscore = -1;
			for (size_t i = 0; i < static_shebang_langs_len; i++) {
				const char *lang = static_shebang_langs[i];
				if (strcmp(alang, lang) == 0) {
					ascore = i;
				}
				if (strcmp(blang, lang) == 0) {
					bscore = i;
				}
			}
			SET_FOREACH(parser_metadata(parser, PARSER_METADATA_SHEBANG_LANGS), const char *, lang) {
				if (strcmp(alang, lang) == 0) {
					ascore = lang_index;
				}
				if (strcmp(blang, lang) == 0) {
					bscore = lang_index;
				}
			}

			bool aold = strcmp(asuffix, "OLD_CMD") == 0;
			bool bold = strcmp(bsuffix, "OLD_CMD") == 0;
			if (ascore == bscore) {
				if (aold && !bold) {
					return -1;
				} else if (!aold && bold) {
					return 1;
				} else {
					return 0;
				}
			} else if (ascore < bscore) {
				return -1;
			} else {
				return 1;
			}
		}
	} else if (ablock == BLOCK_CABAL) {
		// XXX: Yikes!
		if (strcmp(a, "SKIP_CABAL_PLIST") == 0) {
			return 1;
		} else if (strcmp(b, "SKIP_CABAL_PLIST") == 0) {
			return -1;
		} else if (str_endswith(a, "_DATADIR_VARS") && !str_endswith(b, "_DATADIR_VARS")) {
			return 1;
		} else if (!str_endswith(a, "_DATADIR_VARS") && str_endswith(b, "_DATADIR_VARS")) {
			return -1;
		} else if (str_endswith(a, "_DATADIR_VARS") && str_endswith(b, "_DATADIR_VARS")) {
			char *aexe = NULL;
			char *asuffix = NULL;
			char *bexe = NULL;
			char *bsuffix = NULL;
			is_cabal_datadir_vars(pool, parser, a, &aexe, &asuffix);
			is_cabal_datadir_vars(pool, parser, b, &bexe, &bsuffix);
			panic_unless(aexe && asuffix && bexe && bsuffix,
				     "is_cabal_datadir_vars() returned invalid values");

			ssize_t ascore = -1;
			ssize_t bscore = -1;
			SET_FOREACH(parser_metadata(parser, PARSER_METADATA_CABAL_EXECUTABLES), const char *, exe) {
				if (strcmp(aexe, exe) == 0) {
					ascore = exe_index;
				}
				if (strcmp(bexe, exe) == 0) {
					bscore = exe_index;
				}
			}

			bool aold = strcmp(asuffix, "DATADIR_VARS") == 0;
			bool bold = strcmp(bsuffix, "DATADIR_VARS") == 0;
			if (ascore == bscore) {
				if (aold && !bold) {
					return -1;
				} else if (!aold && bold) {
					return 1;
				} else {
					return 0;
				}
			} else if (ascore < bscore) {
				return -1;
			} else {
				return 1;
			}
		}
	} else if (ablock == BLOCK_OPTDESC) {
		return strcmp(a, b);
	} else if (ablock == BLOCK_OPTHELPER) {
		int ascore = -1;
		int bscore = -1;

		char *ahelper = NULL;
		char *aprefix = NULL;
		char *bhelper = NULL;
		char *bprefix = NULL;
		// TODO SUBPKG
		panic_unless(is_options_helper(pool, parser, a, &aprefix, &ahelper, NULL) &&
			     is_options_helper(pool, parser, b, &bprefix, &bhelper, NULL),
			     "is_options_helper() failed");
		panic_unless(ahelper && aprefix && bhelper && bprefix,
			     "is_options_helper() returned invalid values");

		// Only compare if common prefix (helper for the same option)
		int prefix_score = strcmp(aprefix, bprefix);
		if (prefix_score == 0) {
			for (size_t i = 0; i < nitems(variable_order_); i++) {
				if (variable_order_[i].block != BLOCK_OPTHELPER) {
					continue;
				}
				if (strcmp(ahelper, variable_order_[i].var) == 0) {
					ascore = i;
				}
				if (strcmp(bhelper, variable_order_[i].var) == 0) {
					bscore = i;
				}
			}
		}

		if (prefix_score != 0) {
			return prefix_score;
		} else if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	} else if (ablock == BLOCK_OPTDEF) {
		int ascore = -1;
		int bscore = -1;

		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTDEF) {
				continue;
			}
			if (str_startswith(a, variable_order_[i].var)) {
				ascore = i;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = i;
			}
		}

		if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	}

	char *asubpkg = NULL;
	char *a_without_subpkg = extract_subpkg(pool, parser, a, &asubpkg);
	if (a_without_subpkg == NULL) {
		a_without_subpkg = str_dup(pool, a);
	}
	char *bsubpkg = NULL;
	char *b_without_subpkg = extract_subpkg(pool, parser, b, &bsubpkg);
	if (b_without_subpkg == NULL) {
		b_without_subpkg = str_dup(pool, b);
	}
	int ascore = -1;
	int bscore = -1;
	for (size_t i = 0; i < nitems(variable_order_) && (ascore == -1 || bscore == -1); i++) {
		if (strcmp(a_without_subpkg, variable_order_[i].var) == 0) {
			ascore = i;
		}
		if (strcmp(b_without_subpkg, variable_order_[i].var) == 0) {
			bscore = i;
		}
	}

	if (strcmp(a_without_subpkg, b_without_subpkg) == 0 && asubpkg && bsubpkg) {
		return strcmp(asubpkg, bsubpkg);
	} else if (asubpkg && !bsubpkg) {
		return 1;
	} else if (!asubpkg && bsubpkg) {
		return -1;
	} else if (ascore < bscore) {
		return -1;
	} else if (ascore > bscore) {
		return 1;
	} else {
		return strcmp(a_without_subpkg, b_without_subpkg);
	}
}

void
target_extract_opt(struct Mempool *pool, struct Parser *parser, const char *target, char **target_out, char **opt_out, bool *state)
{
	bool colon = str_endswith(target, ":");
	bool on;
	if ((colon && ((on = str_endswith(target, "-on:")) || str_endswith(target, "-off:"))) ||
	    (!colon && ((on = str_endswith(target, "-on")) || str_endswith(target, "-off")))) {
		const char *p = target;
		for (; *p == '-' || (islower((unsigned char)*p) && isalpha((unsigned char)*p)); p++);
		size_t opt_suffix_len;
		if (on) {
			opt_suffix_len = strlen("-on");
		} else {
			opt_suffix_len = strlen("-off");
		}
		if (colon) {
			opt_suffix_len++;
		}
		char *opt = str_ndup(pool, p, strlen(p) - opt_suffix_len);
		char *tmp = str_printf(pool, "%s_USES", opt);
		if (is_options_helper(pool, parser, tmp, NULL, NULL, NULL)) {
			char *target_root = str_ndup(pool, target, strlen(target) - strlen(p) - 1);
			for (size_t i = 0; i < nitems(target_order_); i++) {
				if (target_order_[i].opthelper &&
				    strcmp(target_order_[i].name, target_root) == 0) {
					*state = on;
					if (opt_out) {
						*opt_out = str_dup(pool, opt);
					}
					if (target_out) {
						*target_out = str_dup(pool, target_root);
					}
					return;
				}
			}
		}
	}

	if (opt_out) {
		*opt_out = NULL;
	}
	*state = false;
	if (colon) {
		size_t len = strlen(target);
		if (len > 0) {
			if (target_out) {
				*target_out = str_ndup(pool, target, len - 1);
			}
			return;
		}
	}
	if (target_out) {
		*target_out = str_dup(pool, target);
	}
}

bool
is_known_target(struct Parser *parser, const char *target)
{
	SCOPE_MEMPOOL(pool);

	char *root;
	bool state;
	target_extract_opt(pool, parser, target, &root, NULL, &state);

	for (size_t i = 0; i < nitems(target_order_); i++) {
		if (strcmp(target_order_[i].name, root) == 0) {
			return true;
		}
	}

	return false;
}

bool
is_special_source(const char *source)
{
	for (size_t i = 0; i < nitems(special_sources_); i++) {
		if (strcmp(source, special_sources_[i]) == 0) {
			return true;
		}
	}
	return false;
}

bool
is_special_target(const char *target)
{
	for (size_t i = 0; i < nitems(special_targets_); i++) {
		if (strcmp(target, special_targets_[i]) == 0) {
			return true;
		}
	}
	return false;
}

int
compare_target_order(const void *ap, const void *bp, void *userdata)
{
	SCOPE_MEMPOOL(pool);

	struct Parser *parser = userdata;
	const char *a_ = *(const char **)ap;
	const char *b_ = *(const char **)bp;

	if (strcmp(a_, b_) == 0) {
		return 0;
	}

	char *a, *b, *aopt, *bopt;
	bool aoptstate, boptstate;
	target_extract_opt(pool, parser, a_, &a, &aopt, &aoptstate);
	target_extract_opt(pool, parser, b_, &b, &bopt, &boptstate);

	ssize_t aindex = -1;
	ssize_t bindex = -1;
	for (size_t i = 0; i < nitems(target_order_) && (aindex == -1 || bindex == -1); i++) {
		if (aindex == -1 && strcmp(target_order_[i].name, a) == 0) {
			aindex = i;
		}
		if (bindex == -1 && strcmp(target_order_[i].name, b) == 0) {
			bindex = i;
		}
	}

	if (aindex == -1) {
		return 1;
	} else if (bindex == -1) {
		return -1;
	} else if (aindex == bindex) {
		if (aopt == NULL) {
			return -1;
		}
		if (bopt == NULL) {
			return 1;
		}

		int c = strcmp(aopt, bopt);
		if (c < 0) {
			return -1;
		} else if (c > 0) {
			return 1;
		}

		if (aoptstate && !boptstate) {
			return -1;
		} else if (!aoptstate && boptstate) {
			return 1;
		} else {
			panic("should not happen");
		}
	} else if (aindex < bindex) {
		return -1;
	} else if (aindex > bindex) {
		return 1;
	} else {
		panic("should not happen");
	}
}

bool
target_command_wrap_after_each_token(const char *command)
{
	if (*command == '@') {
		command++;
	}
	for (size_t i = 0; i < nitems(target_command_wrap_after_each_token_); i++) {
		if (strcmp(command, target_command_wrap_after_each_token_[i]) == 0) {
			return true;
		}
	}
	return false;
}

bool
target_command_should_wrap(const char *word)
{
	if (strcmp(word, "&&") == 0 ||
	    strcmp(word, "||") == 0 ||
	    strcmp(word, "then") == 0 ||
	    (str_endswith(word, ";") && !str_endswith(word, "\\;")) ||
	    strcmp(word, "|") == 0) {
		return true;
	}

	return false;
}
