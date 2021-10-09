printf "PORTNAME=tokei\nUSES=gmake\nCARGO_CRATES=\nCARGO_USE_GITHUB=\n" | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
PORTNAME=	sndio

USERS=		_sndio
<<<<<<<<<
PORTNAME=	tokei

USES=		gmake
CARGO_CRATES=
CARGO_USE_GITHUB=

USERS=		_sndio
