#!/bin/bash
# make_release.sh [--no-build]: WiiFlix-hbc.zip, the Homebrew Channel
# package, ready to unzip at the root of an SD card:
#
#   apps/WiiFlix/boot.dol    the app (WiiFlix.dol)
#   apps/WiiFlix/meta.xml    name and description; version (source/version.h)
#                            and release date filled in here
#   apps/WiiFlix/icon.png    128x48, the logo centred on transparency
#   apps/WiiFlix/fonts/      Chinese and Korean, read from the card (too big
#                            for the DOL)
#
# Builds WiiFlix.dol first (build.sh, -Werror) unless --no-build.  Needs
# ImageMagick and zip (apt install imagemagick zip).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" != "--no-build" ]; then
    ./build.sh WERROR=-Werror
fi
[ -f WiiFlix.dol ] || { echo "WiiFlix.dol missing: build it first" >&2; exit 1; }

version=$(sed -n 's/.*WIIFIN_VERSION "\([^"]*\)".*/\1/p' source/version.h)
[ -n "$version" ] || { echo "no version in source/version.h" >&2; exit 1; }
if command -v magick >/dev/null 2>&1; then IM=magick; else IM=convert; fi

pkg=$(mktemp -d)
trap 'rm -rf "$pkg"' EXIT
app="$pkg/apps/WiiFlix"
mkdir -p "$app/fonts"

cp WiiFlix.dol "$app/boot.dol"
sed -e "s|<version>.*</version>|<version>$version</version>|" \
    -e "s|<release_date>.*</release_date>|<release_date>$(date -u +%Y%m%d%H%M%S)</release_date>|" \
    apps/WiiFlix/meta.xml > "$app/meta.xml"
"$IM" assets/logo_wiifin.png -trim +repage -resize 128x48 \
    -background none -gravity center -extent 128x48 "PNG32:$app/icon.png"
cp data/fonts/cjk/NotoSansCJK-WiiFin.otf data/fonts/cjk/OFL.txt "$app/fonts/"

rm -f WiiFlix-hbc.zip
(cd "$pkg" && zip -qr - apps) > WiiFlix-hbc.zip
echo "WiiFlix-hbc.zip: WiiFlix $version for the Homebrew Channel"
unzip -l WiiFlix-hbc.zip
