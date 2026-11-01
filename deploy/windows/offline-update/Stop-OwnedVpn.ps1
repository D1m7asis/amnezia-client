function Get-OwnedVpnServices {
 $expected='C:\Program Files\D1m7asisVPN\D1m7asisVPN-service.exe'
 foreach($name in @('AmneziaWGTunnel$D1m7asisVPN','D1m7asisVPN-service')){
  $s=Get-CimInstance Win32_Service -Filter ("Name='"+$name+"'") -ErrorAction SilentlyContinue
  if(-not $s){continue}
  $m=[regex]::Match($s.PathName,'^(?:"([^"]+)"|(.+?\.exe))(?:\s|$)')
  $exe=if($m.Groups[1].Success){$m.Groups[1].Value}else{$m.Groups[2].Value}
  if($exe.Replace('/','\') -ine $expected){throw ('Unexpected executable for owned service '+$name)}
  $s
 }
}
function Stop-OwnedVpnProcess($Record) {
 if($Record.ExecutablePath -notin @('C:\Program Files\D1m7asisVPN\D1m7asisVPN.exe','C:\Program Files\D1m7asisVPN\D1m7asisVPN-service.exe','C:\Program Files\D1m7asisVPN\tun2socks.exe')){throw 'Refusing to stop a foreign process'}
 $p=Get-CimInstance Win32_Process -Filter ("ProcessId="+$Record.ProcessId) -ErrorAction SilentlyContinue
 if($p -and $p.ExecutablePath -ieq $Record.ExecutablePath -and $p.CreationDate -eq $Record.CreationDate){
  $live=Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
  if($live -and $live.Path -ieq $Record.ExecutablePath -and [Math]::Abs(($live.StartTime-$Record.CreationDate).TotalMilliseconds) -lt 1){Stop-Process -InputObject $live -Force -ErrorAction Stop;return $true}
 }
 return $false
}
function Stop-OwnedVpnForUpdate {
 $root='C:\Program Files\D1m7asisVPN'
 $gui=Join-Path $root 'D1m7asisVPN.exe'
 $backend=Join-Path $root 'D1m7asisVPN-service.exe'
 $tunHelper=Join-Path $root 'tun2socks.exe'
 $services=@(Get-OwnedVpnServices)
 $stoppedProcesses=0
 $session=(Get-Process -Id $PID).SessionId
 $records=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -ieq $gui -and $_.SessionId -eq $session})
 foreach($record in $records){
  $p=Get-Process -Id $record.ProcessId -ErrorAction SilentlyContinue
  if($p -and $p.Path -ieq $record.ExecutablePath -and [Math]::Abs(($p.StartTime-$record.CreationDate).TotalMilliseconds) -lt 1){[void]$p.CloseMainWindow()}
 }
 if($records.Count){Start-Sleep -Seconds 2}
 foreach($record in $records){if(Stop-OwnedVpnProcess $record){$stoppedProcesses++}}
 foreach($service in $services){
  if($service.State -eq 'Stopped'){continue}
  if($service.Name -eq 'D1m7asisVPN-service'){
   & sc.exe failure 'D1m7asisVPN-service' reset= 100 actions= none/0 | Out-Null
   if($LASTEXITCODE){throw 'Cannot pause recovery of owned service'}
  }
  try{Stop-Service -Name $service.Name -ErrorAction Stop}catch{}
  $deadline=(Get-Date).AddSeconds(8)
  do{$current=Get-CimInstance Win32_Service -Filter ("Name='"+$service.Name+"'");if(-not $current -or $current.State -eq 'Stopped'){break};Start-Sleep -Milliseconds 250}while((Get-Date) -lt $deadline)
  if($current -and $current.State -ne 'Stopped'){
   if(-not $current.ProcessId -or [long]$current.ProcessId -le 0){throw 'Owned service is stopping without a process ID; retry after it exits'}
   $record=Get-CimInstance Win32_Process -Filter ("ProcessId="+$current.ProcessId)
   if($record.ExecutablePath -ine $backend){throw 'Owned service process identity changed'}
   if(Stop-OwnedVpnProcess $record){$stoppedProcesses++}
  }
 }
 foreach($record in @(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -ieq $backend -or $_.ExecutablePath -ieq $tunHelper})){
  if(Stop-OwnedVpnProcess $record){$stoppedProcesses++}
 }
 if(@(Get-OwnedVpnServices | Where-Object State -ne 'Stopped').Count){throw 'Owned service did not stop'}
 if(@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -ieq $gui -or $_.ExecutablePath -ieq $backend -or $_.ExecutablePath -ieq $tunHelper}).Count){throw 'Owned process restarted during update'}
 $removed=0
 foreach($adapter in @(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object {$_.Name -in @('d1misxray','D1m7asisVPN') -and -not $_.HardwareInterface})){
  foreach($route in @(Get-NetRoute -InterfaceIndex $adapter.InterfaceIndex -PolicyStore ActiveStore -ErrorAction SilentlyContinue | Where-Object {$_.Protocol -eq 'NetMgmt' -and $_.NextHop -in @('0.0.0.0','::')})){
   Remove-NetRoute -InputObject $route -Confirm:$false -ErrorAction Stop
   $removed++
  }
 }
 [pscustomobject]@{StoppedOwnProcesses=$stoppedProcesses;RemovedOwnStaleRoutes=$removed;OfficialVpnStopped=$false}
}
