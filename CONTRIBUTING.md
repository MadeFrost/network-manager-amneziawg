# Contributing

## Building

```sh
sudo apt install build-essential pkg-config gettext meson debhelper libnm-dev libgtk-4-dev libgtk-3-dev
make deb         # build/network-manager-amneziawg_*_amd64.deb
```

Other targets: `make` (build in `build/`), `make check` (unit tests), `make check-integration`,
`sudo make install` / `uninstall`. The `Makefile` wraps Meson. To skip an editor:
`meson configure build -Dgtk3=disabled` (or `-Dgtk4=disabled`).

`make deb` builds through `debian/`, as Launchpad does, from the working tree.

## Tests

Integration tests need the `amneziawg` module loaded, but not root; see
[tests/README.md](tests/README.md). Manual test plan: [TESTING.md](TESTING.md).

## How it works

| Component | Purpose |
|---|---|
| `/usr/libexec/nm-amneziawg-service` | Service NM runs as root: creates `awg-nmN`, configures it with `awg setconf` (keys go through stdin) and gives NM the addresses, routes from `AllowedIPs` and DNS |
| `/usr/libexec/nm-amneziawg-auth-dialog` | Returns the stored keys to the secret agent (GNOME Shell, nm-applet). Without it the Settings form hangs on opening |
| `libnm-vpn-plugin-amneziawg.so` | `.conf` import and export, loads the GTK3 or GTK4 editor |
| `libnm-gtk4-…-editor.so`, `libnm-…-editor.so` | The form for GNOME Settings (GTK4) and nm-connection-editor (GTK3) |

Sources: `src/shared/` (key registry, value checks, `.conf` handling), `src/service/`,
`src/plugin/`, `src/editor/`, `src/auth-dialog/`.

- Settings live in `vpn.data`: `[Interface]` keys as is, `[Peer]` keys with a `Peer-`
  prefix. Not a dot: netplan on Ubuntu turns `Peer.PublicKey` into a `[vpn.Peer]` group,
  which NM drops.
- `PrivateKey`, `Peer-PresharedKey` and `HeaderProtectionKey` live in `vpn.secrets`.
  Connections from 0.3.3 and older kept `HeaderProtectionKey` in `vpn.data`; it moves to
  the secrets on the next save.
- The editor and the service check values with the same code (`src/shared/awg-validate.c`).
  Error messages name the key, never a secret value; text that `awg` quotes in its errors
  is hidden.
- A crashed service leaves its interface behind, and the server starts sending traffic to
  it instead of the new connection. So before connecting, the service removes `awg-nmN`
  interfaces whose owner (the PID and start time in the interface `alias`) is gone.
- With `::/0` in `AllowedIPs` and no IPv6 address, the placeholder `fd61:7767::1/128` lets
  NM install the IPv6 default route into the tunnel; being a ULA, it makes the system
  prefer IPv4, so sites open without IPv6 timeouts.

## Translations

Labels shared with the WireGuard page come from the GNOME Settings translation. A new
language:

```sh
make pot
msginit -i po/network-manager-amneziawg.pot -o po/uk.po -l uk_UA.UTF-8
echo uk >> po/LINGUAS
```

After changing strings in the code: `make update-po`.

## Releasing

1. Set the version in `meson.build`, add a `debian/changelog` entry with the same version
   (series `noble`) and a section in `CHANGELOG.md`. The package is native: a packaging
   fix needs a new version too.
2. Tag `v<version>` and publish a GitHub release for it. The `Release` workflow uploads the
   source package to the PPA and attaches `install.sh` and the `.deb` to the release.
3. On Launchpad, copy the built binaries to newer Ubuntu series.

By hand instead of step 2: `packaging/ppa.sh` builds the signed source package from the
last commit, then `dput ppa:madefrost/amneziawg build/ppa/network-manager-amneziawg_*_source.changes`.
