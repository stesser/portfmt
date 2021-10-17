.if 1
.else
FOO=fo
.endif
<<<<<<<<<
{ IF/IF, line 1, .indent = 0, .test = { 1 }, .elseif = 0 }
=> else:
	{ IF/ELSE, line 2, .indent = 0, .test = {  }, .elseif = 1 }
	=> if:
		{ VARIABLE, line 3, .name = FOO, .modifier = =, .words = [1]{ fo } }
