${PORTEDIT} bump-revision $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
PORTNAME=	sc3-plugins
DISTVERSIONPREFIX=	Version-
DISTVERSION=	3.9.0
PORTREVISION=	7
CATEGORIES=	audio

CMAKE_ARGS+=	-DSYSTEM_STK:BOOL=ON
CMAKE_ARGS+=	-DSC_PATH:STRING=${LOCALBASE}/include/SuperCollider/ # see https://github.com/supercollider/sc3-plugins/issues/170
<<<<<<<<<
PORTNAME=	sc3-plugins
DISTVERSIONPREFIX=	Version-
DISTVERSION=	3.9.0
PORTREVISION=	8
CATEGORIES=	audio

CMAKE_ARGS+=	-DSYSTEM_STK:BOOL=ON
CMAKE_ARGS+=	-DSC_PATH:STRING=${LOCALBASE}/include/SuperCollider/ # see https://github.com/supercollider/sc3-plugins/issues/170
