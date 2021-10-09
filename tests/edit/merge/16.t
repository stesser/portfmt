${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
LDFLAGS+=	-L${FOO}
DOS2UNIX_FILES=	foo
<<<<<<<<<
LDFLAGS+=	-L${FOO}
LDFLAGS_i386=	-Wl,-znotext
DOS2UNIX_FILES=	foo
