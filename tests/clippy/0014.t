OPTIONS_DEFINE=FOO
post-install-FOO-off:
post-install-FOO-on:
post-install:
do-patch:

.include <bsd.port.mk>
<<<<<<<<<
# Out of order targets
+do-patch:
+post-install:
+post-install-FOO-on:
post-install-FOO-off:
-post-install-FOO-on:
-post-install:
-do-patch:
