.if 0
.    for deptype in ${_OPTIONS_DEPENDS}
.      if defined(${opt}_${deptype}_DEPENDS)
${deptype}_DEPENDS+=	${${opt}_${deptype}_DEPENDS}
.      endif
.    endfor
.else
FOO=asd
.endif
<<<<<<<<<
{ IF/IF, line 1, .indent = 0, .test = { 0 }, .elseif = 0 }
=> if:
	{ FOR, line 2, .indent = 4, .bindings = { deptype }, .words = [1]{ ${_OPTIONS_DEPENDS} } }
		{ IF/IF, line 3, .indent = 6, .test = { defined(, ${opt}_${deptype}_DEPENDS, ) }, .elseif = 0 }
		=> if:
			{ VARIABLE, line 4, .name = ${deptype}_DEPENDS, .modifier = +=, .words = [1]{ ${${opt}_${deptype}_DEPENDS} } }
=> else:
	{ IF/ELSE, line 7, .indent = 0, .test = {  }, .elseif = 1 }
	=> if:
		{ VARIABLE, line 8, .name = FOO, .modifier = =, .words = [1]{ asd } }
