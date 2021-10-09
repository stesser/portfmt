${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
GNU_CONFIGURE=	yes

.include <bsd.port.pre.mk>

.if ${CHOSEN_COMPILER_TYPE} == gcc
CXXFLAGS+=	-fpermissive
.endif

.include <bsd.port.post.mk>
<<<<<<<<<
GNU_CONFIGURE=	yes
LDFLAGS_i386=	-Wl,-znotext

.include <bsd.port.pre.mk>

.if ${CHOSEN_COMPILER_TYPE} == gcc
CXXFLAGS+=	-fpermissive
.endif

.include <bsd.port.post.mk>
