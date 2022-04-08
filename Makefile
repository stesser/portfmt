AWK?=		awk

all:

clean:
	@rm -r _build

tag:
	@[ -z "${V}" ] && echo "must set V" && exit 1; \
	date=$$(git log -1 --pretty=format:%cd --date=format:%Y-%m-%d HEAD); \
	title="## [${V}] - $${date}"; \
	if ! grep -Fq "$${title}" CHANGELOG.md; then \
		echo "# portfmt ${V}"; \
		${AWK} '/^## Unreleased$$/{x=1;next}x{if($$1=="##"){exit}else if($$1=="###"){$$1="##"};print}' \
			CHANGELOG.md >RELNOTES.md.new; \
		${AWK} "/^## Unreleased$$/{print;printf\"\n$${title}\n\";next}{print}" \
			CHANGELOG.md >CHANGELOG.md.new; \
		mv CHANGELOG.md.new CHANGELOG.md; \
		echo "portfmt ${V}" >RELNOTES.md; \
		cat RELNOTES.md.new >>RELNOTES.md; \
		rm -f RELNOTES.md.new; \
	fi; \
	git commit -m "Release ${V}" CHANGELOG.md; \
	git tag -F RELNOTES.md v${V}

release:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	V=$$(echo $${tag} | sed 's,^v,,'); \
	git ls-files --recurse-submodules . ':!:libias/tests' | \
		bsdtar --files-from=- -s ",^,portfmt-$${V}/," --options lzip:compression-level=9 \
			--uid 0 --gid 0 -caf portfmt-$${V}.tar.lz; \
	sha256 portfmt-$${V}.tar.lz >portfmt-$${V}.tar.lz.SHA256 || \
	sha256sum --tag portfmt-$${V}.tar.lz >portfmt-$${V}.tar.lz.SHA256; \
	printf "SIZE (%s) = %s\n" portfmt-$${V}.tar.lz $$(wc -c <portfmt-$${V}.tar.lz) \
		>>portfmt-$${V}.tar.lz.SHA256

publish:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	V=$$(echo $${tag} | sed 's,^v,,'); \
	git push --follow-tags github; \
	gh release create $${tag} -F RELNOTES.md \
		portfmt-$${V}.tar.lz \
		portfmt-$${V}.tar.lz.SHA256

.PHONY: all clean publish release tag
