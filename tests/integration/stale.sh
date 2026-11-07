#!/bin/sh
# Interfaces left behind by a service that was killed hard: the next connect
# removes them, and never touches interfaces that are still in use or not ours.
# (Run inside unshare -rnmpf.)
. "$(dirname "$0")/lib.sh"

setup_ns
start_bus

K1=$(awg genkey)
K2=$(awg genkey | awg pubkey)
printf '[Interface]\nPrivateKey = %s\nAddress = 10.8.1.2/32\nJc = 4\n\n[Peer]\nPublicKey = %s\nEndpoint = 127.0.0.1:51820\nAllowedIPs = 10.8.1.0/24\n' \
	"$K1" "$K2" > "$W/c.conf"

connect() { fake_nm "$1" "$W/c.conf" 2>/dev/null | sed -n 's/^TUNDEV=//p'; }
# Services keep running for a while after disconnect; stop them so that the
# "no other service" rule is tested on purpose, not by accident.
stop_all() { for p in "$@"; do kill "$p" 2>/dev/null; wait "$p" 2>/dev/null; done; }

section "service killed with SIGKILL, then a new connect"
start_service a
A=$SVC_PID
T=$(connect a)
check_eq "first connect on awg-nm0" awg-nm0 "$T"
kill -9 "$A"
wait "$A" 2>/dev/null
check_eq "after SIGKILL the interface is left behind" awg-nm0 "$(awg_links)"
start_service b
B=$SVC_PID
T=$(connect b)
check_eq "reconnect reuses awg-nm0" awg-nm0 "$T"
check_eq "only one interface" awg-nm0 "$(awg_links)"
check "removal is logged" grep -q "removing awg-nm0 left behind" "$W/service.log"

section "a second active connection is not touched"
start_service c
C=$SVC_PID
T=$(connect c)
check_eq "second connection on awg-nm1" awg-nm1 "$T"
check_eq "both interfaces exist" "awg-nm0 awg-nm1" "$(awg_links)"
fake_nm_disconnect c >/dev/null 2>&1
fake_nm_disconnect b >/dev/null 2>&1
sleep 0.3
check_eq "both removed on disconnect" "" "$(awg_links)"
stop_all "$B" "$C"

section "owner PID reused by another service process"
start_service d
D=$SVC_PID
ip link add awg-nm2 type amneziawg
ip link set dev awg-nm2 alias "nm-amneziawg pid=$D start=1"
start_service e
E=$SVC_PID
connect e >/dev/null
check "label with a live PID but another start time is stale" sh -c "! ip link show awg-nm2"
fake_nm_disconnect e >/dev/null 2>&1
stop_all "$D" "$E"

section "unlabelled leftovers (made by 0.3.5 and older)"
ip link add awg-nm0 type amneziawg
start_service f
F=$SVC_PID
T=$(connect f)
check_eq "removed when no other service runs" awg-nm0 "$T"
check_eq "only the new interface" awg-nm0 "$(awg_links)"
ip link add awg-nm7 type amneziawg
start_service g
G=$SVC_PID
connect g >/dev/null
check "kept while another service runs (it may belong to it)" ip link show awg-nm7
fake_nm_disconnect g >/dev/null 2>&1
fake_nm_disconnect f >/dev/null 2>&1
stop_all "$F" "$G"
ip link del awg-nm7

section "not ours"
ip link add awg-nm3 type amneziawg
ip link set dev awg-nm3 alias "my own tunnel"
ip link add awg-nm4 type dummy
ip link add awg-nm5 type amneziawg
ip link set dev awg-nm5 alias "nm-amneziawg pid=$$"
start_service h
H=$SVC_PID
connect h >/dev/null
check "foreign label is kept" ip link show awg-nm3
check "another interface type is kept" ip link show awg-nm4
check "owner PID alive but not the service: removed" sh -c "! ip link show awg-nm5"
fake_nm_disconnect h >/dev/null 2>&1
stop_all "$H"
ip link del awg-nm3
ip link del awg-nm4

stop_bus
finish
