USES=			shebangfix
SHEBANG_LANG=		awk bltwish
awk_CMD=		foo
bltwish_OLD_CMD=	foo
bltwish_CMD=		foo
python_CMD=		foo
awk_OLD_CMD=		foo

.include <bsd.port.mk>
<<<<<<<<<
# USES block
USES

# USES=shebangfix related variables
SHEBANG_LANG
+awk_OLD_CMD
awk_CMD
bltwish_OLD_CMD
bltwish_CMD
python_CMD
-awk_OLD_CMD
