.for x in 1 2
post-install-$x-on:
.endfor
<<<<<<<<<
conditional-start           1 .for -
conditional-token           1 .for .for
conditional-token           1 .for x
conditional-token           1 .for in
conditional-token           1 .for 1
conditional-token           1 .for 2
conditional-end             1 .for -
target-start                2 post-install-$x-on post-install-$x-on:
target-end                  2 - -
conditional-start           3 .endfor -
conditional-token           3 .endfor .endfor
conditional-end             3 .endfor -
