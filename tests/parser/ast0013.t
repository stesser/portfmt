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
	{ TARGET_COMMAND, line 2, .words = { a }, .flags = @ }
	{ TARGET_COMMAND, line 3, .words = { b }, .flags = - }
	{ TARGET_COMMAND, line 4, .words = { c }, .flags = + }
	{ TARGET_COMMAND, line 5, .words = { d }, .flags = @- }
	{ TARGET_COMMAND, line 6, .words = { i }, .flags = @+ }
	{ TARGET_COMMAND, line 7, .words = { e }, .flags = @- }
	{ TARGET_COMMAND, line 8, .words = { f }, .flags = @+ }
	{ TARGET_COMMAND, line 9, .words = {  }, .flags = -+ }
	{ TARGET_COMMAND, line 10, .words = {  }, .flags = @ }
	{ TARGET_COMMAND, line 11, .words = { i }, .flags = + }
	{ TARGET_COMMAND, line 12, .words = { j }, .flags = - }
	{ TARGET_COMMAND, line 13, .words = {  }, .flags = @-+ }
	{ TARGET_COMMAND, line 14, .words = { g }, .flags = @-+ }
	{ TARGET_COMMAND, line 15, .words = {  }, .flags = @-+ }
	{ TARGET_COMMAND, line 16, .words = { i }, .flags = @-+ }
	{ TARGET_COMMAND, line 17, .words = {  }, .flags = @-+ }
	{ TARGET_COMMAND, line 18, .words = { k }, .flags = @-+ }
