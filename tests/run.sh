#!/bin/sh
set -u
ROOT="${PWD}"
LOG="${ROOT}/tests/log"
PORTCLIPPY="${ROOT}/bin/portclippy"
PORTEDIT="${ROOT}/bin/portedit"
PORTFMT="${ROOT}/bin/portfmt"
PORTSCAN="${ROOT}/bin/portscan"
: ${SH:=/bin/sh}

export PORTCLIPPY
export PORTEDIT
export PORTFMT
export PORTSCAN
export ROOT

tests_failed=0
tests_run=0

exec 3>&1
exec >"${LOG}"
exec 2>&1

printf "\n  parser: " >&3
for t in tests/parser/*.sh; do
	if actual="${t}.actual" input="${t}.in" expected="${t}.expected" ${SH} -o pipefail -eu "${t}"; then
		printf . >&3
	else
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_run=$((tests_run + 1))
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
done
for t in tests/parser/ast*.t tests/parser/token*.t; do
	if ! ${ROOT}/tests/split_test "${t}"; then
		printf X >&3
		echo "${t}:1:1: split_test failed" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
	args=""
	end="false"
	case "$(basename "${t}")" in
	ast_err*) args="-ddd"; end="true" ;;
	ast*) args="-ddd" ;;
	token*) args="-dd" ;;
	esac
	if ${PORTFMT} ${args} <"${t}.in" >"${t}.actual" 2>&1 || ${end}; then
		if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
			printf . >&3
		else
			printf X >&3
			echo "${t}:1:1: FAIL" >&2
			tests_failed=$((tests_failed + 1))
			continue
		fi
	else
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
done
rm -f tests/parser/*.t.actual tests/parser/*.t.expected tests/parser/*.t.in

printf "\n  format: " >&3
for t in tests/format/*.t; do
	if ! ${ROOT}/tests/split_test "${t}"; then
		printf X >&3
		echo "${t}:1:1: split_test failed" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.in" >"${t}.actual"; then
		if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
			printf . >&3
		else
			printf X >&3
			echo "${t}:1:1: FAIL #1" >&2
			tests_failed=$((tests_failed + 1))
			continue
		fi
	else
		printf X >&3
		echo "${t}:1:1: FAIL #1" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi

	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.expected" >"${t}.actual2"; then
		if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual2"; then
			printf . >&3
		else
			printf X >&3
			echo "${t}:1:1: FAIL #2" >&2
			tests_failed=$((tests_failed + 1))
		fi
	else
		printf X >&3
		echo "${t}:1:1: FAIL #2" >&2
		tests_failed=$((tests_failed + 1))
	fi
done
rm -f tests/format/*.t.actual tests/format/*.t.actual2 tests/format/*.t.in tests/format/*.t.expected

printf "\n  edit: " >&3
for t in $(cd "${ROOT}"; find tests/edit -name '*.t' | sort); do
	if ! ${ROOT}/tests/split_test "${t}"; then
		printf X >&3
		echo "${t}:1:1: split_test failed" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
	if actual="${t}.actual" input="${t}.in" expected="${t}.expected" ${SH} -o pipefail -eu "${t}.sh"; then
		printf . >&3
	else
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	fi
	rm -f "${t}.in" "${t}.expected" "${t}.sh"
done

printf "\n  clippy: " >&3
for t in tests/clippy/*.t; do
	if ! ${ROOT}/tests/split_test "${t}"; then
		printf X >&3
		echo "${t}:1:1: split_test failed" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
	${PORTCLIPPY} "${t}.in" >"${t}.actual"
	if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
		printf . >&3
	else
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
done
rm -f tests/clippy/*.t.actual tests/clippy/*.t.expected tests/clippy/*.t.in

printf "\n  reject: " >&3
for t in tests/reject/*.in; do
	tests_run=$((tests_run + 1))
	if ${PORTFMT} "${t}"; then
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	else
		printf . >&3
	fi
done

printf "\n  portscan: " >&3
for t in tests/portscan/*.sh; do
	tests_run=$((tests_run + 1))
	if (cd "${ROOT}/tests/portscan"; ${SH} -o pipefail -eu "$(basename "${t}")"); then
		printf . >&3
	else
		printf X >&3
		echo "${t}:1:1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	fi
done

echo >&3
if [ "${tests_failed}" -gt 0 ]; then
	cat "${LOG}" >&3
	rm -f "${LOG}"
	printf "  fail: %s ok: %s/%s\n" "${tests_failed}" "$((tests_run - tests_failed))" "${tests_run}" >&3
	exit 1
else
	rm -f "${LOG}"
	printf "  fail: %s ok: %s/%s\n" "${tests_failed}" "$((tests_run - tests_failed))" "${tests_run}" >&3
	exit 0
fi
