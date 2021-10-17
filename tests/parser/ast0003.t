.if 1
fo=
.elifndef 2
.endif
<<<<<<<<<
IF/IF :line 1 :indent 0 :test [1]["1"] :elseif 0
=> if:
	VARIABLE :line 2 :name "fo" :modifier =
=> else:
	IF/NDEF :line 3 :indent 0 :test [1]["2"] :elseif 1
