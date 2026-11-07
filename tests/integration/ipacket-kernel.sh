#!/bin/sh
# The running amneziawg kernel module must agree with tests/data/ipacket-cases.txt,
# which tests/unit/test-ipacket.c checks our validator against. A failure here
# means the module's I1..I5 syntax changed: update the cases and the validator.
# (Run inside unshare -rnmpf.)
. "$(dirname "$0")/lib.sh"

setup_ns
ip link add t0 type amneziawg
KEY=$(awg genkey)
CASES=$ROOT/tests/data/ipacket-cases.txt

section "kernel verdicts ($(modinfo -F version amneziawg 2>/dev/null || echo 'unknown version'))"
TAB=$(printf '\t')
while IFS= read -r line; do
	case $line in '#'* | '') continue ;; esac
	want=${line%%"$TAB"*}
	value=${line#*"$TAB"}
	if printf '[Interface]\nPrivateKey = %s\nI1 = %s\n' "$KEY" "$value" |
		awg setconf t0 /dev/stdin >/dev/null 2>&1; then got=OK; else got=ERR; fi
	check_eq "$(printf '%-3s %s' "$want" "$value")" "$want" "$got"
done < "$CASES"
finish
