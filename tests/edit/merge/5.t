printf "PORTNAME+=a b" | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
PORTNAME=
<<<<<<<<<
PORTNAME=	a b
