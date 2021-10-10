do-configure:
	@cd ${CONFIGURE_WRKSRC}/${platform} && ${SETENV} ${CONFIGURE_ENV} \
	${CMAKE_BIN} ${CMAKE_ARGS} ${KODI_${platform}_ARGS} \
	${CMAKE_SOURCE_PATH}
<<<<<<<<<
do-configure:
	@cd ${CONFIGURE_WRKSRC}/${platform} && \
		${SETENV} ${CONFIGURE_ENV} ${CMAKE_BIN} ${CMAKE_ARGS} \
		${KODI_${platform}_ARGS} ${CMAKE_SOURCE_PATH}
