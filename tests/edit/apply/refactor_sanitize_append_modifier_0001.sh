${PORTEDIT} apply refactor.sanitize-append-modifier refactor_sanitize_append_modifier_0001.in | \
	diff -L refactor_sanitize_append_modifier_0001.expected -L refactor_sanitize_append_modifier_0001.actual \
		-u refactor_sanitize_append_modifier_0001.expected -
