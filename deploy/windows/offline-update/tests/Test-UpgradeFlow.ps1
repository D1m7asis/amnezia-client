$ErrorActionPreference='Stop'
Import-Module Microsoft.PowerShell.Utility,Microsoft.PowerShell.Management
$kit=Split-Path -Parent $PSScriptRoot
$testRoot=Join-Path $env:TEMP ('D1mis-UpgradeTest-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$source=Get-Content (Join-Path $kit 'Upgrade.ps1') -Raw
$check="if(-not ([Security.Principal.WindowsPrincipal]`$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run the update launcher to elevate'}"
if(-not $source.Contains($check)){throw 'Elevation check changed; review mock harness'}
$source=$source.Replace($check,'# Elevation check omitted only in isolated mocked test.')
function Test-Path {param($LiteralPath,$Path)
 $p=if($LiteralPath){$LiteralPath}else{$Path}
 if($global:Offline103Fixture.Partial -and $p -like 'C:\Program Files\D1m7asisVPN\*.exe'){return $false}
 if($p -like 'C:\Program Files\D1m7asisVPN*'){return $global:Offline103Fixture.Installed}
 Microsoft.PowerShell.Management\Test-Path -LiteralPath $p
}
function Resolve-Path {param($LiteralPath)
 if($LiteralPath -eq 'C:\Program Files\D1m7asisVPN'){return [pscustomobject]@{Path=$LiteralPath}}
 Microsoft.PowerShell.Management\Resolve-Path -LiteralPath $LiteralPath
}
function Get-Item {param($LiteralPath)
 if($LiteralPath -eq 'C:\Program Files\D1m7asisVPN'){return [pscustomobject]@{Attributes=[IO.FileAttributes]::Directory}}
 Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
}
function Move-Item {param($LiteralPath,$Destination)
 if($LiteralPath -ne 'C:\Program Files\D1m7asisVPN' -or -not $Destination.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected quarantine move'}
 $global:Offline103Fixture.Installed=$false
 $global:Offline103Fixture.Quarantined=$true
}
function Get-FileHash {param($LiteralPath,$Path)
 $p=if($LiteralPath){$LiteralPath}else{$Path}
 $f=$global:Offline103Fixture
 $hash=if($p -like '*D1m7asisVPN_5.0.3.102_windows_x64.exe'){'C'*64}
 elseif($p -like '*D1m7asisVPN_5.0.3.103_windows_x64.exe'){'D'*64}
 elseif($p -like '*AmneziaVPN\AmneziaVPN.exe'){'1'*64}
 elseif($p -like '*D1m7asisVPN-service.exe'){if($f.Version -eq 'old'){'B'*64}else{'F'*64}}
 elseif($p -like '*D1m7asisVPN.exe'){if($f.Version -eq 'old'){'A'*64}else{'E'*64}}
 elseif($p -like '*tun2socks.exe'){$f.HelperHash}
 else{throw 'Unexpected hash target in mocked flow'}
 [pscustomobject]@{Hash=$hash}
}
function Get-CimInstance {param($ClassName,$Filter)
 if($ClassName -ne 'Win32_Service' -or $Filter -ne "Name='AmneziaVPN-service'"){throw 'Unexpected CIM call in mocked flow'}
 [pscustomobject]@{ProcessId=900;State='Running'}
}
function Get-Service {param($Name,$ErrorAction)
 if($Name -ne 'D1m7asisVPN-service'){throw 'Unexpected service query'}
 if($global:Offline103Fixture.Installed){[pscustomobject]@{Name=$Name}}
}
function Start-Service {param($Name) if($Name -ne 'D1m7asisVPN-service'){throw 'Foreign service start'} }
function Start-Process {param($FilePath,$ArgumentList,$WindowStyle,[switch]$Wait,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
 if($FilePath -ne 'C:\Program Files\D1m7asisVPN\maintenancetool.exe'){throw 'Unexpected process launch in mocked flow'}
 $global:Offline103Fixture.Installed=$false
 [pscustomobject]@{ExitCode=0}
}
function sc.exe {$global:LASTEXITCODE=0}
function icacls.exe {$global:LASTEXITCODE=0}
function reg.exe {[IO.File]::WriteAllText($args[2],'mock');$global:LASTEXITCODE=0}
function Start-Sleep {param($Seconds,$Milliseconds)}
$profileStub=@'
function Get-VpnProfileState {
 if($global:Offline103Fixture.Missing){throw 'Fixture profiles absent'}
 [pscustomobject]@{Count=2;Hash=$global:Offline103Fixture.Hash}
}
function Save-VpnProfileSnapshot([string]$Path){[IO.File]::WriteAllText($Path,'mock');Get-VpnProfileState}
function Restore-VpnProfileSnapshot([string]$Path){$global:Offline103Fixture.Missing=$false;$global:Offline103Fixture.Hash='base';Get-VpnProfileState}
function Get-VpnSnapshotMetadata([string]$Path){[pscustomobject]@{Count=2}}
function Assert-VpnLegacyRecoveryCompatibility([string]$Path){if($global:Offline103Fixture.Incompatible){throw 'Injected incompatible legacy keys'}}
function Update-VpnProfileLayout([switch]$ValidateOnly){if(-not $ValidateOnly){$global:Offline103Fixture.Hash='migrated'};[pscustomobject]@{Count=2;ObsoleteContainersRemoved=1;Written=(-not $ValidateOnly)}}
'@
$stopStub=@'
function Get-OwnedVpnServices {}
function Stop-OwnedVpnForUpdate {$global:Offline103Fixture.Stops++;[pscustomobject]@{OfficialVpnStopped=$false}}
'@
$installStub=@'
param($Installer,$InstallerSha256)
if($global:Offline103Fixture.FailInstall){throw 'Injected install failure'}
$global:Offline103Fixture.Installed=$true
$global:Offline103Fixture.Version=if($Installer -like '*102*'){'old'}else{'new'}
'@
$passes=0
try{
 foreach($mode in @('dry','upgrade','rollback','install-failure','rollback-missing-profiles','rollback-partial-folder','rollback-incompatible-keys','helper-repair')){
  $dir=Join-Path $testRoot $mode
  New-Item -ItemType Directory -Path $dir | Out-Null
  $global:Offline103Fixture=@{Installed=($mode -ne 'rollback-missing-profiles');Version='old';Hash='base';Missing=($mode -eq 'rollback-missing-profiles');Stops=0;FailInstall=($mode -eq 'install-failure');Partial=($mode -eq 'rollback-partial-folder');Quarantined=$false;Incompatible=($mode -eq 'rollback-incompatible-keys')}
  if($mode -in @('rollback','rollback-incompatible-keys')){$global:Offline103Fixture.Version='new';$global:Offline103Fixture.Hash='migrated'}
  $global:Offline103Fixture.HelperHash='2'*64
  if($mode -eq 'helper-repair'){$global:Offline103Fixture.Version='new'}
  [IO.File]::WriteAllText((Join-Path $dir 'Upgrade.ps1'),$source)
  Copy-Item -LiteralPath (Join-Path $kit 'Update-OwnedHelper.ps1') -Destination $dir
  [IO.File]::WriteAllText((Join-Path $dir 'Profile-Store.ps1'),$profileStub)
  [IO.File]::WriteAllText((Join-Path $dir 'Stop-OwnedVpn.ps1'),$stopStub)
  [IO.File]::WriteAllText((Join-Path $dir 'Install-VerifiedFork.ps1'),$installStub)
  [IO.File]::WriteAllText((Join-Path $dir 'Remove-OwnedXrayAdapters.ps1'),'function Remove-OwnedXrayAdapters {return 2}')
  @{installer='D1m7asisVPN_5.0.3.102_windows_x64.exe';installerSha256=('C'*64);appSha256=('A'*64);serviceSha256=('B'*64)}|ConvertTo-Json|Set-Content (Join-Path $dir 'installed102-manifest.json')
  @{installer='D1m7asisVPN_5.0.3.103_windows_x64.exe';installerSha256=('D'*64);appSha256=('E'*64);serviceSha256=('F'*64)}|ConvertTo-Json|Set-Content (Join-Path $dir 'release-manifest.json')
  if($mode -eq 'helper-repair'){
   @{installer='D1m7asisVPN_5.0.3.103_windows_x64.exe';installerSha256=('D'*64);appSha256=('E'*64);serviceSha256=('F'*64);helperRevision=1;tun2socksSha256=('2'*64);previousTun2socksSha256=('3'*64)}|ConvertTo-Json|Set-Content (Join-Path $dir 'release-manifest.json')
   [IO.File]::WriteAllText((Join-Path $dir 'Update-OwnedHelper.ps1'),'function Update-OwnedHelper {param($Root,$Payload,$Backup,$ExpectedHash,$PreviousHash) if($global:Offline103Fixture.Stops -ne 1){throw "Helper updated before owned stop"};$global:Offline103Fixture.HelperUpdated=$true}')
  }
  $rollback=$mode -like 'rollback*'
  if($rollback){
   $recovery=Join-Path $dir 'live-private\fixture-backup';New-Item -ItemType Directory -Path $recovery -Force|Out-Null
   [IO.File]::WriteAllText((Join-Path $recovery 'profiles.dpapi'),'mock')
   [IO.File]::WriteAllText((Join-Path $dir 'last-upgrade-backup.txt'),$recovery)
   @{appSha256=('A'*64)}|ConvertTo-Json|Set-Content (Join-Path $recovery 'manifest.json')
  }
  $failed=$false
  try{& (Join-Path $dir 'Upgrade.ps1') -ValidateOnly:($mode -eq 'dry') -Rollback:$rollback | Out-Null}catch{$failed=$true;if($mode -notin @('install-failure','rollback-incompatible-keys')){throw}}
  if($mode -eq 'dry'){
   if($global:Offline103Fixture.Stops -ne 0 -or $global:Offline103Fixture.Hash -ne 'base'){throw 'Dry run mutated fixture'}
  }elseif($mode -eq 'rollback-incompatible-keys'){
   if(-not $failed -or $global:Offline103Fixture.Stops -ne 0 -or $global:Offline103Fixture.Version -ne 'new'){throw 'Unsafe credential rollback was not rejected before stopping VPN'}
  }elseif($mode -eq 'install-failure'){
   if(-not $failed -or $global:Offline103Fixture.Hash -ne 'base'){throw 'Failure did not preserve fixture profiles'}
  }else{
   $report=Get-ChildItem -LiteralPath $dir -Directory -Filter 'operation-*' | ForEach-Object {Get-Content (Join-Path $_.FullName 'result.json') -Raw|ConvertFrom-Json}
   if(-not $report.Success -or -not $report.OfficialPreserved -or $global:Offline103Fixture.Stops -ne 1){throw 'Mocked postconditions failed'}
   $expected=if($rollback){'old'}else{'new'}
   if($global:Offline103Fixture.Version -ne $expected){throw 'Wrong mock version after operation'}
   if($mode -eq 'rollback-partial-folder' -and -not $global:Offline103Fixture.Quarantined){throw 'Partial installation was not quarantined'}
   if($mode -eq 'helper-repair' -and (-not $global:Offline103Fixture.HelperUpdated -or -not $report.HelperVerified)){throw 'Existing 103 helper repair was not verified'}
  }
  $passes++
 }
 "PASS: $passes isolated mocked flows: dry-run, upgrade, rollback, failed install recovery, rollback after missing installation/profiles partial-folder quarantine and incompatible legacy-key rejection. No real VPN processes, services, devices or profiles changed."
}finally{
 $resolved=[IO.Path]::GetFullPath($testRoot)
 $allowed=[IO.Path]::GetFullPath($env:TEMP)+[IO.Path]::DirectorySeparatorChar+'D1mis-UpgradeTest-'
 if($resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}
 Remove-Variable Offline103Fixture -Scope Global -ErrorAction SilentlyContinue
}
