GNU_CONFIGURE=	yes
CONFIGURE_ARGS=	--disable-build-details \
		--localstatedir=/var \
		--without-gpm
.if ${FLAVOR:U} == canna
CONFIGURE_ARGS+=	--with-canna
.endif
<<<<<<<<<
GNU_CONFIGURE=		yes
CONFIGURE_ARGS=		--disable-build-details \
			--localstatedir=/var \
			--without-gpm
.if ${FLAVOR:U} == canna
CONFIGURE_ARGS+=	--with-canna
.endif
