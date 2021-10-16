USES=		shebangfix
sh_OLD_CMD=	foo
sh_CMD=		foo

.include <bsd.port.mk>
<<<<<<<<<
# USES block
USES

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
+sh_CMD
sh_OLD_CMD
-sh_CMD
