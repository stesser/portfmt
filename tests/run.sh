#!/bin/sh
set -u
ROOT="${PWD}"
PORTCLIPPY="${ROOT}/portclippy"
PORTEDIT="${ROOT}/portedit"
PORTFMT="${ROOT}/portfmt"
PORTSCAN="${ROOT}/portscan"
: ${SH:=/bin/sh}

export PORTCLIPPY
export PORTEDIT
export PORTFMT
export PORTFMT_PLUGIN_PATH
export PORTSCAN
export ROOT

tests_failed=0
tests_run=0

cd "${ROOT}/tests/parser" || exit 1
for test in *.sh; do
	echo "parser/${test%*.sh}"
	if ! sh -eu "${test}"; then
		tests_run=$((tests_run + 1))
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
done

cd "${ROOT}/tests/format" || exit 1
for test in *.in; do
	t=${test%*.in}
	echo "format/${t}#1"
	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.in" >"${t}.actual"; then
		if ! diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
			echo
			tests_failed=$((tests_failed + 1))
			continue
		fi
	else
		echo
		tests_failed=$((tests_failed + 1))
		continue
	fi
	echo "format/${t}#2"
	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.expected" >"${t}.actual2"; then
		if ! diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual2"; then
			echo
			tests_failed=$((tests_failed + 1))
		fi
	else
		echo
		tests_failed=$((tests_failed + 1))
	fi
done
rm -f ./*.actual ./*.actual2

cd "${ROOT}/tests/edit" || exit 1
for test in bump-epoch/*.sh bump-revision/*.sh get/*.sh merge/*.sh set-version/*.sh; do
	t=${test%*.sh}
	echo "${t}"
	tests_run=$((tests_run + 1))
	cd "${ROOT}/tests/edit/$(dirname "${test}")" || exit 1
	if ! ${SH} -o pipefail -eu "$(basename "${test}")"; then
		tests_failed=$((tests_failed + 1))
	fi
	echo "${t}"
done

cd "${ROOT}/tests/clippy" || exit 1
for test in *.in; do
	t=${test%*.in}
	echo "clippy/${t}"
	tests_run=$((tests_run + 1))
	${PORTCLIPPY} "${t}.in" >"${t}.actual"
	if ! diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
		echo
		tests_failed=$((tests_failed + 1))
		continue
	fi
done
rm -f ./*.actual

cd "${ROOT}/tests" || exit 1
for t in reject/*.in; do
	echo "${t%*.in}"
	tests_run=$((tests_run + 1))
	if ${PORTFMT} "${t}" 2>&1; then
		echo "${t} not rejected!"
		echo
		tests_failed=$((tests_failed + 1))
	else
		echo "error ok"
	fi
done

cd "${ROOT}/tests/portscan" || exit 1
for t in *.sh; do
	echo "portscan/${t%*.sh}"
	tests_run=$((tests_run + 1))
	if ! ${SH} -o pipefail -eu "${t}" 2>&1; then
		tests_failed=$((tests_failed + 1))
	fi
done

printf "fail: %s ok: %s/%s\n" "${tests_failed}" "$((tests_run - tests_failed))" "${tests_run}"
if [ "${tests_failed}" -gt 0 ]; then
	exit 1
fi
