#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^project(llcv VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
out="$root/dist/release/llcv-$version"

rm -rf "$out"
mkdir -p "$out"

for dir in "$root"/dist/*/; do
    case "$(basename "$dir")" in
        release|apt-repo) continue ;;
    esac
    for file in "$dir"llcv_"$version"_*.deb "$dir"llcv-"$version"-*.rpm "$dir"llcv-"$version"-*.pkg.tar.*; do
        [ -f "$file" ] || continue
        name="$(basename "$file")"
        case "$name" in
            *.src.rpm) continue ;;
            *_armhf.deb) name="${name%_armhf.deb}_raspberrypi-32bit_armhf.deb" ;;
        esac
        if [ -e "$out/$name" ]; then
            echo "$name was built more than once. Keep one build per format and architecture in dist/." >&2
            exit 1
        fi
        cp -p "$file" "$out/$name"
        printf '%-16s %s\n' "$(basename "$dir")" "$name"
    done
done

if [ -z "$(ls -A "$out")" ]; then
    echo "No packages found in $root/dist. Build them first." >&2
    exit 1
fi

(cd "$out" && sha256sum -- llcv* > SHA256SUMS)

echo
echo "Release files: $out"
