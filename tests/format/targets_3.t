post-install:
.if ${PORT_OPTIONS:MBHYVE}
.if !exists(${SRC_BASE}/usr.sbin/bhyve/pci_fbuf.c)
IGNORE=	requires kernel source files in ${SRC_BASE}
.else
	(cd ${WRKSRC} && ${MAKE_CMD} bhyve-patch)
.endif
.endif
<<<<<<<<<
post-install:
.if ${PORT_OPTIONS:MBHYVE}
.if !exists(${SRC_BASE}/usr.sbin/bhyve/pci_fbuf.c)
IGNORE=		requires kernel source files in ${SRC_BASE}
.else
	(cd ${WRKSRC} && \
		${MAKE_CMD} bhyve-patch)
.endif
.endif
