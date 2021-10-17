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
IF/IF :line 1 :indent 0 :test [1]["1"] :elseif 0
=> if:
	IF/IF :line 2 :indent 0 :test [1]["2"] :elseif 0
	=> if:
		TARGET/NAMED :line 3 :sources [1]["foo"]
			IF/IF :line 4 :indent 0 :test [1]["3"] :elseif 0
			=> if:
				TARGET_COMMAND :line 5 :words [1]["adflasdf"]
	TARGET/NAMED :line 8 :sources [1]["bar"]
