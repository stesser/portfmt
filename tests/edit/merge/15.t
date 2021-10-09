cat <<EOD | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
GH_TAGNAME=	ababab
EOD
<<<<<<<<<
USES=		autoreconf libtool pkgconfig

USE_LDCONFIG=	yes

USE_GITHUB=	yes
GH_ACCOUNT=	foo
<<<<<<<<<
USES=		autoreconf libtool pkgconfig

USE_LDCONFIG=	yes

USE_GITHUB=	yes
GH_ACCOUNT=	foo
GH_TAGNAME=	ababab
