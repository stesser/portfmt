.if 1
.if 2
foo:
.if 3
	adflasdf
.endif
.endif
bar:
.endif
<<<<<<<<<
{ IF/IF, line 1, .indent = 0, .test = { 1 }, .elseif = 0 }
=> if:
	{ IF/IF, line 2, .indent = 0, .test = { 2 }, .elseif = 0 }
	=> if:
		{ TARGET/NAMED, line 3, .sources = { foo }, .dependencies = {  } }
			{ IF/IF, line 4, .indent = 0, .test = { 3 }, .elseif = 0 }
			=> if:
				{ TARGET_COMMAND, line 5, .words = [1]{ adflasdf } }
	{ TARGET/NAMED, line 8, .sources = { bar }, .dependencies = {  } }
