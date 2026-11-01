[CmdletBinding()]
param([string]$ExpectedIp='2.27.29.231')
$ErrorActionPreference='Stop'
$checks=[Collections.Generic.List[object]]::new()
$route=@(Find-NetRoute -RemoteIPAddress '1.1.1.1' | Select-Object InterfaceAlias,InterfaceIndex,NextHop)
$endpoint=@(Find-NetRoute -RemoteIPAddress '2.27.29.231' | Select-Object InterfaceAlias,InterfaceIndex,NextHop)
$services=@(Get-CimInstance Win32_Service | Where-Object Name -in @('D1m7asisVPN-service','AmneziaVPN-service') | Select-Object Name,State,ProcessId)
$dns=$false
try{$dns=@(Resolve-DnsName example.com -Type A -DnsOnly -ErrorAction Stop | Where-Object IPAddress).Count -gt 0}catch{}
$ip=& curl.exe --noproxy '*' --silent --show-error --max-time 12 https://api.ipify.org 2>$null
$ipOk=$LASTEXITCODE -eq 0 -and "$ip".Trim() -eq $ExpectedIp
foreach($test in @(@{name='example';url='https://example.com';code=200;bytes=1},@{name='gstatic';url='https://www.gstatic.com/generate_204';code=204;bytes=0},@{name='download';url='https://speed.cloudflare.com/__down?bytes=1048576';code=200;bytes=1048576})){
 $output=& curl.exe --noproxy '*' --silent --show-error --location --max-time 25 --output NUL --write-out '%{http_code} %{size_download} %{time_total}' $test.url 2>$null
 $code=$LASTEXITCODE
 $parts="$output".Trim() -split '\s+'
 $ok=$code -eq 0 -and $parts.Count -ge 3 -and [int]$parts[0] -eq $test.code -and [long]$parts[1] -ge $test.bytes
 $checks.Add([pscustomobject]@{name=$test.name;passed=$ok;status=$parts[0];bytes=$parts[1];seconds=$parts[2]})
}
$ownedRoute=@($route | Where-Object InterfaceAlias -in @('D1m7asisVPN','d1misxray')).Count -gt 0
$facts=[ordered]@{timeUtc=[datetime]::UtcNow.ToString('o');passed=($ipOk -and $dns -and $ownedRoute -and @($checks | Where-Object {-not $_.passed}).Count -eq 0);exitIp="$ip".Trim();dns=$dns;route=$route;endpointRoute=$endpoint;services=$services;checks=@($checks);networkChanged=$false}
$folder=Join-Path $env:LOCALAPPDATA 'D1m7asisVPN-Diagnostics'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$report=Join-Path $folder ('connection-'+[datetime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'.json')
$facts | ConvertTo-Json -Depth 5 | Set-Content $report
$facts | ConvertTo-Json -Depth 5
Write-Output "Report: $report"
if(-not $facts.passed){exit 1}
