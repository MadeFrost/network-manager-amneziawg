#!/bin/sh
# The real NetworkManager with this plugin: import with nmcli, connect over an
# NM-managed "uplink", check routes, the tunnel and the IPv6 block, disconnect.
# The installed package is not used: a private .name points NM at $BUILD.
# (Run inside unshare -rnmpf.)
. "$(dirname "$0")/lib.sh"
command -v NetworkManager >/dev/null && command -v nmcli >/dev/null || skip "NetworkManager is not installed"

setup_ns
mount -t tmpfs tmpfs /var/lib/NetworkManager
mount -t tmpfs tmpfs /etc/NetworkManager
mkdir -p /etc/NetworkManager/system-connections /etc/NetworkManager/conf.d

# libnm only loads plugins from root-owned, not group-writable files: copies.
PL=$W/plugin
mkdir -p "$PL/vpn"
for f in libnm-vpn-plugin-amneziawg.so libnm-gtk4-vpn-plugin-amneziawg-editor.so libnm-vpn-plugin-amneziawg-editor.so; do
	[ -f "$BUILD/$f" ] && install -m644 "$BUILD/$f" "$PL/$f"
done
install -m755 "$SERVICE" "$PL/nm-amneziawg-service"
install -m755 "$BUILD/nm-amneziawg-auth-dialog" "$PL/nm-amneziawg-auth-dialog"
sed -e "s|^program=.*|program=$PL/nm-amneziawg-service|" \
	-e "s|^plugin=.*|plugin=$PL/libnm-vpn-plugin-amneziawg.so|" \
	-e "s|^auth-dialog=.*|auth-dialog=$PL/nm-amneziawg-auth-dialog|" \
	"$ROOT/data/nm-amneziawg-service.name.in" > "$PL/vpn/nm-amneziawg-service.name"
chmod 644 "$PL/vpn/nm-amneziawg-service.name"
mount --bind "$PL/vpn" /usr/lib/NetworkManager/VPN

cat > /etc/NetworkManager/NetworkManager.conf <<'EOF'
[main]
plugins=keyfile
dns=none
rc-manager=unmanaged
[keyfile]
unmanaged-devices=none
[logging]
level=DEBUG
domains=DEVICE,CORE,VPN
EOF

# "ISP" and server in netns srv; hosts standing in for the IPv4 and IPv6 internet.
CPRIV=$(awg genkey)
CPUB=$(echo "$CPRIV" | awg pubkey)
start_server "$CPUB"
ip -n srv addr add 2001:db8:1::2/64 dev v1 nodad
ip -n srv addr add 2001:db8:ffff::1/128 dev lo
ip -n srv addr add 10.99.0.1/32 dev lo
ip netns exec srv sysctl -qw net.ipv6.conf.all.forwarding=1
ip addr flush dev v0 # NM configures the uplink

# Typical Amnezia export: IPv4 address only, but AllowedIPs with ::/0.
cat > "$W/client.conf" <<EOF
[Interface]
PrivateKey = $CPRIV
Address = 10.8.1.2/32
DNS = 1.1.1.1
$OBFS

[Peer]
PublicKey = $SPUB
Endpoint = 192.168.77.2:51820
AllowedIPs = 0.0.0.0/0, ::/0
PersistentKeepalive = 25
EOF

start_bus
NetworkManager --no-daemon --debug > "$W/nm.log" 2>&1 &
NM=$!
for i in $(seq 1 20); do nmcli -t general status >/dev/null 2>&1 && break; sleep 0.5; done
check "NetworkManager is running" nmcli -t general status
sleep 1

section "uplink (stands in for Wi-Fi or Ethernet)"
nmcli device set v0 managed yes >/dev/null 2>&1
sleep 1
nmcli -w 10 connection add save no type ethernet ifname v0 con-name uplink \
	ipv4.method manual ipv4.addresses 192.168.77.1/24 ipv4.gateway 192.168.77.2 \
	ipv6.method manual ipv6.addresses 2001:db8:1::1/64 ipv6.gateway 2001:db8:1::2 >/dev/null
check "uplink comes up" nmcli -w 15 connection up uplink
sleep 1
check "IPv4 internet reachable before VPN" retry 10 ping -c1 -W1 10.99.0.1
check "IPv6 internet reachable before VPN" retry 10 ping -6 -c1 -W1 2001:db8:ffff::1

section "VPN up"
check "nmcli imports the config" nmcli connection import --temporary type amneziawg file "$W/client.conf"
check "nmcli connection up" nmcli -w 20 connection up client
sleep 2
check "handshake with the server" retry 10 sh -c "awg show awg-nm0 latest-handshakes | awk '{exit !(\$2 > 0)}'"
check "IPv4 default route via awg-nm0" sh -c "ip -4 route show default | head -1 | grep -q 'dev awg-nm0'"
check "server reached via the uplink, not the tunnel" sh -c "ip route get 192.168.77.2 | grep -q 'dev v0'"
check "tunnel works" ping -c2 -W1 10.8.1.1
check "IPv6 placeholder address on awg-nm0" sh -c "ip -6 addr show dev awg-nm0 | grep -q 'fd61:7767::1/128'"
check "IPv6 internet routed into the tunnel" sh -c "ip -6 route get 2001:db8:ffff::1 | grep -q 'dev awg-nm0'"
check "IPv6 internet blocked (no leak around the VPN)" sh -c "! ping -6 -c1 -W1 2001:db8:ffff::1"

section "VPN down"
check "nmcli connection down" nmcli -w 10 connection down client
sleep 1
check_eq "interface removed" "" "$(awg_links)"
check "IPv6 internet reachable again" retry 10 ping -6 -c1 -W1 2001:db8:ffff::1
check "IPv4 internet reachable again" retry 10 ping -c1 -W1 10.99.0.1

if [ "$FAILED" -ne 0 ]; then
	section "NM log (VPN lines)"
	grep -iE 'vpn|amnezia|awg' "$W/nm.log" | grep -v netplan | sed -E 's/^<[a-z]+> +\[[0-9.]+\] //' | cut -c1-200 | tail -20
fi
kill "$NM" 2>/dev/null
wait "$NM" 2>/dev/null
stop_bus
finish
