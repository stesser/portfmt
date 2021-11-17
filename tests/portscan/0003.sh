logdir="$(mktemp -dt portscan-test.XXXXXXX)"
${PORTSCAN} -p 0002 -l "${logdir}/log"
set +e
${PORTSCAN} -p 0002 -l "${logdir}/log" 2>/dev/null
# no changes
[ $? -eq 2 ] || exit 1
