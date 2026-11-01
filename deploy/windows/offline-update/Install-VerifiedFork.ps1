[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$InstallerSha256
)
$ErrorActionPreference='Stop'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Windows elevation is required to install the separate VPN service'
}
$Installer=[IO.Path]::GetFullPath($Installer)
if(-not (Test-Path -LiteralPath $Installer -PathType Leaf) -or
    (Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash -ine $InstallerSha256) {
    throw 'Installer checksum mismatch'
}
$installRoot=Join-Path $env:ProgramFiles 'D1m7asisVPN'
if(Test-Path -LiteralPath $installRoot) { throw 'Target directory already exists; review its ownership before an update' }
if(Get-Service -Name 'D1m7asisVPN-service' -ErrorAction SilentlyContinue) { throw 'Fork service already exists' }
$officialExe=Join-Path $env:ProgramFiles 'AmneziaVPN\AmneziaVPN.exe'
$officialService=Get-CimInstance Win32_Service -Filter "Name='AmneziaVPN-service'"
if(-not $officialService -or -not (Test-Path -LiteralPath $officialExe -PathType Leaf)) {
    throw 'The protected original installation was not found'
}
$officialHash=(Get-FileHash -LiteralPath $officialExe -Algorithm SHA256).Hash
$reportRoot=Join-Path $PSScriptRoot ('install-'+(Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ'))
New-Item -ItemType Directory -Path $reportRoot | Out-Null
$facts=[ordered]@{success=$false;installerSha256=$InstallerSha256;target=$installRoot;
    originalExecutablePreserved=$false;originalServicePreserved=$false;forkServiceVerified=$false}
try {
    $arguments=@('--root',('"'+$installRoot+'"'),'--accept-licenses','--default-answer','--confirm-command','install')
    $process=Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $reportRoot 'installer-output.log') `
        -RedirectStandardError (Join-Path $reportRoot 'installer-error.log')
    $facts.installerExitCode=$process.ExitCode
    if($process.ExitCode -ne 0) { throw 'Installer did not finish successfully; see local installer logs' }
    $forkService=Get-CimInstance Win32_Service -Filter "Name='D1m7asisVPN-service'"
    $expectedService=Join-Path $installRoot 'D1m7asisVPN-service.exe'
    if(-not $forkService -or $forkService.PathName.Trim('"') -ine $expectedService -or
        -not (Test-Path -LiteralPath (Join-Path $installRoot 'D1m7asisVPN.exe') -PathType Leaf)) {
        throw 'The separate installed client or service could not be verified'
    }
    $facts.forkServiceVerified=$true
    $after=Get-CimInstance Win32_Service -Filter "Name='AmneziaVPN-service'"
    $facts.originalExecutablePreserved=(Get-FileHash -LiteralPath $officialExe -Algorithm SHA256).Hash -eq $officialHash
    $facts.originalServicePreserved=$after -and $after.PathName -eq $officialService.PathName -and $after.State -eq $officialService.State
    if(-not $facts.originalExecutablePreserved -or -not $facts.originalServicePreserved) {
        throw 'The original installation changed unexpectedly; stop before any network test'
    }
    & sc.exe failure 'D1m7asisVPN-service' reset= 100 actions= restart/2000/restart/2000/restart/2000 | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Cannot configure recovery of the owned VPN service'}
    Start-Service -Name 'D1m7asisVPN-service'
    $facts.success=$true
    Write-Output "Fork installed. Original installation preserved. Report: $reportRoot"
} finally {
    $facts | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportRoot 'result.json')
}
