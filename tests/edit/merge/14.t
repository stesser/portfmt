cat <<EOD | ${PORTEDIT} merge $input | diff -L $expected -L $actual -u $expected -
DISTVERSIONSUFFIX=	-gababab
DISTVERSION=	1
DISTVERSIONPREFIX=	v
EOD
<<<<<<<<<
<<<<<<<<<
DISTVERSIONPREFIX=	v
DISTVERSION=	1
DISTVERSIONSUFFIX=	-gababab
