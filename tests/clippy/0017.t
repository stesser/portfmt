.if defined(DEVELOPER)
A=
.elif defined(MAINTAINER_MODE)
B=
.elif make(makesum)
C=
.endif
.include <bsd.port.mk>
<<<<<<<<<
