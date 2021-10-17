quiet-flags:
	@a
	-b
	+c
	@-d
	@+i
	-@e
	+@f
	-+
	@@
	++i
	--j
	@+-
	@-+g
	-@+
	-+@i
	+-@
	+@-k
<<<<<<<<<
{ TARGET/NAMED, line 1, .sources = { quiet-flags }, .dependencies = {  } }
	{ TARGET_COMMAND, line 2, .words = [1]{ a }, .flags = @ }
	{ TARGET_COMMAND, line 3, .words = [1]{ b }, .flags = - }
	{ TARGET_COMMAND, line 4, .words = [1]{ c }, .flags = + }
	{ TARGET_COMMAND, line 5, .words = [1]{ d }, .flags = @- }
	{ TARGET_COMMAND, line 6, .words = [1]{ i }, .flags = @+ }
	{ TARGET_COMMAND, line 7, .words = [1]{ e }, .flags = @- }
	{ TARGET_COMMAND, line 8, .words = [1]{ f }, .flags = @+ }
	{ TARGET_COMMAND, line 9, .words = [0]{}, .flags = -+ }
	{ TARGET_COMMAND, line 10, .words = [0]{}, .flags = @ }
	{ TARGET_COMMAND, line 11, .words = [1]{ i }, .flags = + }
	{ TARGET_COMMAND, line 12, .words = [1]{ j }, .flags = - }
	{ TARGET_COMMAND, line 13, .words = [0]{}, .flags = @-+ }
	{ TARGET_COMMAND, line 14, .words = [1]{ g }, .flags = @-+ }
	{ TARGET_COMMAND, line 15, .words = [0]{}, .flags = @-+ }
	{ TARGET_COMMAND, line 16, .words = [1]{ i }, .flags = @-+ }
	{ TARGET_COMMAND, line 17, .words = [0]{}, .flags = @-+ }
	{ TARGET_COMMAND, line 18, .words = [1]{ k }, .flags = @-+ }
