#!/bin/sh
# Runs the integration tests, each in its own `unshare -rnmpf` (private user,
# network, mount and PID namespaces): no root needed, the real system is not
# touched, and tests running in parallel do not see each other's processes. Needs the amneziawg kernel module loaded and amneziawg-tools.
#
# usage: tests/run-integration.sh [TEST...]   e.g. "e2e ok" "stale"
# `meson test --suite integration` runs it once per test. Run directly, it
# uses build/ (BUILD=<builddir>/src and HELPERS=<builddir>/tests override).
# Exit code: 0 passed, 1 something failed, 77 everything was skipped.
set -u
DIR=$(cd "$(dirname "$0")" && pwd)
export BUILD=${BUILD:-$(dirname "$DIR")/build/src}
export HELPERS=${HELPERS:-$(dirname "$DIR")/build/tests}
LOGS=$HELPERS/logs
mkdir -p "$LOGS"

missing=
for tool in unshare awg ip dbus-daemon timeout; do
	command -v $tool >/dev/null 2>&1 || missing="$missing $tool"
done
[ -x "$HELPERS/fake-nm" ] || missing="$missing fake-nm(meson compile)"
if [ -n "$missing" ]; then
	echo "SKIP all: missing$missing"
	exit 77
fi
if ! unshare -rn true 2>/dev/null; then
	echo "SKIP all: unprivileged user namespaces are disabled (kernel.unprivileged_userns_clone / AppArmor)"
	exit 77
fi
if ! lsmod 2>/dev/null | grep -q '^amneziawg '; then
	echo "SKIP all: amneziawg kernel module is not loaded (sudo modprobe amneziawg)"
	exit 77
fi

if [ $# -eq 0 ]; then
	set -- "e2e ok" "e2e badobfs" "e2e hpk" "e2e badhpk" "e2e badi1" "e2e badkey" "e2e badawg" \
		"stale" "ipacket-kernel" "nm-real"
fi

failed=0
passed=0
for t in "$@"; do
	script=${t%% *}
	args=
	[ "$script" != "$t" ] && args=${t#* }
	log=$LOGS/$(echo "$t" | tr ' ' '-').log
	# shellcheck disable=SC2086
	timeout 180 unshare -rnmpf --mount-proc sh "$DIR/integration/$script.sh" $args > "$log" 2>&1
	rc=$?
	case $rc in
	0) r=PASS ;;
	77) r=SKIP ;;
	124) r=FAIL; echo "(timed out)" >> "$log" ;;
	*) r=FAIL ;;
	esac
	[ $r = PASS ] && passed=1
	summary=$(tail -1 "$log")
	printf '%-5s %-20s %s\n' "$r" "$t" "$summary"
	if [ $r = FAIL ]; then
		failed=1
		grep -E 'FAIL|timed out' "$log" | sed 's/^/      /' | head -15
		echo "      full log: $log"
	fi
done
[ $failed -eq 0 ] && [ $passed -eq 0 ] && exit 77
exit $failed
