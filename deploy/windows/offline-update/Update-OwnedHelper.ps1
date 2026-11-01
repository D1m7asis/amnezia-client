function Update-OwnedHelper {
 param([string]$Root,[string]$Payload,[string]$Backup,[string]$ExpectedHash,[string]$PreviousHash)
 $destination=Join-Path $Root 'tun2socks.exe'
 if((Get-FileHash -LiteralPath $Payload).Hash -ine $ExpectedHash){throw 'Helper payload checksum mismatch'}
 $before=(Get-FileHash -LiteralPath $destination).Hash
 if($before -ieq $ExpectedHash){return}
 if($before -ine $PreviousHash){throw 'Unknown installed helper; update cancelled'}
 $saved=Join-Path $Backup 'tun2socks.exe'
 Copy-Item -LiteralPath $destination -Destination $saved -Force
 if((Get-FileHash -LiteralPath $saved).Hash -ine $before){throw 'Helper backup checksum mismatch'}
 try {
  Copy-Item -LiteralPath $Payload -Destination $destination -Force
  if((Get-FileHash -LiteralPath $destination).Hash -ine $ExpectedHash){throw 'Installed helper checksum mismatch'}
 } catch {
  Copy-Item -LiteralPath $saved -Destination $destination -Force
  if((Get-FileHash -LiteralPath $destination).Hash -ine $before){throw 'Helper restoration failed; protected backup retained'}
  throw
 }
}
