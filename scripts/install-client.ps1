param(
    [ValidateSet('Install','Launch','Shortcut','Uninstall')][string]$Action='Install',
    [Parameter(Mandatory=$true)][string]$GamePath,
    [string]$ServerId='',
    [switch]$Launch,
    [switch]$NoDesktopShortcut,
    [string]$ShortcutPath=''
)
$ErrorActionPreference='Stop'
$gameDirectory=(Resolve-Path -LiteralPath $GamePath).Path
$gameExe=Join-Path $gameDirectory 'MoonSweepers.exe'
$proxy=Join-Path $gameDirectory 'version.dll'
$recordPath=Join-Path $gameDirectory 'moon-client-install.json'
$artifact=Join-Path (Split-Path -Parent $PSScriptRoot) 'native\out\version.dll'
$launcherSource=Join-Path $PSScriptRoot 'launch-client.ps1'
$installedLauncher=Join-Path $gameDirectory 'moon-client-launch.ps1'
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw 'Select the folder containing MoonSweepers.exe.' }
$record=$null
if (Test-Path -LiteralPath $recordPath) { $record=Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json }
if ($Action -in @('Install','Uninstall') -and (Get-CimInstance Win32_Process -Filter "Name='MoonSweepers.exe'" | Where-Object { $_.ExecutablePath -eq $gameExe })) { throw 'Close Moon Sweepers before changing the client module.' }
if ($ServerId) {
    if ($ServerId -notmatch '^\d{17}$') { throw 'ServerId must be the server SteamID shown by the server status command.' }
    $serverNumber=[UInt64]::Parse($ServerId)
    $serverType=($serverNumber -shr 52) -band 15
    if (($serverNumber -shr 56) -ne 1 -or ($serverNumber -band 0xffffffff) -eq 0 -or $serverType -notin @(3,4)) { throw 'ServerId must be a valid anonymous or persistent GameServer SteamID.' }
}
function Get-RecordValue([string]$Name) {
    if ($record -and $record.PSObject.Properties[$Name]) { return $record.PSObject.Properties[$Name].Value }
    return $null
}
function Save-Record {
    $temporaryRecord=Join-Path $gameDirectory ('.moon-client-install-'+[Guid]::NewGuid().ToString('N')+'.tmp')
    try {
        $record | ConvertTo-Json | Set-Content -LiteralPath $temporaryRecord -Encoding UTF8
        Move-Item -LiteralPath $temporaryRecord -Destination $recordPath -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryRecord) { Remove-Item -LiteralPath $temporaryRecord }
    }
}
function Install-Launcher {
    if (-not (Test-Path -LiteralPath $launcherSource -PathType Leaf)) { throw 'The release is missing scripts/launch-client.ps1.' }
    $sourceHash=(Get-FileHash -LiteralPath $launcherSource -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $installedLauncher) {
        $existingHash=(Get-FileHash -LiteralPath $installedLauncher -Algorithm SHA256).Hash
        if ($existingHash -ne $sourceHash -and $existingHash -ne (Get-RecordValue 'launcher_sha256')) { throw 'moon-client-launch.ps1 already exists and was changed or belongs to another installation.' }
    }
    Copy-Item -LiteralPath $launcherSource -Destination $installedLauncher -Force
    $record | Add-Member -NotePropertyName launcher_sha256 -NotePropertyValue $sourceHash -Force
    Save-Record
}
function New-ClientShortcut {
    if (-not $record.server_id) { throw 'Pass -ServerId with the current server SteamID to create the join shortcut.' }
    if (-not $ShortcutPath) { $ShortcutPath=Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Join Moon Sweepers.lnk' }
    $ShortcutPath=[IO.Path]::GetFullPath($ShortcutPath)
    if ([IO.Path]::GetExtension($ShortcutPath) -ne '.lnk' -or -not (Test-Path -LiteralPath (Split-Path -Parent $ShortcutPath) -PathType Container)) { throw 'ShortcutPath must name a .lnk file in an existing folder.' }
    $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $shortcutArguments='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+$installedLauncher+'" -ShowErrors'
    $shell=New-Object -ComObject WScript.Shell
    $shortcut=$shell.CreateShortcut($ShortcutPath)
    if (Test-Path -LiteralPath $ShortcutPath) {
        $recorded=(Get-RecordValue 'shortcut_path') -eq $ShortcutPath -and (Get-RecordValue 'shortcut_sha256') -eq (Get-FileHash -LiteralPath $ShortcutPath -Algorithm SHA256).Hash
        if (-not $recorded -and ($shortcut.TargetPath -ne $powershell -or $shortcut.Arguments -ne $shortcutArguments)) { throw 'That shortcut already exists and belongs to another application or was changed.' }
    }
    Install-Launcher
    $shortcut.TargetPath=$powershell
    $shortcut.Arguments=$shortcutArguments
    $shortcut.WorkingDirectory=$gameDirectory
    $shortcut.IconLocation=$gameExe+',0'
    $shortcut.Description='Join your Moon Sweepers server'
    $shortcut.Save()
    $record | Add-Member -NotePropertyName shortcut_path -NotePropertyValue $ShortcutPath -Force
    $record | Add-Member -NotePropertyName shortcut_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $ShortcutPath -Algorithm SHA256).Hash -Force
    Save-Record
    Write-Output ('Created join shortcut: '+$ShortcutPath)
}
if ($Action -eq 'Uninstall') {
    if (-not $record) { throw 'No client installation record exists; nothing was removed.' }
    if (Test-Path -LiteralPath $proxy) {
        if ((Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $record.sha256) { throw 'version.dll changed since installation; inspect it manually.' }
        Remove-Item -LiteralPath $proxy
    }
    $ownedShortcut=Get-RecordValue 'shortcut_path'
    if ($ownedShortcut -and (Test-Path -LiteralPath $ownedShortcut)) {
        if ((Get-FileHash -LiteralPath $ownedShortcut -Algorithm SHA256).Hash -eq (Get-RecordValue 'shortcut_sha256')) { Remove-Item -LiteralPath $ownedShortcut }
        else { Write-Warning 'The desktop shortcut changed and was preserved.' }
    }
    if (Test-Path -LiteralPath $installedLauncher) {
        if ((Get-FileHash -LiteralPath $installedLauncher -Algorithm SHA256).Hash -eq (Get-RecordValue 'launcher_sha256')) { Remove-Item -LiteralPath $installedLauncher }
        else { Write-Warning 'The installed launcher changed and was preserved.' }
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
    if ($record) { $record.sha256=$artifactHash }
    else { $record=[pscustomobject]@{schema=1;sha256=$artifactHash;server_id=''} }
} elseif (-not $record -or -not (Test-Path -LiteralPath $proxy) -or (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -ne $record.sha256) { throw 'Install the client module first, or restore its recorded version.dll.' }
if ($ServerId) {
    $record.server_id=$ServerId
}
Save-Record
if ($Action -eq 'Shortcut' -or ($Action -eq 'Install' -and -not $NoDesktopShortcut -and $record.server_id)) { New-ClientShortcut }
elseif ($Action -eq 'Install') { Install-Launcher }
if ($Action -eq 'Install') { Write-Output 'Moon Sweepers dedicated client module installed.' }
if ($Action -eq 'Launch' -or $Launch) {
    if (-not (Test-Path -LiteralPath $launcherSource -PathType Leaf)) { throw 'The release is missing scripts/launch-client.ps1.' }
    & $launcherSource -GamePath $gameDirectory
} elseif ($Action -eq 'Install') {
    if (Get-RecordValue 'shortcut_path') { Write-Output 'Next time, double-click Join Moon Sweepers on your desktop or use -Action Launch.' }
    else { Write-Output 'Use -Action Launch to join, or -Action Shortcut -ServerId YOUR_SERVER_ID to create a desktop shortcut.' }
}
