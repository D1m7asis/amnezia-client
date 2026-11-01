# Windows: SSH management through Xray

This fork targets a Windows client whose server management can use a separate
Xray Reality transport. The public VPN server address remains independent of the
SSH forwarding destination.

## Configuration

Self-hosted administrator configurations can contain an optional `sshManagement`
object. It is stored with the administrator's encrypted settings. Guest
serialization does not include this object or SSH credentials.

| Field | Meaning |
| --- | --- |
| `transport` | `direct` (default) or `xray` |
| `targetHost` | SSH destination reached from the Xray server; empty uses the public server host |
| `targetPort` | SSH destination port; zero uses the existing SSH port |
| `hostKeySha256` | Expected SSH host fingerprint in canonical `SHA256:` format |
| `xrayConfig` | Imported client configuration containing one VLESS Reality outbound |

Old profiles remain direct and do not acquire extra serialized fields. Unknown
management modes, malformed configuration objects, missing host pins and
ambiguous Reality outbounds are rejected. Management destinations never replace
`hostName` or the original SSH port in `ServerCredentials`.

## Current implementation boundary

The native transport is integrated in the central libssh client, including SCP.
An unprivileged child process uses the existing native Xray bindings; configuration
is passed through stdin, not a command line or a temporary credential file. The
helper binds its external sockets to a physical uplink and exits when its parent
dies. The parent tries available physical uplinks in order after transport failures.
SSH verifies the pinned host key before authentication. Invalid settings, changed
credentials and dead helpers invalidate cached SSH sessions; there is no silent
fallback to direct SSH. Direct SSH without an explicit pin requires a trusted
known_hosts entry.

Server settings include management transport, target, trusted fingerprint, local
Reality JSON import and a harmless SSH command test. The home page has a separate
explicit HTTPS check against two services, with an exit-IP comparison when the
server address is numeric. This check is evidence of HTTP reachability, not proof
of routing ownership: full acceptance separately checks services, the fork adapter
and routes with the previous VPN stopped.

The Windows fork has separate settings, keychain, IPC, service and adapter names,
firewall identifiers and installer identity. Its updater does not install official
Amnezia builds. The signed split-tunnel driver remains shared; the fork does not
restart a busy driver and initializes it only for application split tunneling.
The installed `5.0.3.101` client passed manual Xray connection and reconnect
checks. AmneziaWG worked with a temporary endpoint route, but inspection found
that its separate legacy control pipe still collided with the official client.
Those results do not establish acceptance of the corrected `5.0.3.102` build.

The `test_ssh_management` executable checks legacy compatibility, preservation
of the public endpoint, administrator/guest boundaries, mandatory host pins,
invalid modes, ambiguous outbounds and invalid management ports. These tests
exercise the model; they do not prove VPN connectivity or an installed client.
`test_ssh_management_runtime` requires an explicitly supplied protected local
administrator fixture and otherwise skips. Its live tests cover pinned SSH,
changed pins/settings, SCP integrity and cleanup, occupied listener ports, stopping
the helper and parent-process death. Never publish that fixture or pass real
credentials to the general test suite.

The management page exposes only transport, SSH destination, trusted fingerprint
and whether a Reality profile is stored. It never returns the raw profile or SSH
secret to QML. The test button verifies the saved settings with a harmless command.
Windows fork file imports use the built-in Qt dialog. Startup tun2socks lines
containing authenticated SOCKS URLs are omitted from logs.

## Build and acceptance

Baseline: upstream tag `5.0.3.0`, Windows x64, VS 2022, Qt 6.10.1 and the existing
Conan dependencies. The Windows fork currently uses version `5.0.3.102` and display
name `D1m7asis VPN`. No signing certificate is included. Guest profiles and release
artifacts must not contain administrator credentials or personal Reality profiles.

Successful builds and backend tests do not establish completion. The installed
client must pass independent internet checks, SSH with the tunnel disconnected,
reconnect/restart, actual rollback and post-reboot checks. Keep the original
installation and the verified local backup until these gates pass.

Version `5.0.3.101` adds Windows service shutdown cleanup before daemon destruction.
The quit callback is bound to the server object's lifetime; a service stop cannot
call a destroyed daemon. Shutdown also clears the process's saved routes and DNS.
IPv6 protection uses the Windows IP Helper API: an existing loopback route is
accepted without taking ownership, and disconnect removes only routes created by
this service. This avoids failing a connection when another client left the same
IPv6 protection routes behind. Xray setup no longer logs VLESS client identifiers.

Version `5.0.3.102` gives both ends of the Windows WireGuard control channel their
own `d1m7asisvpn-wg` named pipe. The fork must not use the official service as a
fallback. Endpoint exclusion failures now abort peer activation rather than
silently routing encrypted WireGuard traffic through its own tunnel. Route
capture registers exclusions before cloning other routes, treats an absent IPv6
endpoint as optional, and checks routing-table reads before using the result.
WireGuard address allocation selects a free host inside the configured subnet;
a full or invalid subnet fails before adding a peer. Existing client addresses
are not rewritten. Local file dialogs receive file URLs made from local paths.

The elevated opt-in `WindowsEndpointRouteTest` creates and removes only a
`192.0.2.47/32` TEST-NET route. Set `D1MISVPN_TEST_ROUTE_ADMIN=1` and
`D1MISVPN_TEST_EXCLUDE_INTERFACE` to the current VPN interface index. The test
refuses to overwrite an existing test route. General CTest runs skip this test's
privileged operation. Acceptance still requires the installed fork's own WG
adapter and daemon, without the temporary manual endpoint route.

### Windows reconnect and settings recovery (5.0.3.103, 2026-11-01)

Fresh local logs confirmed error 101 at Windows TUN address assignment after a
10-second wait. The service now queries the actual interface/address/DAD state,
including already assigned addresses and the final timeout check. It removes
only addresses created by its own session. Native API boundary tests do not
change the user's network.

The Windows tun2socks recipe vendors its pinned WireGuard Go module and opens an
existing `d1misxray` adapter before creating one. Other adapter names retain
upstream behavior. This keeps transport, IP assignment, DNS and routing on the
same interface. The existing Windows dependency is patched in a private vendor
copy, never in the shared Go cache. Source references checked 2026-11-01:
[tun2socks 2.6.0](https://github.com/xjasonlyu/tun2socks/blob/v2.6.0/core/device/tun/tun_wireguard_windows.go)
and its pinned WireGuard Go revision `f333402bd9cb`.

Fork helper processes are created atomically inside a Windows job with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. The fork does not kill helpers by name or
log their arguments. Cancelled Xray attempts invalidate queued retries and stack
callbacks. Matching existing WG routes are borrowed and never deleted; owned
routes are tracked separately. Minimal local failure records include only UTC,
stage and numeric code, even if verbose logging is disabled.

Managed WG cached addresses are validated against the known server subnet before
connection. Imported profiles without authoritative subnet information are not
subject to this check. Invalid data is rejected without overwriting the cache.

The Windows fork stores its encryption key and IV together in a single atomic
Credential Manager entry scoped by keychain, organization and application:
`D1m7asisVPN-Keychain/D1m7asis.VPN/D1m7asisVPN/settingsMaterial-v1`.
Migration reads historical shared keys without modifying them, validates the
existing encrypted JSON array, writes and verifies a CurrentUser-DPAPI recovery
file, and only then copies the material into the scoped credential. An unreadable
or corrupt store never triggers replacement keys; startup shows a recovery
message rather than presenting an empty profile list. Tests use UUID-scoped
credentials and registry data, never the user's production credential targets.

Offline update sources are in `deploy/windows/offline-update`. The packaged
update includes verified 102 and 103 installers, backups, local operation reports
and `ROLLBACK.cmd`. The migration removes only the obsolete `10.8.2.1` WG
container from the identified Reality management profile, preserving Xray, SSH,
profile IDs and the separate working WG profile. It does not revoke server peers.
The updater removes stale own Xray device instances only after stopping exact
owned processes and validating adapter GUID, Wintun device ID and service.
It never removes the shared Wintun driver.

The updater does not launch the GUI or auto-connect. Installation, VPN switching,
outage tests and reboot remain user-controlled.
Build/unit/mock checks do not establish installed-network acceptance. After
installation verify both profiles, restart persistence, WG and Xray DNS/HTTPS/IP,
and pinned SSH through Reality. Installer payloads contain no personal profiles;
backups created later under `live-private` must remain private.

### 103 helper revision 1: Windows error 1168

The installed Xray error 804 was traced to `OpenAdapter` returning
`ERROR_NOT_FOUND` (1168) for a missing own adapter. The helper now creates the
adapter for both missing-file (2) and missing-element (1168), including wrapped
errors; access denied and invalid parameter remain failures. The pinned Go
dependency regression runs during its Windows build.

The offline kit identifies the helper separately with `helperRevision` and its
SHA-256. On an existing 103 installation the updater replaces only the verified
helper after stopping owned resources, keeps a protected old-file backup, and
restores it if copying fails. Unknown binaries and damaged payloads are rejected
before stopping VPN. No VPN is automatically connected after updating.

This fixes the confirmed Xray startup regression. A separate WG Wintun creation
failure (problem 31 / NTSTATUS 0xC00002F0) remains under investigation. Shared
driver resets are deliberately excluded because another active VPN uses it.
Profile pruning was observed at initial startup but the obsolete container later
reappeared; its persistence is not accepted yet. Installed Xray, WG and SSH
acceptance still requires user-controlled switching.

### 104: mutually exclusive Windows tunnels

Fresh installed logs showed WG reaching startup and then failing at address
configuration with Windows 5010. The official `AmneziaVPN` interface was active
with the same `10.8.1.6/32` address. Xray could start but the browser reported
`ERR_NETWORK_ACCESS_DENIED`; overlapping VPN filtering is suspected, not proven
by a WFP capture (enumeration requires elevation).
Read-only checks on 2026-11-01 additionally found the official tunnel process
running continuously since 16:26:55 Kaliningrad, before the 16:42-16:48 failures,
and its `Conf/killSwitchEnabled` set to true. Its current exit IP is the same
server, `2.27.29.231`; server reachability alone is therefore not fork acceptance.

The fork now rejects a WG or Xray start while an active official `AmneziaVPN`
interface has a usable IPv4 address. Error 107 explains manual disconnection.
This check precedes backend activation and is repeated on reconnect. It never
stops a foreign VPN, removes its routes or changes its firewall. Down, empty,
loopback and link-local-only adapters, Radmin and physical interfaces do not
trigger it. This prevents the confirmed concurrent-tunnel address conflict;
single-tunnel installed WG/Xray acceptance is still required. Earlier Wintun
problem-31 failures are not declared solved by this guard.

### Rebuilding

Use Developer PowerShell for VS 2022, Qt `6.10.1` (`msvc2022_64`), Conan `2.28.0`,
CMake `4.1.2` and Qt Installer Framework `4.7.0`. The tested Windows SDK is
`10.0.22621.0`; its x64 tools directory must be on PATH so OpenVPN finds `mc.exe`.
Initialize the pinned upstream submodules and preserve the existing Conan recipes.

```powershell
git submodule update --init --recursive
cmake -S . -B deploy/build -G "Visual Studio 17 2022" `
  -DCMAKE_PREFIX_PATH="<Qt>/6.10.1/msvc2022_64" `
  -DCMAKE_BUILD_TYPE=Release -DCONAN_INSTALL_BUILD_CONFIGURATIONS=Release `
  -DAMNEZIA_BUILD_TESTS=ON
cmake --build deploy/build --config Release --parallel 4
$env:QT_QPA_PLATFORM = "offscreen"
ctest --test-dir deploy/build/client/tests -C Release --output-on-failure
Set-Location deploy/build
cpack -G IFW -D "QTIFWDIR=<Qt>/Tools/QtInstallerFramework/4.7"
```

For tests, add the Qt and resolved Conan package `bin` directories to PATH and set
`QT_PLUGIN_PATH` to the Qt `plugins` directory. Unset external test credentials
before the general suite; production server tests use the separate opt-in fixture.
Store private administrator profiles outside the checkout and installer contents.
