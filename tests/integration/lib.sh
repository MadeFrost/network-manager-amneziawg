# Shared helpers for integration tests. Sourced by scripts that already run
# inside `unshare -rnmpf` (see tests/run-integration.sh): root in a private user,
# network and mount namespace, so nothing here touches the real system.
#
# Paths: ROOT is the source tree, BUILD the build directory with the plugin
# and the service (<builddir>/src), HELPERS the one with the test programs
# (<builddir>/tests), W a scratch directory on a tmpfs that disappears with
# the namespace. `meson test` sets BUILD and HELPERS.

set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-$ROOT/build/src}
HELPERS=${HELPERS:-$ROOT/build/tests}
PLUGIN=$BUILD/libnm-vpn-plugin-amneziawg.so
SERVICE=$BUILD/nm-amneziawg-service
BUS=org.freedesktop.NetworkManager.amneziawg
W=/run/awg-test

PASSED=0
FAILED=0

pass() { echo "  ok    $1"; PASSED=$((PASSED + 1)); }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }
# check NAME COMMAND...: passes when the command succeeds.
check() { _n=$1; shift; if "$@" >/dev/null 2>&1; then pass "$_n"; else fail "$_n"; fi; }
# check_eq NAME EXPECTED ACTUAL
check_eq() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1 (expected '$2', got '$3')"; fi; }
section() { echo "### $*"; }
finish() {
	echo "$PASSED passed, $FAILED failed"
	[ "$FAILED" -eq 0 ]
}
skip() { echo "SKIP: $*"; exit 77; }

# Private /run (tmpfs) and a fresh sysfs that shows this network namespace.
setup_ns() {
	mount -t tmpfs tmpfs /run || skip "cannot mount tmpfs (run me through tests/run-integration.sh)"
	mkdir -p /run/netns /run/dbus "$W"
	mount -t sysfs -o ro sysfs /sys
	ip link set lo up
	ip link add awg-probe type amneziawg 2>/dev/null || skip "amneziawg kernel module is not loaded"
	ip link del awg-probe
}

# A private system bus at the standard path, so libnm and NM find it.
start_bus() {
	cat > /run/dbus/system.conf <<-'EOF'
	<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
	 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
	<busconfig><type>system</type>
	<listen>unix:path=/run/dbus/system_bus_socket</listen><auth>EXTERNAL</auth>
	<policy context="default"><allow user="*"/><allow own="*"/>
	<allow send_destination="*"/><allow receive_sender="*"/></policy></busconfig>
	EOF
	dbus-daemon --config-file=/run/dbus/system.conf --fork --print-pid > "$W/bus.pid"
	export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket
}
stop_bus() { kill "$(cat "$W/bus.pid")" 2>/dev/null; }

# start_service SUFFIX: runs the service under bus name $BUS.SUFFIX (or $BUS
# with an empty suffix) and sets SVC_PID. Logs to $W/service.log.
# Not via $(...): the service must be our child, or `wait` cannot reap it.
start_service() {
	_name=$BUS${1:+.$1}
	"$SERVICE" --debug --bus-name "$_name" >> "$W/service.log" 2>&1 &
	SVC_PID=$!
	for _i in 1 2 3 4 5 6 7 8 9 10; do
		dbus-send --system --print-reply --dest=org.freedesktop.DBus / \
			org.freedesktop.DBus.GetNameOwner "string:$_name" >/dev/null 2>&1 && break
		sleep 0.1
	done
}

# retry SECONDS COMMAND...: succeeds as soon as the command does.
retry() {
	_t=$(($1 * 5)); shift
	while [ $_t -gt 0 ]; do
		"$@" >/dev/null 2>&1 && return 0
		_t=$((_t - 1))
		sleep 0.2
	done
	return 1
}

# fake_nm SUFFIX FILE: plays NetworkManager (see helpers/fake-nm.c).
fake_nm() { "$HELPERS/fake-nm" "$PLUGIN" "$2" "$BUS${1:+.$1}"; }
fake_nm_disconnect() { "$HELPERS/fake-nm" - - "$BUS${1:+.$1}"; }

awg_links() { ip -br link | awk '$1 ~ /^awg-nm/ {print $1}' | sort | tr '\n' ' ' | sed 's/ $//'; }

# Obfuscation parameters shared by client and server configs.
OBFS="Jc = 4
Jmin = 10
Jmax = 50
S1 = 117
S2 = 21
S3 = 13
S4 = 14
H1 = 1157836183
H2 = 1342456823
H3 = 993474852
H4 = 1781947022"

# A server ("srv" namespace, 192.168.77.2:51820, tunnel 10.8.1.1/24) reached
# over a veth pair from 192.168.77.1. Usage: start_server CLIENT_PUB [EXTRA]
# EXTRA is appended to the server's [Interface] (and may override OBFS lines).
start_server() {
	ip netns add srv
	ip link add v0 type veth peer name v1
	ip link set v1 netns srv
	ip addr add 192.168.77.1/24 dev v0
	ip link set v0 up
	ip -n srv addr add 192.168.77.2/24 dev v1
	ip -n srv link set v1 up
	ip -n srv link set lo up
	SPRIV=$(awg genkey)
	SPUB=$(echo "$SPRIV" | awg pubkey)
	ip -n srv link add srv0 type amneziawg
	printf '[Interface]\nPrivateKey = %s\nListenPort = 51820\n%s\n%s\n\n[Peer]\nPublicKey = %s\nAllowedIPs = 10.8.1.2/32\n%s\n' \
		"$SPRIV" "$OBFS" "${2:-}" "$1" "${SERVER_PEER_EXTRA:-}" | ip netns exec srv awg setconf srv0 /dev/stdin
	ip -n srv addr add 10.8.1.1/24 dev srv0
	ip -n srv link set srv0 up
}
