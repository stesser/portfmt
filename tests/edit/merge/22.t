${PORTEDIT} merge -e 'RUN_DEPENDS+=foo' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
RUN_DEPENDS=	${PYTHON_PKGNAMEPREFIX}boto>0:devel/py-boto@${PY_FLAVOR}
.if ${FLAVOR:Upy36:Mpy3*}
RUN_DEPENDS+=	${PYTHON_PKGNAMEPREFIX}distro>0:sysutils/py-distro@${PY_FLAVOR}
.endif
.include <bsd.port.mk>
<<<<<<<<<
RUN_DEPENDS=	${PYTHON_PKGNAMEPREFIX}boto>0:devel/py-boto@${PY_FLAVOR} \
		foo
.if ${FLAVOR:Upy36:Mpy3*}
RUN_DEPENDS+=	${PYTHON_PKGNAMEPREFIX}distro>0:sysutils/py-distro@${PY_FLAVOR}
.endif
.include <bsd.port.mk>
