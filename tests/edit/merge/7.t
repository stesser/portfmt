echo "PLIST_FILES=bin/cloak" | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
PORTNAME=	cloak

.include <bsd.port.mk>
<<<<<<<<<
PORTNAME=	cloak

PLIST_FILES=	bin/cloak

.include <bsd.port.mk>
