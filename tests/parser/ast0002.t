.if 1
PORTSCOUT=	foo
.if 4
.endif
.   elifdef 2
PORTSCOUT=	bar
.else
YEP=	i'm the best
.endif

#foo
foo:: meh meh
	asldfjalsdfj
.ifdef FOO
bar:
	slkdjfalsdkf
.endif
<<<<<<<<<
IF/IF :line 1 :indent 0 :test [1]["1"] :elseif 0
=> if:
	VARIABLE :line 2 :name "PORTSCOUT" :modifier = :words [1]["foo"]
	IF/IF :line 3 :indent 0 :test [1]["4"] :elseif 0
=> else:
	IF/DEF :line 5 :indent 3 :test [1]["2"] :elseif 1
	=> if:
		VARIABLE :line 6 :name "PORTSCOUT" :modifier = :words [1]["bar"]
	=> else:
		IF/ELSE :line 7 :indent 0 :elseif 1
		=> if:
			VARIABLE :line 8 :name "YEP" :modifier = :words [1]["i'm the best"]
COMMENT :lines [10,12) :comment "\n#foo"
TARGET/NAMED :line 12 :sources [1]["foo:"] :dependencies [2]["meh" "meh"]
	TARGET_COMMAND :line 13 :words [1]["asldfjalsdfj"]
	IF/DEF :line 14 :indent 0 :test [1]["FOO"] :elseif 0
	=> if:
		TARGET/NAMED :line 15 :sources [1]["bar"]
			TARGET_COMMAND :line 16 :words [1]["slkdjfalsdkf"]
