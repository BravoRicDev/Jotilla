# Windows

The `windows` branch is the default branch and contains the native C17 application
for Windows 10/11, x64. See the [download table](../README.md#download) for the
installer and portable ZIP. Binaries are unsigned.

## Installation and startup

Run `Jotilla-Setup-x64.exe`, or extract the portable ZIP and run `Jotilla.exe`.
Keep `portable.flag` beside the portable EXE to retain portable mode.

Setup offers **Start Jotilla when I sign in to Windows**. Updates retain this
choice; rerun Setup to change it. At sign-in, Jotilla restores the session without
taking focus or creating an empty note; notes closed with **×** stay in the
archive. Silent Setup accepts `/S /AUTOSTART` or `/S /NOAUTOSTART`; `/S` alone
retains the existing choice. Portable mode does not register automatic startup.
Uninstall removes Jotilla's startup entry and preserves notes.

Setup also upgrades earlier Jotila installations. It retains the startup choice,
replaces the old program and shortcuts, and adopts the existing archive in place.
Existing notes are not moved or renamed. Portable users can replace the old EXE
with `Jotilla.exe` while keeping `portable.flag`, `archive.path` and the archive.

**Ctrl + Alt + +** places the new note's top-left corner exactly at the mouse
pointer. Creating a note from the tray keeps the usual staggered position.
Minimizing the explorer hides it in the tray; double-click the tray icon to return.

## Notes and portable data

Notes initially save in `%LOCALAPPDATA%\Jotilla\notes`, or in the `notes` folder
beside the portable EXE. **Archive** moves or switches folders. Back up the whole
archive, including `notes.ini` and RTF files. Updates and uninstall preserve notes.

## Build and checks

Clone the platform branch:

```sh
git clone --branch windows --single-branch https://github.com/GarauGarau/Jotilla.git
cd Jotilla
```

Build with MSVC and the Windows SDK:

```bat
build.cmd
build.cmd test
build.cmd test-checked
build.cmd analyze
```

Package downloads with `tools/package.ps1`.
See [performance measurements](performance.md).

## Update an existing checkout

If this checkout still calls the branch `main`, rename it and update its tracking
branch before pulling:

```sh
git branch -m main windows
git remote set-branches origin '*'
git fetch origin --prune
git branch -u origin/windows windows
git remote set-head origin -a
git switch windows
git pull --ff-only
```
