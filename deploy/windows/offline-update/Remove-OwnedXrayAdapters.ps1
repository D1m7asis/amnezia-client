function Remove-OwnedXrayAdapters {
 $removed=0
 foreach($adapter in @(Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue | Where-Object {
  $_.Name -match '^d1misxray(?: [1-9][0-9]*)?$' -and -not $_.HardwareInterface -and $_.InterfaceDescription -eq 'tun2socks Tunnel'
 })){
  $guid=([guid]$adapter.InterfaceGuid).ToString('B')
  $record=@(Get-CimInstance Win32_NetworkAdapter | Where-Object {$_.GUID -ieq $guid -and $_.PNPDeviceID -ieq ('SWD\WINTUN\'+$guid) -and $_.ServiceName -eq 'wintun'})
  if($record.Count -ne 1){throw 'Cannot prove stale owned adapter identity; no device removed'}
  # Re-read immediately before removing this particular device instance.
  $current=Get-NetAdapter -IncludeHidden | Where-Object {([guid]$_.InterfaceGuid).ToString('B') -ieq $guid}
  if(@($current).Count -ne 1 -or $current.Name -ne $adapter.Name -or $current.InterfaceIndex -ne $adapter.InterfaceIndex){throw 'Adapter identity changed'}
  & pnputil.exe /remove-device $record[0].PNPDeviceID | Out-Null
  if($LASTEXITCODE -ne 0){throw 'Owned stale adapter removal failed; shared Wintun driver was not removed'}
  $removed++
 }
 return $removed
}
