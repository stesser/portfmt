.if 1
.elif 1
.else
A=
.endif
<<<<<<<<<
IF/IF :line 1 :indent 0 :test [1]["1"] :elseif 0
=> else:
	IF/IF :line 2 :indent 0 :test [1]["1"] :elseif 1
	=> else:
		IF/ELSE :line 3 :indent 0 :elseif 1
		=> if:
			VARIABLE :line 4 :name "A" :modifier =
