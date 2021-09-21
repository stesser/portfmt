echo ".if 1" | ${PORTFMT} -d 2>&1 | diff -L "0010.expected" -L "0010.actual" -u 0010.expected -
