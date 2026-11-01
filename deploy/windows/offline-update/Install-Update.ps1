param([switch]$Rollback)
$ErrorActionPreference='Stop'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'"'
 if($Rollback){$arguments+=' -Rollback'}
 $p=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -Verb RunAs -Wait -PassThru
 if($p.ExitCode -eq 0){Start-Process -FilePath 'C:\Program Files\D1m7asisVPN\D1m7asisVPN.exe'}
 exit $p.ExitCode
}
$mutex=[Threading.Mutex]::new($false,'Local\D1m7asisVPN-OfflineUpdate')
$locked=$false;$exitCode=1
try {
 try{$locked=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$locked=$true}
 if(-not $locked){throw 'Another VPN update is already running'}
 Write-Host 'This offline operation stops only D1m7asis VPN. Connect VPN manually after completion.'
 & (Join-Path $PSScriptRoot 'Upgrade.ps1') -Rollback:$Rollback
 Write-Host 'SUCCESS. Verified profiles and binaries. Connect manually.' -ForegroundColor Green
 $exitCode=0
 Start-Sleep -Seconds 3
}catch{
 Write-Host ('STOPPED: '+$_.Exception.Message) -ForegroundColor Red
 Write-Host 'Keep this folder. Check operation-* reports. Use ROLLBACK.cmd to restore 5.0.3.102.'
 Read-Host 'Press Enter to close'
}finally{if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
exit $exitCode
