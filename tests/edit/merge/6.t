echo "PLIST_FILES=bin/cloak" | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
.include <bsd.port.mk>
<<<<<<<<<
PLIST_FILES=	bin/cloak

.include <bsd.port.mk>
