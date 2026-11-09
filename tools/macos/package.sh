#!/bin/sh
# Local packaging only: no upload, publishing, or notarization submission.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
mkdir -p build/macos dist/macos
stage=$(mktemp -d "$root/build/macos/package.XXXXXX")
app="$stage/install/Jotila.app"
portable="$stage/portable/Jotila.app"
ditto build/macos/arm64/Jotila.app "$app"
lipo -create build/macos/arm64/Jotila.app/Contents/MacOS/Jotila \
    build/macos/x86_64/Jotila.app/Contents/MacOS/Jotila -output "$app/Contents/MacOS/Jotila"
lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/Jotila"
ditto "$app" "$portable"
/usr/libexec/PlistBuddy -c 'Add :JotilaPortable bool true' "$portable/Contents/Info.plist"
# Separate identity prevents Launch Services from routing a portable launch to
# an already-running installed app. Archive locking still protects the data.
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier io.github.GarauGarau.Jotila.portable' "$portable/Contents/Info.plist"
for bundle in "$app" "$portable"; do
    codesign --force --sign "${SIGN_IDENTITY:--}" --options runtime "$bundle"
    codesign --verify --strict "$bundle"
done
mkdir -p "$stage/portable/Notes.jotilaarchive"
cp LICENSE "$stage/portable/LICENSE"
cat > "$stage/portable/README.txt" <<'EOF'
Jotila for macOS — private preview

Run Jotila.app and choose Notes.jotilaarchive (or an existing archive folder).
The portable app asks for the folder each launch; it writes no archive location
to your user preferences. Keep the app and archive together when moving drives.
You can also use --portable --archive PATH. Do not store notes inside Jotila.app.
Back up the entire archive, including RTF and notes.ini. Test a copy of your data.

Global new note: Control+Option+Command+N. In the app: Command+N new note,
Command+E explorer, Command+F search, Command+T tags, Command+D date.
Command+B/I/U and Command+Shift+X format selected text. Command+plus/minus
and Command+wheel change selected font size or resize the note.
Closing a note keeps it in the archive. Closing the explorer hides it.
Use the menu bar icon to return. Quit Jotila before moving an archive.

This preview requires macOS 13 or later, Intel or Apple Silicon.
It has not yet completed native Mac build and desktop validation.
EOF
ditto -c -k --sequesterRsrc --keepParent "$stage/portable" dist/macos/Jotila-portable-universal.zip
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    pkgbuild --component "$app" --install-location /Applications --identifier io.github.GarauGarau.Jotila \
        --version "$version" --sign "$INSTALLER_IDENTITY" dist/macos/Jotila-Setup-universal.pkg
else
    pkgbuild --component "$app" --install-location /Applications --identifier io.github.GarauGarau.Jotila \
        --version "$version" dist/macos/Jotila-Setup-universal.pkg
fi
(cd dist/macos && shasum -a 256 Jotila-portable-universal.zip Jotila-Setup-universal.pkg > SHA256SUMS.txt)
printf 'Downloads prepared in %s/dist/macos\n' "$root"
