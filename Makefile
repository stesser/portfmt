.SUFFIXES: .test

include Makefile.configure
-include Makefile.local

MKDIR?=		mkdir -p
LN?=		ln
SH?=		/bin/sh

CFLAGS+=	-std=gnu99 -I.
LDADD+=		-lm ${LDADD_EXECINFO}

SUBPACKAGES?=	1
CPPFLAGS+=	-DPORTFMT_SUBPACKAGES=${SUBPACKAGES}

OBJS=		ast.o \
		constants.o \
		io/dir.o \
		io/file.o \
		mainutils.o \
		parser.o \
		parser/astbuilder.o \
		parser/astbuilder/conditional.o \
		parser/astbuilder/enum.o \
		parser/astbuilder/target.o \
		parser/astbuilder/token.o \
		parser/astbuilder/variable.o \
		parser/edits/edit/bump_revision.o \
		parser/edits/edit/merge.o \
		parser/edits/edit/set_version.o \
		parser/edits/kakoune/select_object_on_line.o \
		parser/edits/lint/bsd_port.o \
		parser/edits/lint/clones.o \
		parser/edits/lint/commented_portrevision.o \
		parser/edits/lint/order.o \
		parser/edits/output/conditional_token.o \
		parser/edits/output/target_command_token.o \
		parser/edits/output/unknown_targets.o \
		parser/edits/output/unknown_variables.o \
		parser/edits/output/variable_value.o \
		parser/edits/refactor/collapse_adjacent_variables.o \
		parser/edits/refactor/dedup_tokens.o \
		parser/edits/refactor/remove_consecutive_empty_lines.o \
		parser/edits/refactor/sanitize_append_modifier.o \
		parser/edits/refactor/sanitize_cmake_args.o \
		parser/edits/refactor/sanitize_comments.o \
		parser/edits/refactor/sanitize_eol_comments.o \
		parser/tokenizer.o \
		portscan/log.o \
		portscan/status.o \
		regexp.o \
		rules.o
ALL_TESTS=	tests/run.sh
TESTS?=		${ALL_TESTS}

all: bin/portclippy bin/portedit bin/portfmt bin/portscan

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

libportfmt.a: ${OBJS}
	${AR} rcs libportfmt.a ${OBJS}

libias/config.h: config.h
	cp config.h libias

libias/Makefile.configure: Makefile.configure
	cp Makefile.configure libias

libias/libias.a: libias libias/config.h libias/Makefile.configure
	@${MAKE} -C libias libias.a

${TESTS}: libportfmt.a
.o.test:
	${CC} ${LDFLAGS} -o $@ $< libportfmt.a libias/libias.a ${LDADD}

bin/portclippy: portclippy.o libias/libias.a libportfmt.a
	@mkdir -p bin
	${CC} ${LDFLAGS} -o bin/portclippy portclippy.o libportfmt.a libias/libias.a ${LDADD}

bin/portedit: portedit.o libias/libias.a libportfmt.a
	@mkdir -p bin
	${CC} ${LDFLAGS} -o bin/portedit portedit.o libportfmt.a libias/libias.a ${LDADD}

bin/portfmt: portfmt.o libias/libias.a libportfmt.a
	@mkdir -p bin
	${CC} ${LDFLAGS} -o bin/portfmt portfmt.o libportfmt.a libias/libias.a ${LDADD}

bin/portscan: portscan.o libias/libias.a libportfmt.a
	@mkdir -p bin
	${CC} ${LDFLAGS} -o bin/portscan portscan.o libportfmt.a libias/libias.a ${LDADD} -lpthread

#
ast.o: config.h libias/array.h libias/flow.h libias/map.h libias/mempool.h libias/stack.h libias/str.h ast.h
constants.o: config.h constants.h
io/dir.o: config.h libias/mempool/dir.h capsicum_helpers.h io/dir.h
io/file.o: config.h libias/mempool/file.h capsicum_helpers.h io/file.h
mainutils.o: config.h libias/array.h libias/mempool.h libias/mempool/file.h libias/str.h capsicum_helpers.h mainutils.h parser.h
parser.o: config.h libias/array.h libias/color.h libias/diff.h libias/diffutil.h libias/flow.h libias/io.h libias/mempool/file.h libias/map.h libias/mem.h libias/mempool.h libias/path.h libias/set.h libias/str.h ast.h constants.h io/file.h parser.h parser/astbuilder.h parser/edits.h parser/tokenizer.h rules.h
parser/astbuilder.o: config.h libias/array.h libias/flow.h libias/mem.h libias/mempool.h libias/stack.h libias/str.h ast.h parser.h parser/astbuilder.h parser/astbuilder/conditional.h parser/astbuilder/enum.h parser/astbuilder/target.h parser/astbuilder/token.h parser/astbuilder/variable.h rules.h
parser/astbuilder/conditional.o: config.h libias/mempool.h libias/str.h parser/astbuilder/enum.h parser/astbuilder/conditional.h
parser/astbuilder/enum.o: config.h parser/astbuilder/enum.h
parser/astbuilder/target.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h parser/astbuilder/target.h
parser/astbuilder/token.o: config.h libias/flow.h libias/mem.h libias/mempool.h libias/str.h ast.h parser/astbuilder/conditional.h parser/astbuilder/enum.h parser/astbuilder/target.h parser/astbuilder/token.h parser/astbuilder/variable.h
parser/astbuilder/variable.o: config.h libias/mempool.h libias/str.h ast.h parser/astbuilder/variable.h
parser/edits/edit/bump_revision.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/edit/merge.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/edit/set_version.o: config.h libias/array.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/kakoune/select_object_on_line.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/lint/bsd_port.o: config.h libias/array.h libias/flow.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/lint/clones.o: config.h libias/array.h libias/color.h libias/flow.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/lint/commented_portrevision.o: config.h libias/array.h libias/color.h libias/flow.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/lint/order.o: config.h libias/array.h libias/color.h libias/diff.h libias/flow.h libias/map.h libias/mem.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/output/conditional_token.o: config.h libias/array.h libias/flow.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/output/target_command_token.o: config.h libias/array.h libias/flow.h ast.h parser.h parser/edits.h
parser/edits/output/unknown_targets.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/output/unknown_variables.o: config.h libias/array.h libias/flow.h libias/mem.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/output/variable_value.o: config.h libias/array.h libias/flow.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/refactor/collapse_adjacent_variables.o: config.h libias/array.h libias/flow.h libias/mempool.h ast.h parser.h parser/edits.h rules.h
parser/edits/refactor/dedup_tokens.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/refactor/remove_consecutive_empty_lines.o: config.h libias/array.h libias/flow.h libias/mempool.h ast.h parser.h parser/edits.h
parser/edits/refactor/sanitize_append_modifier.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/set.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/refactor/sanitize_cmake_args.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/refactor/sanitize_comments.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h
parser/edits/refactor/sanitize_eol_comments.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/str.h ast.h parser.h parser/edits.h rules.h
parser/edits/template.o: config.h libias/array.h libias/flow.h ast.h parser.h parser/edits.h
parser/tokenizer.o: config.h libias/array.h libias/flow.h libias/mem.h libias/mempool.h libias/str.h ast.h parser/astbuilder.h parser/astbuilder/enum.h parser.h parser/tokenizer.h
portclippy.o: config.h libias/mempool.h mainutils.h parser.h parser/edits.h
portedit.o: config.h libias/array.h libias/flow.h libias/mempool.h libias/set.h libias/str.h mainutils.h parser.h parser/edits.h regexp.h
portfmt.o: config.h libias/mempool.h mainutils.h parser.h
portscan.o: config.h libias/array.h libias/diff.h libias/flow.h libias/io.h libias/io/dir.h libias/map.h libias/mem.h libias/mempool.h libias/mempool/dir.h libias/mempool/file.h libias/set.h libias/str.h ast.h io/dir.h io/file.h mainutils.h parser.h parser/edits.h portscan/log.h portscan/status.h regexp.h
portscan/log.o: config.h libias/array.h libias/diff.h libias/flow.h libias/io.h libias/mem.h libias/mempool.h libias/mempool/file.h libias/set.h libias/str.h capsicum_helpers.h portscan/log.h
portscan/status.o: config.h libias/array.h libias/mempool.h libias/str.h portscan/status.h
regexp.o: config.h libias/flow.h libias/mem.h libias/mempool.h libias/str.h regexp.h
rules.o: config.h libias/array.h libias/flow.h libias/mem.h libias/mempool.h libias/set.h libias/str.h ast.h constants.h rules.h parser.h parser/edits.h

deps:
	@for f in $$(git ls-files | grep '.*\.c$$' | grep -v '^tests\.c$$' | LC_ALL=C sort); do \
		${CC} ${CFLAGS} -MM -MT "$${f%.c}.o" $${f} | sed 's/[\\ ]/\n/g' | grep -vF "$${f}" | tr -s '\n' ' ' | sed 's/ $$//'; \
		echo; \
	done > Makefile.deps
	@mv Makefile Makefile.bak
	@awk '/^#$$/ { print; deps = 1 } \
	deps && /^$$/ { deps = 0; system("cat Makefile.deps") } \
	!deps { print; }' Makefile.bak > Makefile
	@rm -f Makefile.bak Makefile.deps

install: all
	${MKDIR} ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} bin/portclippy bin/portedit bin/portfmt bin/portscan ${DESTDIR}${BINDIR}
	${INSTALL_MAN} man/*.1 ${DESTDIR}${MANDIR}/man1

install-symlinks:
	@${MAKE} INSTALL_MAN="/bin/sh -c 'f=\"$$\$$(echo \"$$\$$@\" | xargs realpath)\"; ln -sf $$\$$f' -- SYMLINK " \
		INSTALL_PROGRAM="\$${INSTALL_MAN}" install

regen-rules:
	@/bin/sh generate_rules.sh

clean:
	@${MAKE} -C libias clean
	@rm -f ${OBJS} *.o libportfmt.a bin/portclippy bin/portedit bin/portfmt \
		bin/portscan config.*.old $$(echo ${ALL_TESTS} | sed 's,tests/run.sh,,')
	@rm -rf bin

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

lint: all
	@${SH} tests/lint.sh

test: all ${TESTS}
	@${SH} libias/tests/run.sh ${TESTS}

tag:
	@[ -z "${V}" ] && echo "must set V" && exit 1; \
	date=$$(git log -1 --pretty=format:%cd --date=format:%Y-%m-%d HEAD); \
	title="## [${V}] - $${date}"; \
	if ! grep -Fq "$${title}" CHANGELOG.md; then \
		echo "# portfmt ${V}"; \
		awk '/^## Unreleased$$/{x=1;next}x{if($$1=="##"){exit}else if($$1=="###"){$$1="##"};print}' \
			CHANGELOG.md >RELNOTES.md.new; \
		awk "/^## Unreleased$$/{print;printf\"\n$${title}\n\";next}{print}" \
			CHANGELOG.md >CHANGELOG.md.new; \
		mv CHANGELOG.md.new CHANGELOG.md; \
		echo "portfmt ${V}" >RELNOTES.md; \
		cat RELNOTES.md.new >>RELNOTES.md; \
		rm -f RELNOTES.md.new; \
	fi; \
	git commit -m "Release ${V}" CHANGELOG.md; \
	git tag v${V}

release:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	V=$$(echo $${tag} | sed 's,^v,,'); \
	git ls-files --recurse-submodules . ':!:libias/tests' | \
		bsdtar --files-from=- -s ",^,portfmt-$${V}/," --options lzip:compression-level=9 \
			--uid 0 --gid 0 -caf portfmt-$${V}.tar.lz; \
	sha256 portfmt-$${V}.tar.lz >portfmt-$${V}.tar.lz.SHA256 || \
	sha256sum --tag portfmt-$${V}.tar.lz >portfmt-$${V}.tar.lz.SHA256; \
	printf "SIZE (%s) = %s\n" portfmt-$${V}.tar.lz $$(wc -c <portfmt-$${V}.tar.lz) \
		>>portfmt-$${V}.tar.lz.SHA256

publish:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	V=$$(echo $${tag} | sed 's,^v,,'); \
	git push --follow-tags origin; \
	hub release create -F RELNOTES.md $${tag} \
		-a portfmt-$${V}.tar.lz \
		-a portfmt-$${V}.tar.lz.SHA256

.PHONY: all clean debug deps install install-symlinks lint publish release regen-rules tag test
