${WRKDIR}/.ccache: ${WRKDIR}
	@${LN} -sf ${CCACHE_DIR} ${WRKDIR}/.ccache
ccache-wrkdir-link: ${WRKDIR}/.ccache .PHONY
post-extract: ccache-wrkdir-link
<<<<<<<<<
${WRKDIR}/.ccache: ${WRKDIR}
	@${LN} -sf ${CCACHE_DIR} ${WRKDIR}/.ccache
ccache-wrkdir-link: ${WRKDIR}/.ccache .PHONY
post-extract: ccache-wrkdir-link
