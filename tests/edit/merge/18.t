${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
USE_SDL=	sdl2
USE_CXXSTD=	c++98
<<<<<<<<<
USE_SDL=	sdl2
USE_CXXSTD=	c++98
LDFLAGS_i386=	-Wl,-znotext
