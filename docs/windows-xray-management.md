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
