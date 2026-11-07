# Manual test plan for network-manager-amneziawg

The service, the handshake and plugin loading are covered by the automated tests. This plan
covers what only shows in real GNOME and NetworkManager.

`amnezia` below is the connection name. Import takes it from the file name: `amnezia.conf`
becomes `amnezia`.

---

## 0. Preparation

1. Export the config from the Amnezia app in the **AmneziaWG** format (not "Amnezia" and
   not plain WireGuard). The `.conf` has `Jc`, `S1`, `H1` and so on:
   ```sh
   grep -E '^(Jc|S1|H1|I1|Endpoint)' ~/amnezia.conf
   ```
2. Note the server IP (from `Endpoint`) and your current public IP:
   ```sh
   curl -4 ifconfig.me; echo
   curl -6 ifconfig.me; echo      # may not answer if your ISP has no IPv6
   ```
3. If an Amnezia VPN is already running on this machine (Amnezia app or `awg-quick`), stop it
   so the two tunnels do not interfere.

## 1. Installation

`build/network-manager-amneziawg_0.4.0_amd64.deb` is built the same way Launchpad
builds it.

Over an installed version (the Amnezia PPA and the module are already there):

```sh
sudo apt install ./build/network-manager-amneziawg_0.4.0_amd64.deb
pkill gnome-control-center
```

On a clean system: `bash install.sh path/to/package.deb` (step 11).

Check:

```sh
dpkg -l network-manager-amneziawg amneziawg-tools amneziawg-dkms | tail -3   # all three ii, plugin 0.4.0
lsmod | grep amneziawg                     # module loaded
ls /usr/lib/NetworkManager/VPN/ | grep amneziawg
```

If `lsmod` prints nothing, run `sudo modprobe amneziawg`. `Key was rejected by service` means
Secure Boot (README, "Secure Boot").

**Expected:** connections made by the previous version are still listed and connect (step 5).

## 2. The VPN type in Settings

1. Quit Settings (`pkill gnome-control-center`) and open it again.
2. Network → VPN → "+".

**Expected:** **AmneziaWG** is listed, described as "WireGuard with AmneziaWG traffic
obfuscation". If not: `sudo systemctl restart NetworkManager` and reopen Settings.

## 3. The form (manual creation)

Choose AmneziaWG. The "Identity" tab opens.

| Check | Expected |
|---|---|
| Language | Same as the rest of Settings |
| Switcher on top | "General \| Obfuscation", switches |
| Empty form | "Add" is disabled, "Private Key" is red |
| Icon in the key field | Shows and hides the key |
| Port, MTU | 0 reads "automatic" |
| "Peers" | "No peers set up", "+" is enabled |
| "+" | The peer popover opens with its fields and "Apply" |
| "Apply" with empty fields | The popover stays, invalid fields are red |
| After filling in the peer | A row with the endpoint, "+" is disabled (tooltip "Only one peer is supported") |
| Gear | Opens the same peer for editing |
| Trash | Removes the peer, "No peers set up" again |
| "Obfuscation" tab | A note on top and fields Jc…I5 |
| Invalid key (e.g. `abc`) | Field red, "Add" disabled, hover tooltip "must be a 32-byte key in base64…" |
| MTU 500 | Field red, tooltip "must be a whole number from 576 to 65535" |
| Address `10.8.1.300/32` | Field red, tooltip "“10.8.1.300/32” is not an IP address with an optional prefix…" |
| Address `fd00::2/128` with AllowedIPs `0.0.0.0/0` | "Add" disabled, address field red, tooltip "an IPv4 address is needed: AllowedIPs has IPv4 networks" |
| Peer endpoint without a port (`vpn.example.com`) | "Apply" keeps the popover open, field red, tooltip "must be host:port…" |

Press "Cancel".

Russian UI (optional):

```sh
pkill gnome-control-center; LANGUAGE=ru gnome-control-center network
```

## 4. Import

1. VPN → "+" → "Import from file…" → `amnezia.conf`.
2. Compare the fields with the file:
   - "General": address, DNS, the server endpoint in the peer list;
   - gear: public key, allowed IP addresses, keepalive;
   - "Obfuscation": values match `grep -E '^(J|S|H|I)[0-9a-z]* =' ~/amnezia.conf`.
3. "Add".

Keys are stored and unreadable for regular users:

```sh
nmcli connection show amnezia | grep -E 'vpn.service-type|vpn.data' | cut -c1-120
sudo ls -l /etc/NetworkManager/system-connections/ | grep amnezia     # -rw------- root
# no keys in vpn.data (names only, no values):
nmcli -g vpn.data connection show amnezia | grep -oE '(PrivateKey|PresharedKey|HeaderProtectionKey)[-a-z]*'
# expected: only ...-flags
```

A plain WireGuard config (no `Jc`/`S1`/`H1`) must **not** import as AmneziaWG: NM offers it
as WireGuard.

## 5. Connecting

Turn the connection on in Settings or in the top-right menu.

**Expected:** the VPN icon in the top bar, no errors. Then:

```sh
nmcli connection show --active | grep amnezia      # active
ip -br addr | grep awg                             # awg-nm0 with the config's address
sudo awg show                                      # latest handshake: seconds ago,
                                                   # transfer grows, jc/s1/h1 as in the config
curl -4 ifconfig.me; echo                          # the VPN server's IP, not yours
ip route get <server_IP>                           # dev wlp…/enp…, NOT awg-nm0
resolvectl status awg-nm0                          # DNS from the config
resolvectl query example.com                       # resolves
```

DNS leaks: https://dnsleaktest.com → "Standard test". Only the config's DNS servers (or their
provider) may show up, not your ISP.

**IPv6:**

```sh
curl -6 ifconfig.me; echo
```

- The config has an IPv6 address and `::/0`: the server's IPv6.
- No IPv6 address but `::/0`: `curl -6` does not answer (IPv6 is blocked, not leaking past the
  VPN), `ip -br addr` shows `fd61:7767::1/128` on `awg-nm0`, and
  `ip -6 route get 2001:4860:4860::8888` shows `dev awg-nm0`. Sites still open at once
  (`curl ifconfig.me` answers over IPv4). After disconnecting, IPv6 works as before.

Service log:

```sh
journalctl -b -u NetworkManager -g 'amneziawg|awg-nm' --no-pager | tail -20
```

**Expected:** `created awg-nm0, endpoint …` and `awg-nm0 is up`, no errors.

## 6. Disconnecting

Turn the connection off.

```sh
ip -br link | grep awg          # nothing: interface removed
curl -4 ifconfig.me; echo       # your IP again
resolvectl status | grep -A2 'DNS Servers'   # ISP/router DNS
```

## 7. Scenarios

| Scenario | Expected |
|---|---|
| Connect, disconnect, connect 2–3 more times | Works every time, one interface (`awg-nm0`) |
| Switch from Wi-Fi to cable (or toggle Wi-Fi) with the VPN on | The VPN reconnects or disconnects with a notification; works after turning it on again |
| Laptop suspend and resume | Traffic flows after resume (`curl -4 ifconfig.me`); if not, reconnecting fixes it |
| Autoconnect: `nmcli connection modify amnezia connection.autoconnect yes`, reboot | The VPN is up after login |
| Wrong endpoint (`1.2.3.4:51820` in the form) | NM shows "connected" but no traffic, no handshake in `sudo awg show`. Normal for WireGuard: the protocol does not report an unreachable server |
| Nonexistent host (`no-such-host.invalid:51820`) | A connection error notification, `ip link \| grep awg` empty |
| Crash: with the VPN on, `sudo pkill -9 nm-amneziawg-se`, then turn the VPN on | NM shows the VPN went down. After turning it on, `ip -br link \| grep awg` shows one interface, the log has `removing awg-nm0 left behind by a service that did not exit cleanly` |
| Change H1 to any other number | No handshake (proves obfuscation is applied) |
| On "Obfuscation", delete the last digit in I1 (inside `<b 0x…>`) | Field red, "Apply" disabled, tooltip "<b> needs an even number of hex digits…". With the digit back the field is normal |
| Type `<x 5>` into I1 | Field red, tooltip "unknown tag <x>…" |

Afterwards restore the values or import the config again.

**Checks in the service.** Values written past the form are checked by the service before
connecting:

```sh
nmcli connection modify amnezia +vpn.data MTU=500
nmcli connection up amnezia          # fails, no tunnel
journalctl -b -u NetworkManager -g 'invalid MTU' --no-pager | tail -1
nmcli connection modify amnezia -vpn.data MTU
```

**Expected:** `invalid MTU: must be a whole number from 576 to 65535` in the log,
`ip -br link | grep awg` empty.

**Values stay out of the log.** An invalid FwMark passes the service's checks but `awg`
rejects it. Its error quotes the value, and the service must hide it:

```sh
nmcli connection modify amnezia +vpn.data FwMark=test-secret-42
nmcli connection up amnezia          # fails
journalctl -b -u NetworkManager -g 'Fwmark' --no-pager | tail -2
journalctl -b -u NetworkManager --no-pager | grep -c test-secret-42
nmcli connection modify amnezia -vpn.data FwMark
```

**Expected:** `Fwmark is neither…` with `(hidden)` instead of the value, count `0`. After both
checks the connection connects again. If `-vpn.data` did not work, import the config again.

## 8. Export

```sh
nmcli connection export amnezia ~/exported.conf
ls -l ~/exported.conf                # -rw------- (owner only)
diff <(grep -v '^#' ~/amnezia.conf | sort) <(sort ~/exported.conf)
```

**Expected:** the same parameters (order may differ). No `PostUp/PreDown/Table`: import drops
them.

## 9. nm-connection-editor (if installed)

```sh
nm-connection-editor
```

Open the connection: the same form (GTK3), saving works.

## 10. Reinstall and removal

```sh
# disconnect the VPN, then:
sudo apt install --reinstall ./build/network-manager-amneziawg_0.4.0_amd64.deb
pkill gnome-control-center
```

**Expected:** the `amnezia` connection is still there and connects.

```sh
sudo apt remove network-manager-amneziawg
```

**Expected:** AmneziaWG is gone from VPN → "+", the connection is still listed but does not
connect. After reinstalling it works again.

## 11. Clean system (virtual machine)

`install.sh` from scratch, easiest in GNOME Boxes:

1. Install Ubuntu 24.04 Desktop, update (`sudo apt update && sudo apt full-upgrade`).
2. Copy `install.sh` and the `.deb` into the VM, run
   `bash install.sh network-manager-amneziawg_*.deb`. Once the PPA has the package, run it
   without the `.deb`.
   **Expected:** the script shows the plan, installs what is missing after confirmation and
   prints "Done.".
3. Go through steps 2–6.
4. If possible, repeat on a newer Ubuntu: the PPA package is copied there too.
5. Separately: a VM with Secure Boot (Boxes: UEFI + Secure Boot). Expected: a MOK password
   prompt during installation and the blue MOK screen after reboot.

---

## If something is wrong, send

```sh
journalctl -b -u NetworkManager --no-pager | grep -iE 'amneziawg|awg-nm|vpn' | tail -50
nmcli connection show amnezia | grep -vE 'secrets|PrivateKey|PresharedKey'
sudo awg show                 # does not print the private key
ip route; ip -6 route
```

Plus a screenshot of the form for UI problems.

**Do not send** `PrivateKey`, `PresharedKey` or anything from
`/etc/NetworkManager/system-connections/`: keys are stored there in plain text.
