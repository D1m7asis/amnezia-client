# Windows offline update and rollback

Package these scripts with the current installer, its generated
`release-manifest.json`, the previous verified `installed-manifest.json`, and
the complete previous installer under `rollback/`. Do not package personal
profiles, credentials, diagnostic logs or an existing `live-private/` directory.

The user runs `INSTALL-UPDATE.cmd`. It elevates the operation, verifies the
installed application and service against known manifests, saves a CurrentUser
DPAPI snapshot, closes only exact owned processes, removes only identified own
stale Xray devices, installs and verifies the binaries, and migrates the obsolete
local WG container out of the Reality profile. The standalone working WG and
Xray/SSH settings remain intact. The updater does not launch the GUI; the user opens the client and connects manually.

`ROLLBACK.cmd` uses the complete packaged previous installer and the pre-update profile
snapshot, including after a failed install removed the installation directory.
Rollback refuses to overwrite a changed profile count. Missing/mismatched
credentials fail closed; no shared Credential Manager target is overwritten.
Keep `live-private/`, the rollback installer and reports until network acceptance.

The update deliberately interrupts the fork when the user runs it. It never
switches VPN connections, stops the official Amnezia service, deletes server
peers, restarts server containers or removes the shared Wintun driver.

After manually selecting one VPN, run `CHECK-CONNECTION.cmd` for a read-only
DNS/HTTPS/download/exit-IP and route check. An active official tunnel is reported
explicitly and prevents acceptance even if its exit IP matches the same server.

The legacy `installed102-manifest.json` filename is accepted for existing kits.
Set `usesScopedCredentials` only for a previous build already using the fork's
Credential Manager namespace; legacy rollback additionally validates shared keys.

Run the standalone `tests/Test-*.ps1` files using Windows PowerShell 5.1.
The additional `Test-QtCodec.ps1` requires the built `test_profile_codec` probe
through its `-Probe` argument and checks the real PowerShell-to-Qt storage format. They use
synthetic profiles, isolated UUID registry keys and mocked system operations.
CTest registers them on Windows. These checks do not prove a real installation
or a working VPN; those are separate user-controlled acceptance steps.
