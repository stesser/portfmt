bla:
	@${REINPLACE_CMD} \
		-e 's,/usr/share/doc/cmus/examples,${EXAMPLESDIR},g' \
		-e 's,/usr/share/cmus,${DATADIR},g' \
		${WRKSRC}/Doc/cmus.txt

bla:
	@cd ${WRKSRC}/modules/web/src/main/native/Source/ThirdParty && ${RM} -r icu \
		libxml libxslt sqlite

bla:
	@cd ${WRKSRC}/modules/media/src/main/native/gstreamer/gstreamer-lite/gst-plugins-base/ext/bsdaudio && \
		${LN} -s gstsndio.c gstbsdaudio.c && \
		${LN} -s sndiosink.c bsdaudiosink.c
<<<<<<<<<
bla:
	@${REINPLACE_CMD} -e 's,/usr/share/doc/cmus/examples,${EXAMPLESDIR},g' \
		-e 's,/usr/share/cmus,${DATADIR},g' \
		${WRKSRC}/Doc/cmus.txt

bla:
	@cd ${WRKSRC}/modules/web/src/main/native/Source/ThirdParty && \
		${RM} -r icu libxml libxslt sqlite

bla:
	@cd ${WRKSRC}/modules/media/src/main/native/gstreamer/gstreamer-lite/gst-plugins-base/ext/bsdaudio && \
		${LN} -s gstsndio.c gstbsdaudio.c && \
		${LN} -s sndiosink.c bsdaudiosink.c
