# Changelog

All notable changes to portfmt are documented in this file.
The sections should follow the order `Packaging`, `Added`, `Changed`, `Fixed` and `Removed`.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## Unreleased

### Changed

- Recognize more framework targets
- Catch up with FreeBSD Ports:
  - Remove support for `PKGUPGRADE` and friends
- portclippy, portscan: Report unknown target sources too

### Fixed

- portclippy, portscan: Do not report on targets defined in `POST_PLIST`
- Do not recognize false options helper targets like `makesum-OPT-on`
- Properly split target names and dependencies.  This improves
  overall reporting on targets in portclippy and portscan

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