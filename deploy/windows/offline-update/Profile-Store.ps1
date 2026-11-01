$ErrorActionPreference='Stop'
if(-not ('VpnStoreRead' -as [type])) { Add-Type @"
using System;using System.Runtime.InteropServices;
public static class VpnStoreRead {
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct C {public uint Flags,Type;public IntPtr Target,Comment;public System.Runtime.InteropServices.ComTypes.FILETIME Written;public uint Size;public IntPtr Blob;public uint Persist,Count;public IntPtr Attributes,Alias,User;}
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CredRead(string target,uint type,uint flags,out IntPtr p);
 [DllImport("advapi32.dll")] static extern void CredFree(IntPtr p);
 public static byte[] Read(string tag){IntPtr p;if(!CredRead(tag,1,0,out p)){if(Marshal.GetLastWin32Error()==1168)return null;throw new Exception("Application encryption credential unavailable");}try{C c=(C)Marshal.PtrToStructure(p,typeof(C));byte[] b=new byte[c.Size];Marshal.Copy(c.Blob,b,0,b.Length);return b;}finally{CredFree(p);}}
}
"@
}
function Get-VpnProfileState {
 $key=$null;$iv=$null;$plain=$null;$material=$null
 try {
  $material=Get-VpnKeyMaterial; $key=[byte[]]$material[0..31];$iv=[byte[]]$material[32..63]
  $raw=(Get-ItemProperty 'HKCU:\Software\D1m7asis.VPN\D1m7asisVPN\Servers' -ErrorAction Stop).serversList
  $text=[Text.Encoding]::Unicode.GetString($raw).TrimEnd([char]0)
  if(-not $text.StartsWith('@ByteArray(') -or -not $text.EndsWith(')')){throw 'Invalid stored envelope'}
  $value=[Text.Encoding]::GetEncoding(28591).GetBytes($text.Substring(11,$text.Length-12))
  if([Text.Encoding]::ASCII.GetString($value,0,7) -ne 'EncData'){throw 'Invalid encrypted envelope'}
  $aes=[Security.Cryptography.Aes]::Create()
  try{$aes.Key=$key;$aes.IV=[byte[]]$iv[0..15];$d=$aes.CreateDecryptor();try{$plain=$d.TransformFinalBlock($value,7,$value.Length-7)}finally{$d.Dispose()}}finally{$aes.Dispose()}
  try{[object[]]$profiles=([Text.Encoding]::UTF8.GetString($plain,9,$plain.Length-9) | ConvertFrom-Json -ErrorAction Stop)}catch{throw 'Profile data cannot be decoded'}
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$hash=([BitConverter]::ToString($sha.ComputeHash($raw))).Replace('-','')}finally{$sha.Dispose()}
  [pscustomobject]@{Count=$profiles.Count;Hash=$hash}
 }finally{foreach($bytes in @($key,$iv,$plain,$material)){if($null -ne $bytes){[Array]::Clear($bytes,0,$bytes.Length)}}}
}
function Save-VpnProfileSnapshot([string]$Path) {
 $state=Get-VpnProfileState
 if($state.Count -lt 1){throw 'No readable profiles to back up'}
 $k=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\D1m7asis.VPN\D1m7asisVPN\Servers')
 try{$payload=@{version=2;material=[Convert]::ToBase64String((Get-VpnKeyMaterial));sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;count=$state.Count;blob=[Convert]::ToBase64String($k.GetValue('serversList'));defaultId=$k.GetValue('defaultServerId')}}finally{$k.Dispose()}
 Add-Type -AssemblyName System.Security
 $bytes=[Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Compress))
 try{$protected=[Security.Cryptography.ProtectedData]::Protect($bytes,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);[IO.File]::WriteAllBytes($Path,$protected)}finally{[Array]::Clear($bytes,0,$bytes.Length)}
 return $state
}
function Restore-VpnProfileSnapshot([string]$Path) {
 Add-Type -AssemblyName System.Security
 $bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($Path),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
 try{$p=[Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json}finally{[Array]::Clear($bytes,0,$bytes.Length)}
 if($p.version -ne 2 -or $p.count -lt 1 -or $p.sid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){throw 'Recovery snapshot identity mismatch'}
 if([Convert]::ToBase64String((Get-VpnKeyMaterial)) -cne $p.material){throw 'Recovery key identity mismatch; no profiles changed'}
 $k=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\D1m7asis.VPN\D1m7asisVPN\Servers')
 try{$k.SetValue('serversList',[Convert]::FromBase64String($p.blob),[Microsoft.Win32.RegistryValueKind]::Binary);if($p.defaultId){$k.SetValue('defaultServerId',[string]$p.defaultId,[Microsoft.Win32.RegistryValueKind]::String)}}finally{$k.Dispose()}
 $state=Get-VpnProfileState
 if($state.Count -ne $p.count){throw 'Recovered profile count mismatch'}
 return $state
}


function Get-VpnKeyMaterial {
 $scoped=[VpnStoreRead]::Read('D1m7asisVPN-Keychain/D1m7asis.VPN/D1m7asisVPN/settingsMaterial-v1')
 if($null -ne $scoped){if($scoped.Length -ne 64){throw 'Invalid scoped application credential'};return ,$scoped}
 $key=[VpnStoreRead]::Read('settingsKeyTag');$iv=[VpnStoreRead]::Read('settingsIvTag')
 if($null -eq $key -or $null -eq $iv -or $key.Length -ne 32 -or $iv.Length -ne 32){throw 'Application recovery credentials unavailable'}
 try{return ,([byte[]]($key+$iv))}finally{[Array]::Clear($key,0,$key.Length);[Array]::Clear($iv,0,$iv.Length)}
}
function Convert-VpnProfiles([byte[]]$Raw,[byte[]]$Material) {
 $text=[Text.Encoding]::Unicode.GetString($Raw).TrimEnd([char]0)
 if(-not $text.StartsWith('@ByteArray(') -or -not $text.EndsWith(')')){throw 'Invalid profile envelope'}
 $value=[Text.Encoding]::GetEncoding(28591).GetBytes($text.Substring(11,$text.Length-12))
 if($value.Length -lt 23 -or [Text.Encoding]::ASCII.GetString($value,0,7) -ne 'EncData'){throw 'Invalid encrypted profile envelope'}
 $aes=[Security.Cryptography.Aes]::Create()
 try{$aes.Key=[byte[]]$Material[0..31];$aes.IV=[byte[]]$Material[32..47];$d=$aes.CreateDecryptor();try{$plain=$d.TransformFinalBlock($value,7,$value.Length-7)}finally{$d.Dispose()}}finally{$aes.Dispose()}
 try{
  if($plain.Length -lt 9 -or $plain[0] -ne 0 -or $plain[1] -ne 0 -or $plain[2] -ne 0 -or $plain[3] -ne 12 -or $plain[4] -ne 0){throw 'Invalid profile variant'}
  [object[]]$profiles=([Text.Encoding]::UTF8.GetString($plain,9,$plain.Length-9)|ConvertFrom-Json -ErrorAction Stop)
  return ,$profiles
 }catch{throw 'Profile data could not be decoded'}finally{[Array]::Clear($plain,0,$plain.Length)}
}
function New-VpnProfileBlob([object[]]$Profiles,[byte[]]$Material) {
 $json=ConvertTo-Json -InputObject @($Profiles) -Compress -Depth 100
 $body=[Text.Encoding]::UTF8.GetBytes($json)
 $length=[BitConverter]::GetBytes([uint32]$body.Length);[Array]::Reverse($length)
 $plain=[byte[]](@(0,0,0,12,0)+$length+$body)
 $aes=[Security.Cryptography.Aes]::Create()
 try{$aes.Key=[byte[]]$Material[0..31];$aes.IV=[byte[]]$Material[32..47];$e=$aes.CreateEncryptor();try{$cipher=$e.TransformFinalBlock($plain,0,$plain.Length)}finally{$e.Dispose()}}finally{$aes.Dispose();[Array]::Clear($plain,0,$plain.Length)}
 $encoded=[Text.Encoding]::GetEncoding(28591).GetString([byte[]]([Text.Encoding]::ASCII.GetBytes('EncData')+$cipher))
 return ,([Text.Encoding]::Unicode.GetBytes('@ByteArray('+$encoded+')'+[char]0))
}
function Update-VpnProfileLayout([switch]$ValidateOnly) {
 $material=Get-VpnKeyMaterial
 try {
  $registry=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\D1m7asis.VPN\D1m7asisVPN\Servers')
  try{$raw=$registry.GetValue('serversList')}finally{$registry.Dispose()}
  $profiles=Convert-VpnProfiles $raw $material
  $before=ConvertTo-Json -InputObject $profiles -Depth 100 -Compress
  $removed=0
  foreach($profile in $profiles){
   if($profile.description -ne 'D1m7asis | Reality management' -or $profile.hostName -ne '2.27.29.231'){continue}
   if(@($profile.containers|Where-Object container -eq 'amnezia-xray').Count -ne 1){throw 'Reality profile has no unique Xray configuration'}
   $kept=@()
   foreach($container in $profile.containers){
    if($container.container -eq 'amnezia-awg2'){
     $data=ConvertTo-Json -InputObject $container.awg -Depth 100 -Compress
     if($data -match '10\.8\.2\.1(?:/32|["\\])'){$removed++;continue}
    }
    $kept+=,$container
   }
   $profile.containers=$kept
   $profile.defaultContainer='amnezia-xray'
  }
  $new=New-VpnProfileBlob $profiles $material
  $verified=Convert-VpnProfiles $new $material
  if((ConvertTo-Json -InputObject $verified -Depth 100 -Compress) -cne (ConvertTo-Json -InputObject $profiles -Depth 100 -Compress)){throw 'Profile migration roundtrip failed'}
  $changed=$before -cne (ConvertTo-Json -InputObject $profiles -Depth 100 -Compress)
  if($changed -and -not $ValidateOnly){
   $registry=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\D1m7asis.VPN\D1m7asisVPN\Servers',$true)
   try{if([Convert]::ToBase64String($registry.GetValue('serversList')) -cne [Convert]::ToBase64String($raw)){throw 'Profiles changed concurrently'};$registry.SetValue('serversList',$new,[Microsoft.Win32.RegistryValueKind]::Binary)}finally{$registry.Dispose()}
   [void](Get-VpnProfileState)
  }
  [pscustomobject]@{Count=$profiles.Count;ObsoleteContainersRemoved=$removed;Changed=$changed;Written=($changed -and -not $ValidateOnly)}
 }finally{[Array]::Clear($material,0,$material.Length)}
}

function Get-VpnSnapshotMetadata([string]$Path) {
 Add-Type -AssemblyName System.Security
 $bytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($Path),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
 try{$p=[Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json}finally{[Array]::Clear($bytes,0,$bytes.Length)}
 if($p.version -ne 2 -or $p.count -lt 1 -or $p.sid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){throw 'Snapshot identity mismatch'}
 $material=[Convert]::FromBase64String($p.material)
 try{$profiles=Convert-VpnProfiles ([Convert]::FromBase64String($p.blob)) $material;if($profiles.Count -ne $p.count){throw 'Snapshot content mismatch'}}finally{[Array]::Clear($material,0,$material.Length)}
 [pscustomobject]@{Count=$p.count}
}
