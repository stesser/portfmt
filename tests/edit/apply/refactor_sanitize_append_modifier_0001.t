${PORTEDIT} apply refactor.sanitize-append-modifier $input | \
	diff -L $expected -L $actual -u $expected -
<<<<<<<<<
USES+=a
.if 1
USES+=b
.endif
USES+=c
.include <bsd.port.options.mk>
USES+=a
USES+=b
USES+=c
<<<<<<<<<
USES=		a
.if 1
USES+=		b
.endif
USES+=		c
.include <bsd.port.options.mk>
USES+=a
USES+=b
USES+=c
