param([Parameter(Mandatory)][string]$Probe)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'Profile-Store.ps1')
$organization='D1m7asis.CodecTest.'+[guid]::NewGuid().ToString('N')
$testRoot='Software\'+$organization
$material=[byte[]](1..64)
$registry=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($testRoot+'\Fixture\Servers')
try {
 $profiles=@([pscustomobject]@{description='codec-one'},[pscustomobject]@{description=('codec-two'+[char]10+'"quoted"'+[char]92+'path')})
 $blob=New-VpnProfileBlob $profiles $material
 $registry.SetValue('serversList',$blob,[Microsoft.Win32.RegistryValueKind]::Binary)
 $process=Start-Process -FilePath $Probe -ArgumentList $organization -WindowStyle Hidden -Wait -PassThru
 if($process.ExitCode -ne 0){throw ('Native Qt/OpenSSL profile read failed: '+$process.ExitCode)}
 # REG_BINARY must not include a REG_SZ terminator: Qt would read a QString.
 $registry.SetValue('serversList',[byte[]]($blob+@(0,0)),[Microsoft.Win32.RegistryValueKind]::Binary)
 $process=Start-Process -FilePath $Probe -ArgumentList $organization -WindowStyle Hidden -Wait -PassThru
 if($process.ExitCode -ne 3){throw 'Native Qt did not reject the legacy terminator regression'}
 'PASS: PowerShell-to-Qt registry/variant/AES interoperability, escaped text, and trailing-NUL regression; production settings unchanged.'
} finally {
 $registry.Dispose()
 if($testRoot.StartsWith('Software\D1m7asis.CodecTest.')){[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testRoot,$false)}
 [Array]::Clear($material,0,$material.Length)
}
