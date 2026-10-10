#!/bin/sh
set -eu

image="${1:-archlinux:latest}"
root="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$root/dist"

docker="docker"
if ! docker info >/dev/null 2>&1 && docker --context default info >/dev/null 2>&1; then
    docker="docker --context default"
fi

cache="llcv-pacman-cache-$(echo "$image" | tr ':/.' '---')"

$docker run --rm \
    -e HOST_UID="$(id -u)" \
    -e HOST_GID="$(id -g)" \
    -v "$root:/src:ro" \
    -v "$root/dist:/out" \
    -v "$cache:/var/cache/pacman/pkg" \
    "$image" /bin/sh -euc '
        pacman -Syu --noconfirm --needed --noprogressbar \
            base-devel cmake pkgconf sdl3 libjpeg-turbo libpng wayland wayland-protocols libglvnd >/dev/null
        useradd -m builder
        mkdir -p /build/linux
        tar -C /src --exclude=./build --exclude=./dist --exclude="./obj-*" \
            --exclude=./debian/llcv --exclude=./debian/.debhelper --exclude=./third_party/sdl3 \
            --exclude=./packaging/arch/src --exclude=./packaging/arch/pkg \
            -cf - . | tar -C /build/linux -xf -
        chown -R builder:builder /build
        cd /build/linux/packaging/arch
        su builder -c "makepkg -f --noconfirm"
        mkdir -p /out/arch
        rm -f /out/arch/*.pkg.tar.*
        cp ./*.pkg.tar.* /out/arch/
        chown -R "$HOST_UID:$HOST_GID" /out/arch
        ls -1 /out/arch
    '
