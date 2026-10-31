param(
    [ValidateSet('Install','Launch','Uninstall')][string]$Action='Install',
    [Parameter(Mandatory=$true)][string]$GamePath,
    [string]$ServerId='',
    [switch]$Launch
)
$ErrorActionPreference='Stop'
$gameDirectory=(Resolve-Path -LiteralPath $GamePath).Path
$gameExe=Join-Path $gameDirectory 'MoonSweepers.exe'
$proxy=Join-Path $gameDirectory 'version.dll'
$recordPath=Join-Path $gameDirectory 'moon-client-install.json'
$artifact=Join-Path (Split-Path -Parent $PSScriptRoot) 'native\out\version.dll'
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw 'Select the folder containing MoonSweepers.exe.' }
$record=$null
if (Test-Path -LiteralPath $recordPath) { $record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json }
if (Get-CimInstance Win32_Process -Filter "Name='MoonSweepers.exe'" | Where-Object { $_.ExecutablePath -eq $gameExe }) { throw 'Close Moon Sweepers before changing or launching the client module.' }
if ($Action -eq 'Uninstall') {
    if (-not $record) { throw 'No client installation record exists; nothing was removed.' }
    if (Test-Path -LiteralPath $proxy) {
        if ((Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $record.sha256) { throw 'version.dll changed since installation; inspect it manually.' }
        Remove-Item -LiteralPath $proxy
    }
    Remove-Item -LiteralPath $recordPath
    Write-Output 'Client module removed. Your game saves and Steam settings were preserved.'
    exit
}
$supported='14f711a6a3b1b7cd803a03f85cc6c6201c5b546ab9b90329533f5cb8ae2e7f13'
if ((Get-FileHash -LiteralPath (Join-Path $gameDirectory 'GameAssembly.dll') -Algorithm SHA256).Hash -ne $supported) { throw 'This module supports Moon Sweepers 1.0.5, Steam build 26317497. The installed game differs.' }
if ($Action -eq 'Install') {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw 'The release is missing native/out/version.dll. Build it with native/Dockerfile.build.' }
    $artifactHash=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $proxy) {
        $existingHash=(Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash
        if ($existingHash -ne $artifactHash -and (-not $record -or $record.sha256 -ne $existingHash)) { throw 'Another version.dll already exists. This installer will not overwrite another mod.' }
    }
    Copy-Item -LiteralPath $artifact -Destination $proxy -Force
    $record=[pscustomobject]@{schema=1;sha256=$artifactHash;server_id=if($record){$record.server_id}else{''}}
} elseif (-not $record -or -not (Test-Path -LiteralPath $proxy) -or (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $record.sha256) { throw 'Install the client module first, or restore its recorded version.dll.' }
if ($ServerId) {
    if ($ServerId -notmatch '^\d{17}$' -or (([UInt64]::Parse($ServerId) -shr 52) -band 15) -ne 4) { throw 'ServerId must be the anonymous server SteamID shown by the server status command.' }
    $record.server_id=$ServerId
}
$record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding ASCII
Write-Output 'Moon Sweepers dedicated client module installed.'
if ($Action -eq 'Launch' -or $Launch) {
    if (-not $record.server_id) { throw 'Pass -ServerId with the server SteamID shown in the Docker status output.' }
    $steam=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
    if (-not $steam) { $steam=Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe' }
    if (-not (Test-Path -LiteralPath $steam -PathType Leaf)) { throw 'Steam was not found. Launch the game in Steam with: -moon-connect followed by your server SteamID.' }
    Start-Process -FilePath $steam -ArgumentList ('-applaunch 5335510 -moon-connect '+$record.server_id)
    Write-Output ('Requested Steam launch connecting to server '+$record.server_id+'.')
} else { Write-Output 'Launch through Steam with: -moon-connect YOUR_SERVER_STEAM_ID' }
