$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'Stop-OwnedVpn.ps1')
$root='C:\Program Files\D1m7asisVPN'
$script:time=[datetime]'2026-11-01T00:00:00'
function Reset-Fixture([bool]$Running){
 $script:state=if($Running){'Running'}else{'Stopped'}
 $script:procs=@{}
 $script:procs[123]=[pscustomobject]@{ProcessId=123;ExecutablePath=(Join-Path $root 'D1m7asisVPN.exe');CreationDate=$script:time;SessionId=3}
 $script:procs[999]=[pscustomobject]@{ProcessId=999;ExecutablePath='C:\Program Files\AmneziaVPN\AmneziaVPN.exe';CreationDate=$script:time;SessionId=3}
 if($Running){$script:procs[200]=[pscustomobject]@{ProcessId=200;ExecutablePath=(Join-Path $root 'D1m7asisVPN-service.exe');CreationDate=$script:time;SessionId=0}}
 $script:procs[456]=[pscustomobject]@{ProcessId=456;ExecutablePath=(Join-Path $root 'tun2socks.exe');CreationDate=$script:time;SessionId=0}
 $script:procs[888]=[pscustomobject]@{ProcessId=888;ExecutablePath='C:\Program Files\AmneziaVPN\tun2socks.exe';CreationDate=$script:time;SessionId=0}
 $script:killed=@();$script:removed=@();$script:stoppedServices=@()
}
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction)
 if($ClassName -eq 'Win32_Service'){
  if($Filter -like "*Name='D1m7asisVPN-service'*"){[pscustomobject]@{Name='D1m7asisVPN-service';State=$script:state;ProcessId=200;PathName='"C:\Program Files\D1m7asisVPN\D1m7asisVPN-service.exe"'}}
  return
 }
 if($Filter -match '^ProcessId=(\d+)$'){return $script:procs[[int]$matches[1]]}
 $script:procs.Values
}
function Get-Process {param($Id,$ErrorAction)
 if($Id -eq $PID){return [pscustomobject]@{SessionId=3}}
 $r=$script:procs[$Id];if(-not $r){return}
 $p=[pscustomobject]@{Id=$Id;Path=$r.ExecutablePath;StartTime=$r.CreationDate}
 $p | Add-Member ScriptMethod CloseMainWindow {return $false}
 return $p
}
function Stop-Process {param($InputObject,[switch]$Force,$ErrorAction) $script:killed+= $InputObject.Id;$script:procs.Remove([int]$InputObject.Id)}
function Stop-Service {param($Name,$ErrorAction) if($Name -ne 'D1m7asisVPN-service'){throw 'Foreign service stopped'};$script:stoppedServices+=$Name;$script:state='Stopped';$script:procs.Remove(200)}
function sc.exe {$global:LASTEXITCODE=0}
function Start-Sleep {param($Seconds,$Milliseconds)}
function Get-NetAdapter {param($ErrorAction)
 [pscustomobject]@{Name='d1misxray';InterfaceIndex=35;HardwareInterface=$false;Status='Up'}
 [pscustomobject]@{Name='AmneziaVPN';InterfaceIndex=40;HardwareInterface=$false;Status='Up'}
}
function Get-NetRoute {param($InterfaceIndex,$PolicyStore,$ErrorAction)
 if($InterfaceIndex -ne 35){throw 'Foreign interface queried for removal'}
 [pscustomobject]@{InterfaceIndex=35;Protocol='NetMgmt';NextHop='0.0.0.0'}
 [pscustomobject]@{InterfaceIndex=35;Protocol='Local';NextHop='0.0.0.0'}
}
function Remove-NetRoute {param($InputObject,$Confirm,$ErrorAction) if($InputObject.InterfaceIndex -ne 35){throw 'Foreign route removed'};$script:removed+=$InputObject.InterfaceIndex}
foreach($running in @($false,$true)){
 Reset-Fixture $running
 $facts=Stop-OwnedVpnForUpdate
 if($facts.RemovedOwnStaleRoutes -ne 1 -or $script:removed.Count -ne 1){throw 'Stale own route cleanup failed'}
 if($script:killed -contains 999 -or -not $script:procs.ContainsKey(999)){throw 'Official GUI was affected'}
 if($script:procs.ContainsKey(123) -or $script:procs.ContainsKey(456)){throw 'Own GUI/helper not closed'}
 if(-not $script:procs.ContainsKey(888)){throw 'Official helper was affected'}
 if($script:state -ne 'Stopped'){throw 'Own service not stopped'}
}
'PASS: 2 complete mocked update-stop flows, including stopped service with orphan Up adapter and running service. Official VPN preserved; no real processes/services/routes changed.'
