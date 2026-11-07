#!/bin/sh
# The service against a real AmneziaWG server, with fake-nm in NM's role.
# The connection is saved and read back the way Ubuntu does it
# (NM keyfile -> netplan YAML -> generated keyfile) when netplan is installed.
#
# usage (inside unshare -rnmpf): e2e.sh MODE
#   ok       matching settings: handshake, traffic, clean disconnect
#   badobfs  server uses another H1: no traffic (obfuscation is applied)
#   hpk      HeaderProtectionKey on both sides: traffic; key kept as a secret
#   badhpk   server has another HeaderProtectionKey: no traffic
#   badi1    invalid I1: refused with a clear reason, no interface left
#   badkey   malformed PresharedKey: refused before awg, the value is not shown
#   badawg   a value only awg rejects (FwMark): awg's error without the value
. "$(dirname "$0")/lib.sh"
MODE=${1:?usage: e2e.sh ok|badobfs|hpk|badhpk|badi1|badkey|badawg}

setup_ns
start_bus

CPRIV=$(awg genkey)
CPUB=$(echo "$CPRIV" | awg pubkey)
PSK=$(awg genpsk)
HPK=$(awg genpsk)
CLIENT_EXTRA=
SERVER_EXTRA=
SERVER_PEER_EXTRA="PresharedKey = $PSK"
CLIENT_PSK=$PSK
case $MODE in
ok) ;;
badobfs) SERVER_EXTRA="H1 = 1111111111" ;;
hpk) CLIENT_EXTRA="HeaderProtectionKey = $HPK"; SERVER_EXTRA="HeaderProtectionKey = $HPK" ;;
badhpk) CLIENT_EXTRA="HeaderProtectionKey = $HPK"; SERVER_EXTRA="HeaderProtectionKey = $(awg genpsk)" ;;
badi1) CLIENT_EXTRA="I1 = <b 0x1a2><r 8>" ;;
badkey) CLIENT_PSK="NOT-A-KEY-but-a-secret-value" ;;
badawg) CLIENT_EXTRA="FwMark = NOT-A-KEY-but-a-secret-value" ;;
*) echo "unknown mode $MODE"; exit 2 ;;
esac
start_server "$CPUB" "$SERVER_EXTRA"

# The client .conf as exported by the Amnezia app.
cat > "$W/client.conf" <<EOF
[Interface]
PrivateKey = $CPRIV
Address = 10.8.1.2/32
DNS = 1.1.1.1, 1.0.0.1
$OBFS
$CLIENT_EXTRA

[Peer]
PublicKey = $SPUB
PresharedKey = $CLIENT_PSK
Endpoint = 192.168.77.2:51820
AllowedIPs = 10.8.1.0/24, 172.16.0.0/12
PersistentKeepalive = 25
EOF

start_service
SVC=$SVC_PID

section "save and read back"
"$HELPERS/kfwrite" "$PLUGIN" "$W/client.conf" "$W/in.nmconnection"
check "plugin imports the config" test -s "$W/in.nmconnection"
KF=$W/in.nmconnection
if python3 -I -c 'import netplan' 2>/dev/null && [ -x /usr/libexec/netplan/generate ]; then
	R=$W/np
	mkdir -p "$R/etc/netplan"
	python3 -I -c "
import sys, netplan
p = netplan.Parser(); p.load_keyfile(sys.argv[1])
s = netplan.State(); s.import_parser_results(p)
s._dump_yaml(open(sys.argv[2] + '/etc/netplan/90-NM-test.yaml', 'w'))" "$KF" "$R"
	chmod 600 "$R"/etc/netplan/*
	/usr/libexec/netplan/generate --root-dir "$R" 2>/dev/null
	KF=$(ls "$R"/run/NetworkManager/system-connections/*.nmconnection 2>/dev/null | head -1)
	check "netplan round trip produces a keyfile" test -n "$KF"
else
	echo "  (netplan not installed: using the NM keyfile directly)"
fi
# Section of each key, by name only (values are never printed).
section_of() { awk -F= -v k="$1" '/^\[/{g=$0} $1==k{print g}' "$KF" | tr '\n' ' ' | sed 's/ $//'; }
check_eq "PrivateKey stored in [vpn-secrets]" "[vpn-secrets]" "$(section_of PrivateKey)"
check_eq "Peer-PresharedKey stored in [vpn-secrets]" "[vpn-secrets]" "$(section_of Peer-PresharedKey)"
check_eq "Peer-PublicKey kept in [vpn] (not dropped by netplan)" "[vpn]" "$(section_of Peer-PublicKey)"
case $MODE in hpk | badhpk)
	check_eq "HeaderProtectionKey stored in [vpn-secrets]" "[vpn-secrets]" "$(section_of HeaderProtectionKey)" ;;
esac

section "connect"
fake_nm "" "$KF" > "$W/nm.out" 2> "$W/signals.log"
RC=$?
TUN=$(sed -n 's/^TUNDEV=//p' "$W/nm.out")

case $MODE in
badi1)
	check "connect is refused" test $RC -ne 0
	check "reason names I1" grep -q "invalid I1: <b> needs an even number of hex digits" "$W/nm.out"
	check_eq "no interface is left" "" "$(awg_links)"
	;;
badkey)
	check "connect is refused" test $RC -ne 0
	check "reason names the key" grep -q "invalid PresharedKey: must be a 32-byte key" "$W/nm.out"
	check "the key value is not in the error" sh -c "! grep -q 'NOT-A-KEY' '$W/nm.out' '$W/service.log'"
	check_eq "no interface is created" "" "$(awg_links)"
	;;
badawg)
	check "connect is refused" test $RC -ne 0
	check "awg's error is reported" grep -q "Fwmark is neither" "$W/nm.out"
	check "the quoted value is hidden" sh -c "! grep -q 'NOT-A-KEY' '$W/nm.out' '$W/service.log'"
	check_eq "no interface is left" "" "$(awg_links)"
	;;
*)
	check "connect succeeds" test $RC -eq 0
	check_eq "interface name" "awg-nm0" "$TUN"
	check_eq "address for NM" "10.8.1.2/32" "$(sed -n 's/^ADDR4=//p' "$W/nm.out")"
	check_eq "routes from AllowedIPs" "10.8.1.0/24 172.16.0.0/12" \
		"$(sed -n 's/^ROUTE4=//p' "$W/nm.out" | tr '\n' ' ' | sed 's/ $//')"
	check_eq "DNS for NM" "1.1.1.1 1.0.0.1" "$(sed -n 's/^DNS4=//p' "$W/nm.out" | tr '\n' ' ' | sed 's/ $//')"
	check_eq "not the default route (no 0.0.0.0/0)" "1" "$(sed -n 's/^NEVER_DEFAULT4=//p' "$W/nm.out")"
	check_eq "interface is labelled with its owner" "nm-amneziawg pid=$SVC" \
		"$(sed 's/ start=.*//' "/sys/class/net/$TUN/ifalias" 2>/dev/null)"
	check_eq "obfuscation applied (jc)" "4" "$(awg show "$TUN" jc 2>/dev/null)"
	# Apply what NM would apply, then send traffic through the tunnel.
	ip addr add 10.8.1.2/32 dev "$TUN"
	ip route add 10.8.1.0/24 dev "$TUN"
	if ping -c3 -W1 10.8.1.1 >/dev/null 2>&1; then GOT=traffic; else GOT=none; fi
	case $MODE in
	ok | hpk) check_eq "traffic through the tunnel" traffic $GOT ;;
	*) check_eq "no traffic with mismatched settings" none $GOT ;;
	esac
	case $MODE in hpk | badhpk)
		check "header protection key is set in the kernel" sh -c "awg show '$TUN' | grep -q 'header protection key'" ;;
	esac

	section "disconnect"
	check "disconnect succeeds" fake_nm_disconnect ""
	sleep 0.3
	check_eq "interface removed" "" "$(awg_links)"
	;;
esac

kill "$SVC" 2>/dev/null
wait "$SVC"
check_eq "service exits cleanly on SIGTERM" 0 $?
stop_bus
finish
