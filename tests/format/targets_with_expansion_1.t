${target}:
	@${IGNORECMD}

.if !target(${stage}-${name}-script)
.if exists(${SCRIPTDIR}/${stage}-${name})
${stage}-${name}-script:
	@ cd ${.CURDIR} && ${SETENV} ${SCRIPTS_ENV} ${SH} \
		${SCRIPTDIR}/${.TARGET:S/-script$//}
.endif
.endif

${PKGFILE}: ${WRKDIR_PKGFILE} ${PKGREPOSITORY}
	@${LN} -f ${WRKDIR_PKGFILE} ${PKGFILE} 2>/dev/null \
		|| ${CP} -f ${WRKDIR_PKGFILE} ${PKGFILE}
<<<<<<<<<
${target}:
	@${IGNORECMD}

.if !target(${stage}-${name}-script)
.if exists(${SCRIPTDIR}/${stage}-${name})
${stage}-${name}-script:
	@cd ${.CURDIR} && \
		${SETENV} ${SCRIPTS_ENV} ${SH} ${SCRIPTDIR}/${.TARGET:S/-script$//}
.endif
.endif

${PKGFILE}: ${WRKDIR_PKGFILE} ${PKGREPOSITORY}
	@${LN} -f ${WRKDIR_PKGFILE} ${PKGFILE} 2>/dev/null || \
		${CP} -f ${WRKDIR_PKGFILE} ${PKGFILE}
