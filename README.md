# portfmt

Portfmt is a collection of tools for editing, formatting, and linting FreeBSD Ports Collection Makefiles.

It comes with several tools:

- `portfmt` formats Makefiles
- `portclippy` is a linter that checks if variables are in the correct order in a more comprehensive way than `portlint`
- `portedit` edits Makefiles.  It comes with several commands that can be used as a basis for your own port update scripts:
  - `bump-epoch`: bumps `PORTEPOCH` or inserts it at the right place
  - `bump-revision`: bumps `PORTREVISION` or inserts it at the right place
  - `set-version`: resets `PORTREVISION`, sets `DISTVERSION` or `PORTVERSION`
  - `get`: lookup unevaluated variable values
  - `merge`: Generic command to set/update variables while also formatting the updated variables properly and inserting them in the right places if necessary.  Useful for merging output of other tools like make cargo-crates, modules2tuple, or make stage-qa.  For example to mark a port deprecated:
```
	printf "DEPRECATED=%s\nEXPIRATION_DATE=%s" \
		Abandonware 2019-08-15 | portedit merge -i Makefile
```
- `portscan` checks the entire Ports Collection for mistakes like unreferenced variables, etc.

## Example

A Makefile like this
```
LICENSE_PERMS=  dist-mirror pkg-mirror auto-accept dist-sell pkg-sell

RUN_DEPENDS+=   ${PYTHON_PKGNAMEPREFIX}paho-mqtt>=0:net/py-paho-mqtt@${PY_FLAVOR}
RUN_DEPENDS+=   ${PYTHON_PKGNAMEPREFIX}supervisor>=0:sysutils/py-supervisor@${PY_FLAVOR}

USES=           cmake \
                compiler:c++11-lib \
                desktop-file-utils \
                gettext-tools \
                pkgconfig \
                qt:5 \
                sqlite \
                gl
USE_QT=         buildtools_build \
                concurrent \
                core \
                dbus \
                gui \
                imageformats \
                linguist_build \
                network \
                opengl \
                qmake_build \
                testlib_build \
                sql \
                widgets \
                x11extras \
                xml

FOOBAR_CXXFLAGS=	-DBLA=foo # workaround for https://github.com/... with a very long explanation
```
is turned into
```
LICENSE_PERMS=	dist-mirror dist-sell pkg-mirror pkg-sell auto-accept

RUN_DEPENDS+=	${PYTHON_PKGNAMEPREFIX}paho-mqtt>=0:net/py-paho-mqtt@${PY_FLAVOR} \
		${PYTHON_PKGNAMEPREFIX}supervisor>=0:sysutils/py-supervisor@${PY_FLAVOR}

USES=		cmake compiler:c++11-lib desktop-file-utils gettext-tools gl \
		pkgconfig qt:5 sqlite
USE_QT=		concurrent core dbus gui imageformats network opengl sql widgets \
		x11extras xml buildtools_build linguist_build qmake_build \
		testlib_build

# workaround for https://github.com/... with a very long explanation
FOOBAR_CXXFLAGS=	-DBLA=foo
```

## Building portfmt

If you want to build `portfmt` from the repository make sure to also clone the submodules: `git clone --recurse-submodules https://github.com/t6/portfmt`

Building `portfmt` requires Ninja (packaged often as `ninja` or `ninja-build`) or Samurai (package `samurai`).

- Prepare the build: `./configure PREFIX=/usr/local`
- Build it: `ninja`
- The binaries are available under `_build/.bin/` and can be run directly or optionally installed with: `ninja install`

## Editor integration

You can integrate Portfmt into your editor to conveniently run it
only on parts of the port, e.g., to reformat `USES` after adding a
new item to it.

### Emacs

Add this to `~/.emacs.d/init.el` to format the current region with
`C-c p`.

```
(defun portfmt (&optional b e)
  "PORTFMT(1) on region"
  (interactive "r")
  (shell-command-on-region b e "portfmt " (current-buffer) t
                           "*portfmt errors*" t))

(with-eval-after-load 'make-mode
  (define-key makefile-bsdmake-mode-map (kbd "C-c p") 'portfmt))
```

### Kakoune

Add this to `~/.config/kak/kakrc` for filtering the current selection
through portfmt with `,1`:
```
map global user 1 '|portfmt<ret>;' -docstring "portfmt on selection"
```

### Vim

Add this to `~/.vimrc` for filtering the current selection through
portfmt with `\1`:
```
xnoremap <leader>1 <esc>:'<,'>!portfmt<CR>
```
