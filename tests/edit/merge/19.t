${PORTEDIT} merge -e 'LLD_UNSAFE!=' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
USE_GL=		gl glu

LLD_UNSAFE=	yes

MAKE_ARGS=	CC="${CC}" CXX="${CXX}"
<<<<<<<<<
USE_GL=		gl glu

MAKE_ARGS=	CC="${CC}" CXX="${CXX}"
