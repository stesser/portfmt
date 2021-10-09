${PORTEDIT} merge -e 'PATCHFILES+=foo # asf' -e 'COMMENT+=foo' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
PATCHFILES+=	91076a814ff2.patch:-p1 # https://github.com/yuzu-emu/yuzu/pull/4836
PATCHFILES+=	ec7337d1da05.patch:-p1 # https://github.com/yuzu-emu/yuzu/pull/5896

COMMENT=	blabla # asdfasd
<<<<<<<<<
PATCHFILES+=	91076a814ff2.patch:-p1 # https://github.com/yuzu-emu/yuzu/pull/4836
PATCHFILES+=	ec7337d1da05.patch:-p1 # https://github.com/yuzu-emu/yuzu/pull/5896
PATCHFILES+=	foo # asf

COMMENT=	blabla # asdfasd
COMMENT+=	foo
