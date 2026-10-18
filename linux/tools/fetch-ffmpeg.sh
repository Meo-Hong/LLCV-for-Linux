#!/bin/sh
set -eu

ffmpeg_version=7.1.1
ffmpeg_sha256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
ffmpeg_url="https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz"
nvenc_version=12.0.16.1
nvenc_sha256=bfb3306f22c169fedcfc5489b344f414089eec796ffd528412677b0c872bc6e2
nvenc_url="https://github.com/FFmpeg/nv-codec-headers/releases/download/n$nvenc_version/nv-codec-headers-$nvenc_version.tar.gz"

root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/third_party/ffmpeg"
mkdir -p "$target"

fetch() {
    url="$1"
    sha256="$2"
    archive="$target/$(basename "$url")"
    if [ ! -f "$archive" ]; then
        echo "Downloading $url" >&2
        curl -fL --retry 3 -o "$archive.part" "$url"
        mv "$archive.part" "$archive"
    fi
    if ! echo "$sha256  $archive" | sha256sum -c - >/dev/null 2>&1; then
        echo "SHA256 mismatch for $archive; delete it and run this script again." >&2
        exit 1
    fi
    tar -xf "$archive" -C "$target"
}

if [ ! -f "$target/ffmpeg-$ffmpeg_version/configure" ]; then
    fetch "$ffmpeg_url" "$ffmpeg_sha256"
fi
if [ ! -f "$target/nv-codec-headers-$nvenc_version/Makefile" ]; then
    fetch "$nvenc_url" "$nvenc_sha256"
fi
echo "$target/ffmpeg-$ffmpeg_version"
echo "$target/nv-codec-headers-$nvenc_version"
