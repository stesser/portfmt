#!/bin/sh
set -eu
ROOT="${PWD}"
BUILDDIR="$(readlink -f "$1" 2>/dev/null || realpath "$1")"
PORTCLIPPY="${BUILDDIR}/.bin/portclippy"
PORTEDIT="${BUILDDIR}/.bin/portedit"
PORTFMT="${BUILDDIR}/.bin/portfmt"
PORTSCAN="${BUILDDIR}/.bin/portscan"
SPLIT_TEST="${BUILDDIR}/.tool/tests/split_test"
: ${SH:=/bin/sh}

export PORTCLIPPY
export PORTEDIT
export PORTFMT
export PORTSCAN
export ROOT

t="$2"
mkdir -p "$(dirname "${BUILDDIR}/$t")"

fail() {
	echo "$t:1:1: FAIL" >&2
	exit 1
}

split_test() {
	cd "${BUILDDIR}"
	if ! "${SPLIT_TEST}" "${ROOT}" "${BUILDDIR}" "$t"; then
		echo "$t:1:1: split_test failed" >&2
		exit 1
	fi
}

case "$t" in
tests/parser/ast*.t|tests/parser/token*.t)
	split_test
	args=""
	end="false"
	case "$(basename "$t")" in
	ast_err*) args="-ddd"; end="true" ;;
	ast*) args="-ddd" ;;
	token*) args="-dd" ;;
	esac
	if ${PORTFMT} ${args} <"$t.in" >"$t.actual" 2>&1 || ${end}; then
		if ! diff -L "$t.expected" -L "$t.actual" -u "$t.expected" "$t.actual"; then
			fail
		fi
	else
		fail
	fi
	;;
tests/parser/*.sh)
	if ! ${SH} -o pipefail -eu "$t"; then
		fail
	fi
	;;
tests/format/*.t)
	split_test
	if ${PORTFMT} -t <"$t.in" >"$t.actual"; then
		if ! diff -L "$t.expected" -L "$t.actual" -u "$t.expected" "$t.actual"; then
			fail
		fi
	else
		fail
	fi
	if ${PORTFMT} -t <"$t.expected" >"$t.actual2"; then
		if ! diff -L "$t.expected" -L "$t.actual" -u "$t.expected" "$t.actual2"; then
			fail
		fi
	else
		fail
	fi
	;;
tests/edit/*.t)
	split_test
	if ! actual="$t.actual" input="$t.in" expected="$t.expected" ${SH} -o pipefail -eu "$t.sh"; then
		fail
	fi
	;;
tests/clippy/*.t)
	split_test
	${PORTCLIPPY} "$t.in" >"$t.actual" || true
	if ! diff -L "$t.expected" -L "$t.actual" -u "$t.expected" "$t.actual"; then
		fail
	fi
	;;
tests/reject/*.in)
	if ${PORTFMT} "$t" 2>/dev/null; then
		fail
	fi
	;;
tests/portscan/*.sh)
	if ! (cd "${ROOT}/tests/portscan"; ${SH} -o pipefail -eu "$(basename "$t")"); then
		fail
	fi
	;;
esac
