# Tests

Two suites: fast unit tests and integration tests with the real kernel module. Neither needs
root or touches the real system.

```sh
make check                              # unit tests, a few seconds
make check-integration                  # integration tests, about 15 seconds
meson test -C build                     # everything
meson test -C build "e2e hpk" stale     # selected tests
```

Full output: `build/meson-logs/testlog.txt`, for integration tests also `build/tests/logs/`.

## Unit (`tests/unit/`)

No kernel module or network needed.

| Test | Checks |
|---|---|
| `test-conf` | `.conf` import through the plugin, refusal of plain WireGuard and broken configs, export and re-import, where secrets are stored, the `awg setconf` text, CIDR and Endpoint parsing |
| `test-service` | what the service gives NM: addresses, routes, DNS, the IPv6 placeholder `fd61:7767::1/128` for `::/0` without an IPv6 address; hiding values in `awg` errors |
| `test-ipacket` | I1–I5 validation against `tests/data/ipacket-cases.txt` |
| `test-editor` | saving from the GTK4 form (GNOME Settings): secrets without a field are kept, an old `HeaderProtectionKey` moves to the secrets, an invalid I1 is not saved. Needs a display, otherwise `SKIP` |

## Integration (`tests/integration/`)

Each test runs in its own `unshare -rnmpf` (user, network, mount and PID namespaces):
interfaces, the test server and the D-Bus bus vanish with it.

Needs: the `amneziawg` module loaded (`sudo modprobe amneziawg`), `amneziawg-tools`,
`dbus-daemon`; for `nm-real` also NetworkManager. If something is missing, the test is
skipped and says why.

| Test | Checks |
|---|---|
| `e2e ok` | the service against a test server: storage through netplan as on Ubuntu, connecting, addresses and routes for NM, traffic through the tunnel, disconnecting |
| `e2e badobfs` | the server has a different H1: no traffic (obfuscation is really applied) |
| `e2e hpk` / `badhpk` | `HeaderProtectionKey` matches / does not match the server |
| `e2e badi1` | invalid I1: a clear refusal, no interface created |
| `e2e badkey` | a broken key: refused before `awg` runs, the key value shown nowhere |
| `e2e badawg` | a value only `awg` rejects (`FwMark`): `awg`'s error in the log, without the value |
| `stale` | removal of interfaces left by a crashed service; foreign and live ones are kept |
| `ipacket-kernel` | the kernel module reads I1–I5 as the case table says |
| `nm-real` | real NetworkManager: import with `nmcli`, connecting, the route to the server outside the tunnel, the IPv6 block, disconnecting |

Without Meson: `tests/run-integration.sh "e2e hpk" stale` (uses the programs in `build/`).

## I1–I5 case table

`tests/data/ipacket-cases.txt` is shared by `test-ipacket` (our check) and `ipacket-kernel`
(the kernel). If `ipacket-kernel` fails after a module update, the kernel's I1–I5 syntax has
changed: update the table and `awg_check_ipacket()` in `src/shared/awg-validate.c`.

## Adding a test

- Unit: a new `tests/unit/test-*.c` with `CHECK (name, condition)` from `check.h`, plus
  `executable()` and `test(..., suite: 'unit')` in `tests/meson.build`.
- Integration: a script in `tests/integration/` that starts with
  `. "$(dirname "$0")/lib.sh"` and `setup_ns`, checks with `check` / `check_eq` and ends
  with `finish`. Add it to the list in `tests/meson.build` and in `tests/run-integration.sh`.
