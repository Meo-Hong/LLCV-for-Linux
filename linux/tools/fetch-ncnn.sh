#!/bin/sh
set -eu

version=20261020
sha256=699d9acc67489eee0993a551ca1ebc247bbf0f4e33babe1dec810e6a9a52b988
url="https://github.com/Tencent/ncnn/releases/download/$version/ncnn-$version-full-source.zip"

root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/third_party/ncnn"
archive="$target/ncnn-$version-full-source.zip"
source_dir="$target/ncnn-$version"
patch_file="$root/third_party/frame-interpolation/ncnn-$version-replay.patch"

if [ -f "$source_dir/.llcv-patched" ]; then
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

rm -rf "$source_dir"
mkdir -p "$source_dir"
(cd "$source_dir" && cmake -E tar xf "$archive")
patch -d "$source_dir" -p1 --quiet < "$patch_file"
touch "$source_dir/.llcv-patched"
echo "$source_dir"
