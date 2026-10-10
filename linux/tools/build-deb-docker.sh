#!/bin/sh
set -eu

image="${1:-ubuntu:24.04}"
cross_arch="${2:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"

"$root/tools/fetch-sdl3.sh" >/dev/null
mkdir -p "$root/dist"

docker="docker"
if ! docker info >/dev/null 2>&1 && docker --context default info >/dev/null 2>&1; then
    docker="docker --context default"
fi

cache="llcv-apt-cache-$(echo "$image" | tr ':/.' '---')${cross_arch:+-$cross_arch}"

$docker run --rm \
    -e HOST_UID="$(id -u)" \
    -e HOST_GID="$(id -g)" \
    -e CROSS_ARCH="$cross_arch" \
    -v "$root:/src:ro" \
    -v "$root/dist:/out" \
    -v "$cache:/var/cache/apt/archives" \
    "$image" /bin/sh -euc '
        export DEBIAN_FRONTEND=noninteractive
        rm -f /etc/apt/apt.conf.d/docker-clean
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends build-essential debhelper dpkg-dev pkgconf ca-certificates curl >/dev/null
        mkdir -p /build/linux
        tar -C /src --exclude=./build --exclude=./dist --exclude="./obj-*" \
            --exclude=./debian/llcv --exclude=./debian/.debhelper -cf - . | tar -C /build/linux -xf -
        cd /build/linux
        profiles=""
        if [ -n "$CROSS_ARCH" ]; then
            dpkg --add-architecture "$CROSS_ARCH"
            apt-get update -qq
            apt-get install -y -qq --no-install-recommends "crossbuild-essential-$CROSS_ARCH" "pkgconf:$CROSS_ARCH" >/dev/null
            if ! apt-cache show "libsdl3-dev:$CROSS_ARCH" >/dev/null 2>&1; then
                profiles="pkg.llcv.bundled-sdl3"
            fi
            apt-get build-dep -y -qq -a "$CROSS_ARCH" ${profiles:+-P "$profiles"} ./ >/dev/null
            export LLCV_HOST_ARCH="$CROSS_ARCH"
        else
            if ! apt-cache show libsdl3-dev >/dev/null 2>&1; then
                profiles="pkg.llcv.bundled-sdl3"
            fi
            apt-get build-dep -y -qq ${profiles:+-P "$profiles"} ./ >/dev/null
        fi
        export LLCV_DEB_PROFILES="$profiles"
        ./tools/build-deb.sh
        cp -r dist/. /out/
        chown -R "$HOST_UID:$HOST_GID" /out
    '
