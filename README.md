# network-manager-amneziawg

**English** | [Русский](README.ru.md)

NetworkManager VPN plugin for **AmneziaWG**: an **AmneziaWG** VPN type in
Settings → Network → VPN and in `nm-connection-editor`, with import and export of
Amnezia `.conf` files.

> **Unofficial project**, not affiliated with the [Amnezia](https://github.com/amnezia-vpn)
> team. The plugin uses their kernel module and the `awg` tool. Report plugin bugs here.

## Installation

Ubuntu 24.04 or newer:

```sh
curl -fsSL https://github.com/MadeFrost/network-manager-amneziawg/releases/latest/download/install.sh | sh
```

The script adds [`ppa:amnezia/ppa`](https://launchpad.net/~amnezia/+archive/ubuntu/ppa)
(the `amneziawg` kernel module and `amneziawg-tools`) and
[`ppa:madefrost/amneziawg`](https://launchpad.net/~madefrost/+archive/ubuntu/amneziawg)
(the plugin), installs kernel headers if missing and checks that the module loads.

<details>
<summary>Manual installation</summary>

```sh
sudo add-apt-repository ppa:amnezia/ppa
sudo add-apt-repository ppa:madefrost/amneziawg
sudo apt install network-manager-amneziawg
```

</details>

## Usage

1. In the Amnezia app, export the config in the **AmneziaWG** format.
2. Settings → Network → VPN → "+" → "Import from file…".

From the command line: `nmcli connection import type amneziawg file amnezia.conf`.

## Good to know

- Keys are stored in the system connection, readable by root only.
- `PreUp`, `PostUp`, `PreDown`, `PostDown`, `Table` and `SaveConfig` are dropped on import.
- Configs without obfuscation parameters (Jc, S1, H1, I1…) are imported as plain WireGuard.
- Parameters the plugin does not know are passed to AmneziaWG as is, so new ones work
  without a plugin update.

## Limitations

- One `[Peer]` per connection.
- One IPv4 and one IPv6 address are taken from `Address`.
- If `AllowedIPs` has `::/0` but there is no IPv6 address (a typical Amnezia export), IPv6
  is blocked, as with `awg-quick`, rather than going around the VPN. The interface shows the
  placeholder address `fd61:7767::1/128`.
- Requires the kernel module; `amneziawg-go` is not supported.

## Troubleshooting

- **AmneziaWG is missing from the VPN list:** `sudo systemctl restart NetworkManager`.
- **Secure Boot.** DKMS signs the module with a local key (MOK), and the kernel accepts it
  only once the key is enrolled: after a reboot, in "MOK management" choose
  *Enroll MOK → Continue* and enter the password set during installation. If no password
  was asked: `sudo update-secureboot-policy --enroll-key` and reboot. The symptom:
  `modprobe amneziawg` prints `Key was rejected by service`.
- **Log:** `journalctl -u NetworkManager -g amneziawg`.

## Development

Building, tests, internals and releases: [CONTRIBUTING.md](CONTRIBUTING.md).

## Security and license

Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md), not in issues.
License: GPL-2.0-or-later ([LICENSE](LICENSE)). Changes per version: [CHANGELOG.md](CHANGELOG.md).
