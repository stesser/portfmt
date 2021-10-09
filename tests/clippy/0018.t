.if defined(DEVELOPER)
a:
.elif defined(MAINTAINER_MODE)
b:
.elif make(makesum)
c:
.endif
.include <bsd.port.mk>
<<<<<<<<<
