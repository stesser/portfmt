OPTIONS_DEFINE=		PGSQL_SERVER A
A_VARS_OFF=		WANT_PGSQL=""
PGSQL_SERVER_USE=	GNOME=vte3
PGSQL_SERVER_VARS=	WANT_PGSQL+="server contrib" _IGNORED=bar WHATSTHIS=""

.include <bsd.port.mk>
<<<<<<<<<
# Options definitions
OPTIONS_DEFINE

# Options helpers
A_VARS_OFF
PGSQL_SERVER_USE
PGSQL_SERVER_VARS

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

# Unknown variables in options helpers
USE_GNOME      in PGSQL_SERVER_USE
               missing USES=gnome ?
WANT_PGSQL     in A_VARS_OFF
               in PGSQL_SERVER_VARS
               missing USES=pgsql ?
WHATSTHIS      in PGSQL_SERVER_VARS
