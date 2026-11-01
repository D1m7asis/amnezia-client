$ErrorActionPreference='Stop'
$kit=Split-Path -Parent $PSScriptRoot
$testRoot='Software\D1m7asis.VPN\OfflineUpdateTests\'+[guid]::NewGuid().ToString('N')
$testServer=$testRoot+'\Servers'
$source=Get-Content (Join-Path $kit 'Profile-Store.ps1') -Raw
$source=$source.Replace('HKCU:\Software\D1m7asis.VPN\D1m7asisVPN\Servers',('HKCU:\'+$testServer)).Replace('Software\D1m7asis.VPN\D1m7asisVPN\Servers',$testServer)
Invoke-Expression $source
function Get-VpnKeyMaterial {return ,([byte[]](1..64))}
$material=Get-VpnKeyMaterial
$profiles=@(
 [pscustomobject]@{description='D1m7asis | Reality management';hostName='2.27.29.231';storageServerId='admin-id';defaultContainer='amnezia-awg2';sshManagement=@{transport='unchanged'};containers=@(
  @{container='amnezia-awg2';awg=@{last_config='{"client_ip":"10.8.2.1/32"}'}},
  @{container='amnezia-xray';xray=@{unchanged='test-only'}}
 )},
 [pscustomobject]@{description='D1m7asis | AmneziaWG';storageServerId='wg-id';containers=@(@{container='amnezia-awg';awg=@{client_ip='10.8.1.6/32'}})}
)
$original=New-VpnProfileBlob $profiles $material
$snapshot=Join-Path $env:TEMP ('d1mis-offline-test-'+[guid]::NewGuid().ToString('N')+'.dpapi')
$k=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($testServer)
try{$k.SetValue('serversList',$original,[Microsoft.Win32.RegistryValueKind]::Binary);$k.SetValue('defaultServerId','wg-id')}finally{$k.Dispose()}
try {
 $before=Save-VpnProfileSnapshot $snapshot
 if($before.Count -ne 2){throw 'Expected two fixture profiles'}
 $dry=Update-VpnProfileLayout -ValidateOnly
 if($dry.ObsoleteContainersRemoved -ne 1 -or $dry.Written){throw 'Dry-run migration failed'}
 if((Get-VpnProfileState).Hash -ne $before.Hash){throw 'Dry-run changed registry'}
 $changed=Update-VpnProfileLayout
 if($changed.ObsoleteContainersRemoved -ne 1 -or -not $changed.Written){throw 'Layout migration failed'}
 $k=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testServer)
 try{$after=Convert-VpnProfiles $k.GetValue('serversList') $material;$id=$k.GetValue('defaultServerId')}finally{$k.Dispose()}
 if($after[0].storageServerId -ne 'admin-id' -or $after[0].sshManagement.transport -ne 'unchanged' -or $id -ne 'wg-id'){throw 'Migration changed identity or SSH'}
 if((ConvertTo-Json -InputObject $after[1] -Depth 100 -Compress) -cne (ConvertTo-Json -InputObject $profiles[1] -Depth 100 -Compress)){throw 'Working WG profile changed'}
 if($after[0].containers.Count -ne 1 -or $after[0].containers[0].container -ne 'amnezia-xray'){throw 'Unexpected remaining containers'}
 if((Update-VpnProfileLayout).Changed){throw 'Migration is not idempotent'}
 $k=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testServer,$true)
 try{$k.SetValue('serversList',[byte[]]@(0,0),[Microsoft.Win32.RegistryValueKind]::Binary)}finally{$k.Dispose()}
 $rejected=$false;try{Get-VpnProfileState | Out-Null}catch{$rejected=$true}
 if(-not $rejected){throw 'Corrupt store was accepted'}
 $restored=Restore-VpnProfileSnapshot $snapshot
 if($restored.Hash -ne $before.Hash -or $restored.Count -ne 2){throw 'Exact recovery failed'}
 'PASS: isolated two-profile migration, identity/SSH/WG preservation, no-write dry-run, idempotence, corruption rejection, DPAPI recovery.'
}finally{
 if($testRoot.StartsWith('Software\D1m7asis.VPN\OfflineUpdateTests\')){[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testRoot,$false)}
 if(Test-Path -LiteralPath $snapshot){Remove-Item -LiteralPath $snapshot}
}
