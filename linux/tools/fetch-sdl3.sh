#!/bin/sh
set -eu

version=3.4.18
sha256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
url="https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-$version.tar.gz"

root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/third_party/sdl3"
archive="$target/SDL3-$version.tar.gz"
source_dir="$target/SDL3-$version"

if [ -f "$source_dir/CMakeLists.txt" ]; then
    echo "$source_dir"
    exit 0
fi

mkdir -p "$target"
if [ ! -f "$archive" ]; then
    echo "Downloading $url" >&2
    curl -fL --retry 3 -o "$archive.part" "$url"
    mv "$archive.part" "$archive"
fi

if ! echo "$sha256  $archive" | sha256sum -c - >/dev/null 2>&1; then
    echo "SHA256 mismatch for $archive; delete it and run this script again." >&2
    exit 1
fi

tar -xzf "$archive" -C "$target"
echo "$source_dir"
