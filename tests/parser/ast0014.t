.if 1 # a
.elif 2 # b
.elif 3
.else # c
.endif
<<<<<<<<<
IF/IF :line 1 :indent 0 :test [1]["1"] :comment "# a" :elseif 0
=> else:
	IF/IF :line 2 :indent 0 :test [1]["2"] :comment "# b" :elseif 1
	=> else:
		IF/IF :line 3 :indent 0 :test [1]["3"] :elseif 1
		=> else:
			IF/ELSE :line 4 :indent 0 :comment "# c" :elseif 1
