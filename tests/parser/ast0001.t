PORTNAME=llvm90
.   for option a in CLANG COMPILER_RT EXTRAS LLD LLDB OPENMP
WRKSRC_${option:tl}_$a=	${WRKDIR}/${${option}_DISTFILES}
. endfor
FOO=meh

. include <bsd.port.mk>
<<<<<<<<<
VARIABLE :line 1 :name "PORTNAME" :modifier = :words [1]["llvm90"]
FOR :line 2 :indent 3 :bindings [2]["option" "a"] :words [6]["CLANG" "COMPILER_RT" "EXTRAS" "LLD" "LLDB" "OPENMP"]
	VARIABLE :line 3 :name "WRKSRC_${option:tl}_$a" :modifier = :words [1]["${WRKDIR}/${${option}_DISTFILES}"]
VARIABLE :line 5 :name "FOO" :modifier = :words [1]["meh"]
COMMENT :line 6 :comment ""
INCLUDE/BMAKE :line 7 :indent 1 :path "bsd.port.mk" :sys 1 :loaded 0
