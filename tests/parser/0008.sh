echo ".endfor" | ${PORTFMT} -d 2>&1 | diff -L "0008.expected" -L "0008.actual" -u 0008.expected -
