$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\PicoSlip'
if (Test-Path -LiteralPath $installDir) {
    throw 'An existing PicoSlip installation must be removed manually before this test.'
}
$setupPath = Join-Path $root 'dist\PicoSlip-Setup-x64.exe'
$setup = Start-Process -FilePath $setupPath -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -ne 0) { throw "Setup failed: $($setup.ExitCode)" }
$installedExe = Join-Path $installDir 'PicoSlip.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'dist\PicoSlip.exe')).Hash) {
    throw 'Installed binary differs from release binary.'
}
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicoSlip'
if ((Get-ItemProperty -LiteralPath $registry).DisplayName -ne 'PicoSlip') { throw 'Uninstall registration missing.' }
if (!(Test-Path -LiteralPath (Join-Path $installDir 'LICENSE.txt'))) { throw 'MIT license was not installed.' }
$archive = Join-Path $env:LOCALAPPDATA 'PicoSlip\notes'
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
