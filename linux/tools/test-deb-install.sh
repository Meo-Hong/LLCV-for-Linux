#!/bin/sh
set -eu

image="$1"
arch="$2"
deb="$3"

case "$arch" in
    amd64) platform=linux/amd64 ;;
    arm64) platform=linux/arm64 ;;
    armhf) platform=linux/arm/v7 ;;
    *)
        echo "Unknown architecture: $arch" >&2
        exit 1
        ;;
esac

docker="docker"
if ! docker info >/dev/null 2>&1 && docker --context default info >/dev/null 2>&1; then
    docker="docker --context default"
fi

directory="$(cd "$(dirname "$deb")" && pwd)"
name="$(basename "$deb")"

$docker run --rm --platform "$platform" -e DEB="/deb/$name" -v "$directory:/deb:ro" "$image" /bin/sh -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends "$DEB" >/dev/null
    dpkg-query -W -f="\${Package} \${Version} \${Architecture}\n" llcv
    llcv --version
    if ldd /usr/bin/llcv | grep "not found"; then
        echo "Missing shared libraries" >&2
        exit 1
    fi
    if ldd /usr/bin/llcv | grep -E "libav(codec|format|util)|libswscale|libx264"; then
        echo "FFmpeg or x264 is linked dynamically" >&2
        exit 1
    fi
    echo "install check passed: $(. /etc/os-release; echo "$PRETTY_NAME")"
'
