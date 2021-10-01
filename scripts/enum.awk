BEGIN {
	buf = sprintf("// Generated file. Do not edit.\n\n#include \"config.h\"\n#include <stdio.h>\n\n#include <libias/flow.h>\n")
	filename = ""
	error = 0
}

function err(line, msg) {
	error = 1
	printf("%s:%s: %s\n", filename, line, msg) >"/dev/stderr"
	exit(1)
}

function print_enum(key,		keycnt, i) {
	# Only continue if all values have the key
	if (key) {
		keycnt = 0
		for (i = 1; i <= enum_len; i++) {
			if (enum[i, key]) {
				keycnt++
			}
		}
		if (keycnt == 0) {
			return
		} else if (keycnt != enum_len) {
			err(enum[1, "line"], sprintf("some but not all values in %s have a '%s' key", name, key))
		}
	}

	buf = buf sprintf("\nconst char *\n%s_%s(enum %s value)\n{\n\tswitch (value) {\n", name, key, name)
	for (i = 1; i <= enum_len; i++) {
		if (!enum[i, key]) {
			err(enum[i, "line"], sprintf("missing '%s' for %s in %s", key, enum[i], name))
		}
		if (enum[i, "#if"]) {
			buf = buf sprintf("#if %s\n", enum[i, "#if"])
		}
		buf = buf sprintf("\tcase %s: return \"%s\";\n", enum[i], enum[i, key]);
		if (enum[i, "#if"]) {
			buf = buf sprintf("#endif\n")
		}
	}
	buf = buf sprintf("\t}\n\n\tpanic(\"invalid %s value\");\n}\n", name)
}

function parse_string(key, n,		i) {
	$n = substr($n, length(key) + 1 + 1)
	if ($n ~ /"$/) {
		i = n + 1
	} else {
		for (i = n + 1; i <= NF; i++) {
			$n = $n " " $i
			if ($i ~ /"$/) {
				i++
				break
			}
		}
	}
	gsub(/(^"|"$)/, "", $n)
	return i
}

function reset() {
	if (filename != FILENAME) {
		buf = buf sprintf("\n#include \"%s\"\n", FILENAME)
		filename = FILENAME
	}
	if (in_enum) {
		print_enum("tostring");
		print_enum("human");
		print_enum("identifier");
	}
	delete enum
	name = ""
	in_enum = 0
	enum_len = 0
}

in_enum && $1 == "#if" {
	enum[enum_len + 1, "#if"] = $2
}

$1 == "#if" || $1 == "#endif" || $1 ~ "^//" || $1 == "^/\*" {
	next
}

$1 == "enum" && $3 == "{" {
	if (in_enum) {
		reset()
	}
	in_enum = 1
	name = $2
	next
}

in_enum && $1 == "};" {
	reset()
}

in_enum {
	enum[++enum_len] = $1
	sub(/,$/, "", enum[enum_len])
	enum[enum_len, "tostring"] = enum[enum_len]
	enum[enum_len, "line"] = sprintf("%d:%d", FNR, length($0))
	if ($2 == "//") {
		for (i = 3; i <= NF;) {
			n = i
			if ($i ~ /^identifier:/) {
				i = parse_string("identifier", i)
				enum[enum_len, "identifier"] = $n
			} else if ($i ~ /^human:/) {
				i = parse_string("human", i)
				enum[enum_len, "human"] = $n
			} else {
				err(enum[enum_len, "line"], sprintf("unknown key %s", $i))
			}
		}
	}
}

END {
	if (!error) {
		reset()
		printf("%s", buf)
	}
}
