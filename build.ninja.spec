CSTD = gnu99
LDADD += -lm $LDADD_EXECINFO

default-features
	subpackages
if subpackages
	CPPFLAGS += -DPORTFMT_SUBPACKAGES=1
if !subpackages
	CPPFLAGS += -DPORTFMT_SUBPACKAGES=0

bundle libias.a
	subdir = $srcdir/libias
	libias/array.c
	libias/compats.c
	libias/diff.c
	libias/diffutil.c
	libias/flow.c
	libias/io.c
	libias/io/dir.c
	libias/map.c
	libias/mem.c
	libias/mempool.c
	libias/mempool/dir.c
	libias/mempool/file.c
	libias/path.c
	libias/queue.c
	libias/set.c
	libias/stack.c
	libias/str.c
	libias/util.c

bundle libportfmt.a
	ast.c
	constants.c
	io/dir.c
	io/file.c
	mainutils.c
	$builddir/enum.c
	parser.c
	parser/astbuilder.c
	parser/astbuilder/conditional.c
	parser/astbuilder/target.c
	parser/astbuilder/token.c
	parser/astbuilder/variable.c
	parser/edits/edit/bump_revision.c
	parser/edits/edit/merge.c
	parser/edits/edit/set_version.c
	parser/edits/kakoune/select_object_on_line.c
	parser/edits/lint/bsd_port.c
	parser/edits/lint/clones.c
	parser/edits/lint/commented_portrevision.c
	parser/edits/lint/order.c
	parser/edits/output/conditional_token.c
	parser/edits/output/target_command_token.c
	parser/edits/output/unknown_targets.c
	parser/edits/output/unknown_variables.c
	parser/edits/output/variable_value.c
	parser/edits/refactor/collapse_adjacent_variables.c
	parser/edits/refactor/dedup_tokens.c
	parser/edits/refactor/remove_consecutive_empty_lines.c
	parser/edits/refactor/sanitize_append_modifier.c
	parser/edits/refactor/sanitize_cmake_args.c
	parser/edits/refactor/sanitize_comments.c
	parser/edits/refactor/sanitize_eol_comments.c
	parser/tokenizer.c
	regexp.c
	rules.c

gen $builddir/enum.c $srcdir/scripts/enum.awk
	ast.h
	mainutils.h
	parser.h
	parser/astbuilder/enum.h
	portscan/log.h
	portscan/status.h
	rules.h

bin portclippy
	libias.a
	libportfmt.a
	portclippy.c

bin portedit
	libias.a
	libportfmt.a
	portedit.c

bin portfmt
	libias.a
	libportfmt.a
	portfmt.c

bin portscan
	LDFLAGS += -pthread
	libias.a
	libportfmt.a
	portscan.c
	portscan/log.c
	portscan/status.c

tool tests/split_test
	libias.a
	tests/split_test.c

install-man
	man/portclippy.1
	man/portedit.1
	man/portfmt.1
	man/portscan.1

include $builddir/tests.ninja
