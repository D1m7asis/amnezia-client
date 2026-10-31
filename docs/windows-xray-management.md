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
Installer and real network acceptance remain pending.

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
Conan dependencies. The Windows fork currently uses version `5.0.3.100` and display
name `D1m7asis VPN`. No signing certificate is included. Guest profiles and release
artifacts must not contain administrator credentials or personal Reality profiles.

Successful builds and backend tests do not establish completion. The installed
client must pass independent internet checks, SSH with the tunnel disconnected,
reconnect/restart, actual rollback and post-reboot checks. Keep the original
installation and the verified local backup until these gates pass.
