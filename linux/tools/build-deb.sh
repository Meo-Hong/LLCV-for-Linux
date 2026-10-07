#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
parent="$(dirname "$root")"
. /etc/os-release
dist="$root/dist/${ID}-${VERSION_ID:-rolling}"

cd "$root"
profiles=""
if ! pkg-config --exists 'sdl3 >= 3.2'; then
    echo "SDL3 3.2 or newer is not installed; building with the bundled SDL3." >&2
    "$root/tools/fetch-sdl3.sh" >/dev/null
    profiles="pkg.llcv.bundled-sdl3"
fi

dpkg-buildpackage -b -us -uc ${profiles:+-P"$profiles"}

mkdir -p "$dist"
rm -f "$dist"/llcv_* "$dist"/llcv-dbgsym_*
echo "${VERSION_CODENAME:-}" > "$dist/suite"
for file in "$parent"/llcv_*.deb "$parent"/llcv-dbgsym_*.ddeb "$parent"/llcv_*.buildinfo "$parent"/llcv_*.changes; do
    if [ -e "$file" ]; then
        mv -f "$file" "$dist/"
    fi
done
ls -1 "$dist"
