$ErrorActionPreference='Stop'
Import-Module Microsoft.PowerShell.Utility,Microsoft.PowerShell.Management
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'Remove-OwnedXrayAdapters.ps1')
$guid='{35C56996-7FA8-4D7F-BF1B-3BEB7015D44A}'
$script:mode='normal';$script:queries=0;$script:removed=@()
function Get-NetAdapter {param([switch]$IncludeHidden,$ErrorAction)
 $script:queries++
 if($script:mode -eq 'none'){return}
 [pscustomobject]@{Name='d1misxray 1';InterfaceGuid=$guid;InterfaceIndex=$(if($script:mode -eq 'changed' -and $script:queries -gt 1){99}else{35});HardwareInterface=$false;InterfaceDescription='tun2socks Tunnel'}
 [pscustomobject]@{Name='AmneziaVPN';InterfaceGuid='{11111111-1111-1111-1111-111111111111}';InterfaceIndex=40;HardwareInterface=$false;InterfaceDescription='AmneziaWG Tunnel'}
}
function Get-CimInstance {param($ClassName)
 if($ClassName -ne 'Win32_NetworkAdapter'){throw 'Unexpected device identity source'}
 [pscustomobject]@{GUID=$guid;PNPDeviceID=$(if($script:mode -eq 'spoofed'){'ROOT\FOREIGN'}else{'SWD\WINTUN\'+$guid});ServiceName='wintun';PhysicalAdapter=$true}
}
function pnputil.exe {
 if($args.Count -ne 2 -or $args[0] -ne '/remove-device' -or $args[1] -ine ('SWD\WINTUN\'+$guid)){throw 'Foreign device or shared driver removal attempted'}
 $script:removed+=,$args[1]
 $global:LASTEXITCODE=if($script:mode -eq 'failure'){5}else{0}
}
foreach($mode in @('normal','none','spoofed','changed','failure')){
 $script:mode=$mode;$script:queries=0;$script:removed=@();$failed=$false
 try{$count=Remove-OwnedXrayAdapters}catch{$failed=$true}
 if($mode -eq 'normal'){if($failed -or $count -ne 1 -or $script:removed.Count -ne 1){throw 'Owned device cleanup failed'}}
 elseif($mode -eq 'none'){if($failed -or $count -ne 0){throw 'Empty device cleanup failed'}}
 else{if(-not $failed){throw 'Unsafe or failed cleanup was accepted'};if($mode -ne 'failure' -and $script:removed.Count){throw 'Device removed before identity checks'}}
}
'PASS: 5 mocked device cleanup cases; GUID/device/service identity, changed-device rejection, failures, official adapter/shared driver untouched.'
