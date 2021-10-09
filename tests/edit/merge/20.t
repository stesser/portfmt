${PORTEDIT} merge -e 'SUBDIR+=OpenTomb' $input | diff -L $expected -L $actual -u $expected -
<<<<<<<<<
# $FreeBSD: head/x11-fonts/Makefile 524059 2020-01-25 18:18:06Z sunpoet $
#

    COMMENT = X11 fonts and font utilities

    SUBDIR += 3270font
    SUBDIR += Hasklig
    SUBDIR += alef
    SUBDIR += alegreya
    SUBDIR += alegreya-sans
    SUBDIR += alfont
    SUBDIR += andika
    SUBDIR += anonymous-pro
    SUBDIR += apl385
    SUBDIR += artwiz-aleczapka
    SUBDIR += artwiz-aleczapka-de
    SUBDIR += artwiz-aleczapka-se
    SUBDIR += artwiz-fonts
    SUBDIR += averiagwf
    SUBDIR += b612
    SUBDIR += bdfresize
    SUBDIR += bdftopcf
    SUBDIR += bitstream-vera
    SUBDIR += bitter
    SUBDIR += blackout
    SUBDIR += c64bdf
    SUBDIR += cantarell-fonts
    SUBDIR += cascadia-code
    SUBDIR += charis
    SUBDIR += charis-compact
    SUBDIR += clearsans
    SUBDIR += code2000
    SUBDIR += comfortaa-ttf
    SUBDIR += comic-neue
    SUBDIR += consolamono-ttf
    SUBDIR += courier-prime
    SUBDIR += croscorefonts-fonts-ttf
    SUBDIR += crosextrafonts-caladea-ttf
    SUBDIR += crosextrafonts-carlito-ttf
    SUBDIR += cyberbit-ttfonts
    SUBDIR += cyr-rfx
    SUBDIR += datalegreya
    SUBDIR += dejavu
    SUBDIR += dina
    SUBDIR += doulos
    SUBDIR += doulos-compact
    SUBDIR += droid-fonts-ttf
    SUBDIR += encodings
    SUBDIR += exo
    SUBDIR += fantasque-sans-mono
    SUBDIR += fanwood
    SUBDIR += farsifonts
    SUBDIR += fifteen
    SUBDIR += fira
    SUBDIR += firacode
    SUBDIR += firago
    SUBDIR += fntsample
    SUBDIR += font-adobe-100dpi
    SUBDIR += font-adobe-75dpi
    SUBDIR += font-adobe-utopia-100dpi
    SUBDIR += font-adobe-utopia-75dpi
    SUBDIR += font-adobe-utopia-type1
    SUBDIR += font-alias
    SUBDIR += font-arabic-misc
    SUBDIR += font-awesome
    SUBDIR += font-bh-100dpi
    SUBDIR += font-bh-75dpi
    SUBDIR += font-bh-lucidatypewriter-100dpi
    SUBDIR += font-bh-lucidatypewriter-75dpi
    SUBDIR += font-bh-ttf
    SUBDIR += font-bh-type1
    SUBDIR += font-bitstream-100dpi
    SUBDIR += font-bitstream-75dpi
    SUBDIR += font-bitstream-type1
    SUBDIR += font-cronyx-cyrillic
    SUBDIR += font-cursor-misc
    SUBDIR += font-daewoo-misc
    SUBDIR += font-dec-misc
    SUBDIR += font-gost
    SUBDIR += font-ibm-type1
    SUBDIR += font-isas-misc
    SUBDIR += font-jis-misc
    SUBDIR += font-manager
    SUBDIR += font-micro-misc
    SUBDIR += font-misc-cyrillic
    SUBDIR += font-misc-ethiopic
    SUBDIR += font-misc-meltho
    SUBDIR += font-misc-misc
    SUBDIR += font-mutt-misc
    SUBDIR += font-schumacher-misc
    SUBDIR += font-screen-cyrillic
    SUBDIR += font-sony-misc
    SUBDIR += font-sun-misc
    SUBDIR += font-tex-gyre-bonum-math
    SUBDIR += font-tex-gyre-pagella-math
    SUBDIR += font-tex-gyre-schola-math
    SUBDIR += font-tex-gyre-termes-math
    SUBDIR += font-util
    SUBDIR += font-winitzki-cyrillic
    SUBDIR += font-xfree86-type1
    SUBDIR += fontconfig
    SUBDIR += fontconfig-reference
    SUBDIR += fontmatrix
    SUBDIR += fonts-indic
    SUBDIR += fonttosfnt
    SUBDIR += freefont-ttf
    SUBDIR += freefonts
    SUBDIR += fslsfonts
    SUBDIR += gbdfed
    SUBDIR += geminifonts
    SUBDIR += gentium-basic
    SUBDIR += gentium-plus
    SUBDIR += gnu-unifont
    SUBDIR += gnu-unifont-ttf
    SUBDIR += gofont-ttf
    SUBDIR += gohufont
    SUBDIR += google-fonts
    SUBDIR += hack-font
    SUBDIR += hanazono-fonts-ttf
    SUBDIR += hermit
    SUBDIR += inconsolata-lgc-ttf
    SUBDIR += inconsolata-ttf
    SUBDIR += intlfonts
    SUBDIR += iosevka
    SUBDIR += isabella
    SUBDIR += jetbrains-mono
    SUBDIR += jmk-x11-fonts
    SUBDIR += junction
    SUBDIR += junicode
    SUBDIR += kaputa
    SUBDIR += khmeros
    SUBDIR += lato
    SUBDIR += league-gothic
    SUBDIR += league-spartan
    SUBDIR += lfpfonts-fix
    SUBDIR += lfpfonts-var
    SUBDIR += libFS
    SUBDIR += libXfont
    SUBDIR += libXfont2
    SUBDIR += libXft
    SUBDIR += liberation-fonts-ttf
    SUBDIR += libfontenc
    SUBDIR += linden-hill
    SUBDIR += linux-c7-fontconfig
    SUBDIR += linuxlibertine
    SUBDIR += linuxlibertine-g
    SUBDIR += lohit
    SUBDIR += manu-gothica
    SUBDIR += material-icons-ttf
    SUBDIR += materialdesign-ttf
    SUBDIR += meslo
    SUBDIR += mgopen
    SUBDIR += mkbold
    SUBDIR += mkbold-mkitalic
    SUBDIR += mkfontscale
    SUBDIR += mkitalic
    SUBDIR += mondulkiri
    SUBDIR += monoid
    SUBDIR += montecarlo_fonts
    SUBDIR += montserrat
    SUBDIR += moveable-type-fonts
    SUBDIR += nerd-fonts
    SUBDIR += nexfontsel
    SUBDIR += noto
    SUBDIR += noto-basic
    SUBDIR += noto-extra
    SUBDIR += noto-jp
    SUBDIR += noto-kr
    SUBDIR += noto-sc
    SUBDIR += noto-tc
    SUBDIR += nucleus
    SUBDIR += ohsnap
    SUBDIR += oldschool-pc-fonts
    SUBDIR += open-sans
    SUBDIR += orbitron
    SUBDIR += ots
    SUBDIR += oxygen-fonts
    SUBDIR += p5-Font-AFM
    SUBDIR += p5-Font-TTF
    SUBDIR += p5-Font-TTFMetrics
    SUBDIR += p5-type1inst
    SUBDIR += padauk
    SUBDIR += paratype
    SUBDIR += pcf2bdf
    SUBDIR += plex-ttf
    SUBDIR += powerline-fonts
    SUBDIR += prociono
    SUBDIR += profont
    SUBDIR += proggy_fonts
    SUBDIR += proggy_fonts-ttf
    SUBDIR += psftools
    SUBDIR += py-QtAwesome
    SUBDIR += py-bdflib
    SUBDIR += py-booleanOperations
    SUBDIR += py-compreffor
    SUBDIR += py-cu2qu
    SUBDIR += py-defcon
    SUBDIR += py-fontMath
    SUBDIR += py-fontmake
    SUBDIR += py-glyphsLib
    SUBDIR += py-opentype-sanitizer
    SUBDIR += py-ufo2ft
    SUBDIR += py-ufoLib
    SUBDIR += py-ufolint
    SUBDIR += raleway
    SUBDIR += roboto-fonts-ttf
    SUBDIR += sgifonts
    SUBDIR += sharefonts
    SUBDIR += showfont
    SUBDIR += sourcecodepro-ttf
    SUBDIR += sourcesanspro-ttf
    SUBDIR += sourceserifpro-ttf
    SUBDIR += spleen
    SUBDIR += stix-fonts
    SUBDIR += sudo-font
    SUBDIR += suxus
    SUBDIR += symbola
    SUBDIR += tamsyn
    SUBDIR += tamzen
    SUBDIR += terminus-font
    SUBDIR += terminus-ttf
    SUBDIR += tkfont
    SUBDIR += tlwg-ttf
    SUBDIR += tmu
    SUBDIR += tv-fonts
    SUBDIR += twemoji-color-font-ttf
    SUBDIR += ubuntu-font
    SUBDIR += urwfonts
    SUBDIR += urwfonts-ttf
    SUBDIR += uw-ttyp0
    SUBDIR += victor-mono-ttf
    SUBDIR += vollkorn-ttf
    SUBDIR += vtfontcvt-ng
    SUBDIR += webfonts
    SUBDIR += wqy
    SUBDIR += xfontsel
    SUBDIR += xfs
    SUBDIR += xfsinfo
    SUBDIR += xlsfonts
    SUBDIR += xorg-fonts
    SUBDIR += xorg-fonts-100dpi
    SUBDIR += xorg-fonts-75dpi
    SUBDIR += xorg-fonts-cyrillic
    SUBDIR += xorg-fonts-miscbitmaps
    SUBDIR += xorg-fonts-truetype
    SUBDIR += xorg-fonts-type1

.include <bsd.port.subdir.mk>
<<<<<<<<<
# $FreeBSD: head/x11-fonts/Makefile 524059 2020-01-25 18:18:06Z sunpoet $
#

    COMMENT = X11 fonts and font utilities

    SUBDIR += 3270font
    SUBDIR += Hasklig
    SUBDIR += OpenTomb
    SUBDIR += alef
    SUBDIR += alegreya
    SUBDIR += alegreya-sans
    SUBDIR += alfont
    SUBDIR += andika
    SUBDIR += anonymous-pro
    SUBDIR += apl385
    SUBDIR += artwiz-aleczapka
    SUBDIR += artwiz-aleczapka-de
    SUBDIR += artwiz-aleczapka-se
    SUBDIR += artwiz-fonts
    SUBDIR += averiagwf
    SUBDIR += b612
    SUBDIR += bdfresize
    SUBDIR += bdftopcf
    SUBDIR += bitstream-vera
    SUBDIR += bitter
    SUBDIR += blackout
    SUBDIR += c64bdf
    SUBDIR += cantarell-fonts
    SUBDIR += cascadia-code
    SUBDIR += charis
    SUBDIR += charis-compact
    SUBDIR += clearsans
    SUBDIR += code2000
    SUBDIR += comfortaa-ttf
    SUBDIR += comic-neue
    SUBDIR += consolamono-ttf
    SUBDIR += courier-prime
    SUBDIR += croscorefonts-fonts-ttf
    SUBDIR += crosextrafonts-caladea-ttf
    SUBDIR += crosextrafonts-carlito-ttf
    SUBDIR += cyberbit-ttfonts
    SUBDIR += cyr-rfx
    SUBDIR += datalegreya
    SUBDIR += dejavu
    SUBDIR += dina
    SUBDIR += doulos
    SUBDIR += doulos-compact
    SUBDIR += droid-fonts-ttf
    SUBDIR += encodings
    SUBDIR += exo
    SUBDIR += fantasque-sans-mono
    SUBDIR += fanwood
    SUBDIR += farsifonts
    SUBDIR += fifteen
    SUBDIR += fira
    SUBDIR += firacode
    SUBDIR += firago
    SUBDIR += fntsample
    SUBDIR += font-adobe-100dpi
    SUBDIR += font-adobe-75dpi
    SUBDIR += font-adobe-utopia-100dpi
    SUBDIR += font-adobe-utopia-75dpi
    SUBDIR += font-adobe-utopia-type1
    SUBDIR += font-alias
    SUBDIR += font-arabic-misc
    SUBDIR += font-awesome
    SUBDIR += font-bh-100dpi
    SUBDIR += font-bh-75dpi
    SUBDIR += font-bh-lucidatypewriter-100dpi
    SUBDIR += font-bh-lucidatypewriter-75dpi
    SUBDIR += font-bh-ttf
    SUBDIR += font-bh-type1
    SUBDIR += font-bitstream-100dpi
    SUBDIR += font-bitstream-75dpi
    SUBDIR += font-bitstream-type1
    SUBDIR += font-cronyx-cyrillic
    SUBDIR += font-cursor-misc
    SUBDIR += font-daewoo-misc
    SUBDIR += font-dec-misc
    SUBDIR += font-gost
    SUBDIR += font-ibm-type1
    SUBDIR += font-isas-misc
    SUBDIR += font-jis-misc
    SUBDIR += font-manager
    SUBDIR += font-micro-misc
    SUBDIR += font-misc-cyrillic
    SUBDIR += font-misc-ethiopic
    SUBDIR += font-misc-meltho
    SUBDIR += font-misc-misc
    SUBDIR += font-mutt-misc
    SUBDIR += font-schumacher-misc
    SUBDIR += font-screen-cyrillic
    SUBDIR += font-sony-misc
    SUBDIR += font-sun-misc
    SUBDIR += font-tex-gyre-bonum-math
    SUBDIR += font-tex-gyre-pagella-math
    SUBDIR += font-tex-gyre-schola-math
    SUBDIR += font-tex-gyre-termes-math
    SUBDIR += font-util
    SUBDIR += font-winitzki-cyrillic
    SUBDIR += font-xfree86-type1
    SUBDIR += fontconfig
    SUBDIR += fontconfig-reference
    SUBDIR += fontmatrix
    SUBDIR += fonts-indic
    SUBDIR += fonttosfnt
    SUBDIR += freefont-ttf
    SUBDIR += freefonts
    SUBDIR += fslsfonts
    SUBDIR += gbdfed
    SUBDIR += geminifonts
    SUBDIR += gentium-basic
    SUBDIR += gentium-plus
    SUBDIR += gnu-unifont
    SUBDIR += gnu-unifont-ttf
    SUBDIR += gofont-ttf
    SUBDIR += gohufont
    SUBDIR += google-fonts
    SUBDIR += hack-font
    SUBDIR += hanazono-fonts-ttf
    SUBDIR += hermit
    SUBDIR += inconsolata-lgc-ttf
    SUBDIR += inconsolata-ttf
    SUBDIR += intlfonts
    SUBDIR += iosevka
    SUBDIR += isabella
    SUBDIR += jetbrains-mono
    SUBDIR += jmk-x11-fonts
    SUBDIR += junction
    SUBDIR += junicode
    SUBDIR += kaputa
    SUBDIR += khmeros
    SUBDIR += lato
    SUBDIR += league-gothic
    SUBDIR += league-spartan
    SUBDIR += lfpfonts-fix
    SUBDIR += lfpfonts-var
    SUBDIR += libFS
    SUBDIR += libXfont
    SUBDIR += libXfont2
    SUBDIR += libXft
    SUBDIR += liberation-fonts-ttf
    SUBDIR += libfontenc
    SUBDIR += linden-hill
    SUBDIR += linux-c7-fontconfig
    SUBDIR += linuxlibertine
    SUBDIR += linuxlibertine-g
    SUBDIR += lohit
    SUBDIR += manu-gothica
    SUBDIR += material-icons-ttf
    SUBDIR += materialdesign-ttf
    SUBDIR += meslo
    SUBDIR += mgopen
    SUBDIR += mkbold
    SUBDIR += mkbold-mkitalic
    SUBDIR += mkfontscale
    SUBDIR += mkitalic
    SUBDIR += mondulkiri
    SUBDIR += monoid
    SUBDIR += montecarlo_fonts
    SUBDIR += montserrat
    SUBDIR += moveable-type-fonts
    SUBDIR += nerd-fonts
    SUBDIR += nexfontsel
    SUBDIR += noto
    SUBDIR += noto-basic
    SUBDIR += noto-extra
    SUBDIR += noto-jp
    SUBDIR += noto-kr
    SUBDIR += noto-sc
    SUBDIR += noto-tc
    SUBDIR += nucleus
    SUBDIR += ohsnap
    SUBDIR += oldschool-pc-fonts
    SUBDIR += open-sans
    SUBDIR += orbitron
    SUBDIR += ots
    SUBDIR += oxygen-fonts
    SUBDIR += p5-Font-AFM
    SUBDIR += p5-Font-TTF
    SUBDIR += p5-Font-TTFMetrics
    SUBDIR += p5-type1inst
    SUBDIR += padauk
    SUBDIR += paratype
    SUBDIR += pcf2bdf
    SUBDIR += plex-ttf
    SUBDIR += powerline-fonts
    SUBDIR += prociono
    SUBDIR += profont
    SUBDIR += proggy_fonts
    SUBDIR += proggy_fonts-ttf
    SUBDIR += psftools
    SUBDIR += py-QtAwesome
    SUBDIR += py-bdflib
    SUBDIR += py-booleanOperations
    SUBDIR += py-compreffor
    SUBDIR += py-cu2qu
    SUBDIR += py-defcon
    SUBDIR += py-fontMath
    SUBDIR += py-fontmake
    SUBDIR += py-glyphsLib
    SUBDIR += py-opentype-sanitizer
    SUBDIR += py-ufo2ft
    SUBDIR += py-ufoLib
    SUBDIR += py-ufolint
    SUBDIR += raleway
    SUBDIR += roboto-fonts-ttf
    SUBDIR += sgifonts
    SUBDIR += sharefonts
    SUBDIR += showfont
    SUBDIR += sourcecodepro-ttf
    SUBDIR += sourcesanspro-ttf
    SUBDIR += sourceserifpro-ttf
    SUBDIR += spleen
    SUBDIR += stix-fonts
    SUBDIR += sudo-font
    SUBDIR += suxus
    SUBDIR += symbola
    SUBDIR += tamsyn
    SUBDIR += tamzen
    SUBDIR += terminus-font
    SUBDIR += terminus-ttf
    SUBDIR += tkfont
    SUBDIR += tlwg-ttf
    SUBDIR += tmu
    SUBDIR += tv-fonts
    SUBDIR += twemoji-color-font-ttf
    SUBDIR += ubuntu-font
    SUBDIR += urwfonts
    SUBDIR += urwfonts-ttf
    SUBDIR += uw-ttyp0
    SUBDIR += victor-mono-ttf
    SUBDIR += vollkorn-ttf
    SUBDIR += vtfontcvt-ng
    SUBDIR += webfonts
    SUBDIR += wqy
    SUBDIR += xfontsel
    SUBDIR += xfs
    SUBDIR += xfsinfo
    SUBDIR += xlsfonts
    SUBDIR += xorg-fonts
    SUBDIR += xorg-fonts-100dpi
    SUBDIR += xorg-fonts-75dpi
    SUBDIR += xorg-fonts-cyrillic
    SUBDIR += xorg-fonts-miscbitmaps
    SUBDIR += xorg-fonts-truetype
    SUBDIR += xorg-fonts-type1

.include <bsd.port.subdir.mk>
