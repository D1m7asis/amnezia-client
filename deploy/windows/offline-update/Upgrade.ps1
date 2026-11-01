[CmdletBinding()]
param([switch]$ValidateOnly,[switch]$Rollback)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Profile-Store.ps1')
. (Join-Path $PSScriptRoot 'Stop-OwnedVpn.ps1')
. (Join-Path $PSScriptRoot 'Remove-OwnedXrayAdapters.ps1')
. (Join-Path $PSScriptRoot 'Update-OwnedHelper.ps1')
$root='C:\Program Files\D1m7asisVPN'
$old=Get-Content (Join-Path $PSScriptRoot 'installed102-manifest.json') -Raw | ConvertFrom-Json
$new=Get-Content (Join-Path $PSScriptRoot 'release-manifest.json') -Raw | ConvertFrom-Json
$target=if($Rollback){$old}else{$new}
$installer=if($Rollback){Join-Path $PSScriptRoot ('rollback\'+$old.installer)}else{Join-Path $PSScriptRoot $new.installer}
if((Get-FileHash -LiteralPath $installer).Hash -ine $target.installerSha256){throw 'Offline installer checksum mismatch'}
$current=$null
if(Test-Path -LiteralPath (Join-Path $root 'D1m7asisVPN.exe')){
 $appHash=(Get-FileHash -LiteralPath (Join-Path $root 'D1m7asisVPN.exe')).Hash
 $servicePath=Join-Path $root 'D1m7asisVPN-service.exe'
 $serviceHash=if(Test-Path -LiteralPath $servicePath){(Get-FileHash -LiteralPath $servicePath).Hash}else{$null}
 foreach($manifest in @($old,$new)){if($manifest.appSha256 -eq $appHash -and $manifest.serviceSha256 -eq $serviceHash){$current=$manifest}}
 if(-not $current -and (-not $Rollback -or $appHash -notin @($old.appSha256,$new.appSha256) -or ($serviceHash -and $serviceHash -notin @($old.serviceSha256,$new.serviceSha256)))){throw 'Unknown installed binaries; refusing to remove them'}
}
if(-not $current -and -not $Rollback){throw 'Verified previous installation is required'}
[void]@(Get-OwnedVpnServices)
$recovery=$null
if($Rollback){
 $pointer=Join-Path $PSScriptRoot 'last-upgrade-backup.txt'
 if(-not (Test-Path -LiteralPath $pointer)){throw 'No completed backup reference for rollback'}
 $recovery=[IO.Path]::GetFullPath((Get-Content -LiteralPath $pointer -Raw).Trim())
 $allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'live-private'))+[IO.Path]::DirectorySeparatorChar
 if(-not $recovery.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Recovery path outside protected backup directory'}
}
$hasCurrentProfiles=$true
try{$state=Get-VpnProfileState}catch{
 if(-not $Rollback){throw}
 $hasCurrentProfiles=$false
 $metadata=Get-VpnSnapshotMetadata (Join-Path $recovery 'profiles.dpapi')
 $state=[pscustomobject]@{Count=$metadata.Count;Hash=$null}
}
if($Rollback){
 Assert-VpnLegacyRecoveryCompatibility (Join-Path $recovery 'profiles.dpapi')
 $metadata=Get-VpnSnapshotMetadata (Join-Path $recovery 'profiles.dpapi')
 if($hasCurrentProfiles -and $state.Count -ne $metadata.Count){throw 'Profile count changed since upgrade; automatic rollback would overwrite newer profiles'}
}

if($state.Count -lt 1){throw 'No readable profiles; update cancelled without stopping VPN'}
if(-not $Rollback -and $new.helperRevision){
 $helperPayload=Join-Path $PSScriptRoot 'helper-update\tun2socks.exe'
 if((Get-FileHash -LiteralPath $helperPayload).Hash -ine $new.tun2socksSha256){throw 'Helper payload checksum mismatch'}
 if($current.appSha256 -eq $new.appSha256 -and (Get-FileHash -LiteralPath (Join-Path $root 'tun2socks.exe')).Hash -notin @($new.tun2socksSha256,$new.previousTun2socksSha256)){throw 'Unknown installed helper; update cancelled without stopping VPN'}
}
if($ValidateOnly){
 $layout=if($hasCurrentProfiles){Update-VpnProfileLayout -ValidateOnly}else{$null}
 [pscustomobject]@{PreflightPassed=$true;ProfileCount=$state.Count;Layout=$layout;NoProcessesStopped=$true}
 return
}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run the update launcher to elevate'}
$official=Get-CimInstance Win32_Service -Filter "Name='AmneziaVPN-service'"
$officialHash=(Get-FileHash -LiteralPath 'C:\Program Files\AmneziaVPN\AmneziaVPN.exe').Hash
$stamp=[datetime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$report=Join-Path $PSScriptRoot ('operation-'+$stamp)
$backup=Join-Path $PSScriptRoot ('live-private\before103-'+$stamp)
New-Item -ItemType Directory -Path $report,$backup -Force | Out-Null
& icacls.exe $backup /inheritance:r /grant:r ($identity.Name+':(OI)(CI)F') | Out-Null
if($LASTEXITCODE){throw 'Cannot protect backup directory'}
$facts=[ordered]@{Success=$false;Rollback=[bool]$Rollback;Backup=$backup;ProfilesPreserved=$false;OfficialPreserved=$false}

try {
 if($hasCurrentProfiles){[void](Save-VpnProfileSnapshot (Join-Path $backup 'profiles.dpapi'))}
 if($hasCurrentProfiles){
 & reg.exe export 'HKCU\Software\D1m7asis.VPN\D1m7asisVPN' (Join-Path $backup 'settings.reg') /y | Out-Null
 if($LASTEXITCODE){throw 'Settings backup failed'}
 }
 if($current){$current | ConvertTo-Json | Set-Content (Join-Path $backup 'manifest.json')}
 if(-not $Rollback -and ($current.appSha256 -eq $old.appSha256 -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'last-upgrade-backup.txt')))){[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'last-upgrade-backup.txt'),$backup)}
 $facts.Stop=Stop-OwnedVpnForUpdate
 $facts.StaleOwnXrayAdaptersRemoved=Remove-OwnedXrayAdapters
 # Capture the final state after the GUI has saved its selected profile.
 if($hasCurrentProfiles){$state=Save-VpnProfileSnapshot (Join-Path $backup 'profiles.dpapi')}
 if($Rollback -and -not $current -and (Test-Path -LiteralPath $root)){
  $previous=Get-Content (Join-Path $recovery 'manifest.json') -Raw | ConvertFrom-Json
  if($previous.appSha256 -notin @($old.appSha256,$new.appSha256)){throw 'Partial installation has no verified recovery receipt'}
  $resolved=(Resolve-Path -LiteralPath $root).Path
  if($resolved.TrimEnd('\') -ine $root -or ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected partial installation path'}
  $quarantine=[IO.Path]::GetFullPath((Join-Path $backup 'partial-installation'))
  if(-not $quarantine.StartsWith(([IO.Path]::GetFullPath($backup)+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid quarantine path'}
  foreach($service in @(Get-OwnedVpnServices)){
   if($service.State -ne 'Stopped'){throw 'Owned service still running before recovery'}
   & sc.exe delete $service.Name | Out-Null
   if($LASTEXITCODE){throw 'Cannot remove partial owned service registration'}
  }
  Move-Item -LiteralPath $resolved -Destination $quarantine
  $deadline=(Get-Date).AddSeconds(20)
  while((Get-Service D1m7asisVPN-service -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 250}
  if(Get-Service D1m7asisVPN-service -ErrorAction SilentlyContinue){throw 'Partial owned service is still pending deletion'}
 }
 if($current -and $current.appSha256 -ne $target.appSha256){
  $maintenance=Join-Path $root 'maintenancetool.exe'
  if(-not (Test-Path -LiteralPath $maintenance)){throw 'Owned maintenance tool missing; use recovery instructions'}
  $p=Start-Process -FilePath $maintenance -ArgumentList @('--default-answer','--confirm-command','purge') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $report 'uninstall.log') -RedirectStandardError (Join-Path $report 'uninstall-error.log')
  if($p.ExitCode){throw 'Owned uninstall failed'}
  $deadline=(Get-Date).AddSeconds(20)
  while(((Test-Path -LiteralPath $root) -or (Get-Service D1m7asisVPN-service -ErrorAction SilentlyContinue)) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 250}
  if(Test-Path -LiteralPath $root){throw 'Owned installation remains; no foreign paths were removed'}
 }
 if(-not (Test-Path -LiteralPath $root)){
  & (Join-Path $PSScriptRoot 'Install-VerifiedFork.ps1') -Installer $installer -InstallerSha256 $target.installerSha256
 }else{
  if(-not $Rollback -and $new.helperRevision){Update-OwnedHelper -Root $root -Payload $helperPayload -Backup $backup -ExpectedHash $new.tun2socksSha256 -PreviousHash $new.previousTun2socksSha256}
  & sc.exe failure D1m7asisVPN-service reset= 100 actions= restart/2000/restart/2000/restart/2000 | Out-Null
  if($LASTEXITCODE){throw 'Cannot restore owned service recovery'}
  Start-Service D1m7asisVPN-service
 }
 if($Rollback){
  [void](Restore-VpnProfileSnapshot (Join-Path $recovery 'profiles.dpapi'))
 }else{
  try{$after=Get-VpnProfileState}catch{$after=$null}
  if(-not $after -or $after.Hash -ne $state.Hash){[void](Restore-VpnProfileSnapshot (Join-Path $backup 'profiles.dpapi'))}
  $facts.Layout=Update-VpnProfileLayout
 }
 $after=Get-VpnProfileState
 $facts.ProfilesPreserved=$after.Count -eq $state.Count
 $facts.BinariesVerified=(Get-FileHash (Join-Path $root 'D1m7asisVPN.exe')).Hash -eq $target.appSha256 -and (Get-FileHash (Join-Path $root 'D1m7asisVPN-service.exe')).Hash -eq $target.serviceSha256
 if(-not $Rollback -and $new.helperRevision){$facts.HelperVerified=(Get-FileHash -LiteralPath (Join-Path $root 'tun2socks.exe')).Hash -eq $new.tun2socksSha256;$facts.BinariesVerified=$facts.BinariesVerified -and $facts.HelperVerified}
 $officialAfter=Get-CimInstance Win32_Service -Filter "Name='AmneziaVPN-service'"
 $facts.OfficialPreserved=$officialAfter.ProcessId -eq $official.ProcessId -and $officialAfter.State -eq $official.State -and (Get-FileHash 'C:\Program Files\AmneziaVPN\AmneziaVPN.exe').Hash -eq $officialHash
 $facts.Success=$facts.ProfilesPreserved -and $facts.BinariesVerified -and $facts.OfficialPreserved
 if(-not $facts.Success){throw 'Post-install verification failed; use ROLLBACK.cmd'}
} catch {
 $facts.Error=$_.Exception.Message
 $facts.ErrorId=$_.FullyQualifiedErrorId
 $facts.ErrorLine=$_.InvocationInfo.ScriptLineNumber
 $facts.ErrorScript=[IO.Path]::GetFileName($_.InvocationInfo.ScriptName)
 if(Test-Path -LiteralPath (Join-Path $backup 'profiles.dpapi')){
  try{[void](Restore-VpnProfileSnapshot (Join-Path $backup 'profiles.dpapi'));$facts.ProfilesRestoredAfterFailure=$true}catch{$facts.ProfilesRestoredAfterFailure=$false}
 }
 if(Get-Service D1m7asisVPN-service -ErrorAction SilentlyContinue){& sc.exe failure D1m7asisVPN-service reset= 100 actions= restart/2000/restart/2000/restart/2000 | Out-Null}
 throw
} finally {$facts | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $report 'result.json')}
