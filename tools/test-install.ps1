$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\IOTA'
if (Test-Path -LiteralPath $installDir) {
    throw 'An existing IOTA installation must be removed manually before this test.'
}
if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\PicoSlip')) {
    throw 'An existing legacy installation must not be used for this destructive clean-install test.'
}
$setupPath = Join-Path $root 'dist\IOTA-Setup-x64.exe'
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0) { throw "Setup failed: $($setup.ExitCode)" }
$installedExe = Join-Path $installDir 'IOTA.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'dist\IOTA.exe')).Hash) {
    throw 'Installed binary differs from release binary.'
}
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\IOTA'
if ((Get-ItemProperty -LiteralPath $registry).DisplayName -ne 'IOTA') { throw 'Uninstall registration missing.' }
if (!(Test-Path -LiteralPath (Join-Path $installDir 'LICENSE.txt'))) { throw 'MIT license was not installed.' }
$archive = Join-Path $env:LOCALAPPDATA 'IOTA\notes'
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
    if ([IO.File]::ReadAllText($sentinel) -ne 'Uninstall must preserve notes.') { throw 'Notes archive changed.' }
    Write-Output 'PASS: per-user installation, exact binary, uninstall registration and notes preserved during uninstall.'
} finally {
    if (Test-Path -LiteralPath $sentinel) { Remove-Item -LiteralPath $sentinel }
}
