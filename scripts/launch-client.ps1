param(
    [string]$GamePath=$PSScriptRoot,
    [switch]$ShowErrors
)
$ErrorActionPreference='Stop'
try {
    $gameDirectory=(Resolve-Path -LiteralPath $GamePath).Path
    $gameExe=Join-Path $gameDirectory 'MoonSweepers.exe'
    $recordPath=Join-Path $gameDirectory 'moon-client-install.json'
    $proxy=Join-Path $gameDirectory 'version.dll'
    if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw 'MoonSweepers.exe was not found. Recreate the shortcut using the client installer.' }
    if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) { throw 'Install the Moon Sweepers client module first.' }
    $record=Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not (Test-Path -LiteralPath $proxy -PathType Leaf) -or (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $record.sha256) { throw 'The installed client module changed. Update it using the client installer.' }
    $supported='14f711a6a3b1b7cd803a03f85cc6c6201c5b546ab9b90329533f5cb8ae2e7f13'
    if ((Get-FileHash -LiteralPath (Join-Path $gameDirectory 'GameAssembly.dll') -Algorithm SHA256).Hash -ne $supported) { throw 'This module supports Moon Sweepers 1.0.5, Steam build 26317497. The installed game differs.' }
    $serverId=[string]$record.server_id
    if ($serverId -notmatch '^\d{17}$') { throw 'Set the server SteamID with the client installer before joining.' }
    $serverNumber=[UInt64]::Parse($serverId)
    $serverType=($serverNumber -shr 52) -band 15
    if (($serverNumber -shr 56) -ne 1 -or ($serverNumber -band 0xffffffff) -eq 0 -or $serverType -notin @(3,4)) { throw 'The saved server SteamID is invalid. Reinstall with the current server ID.' }
    if (Get-CimInstance Win32_Process -Filter "Name='MoonSweepers.exe'" | Where-Object { $_.ExecutablePath -eq $gameExe }) { throw 'Moon Sweepers is already running. Close it before joining again.' }
    $steam=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
    if (-not $steam) { $steam=Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe' }
    if (-not (Test-Path -LiteralPath $steam -PathType Leaf)) { throw 'Steam was not found. Open Steam, then try the shortcut again.' }
    Start-Process -FilePath $steam -ArgumentList ('-applaunch 5335510 -moon-connect '+$serverId)
    Write-Output ('Requested Steam launch connecting to server '+$serverId+'.')
} catch {
    if ($ShowErrors) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Moon Sweepers - Join server',[System.Windows.Forms.MessageBoxButtons]::OK,[System.Windows.Forms.MessageBoxIcon]::Warning) > $null
        exit 1
    }
    throw
}
