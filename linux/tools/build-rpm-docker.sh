#!/bin/sh
set -eu

image="${1:-fedora:latest}"
root="$(cd "$(dirname "$0")/.." && pwd)"

"$root/tools/fetch-sdl3.sh" >/dev/null
mkdir -p "$root/dist"

docker="docker"
if ! docker info >/dev/null 2>&1 && docker --context default info >/dev/null 2>&1; then
    docker="docker --context default"
fi

cache="llcv-dnf-cache-$(echo "$image" | tr ':/.' '---')"

$docker run --rm \
    -e HOST_UID="$(id -u)" \
    -e HOST_GID="$(id -g)" \
    -v "$root:/src:ro" \
    -v "$root/dist:/out" \
    -v "$cache:/var/cache/dnf" \
    "$image" /bin/sh -euc '
        echo "keepcache=True" >> /etc/dnf/dnf.conf
        . /etc/os-release
        if [ "$ID" = "fedora" ]; then
            dnf -y -q install rpm-build rpmdevtools tar gzip curl findutils pkgconf >/dev/null
        else
            dnf -y -q install dnf-plugins-core rpm-build tar gzip findutils pkgconf >/dev/null
            dnf config-manager --set-enabled crb
        fi
        mkdir -p /build/linux
        tar -C /src --exclude=./build --exclude=./dist --exclude="./obj-*" \
            --exclude=./debian/llcv --exclude=./debian/.debhelper \
            --exclude=./packaging/arch/src --exclude=./packaging/arch/pkg \
            -cf - . | tar -C /build/linux -xf -
        dnf -y -q builddep /build/linux/packaging/rpm/llcv.spec >/dev/null
        /build/linux/tools/build-rpm.sh
        cp -r /build/linux/dist/. /out/
        chown -R "$HOST_UID:$HOST_GID" /out
    '
