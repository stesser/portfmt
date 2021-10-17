.if 1
.for a in 1 2
FOO=bar
.endfor
.for a in 1 2
FOO=bar
.endfor
.for a in 1 2
FOO=bar
.endfor
.else
MUH=muh
.endif
<<<<<<<<<
{ IF/IF, line 1, .indent = 0, .test = { 1 }, .elseif = 0 }
=> if:
	{ FOR, line 2, .indent = 0, .bindings = { a }, .words = [2]{ 1, 2 } }
		{ VARIABLE, line 3, .name = FOO, .modifier = =, .words = [1]{ bar } }
	{ FOR, line 5, .indent = 0, .bindings = { a }, .words = [2]{ 1, 2 } }
		{ VARIABLE, line 6, .name = FOO, .modifier = =, .words = [1]{ bar } }
	{ FOR, line 8, .indent = 0, .bindings = { a }, .words = [2]{ 1, 2 } }
		{ VARIABLE, line 9, .name = FOO, .modifier = =, .words = [1]{ bar } }
=> else:
	{ IF/ELSE, line 11, .indent = 0, .test = {  }, .elseif = 1 }
	=> if:
		{ VARIABLE, line 12, .name = MUH, .modifier = =, .words = [1]{ muh } }
