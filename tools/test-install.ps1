$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\Jotila'
if (Test-Path -LiteralPath $installDir) {
    throw 'An existing Jotila installation must be removed manually before this test.'
}
if ((Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\PicoSlip')) -or (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\IOTA'))) {
    throw 'An existing legacy installation must not be used for this destructive clean-install test.'
}
$setupPath = Join-Path $root 'dist\Jotila-Setup-x64.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Get-ItemProperty -LiteralPath $runKey -Name Jotila -ErrorAction SilentlyContinue) {
    throw 'An existing Jotila startup setting must not be used for this clean-install test.'
}
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S /AUTOSTART' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0) { throw "Setup failed: $($setup.ExitCode)" }
$installedExe = Join-Path $installDir 'Jotila.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'dist\Jotila.exe')).Hash) {
    throw 'Installed binary differs from release binary.'
}
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Jotila'
if ((Get-ItemProperty -LiteralPath $registry).DisplayName -ne 'Jotila') { throw 'Uninstall registration missing.' }
if (!(Test-Path -LiteralPath (Join-Path $installDir 'LICENSE.txt'))) { throw 'MIT license was not installed.' }
$startupCommand = '"' + $installedExe + '" --startup'
if ((Get-ItemPropertyValue -LiteralPath $runKey -Name Jotila) -ne $startupCommand) { throw 'Startup command missing or incorrectly quoted.' }
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0 -or (Get-ItemPropertyValue -LiteralPath $runKey -Name Jotila) -ne $startupCommand) {
    throw 'Upgrade did not retain enabled startup.'
}
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S /NOAUTOSTART' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0 -or (Get-ItemProperty -LiteralPath $runKey -Name Jotila -ErrorAction SilentlyContinue)) {
    throw 'Setup did not disable startup.'
}
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0 -or (Get-ItemProperty -LiteralPath $runKey -Name Jotila -ErrorAction SilentlyContinue)) {
    throw 'Upgrade did not retain disabled startup.'
}
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S /AUTOSTART' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0) { throw 'Could not re-enable startup for the uninstall check.' }
$archive = Join-Path $env:LOCALAPPDATA 'Jotila\notes'
New-Item -ItemType Directory -Path $archive -Force | Out-Null
$sentinel = Join-Path $archive ('install-test-' + [guid]::NewGuid().ToString('N') + '.txt')
[IO.File]::WriteAllText($sentinel, 'Uninstall must preserve notes.')
try {
    $uninstall = Start-Process -FilePath (Join-Path $installDir 'Uninstall.exe') -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
    if ($uninstall.ExitCode -ne 0) { throw 'Uninstaller launcher failed.' }
    $deadline = [Environment]::TickCount64 + 10000
    while (((Test-Path -LiteralPath $installedExe) -or (Test-Path -LiteralPath $registry)) -and [Environment]::TickCount64 -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if ((Test-Path -LiteralPath $installedExe) -or (Test-Path -LiteralPath $registry)) { throw 'Uninstall incomplete.' }
    if (Get-ItemProperty -LiteralPath $runKey -Name Jotila -ErrorAction SilentlyContinue) { throw 'Uninstall left its startup entry behind.' }
    if ([IO.File]::ReadAllText($sentinel) -ne 'Uninstall must preserve notes.') { throw 'Notes archive changed.' }
    Write-Output 'PASS: per-user installation, exact binary, startup opt-in/out and upgrade retention, uninstall registration, startup cleanup and preserved notes.'
} finally {
    if (Test-Path -LiteralPath $sentinel) { Remove-Item -LiteralPath $sentinel }
}
