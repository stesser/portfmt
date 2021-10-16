# Changelog

All notable changes to portfmt are documented in this file.
The sections should follow the order `Packaging`, `Added`, `Changed`, `Fixed` and `Removed`.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## Unreleased

### Changed

- General performance has been improved due to the following changes:
  - The underlying data structures have been changed to use an Abstract
    Syntax Tree instead of operating directly on the token stream from
    the tokenizer
  - The last regular expression in the tokenizer has been replaced with
    a manually written matcher
  - When possible the build now uses Link Time Optimization

  Benchmark on FreeBSD 13.0-RELEASE in Hyper-V on a i3-4130T on Windows 10
  with 2 cores:
```
$ hyperfine -w 2 -- './portscan-main-no-lto --progress=0' './portscan-main-lto --progress=0' ./portscan-1.0.0-no-lto ./portscan-1.0.0-lto
Benchmark #1: ./portscan-main-no-lto --progress=0
  Time (mean ± σ):     28.444 s ±  0.574 s    [User: 52.437 s, System: 1.133 s]
  Range (min … max):   27.887 s … 29.664 s    10 runs

Benchmark #2: ./portscan-main-lto --progress=0
  Time (mean ± σ):     22.771 s ±  0.195 s    [User: 41.234 s, System: 1.000 s]
  Range (min … max):   22.538 s … 23.171 s    10 runs

Benchmark #3: ./portscan-1.0.0-no-lto
  Time (mean ± σ):     108.637 s ±  0.500 s    [User: 209.864 s, System: 1.420 s]
  Range (min … max):   107.824 s … 109.260 s    10 runs

Benchmark #4: ./portscan-1.0.0-lto
  Time (mean ± σ):     103.229 s ±  2.668 s    [User: 199.575 s, System: 1.404 s]
  Range (min … max):   101.674 s … 110.710 s    10 runs

Summary
  './portscan-main-lto --progress=0' ran
    1.25 ± 0.03 times faster than './portscan-main-no-lto --progress=0'
    4.53 ± 0.12 times faster than './portscan-1.0.0-lto'
    4.77 ± 0.05 times faster than './portscan-1.0.0-no-lto'
```
- portscan: The progress report has been enabled by default with the
  interval shortened to 1 s when `stderr` is a TTY.  The output
  is also kept on only one line in that case.  `--progress=0`
  disables this.
- portscan: The `--all` option has been removed
- portclippy: Ignore blocks wrapped in `.ifnmake portclippy`

## [1.0.0] - 2021-09-01

### Added

- portscan: print progress reports on `SIGINFO` or `SIGUSR2` or in
  regular intervals when requested with `--progress`
- portscan: Report commented `PORTEPOCH` or `PORTREVISION` lines
  via new `lint.commented-portrevision` lint, selectable with
  `--comments`, enabled by default
- portedit: `apply list` will now print a list of available edits
- portclippy, portscan: Make sure ports have the required `USES`
  before accepting variables as "known"
- portclippy: provide hints which `USES` might be missing
- portclippy: Check `opt_USE` and `opt_VARS` for unknowns too
- portclippy: provide hints for wrong case variable misspellings, e.g.,
  for `license` it will suggest using `LICENSE` instead

### Changed

- Recognize more framework targets
- Catch up with FreeBSD Ports:
  - Remove support for `PKGUPGRADE` and friends
  - Add `USES=ansible`, `USES=cmake:testing` support
- portclippy, portscan: Report unknown target sources too
- portscan: default `-p` to `/usr/ports` or the value of `PORTSDIR` in the environment
- portedit, portfmt: Stop messing with inline comments.  This should let it
  deal better with the commonly used `PATCHFILES+=<commit>.patch # <pr>`
  pattern. This disables `refactor.sanitize-eol-comments` by default but it is
  still accessible with `portedit apply refactor.sanitize-eol-comments`.
- portedit: `merge` now tries to only append to the last variable in
  "`+=` groups".  Something like
  `portedit merge -e 'PATCHFILES+=deadbeef.patch:-p1 # https://github.com/t6/portfmt/pulls/1'`
  should work now as one would expect.
- portscan: Replaced `-o <check>` with `--<check>`; `-o <check>`
  will continue to work but is deprecated
- portedit merge: Ignore variables in conditionals
- portclippy, portscan: Add check to see if variables are referenced
  in the Makefile before reporting them as unknown.  This should
  reduce the number of false positive reports but can be disabled
  with the new `--strict` option

### Fixed

- portclippy, portscan: Do not report on targets defined in `POST_PLIST`
- Do not recognize false options helper targets like `makesum-OPT-on`
- Properly split target names and dependencies.  This improves
  overall reporting on targets in portclippy and portscan
- portfmt: Do not try to sort tokens in `*_CMD`
- portedit: print right usage for `apply`
- Ignore `NO_COLOR` when `CLICOLOR_FORCE` is set and force colors
  on per the FAQ on https://no-color.org/
- portclippy: check slave ports again.  The check if a Makefile
  is a FreeBSD Ports Makefile is flawed and is now also positive
  if `MASTERDIR` is set in it.
- portclippy, portscan: Look up `opt_USES_OFF` and `opt_VARS_OFF` too
- portedit set-version: Deal with `PORTREVISION?=` and reset it to 0
- portedit, portfmt: Ignore `-i` when `-D` was specified
- portedit bump-epoch: Reset `PORTREVISION` on `PORTEPOCH` bump
- portclippy: Ignore vars like `CONFIGURE_ARGS_${ARCH}` (assuming `CONFIGURE_ARGS`
  references it) or `COMMENT_${FLAVOR}` (assuming `COMMENT` references
  it).  Fixes #15.

## [g20210321] - 2021-03-21

### Added

- portscan: Report on option descriptions that fuzzy match the default descriptions
  in `Mk/bsd.options.desc.mk`.  Enabled by default but is also
  selectable with `-o option-default-descriptions`.

### Changed

- Format `UNIQUE_PREFIX_FILES` and `UNIQUE_SUFFIX_FILES` similar to `PLIST_FILES`
- Recognize `DO_MAKE_BUILD` and `DO_MAKE_TEST`
- Recognize `makeplist` overrides
- Leave `CFLAGS`, `MAKE_FLAGS`, etc. unsorted
- Catch up with FreeBSD Ports:
  - Add new vars like `GO_MODULE`, `KDE_INVENT`
  - Update known `USES={gnome,kde,pyqt,qt}` components
  - Recognize `*_FreeBSD_14` variables
  - Recognize `USES=emacs` flavors

### Fixed

- portclippy: Refuse to check non-FreeBSD Ports files
- portedit, portfmt: `-D` produces less cluttered unified diffs with reduced context.
  3 lines of context by default but more can be asked for with an
  optional argument to `-D`.  Use `-D0` to get the full context as before.
- portscan: Handle `.include` with `${.PARSEDIR}`

## [g20200924] - 2020-09-24

No changelog for old releases.
