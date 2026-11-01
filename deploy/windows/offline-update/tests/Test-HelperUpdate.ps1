$ErrorActionPreference='Stop'
Import-Module Microsoft.PowerShell.Utility,Microsoft.PowerShell.Management
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'Update-OwnedHelper.ps1')
$testRoot=Join-Path $env:TEMP ('D1mis-HelperTest-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$installed=Join-Path $testRoot 'installed'
$backup=Join-Path $testRoot 'backup'
New-Item -ItemType Directory -Path $installed,$backup | Out-Null
$payload=Join-Path $testRoot 'payload.exe'
$destination=Join-Path $installed 'tun2socks.exe'
[IO.File]::WriteAllText($payload,'new helper fixture')
[IO.File]::WriteAllText($destination,'old helper fixture')
$oldHash=(Get-FileHash $destination).Hash
$newHash=(Get-FileHash $payload).Hash
try {
 Update-OwnedHelper -Root $installed -Payload $payload -Backup $backup -ExpectedHash $newHash -PreviousHash $oldHash
 if((Get-FileHash $destination).Hash -ne $newHash -or (Get-FileHash (Join-Path $backup 'tun2socks.exe')).Hash -ne $oldHash){throw 'Replacement or backup failed'}
 Update-OwnedHelper -Root $installed -Payload $payload -Backup $backup -ExpectedHash $newHash -PreviousHash $oldHash
 if((Get-FileHash (Join-Path $backup 'tun2socks.exe')).Hash -ne $oldHash){throw 'Repeat update overwrote backup'}
 [IO.File]::WriteAllText($destination,'unknown helper fixture')
 $unknownHash=(Get-FileHash $destination).Hash
 $rejected=$false
 try{Update-OwnedHelper -Root $installed -Payload $payload -Backup $backup -ExpectedHash $newHash -PreviousHash $oldHash}catch{$rejected=$true}
 if(-not $rejected -or (Get-FileHash $destination).Hash -ne $unknownHash){throw 'Unknown helper was overwritten'}
 $rejected=$false
 try{Update-OwnedHelper -Root $installed -Payload $payload -Backup $backup -ExpectedHash $oldHash -PreviousHash $unknownHash}catch{$rejected=$true}
 if(-not $rejected -or (Get-FileHash $destination).Hash -ne $unknownHash){throw 'Wrong payload was accepted'}
 [IO.File]::WriteAllText($destination,'old helper fixture')
 function Copy-Item {param($LiteralPath,$Destination,[switch]$Force)
  if($LiteralPath -eq $payload){[IO.File]::WriteAllText($Destination,'injected partial copy');throw 'Injected copy failure'}
  Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
 }
 $rejected=$false
 try{Update-OwnedHelper -Root $installed -Payload $payload -Backup $backup -ExpectedHash $newHash -PreviousHash $oldHash}catch{$rejected=$true}
 if(-not $rejected -or (Get-FileHash $destination).Hash -ne $oldHash){throw 'Failed copy did not restore previous helper'}
 'PASS: helper replacement, repeat update, unknown binary, corrupt payload, partial-copy rollback; isolated files only.'
} finally {
 Remove-Item Function:\Copy-Item -ErrorAction SilentlyContinue
 $resolved=[IO.Path]::GetFullPath($testRoot)
 $allowed=[IO.Path]::GetFullPath($env:TEMP)+[IO.Path]::DirectorySeparatorChar+'D1mis-HelperTest-'
 if($resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
