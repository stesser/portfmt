${PORTEDIT} merge -e 'IGNORE=foo' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
.if ${OPSYS} != FreeBSD
IGNORE=	is only for FreeBSD
.elif ${OSVERSION} < 1200502
IGNORE=	will not build on 12.0 due to old toolchain; 11.x untested
.endif
.if ${FLAVOR} == powerpc64le && ${OSVERSION} < 1300116
IGNORE=	will not build on 12.x due to old system
.endif
.include <bsd.port.mk>
<<<<<<<<<
IGNORE=		foo

.if ${OPSYS} != FreeBSD
IGNORE=	is only for FreeBSD
.elif ${OSVERSION} < 1200502
IGNORE=	will not build on 12.0 due to old toolchain; 11.x untested
.endif
.if ${FLAVOR} == powerpc64le && ${OSVERSION} < 1300116
IGNORE=	will not build on 12.x due to old system
.endif
.include <bsd.port.mk>
