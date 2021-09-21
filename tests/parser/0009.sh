echo ".endif" | ${PORTFMT} -d 2>&1 | diff -L "0009.expected" -L "0009.actual" -u 0009.expected -
