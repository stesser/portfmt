MAINTAINER=
COMMENT=
COMMENT.sndio=	
COMMENT.gdbm=

SUBPACKAGES=	gitweb sndio gdbm

OPTIONS_DEFINE=	GITWEB SNDIO

GITWEB_IMPLIES.gitweb=
GITWEB_RUN_DEPENDS.gitweb=
GITWEB_RUN_DEPENDS=
GITWEB_BUILD_DEPENDS.sndio=
GITWEB_BUILD_DEPENDS.gitweb=
GITWEB_BUILD_DEPENDS=
GITWEB_SUBPACKAGES=

SNDIO_LIB_DEPENDS.sndio=

.include <bsd.port.mk>
<<<<<<<<<
# Maintainer block
MAINTAINER
COMMENT
+COMMENT.gdbm
COMMENT.sndio
-COMMENT.gdbm

# Subpackages block
SUBPACKAGES

# Options definitions
OPTIONS_DEFINE

-GITWEB_IMPLIES.gitweb

# Options helpers
+GITWEB_SUBPACKAGES
+GITWEB_BUILD_DEPENDS
+GITWEB_BUILD_DEPENDS.gitweb
+GITWEB_BUILD_DEPENDS.sndio
+GITWEB_RUN_DEPENDS
GITWEB_RUN_DEPENDS.gitweb
-GITWEB_RUN_DEPENDS
-GITWEB_BUILD_DEPENDS.sndio
-GITWEB_BUILD_DEPENDS.gitweb
-GITWEB_BUILD_DEPENDS
-GITWEB_SUBPACKAGES
SNDIO_LIB_DEPENDS.sndio

# Unknown variables
# WARNING:
# The following variables were not recognized.
# They could just be typos or Portclippy needs to be made aware of them.
# Please double check them.
#
# Prefix them with an _ or wrap in '.ifnmake portclippy' to tell
# Portclippy to ignore them.
#
# If in doubt please report this on portfmt's bug tracker:
# https://github.com/t6/portfmt/issues
+GITWEB_IMPLIES.gitweb
