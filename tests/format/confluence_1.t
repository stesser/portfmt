LIB_DEPENDS=	libsndio.so:audio/sndio libcurl.so:ftp/curl \
	libABC.so:asfd/asdfa \
	libX11.so:x11/xlib
USES+=	a b c d f e h z a   6 87  3 3 5 67 8 9 9  \
	345345 3453 \
	3 \
	sdf \
	gsd

CONFIGURE_ARGS=	prefix=${PREFIX} mandir=${MANPREFIX}/man \
		exampledir=${EXAMPLESDIR}

OPTIONS_GROUP_IN=	AAC CDDB CDIO CUE DISCID FFMPEG FLAC MAD MIKMOD	\
			MODPLUG MP4 MUSEPACK OPUS SAMPLERATE TREMOR	\
			VORBIS WAV WAVPACK
<<<<<<<<<
LIB_DEPENDS=	libABC.so:asfd/asdfa \
		libcurl.so:ftp/curl \
		libsndio.so:audio/sndio \
		libX11.so:x11/xlib
USES+=		3 3453 345345 5 6 67 8 87 9 a b c d e f gsd h sdf z

CONFIGURE_ARGS=	exampledir=${EXAMPLESDIR} \
		mandir=${MANPREFIX}/man \
		prefix=${PREFIX}

OPTIONS_GROUP_IN=	AAC CDDB CDIO CUE DISCID FFMPEG FLAC MAD MIKMOD MODPLUG \
			MP4 MUSEPACK OPUS SAMPLERATE TREMOR VORBIS WAV WAVPACK
