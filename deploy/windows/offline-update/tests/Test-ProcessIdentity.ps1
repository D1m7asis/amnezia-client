$ErrorActionPreference='Stop'
$path=Join-Path (Split-Path -Parent $PSScriptRoot) 'Stop-OwnedVpn.ps1'
. $path
$script:servicePath='"C:\Program Files\D1m7asisVPN\D1m7asisVPN-service.exe"'
$script:created=[datetime]'2026-11-01T00:00:00'
$script:currentPath='C:\Program Files\D1m7asisVPN\D1m7asisVPN.exe'
$script:stopped=0
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction)
 if($ClassName -eq 'Win32_Service') {if($Filter -like "*Name='D1m7asisVPN-service'*"){[pscustomobject]@{Name='D1m7asisVPN-service';PathName=$script:servicePath;State='Stopped';ProcessId=0}};return}
 [pscustomobject]@{ProcessId=123;ExecutablePath=$script:currentPath;CreationDate=$script:created}
}
function Get-Process {param($Id,$ErrorAction) [pscustomobject]@{Id=$Id;Path=$script:currentPath;StartTime=$script:created}}
function Stop-Process {param($InputObject,[switch]$Force,$ErrorAction) $script:stopped++}
$record=[pscustomobject]@{ProcessId=123;ExecutablePath=$script:currentPath;CreationDate=$script:created}
if(@(Get-OwnedVpnServices).Count -ne 1){throw 'Own service not recognized'}
$script:servicePath='"C:/Program Files/D1m7asisVPN/D1m7asisVPN-service.exe" tunneldaemon secret-not-printed'
if(@(Get-OwnedVpnServices).Count -ne 1){throw 'Normalized own tunnel path not recognized'}
$script:servicePath='"C:\Program Files\AmneziaVPN\AmneziaVPN-service.exe"'
$rejected=$false;try{[void]@(Get-OwnedVpnServices)}catch{$rejected=$true};if(-not $rejected){throw 'Foreign service accepted'}
if(-not (Stop-OwnedVpnProcess $record) -or $script:stopped -ne 1){throw 'Own process not stopped by mock'}
$script:created=$script:created.AddMinutes(1)
if((Stop-OwnedVpnProcess $record) -or $script:stopped -ne 1){throw 'Reused PID stopped'}
$script:currentPath='C:\Program Files\AmneziaVPN\AmneziaVPN.exe'
if((Stop-OwnedVpnProcess $record) -or $script:stopped -ne 1){throw 'Foreign process stopped'}
$record.ExecutablePath=$script:currentPath
$rejected=$false;try{[void](Stop-OwnedVpnProcess $record)}catch{$rejected=$true};if(-not $rejected){throw 'Foreign record accepted'}
'PASS: 7 ownership/PID scenarios. All process stops mocked; no real process or service changed.'
