#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
. /etc/os-release

run() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

family=""
for name in $ID ${ID_LIKE:-}; do
    case "$name" in
        debian|ubuntu) family=debian; break ;;
        fedora|rhel|centos) family=rpm; break ;;
        arch) family=arch; break ;;
    esac
done

case "$family" in
    debian)
        run apt-get update
        run apt-get install -y --no-install-recommends build-essential debhelper dpkg-dev pkgconf ca-certificates curl
        if apt-cache show libsdl3-dev >/dev/null 2>&1; then
            echo "Using the system SDL3 (libsdl3-dev)."
            run apt-get build-dep -y "$root"
        else
            echo "libsdl3-dev is not available; installing the dependencies for the bundled SDL3."
            run apt-get build-dep -y -P pkg.llcv.bundled-sdl3 "$root"
        fi
        ;;
    rpm)
        packages="rpm-build tar gzip pkgconf"
        if ! command -v curl >/dev/null 2>&1; then
            packages="$packages curl"
        fi
        if [ -n "$(rpm --eval '%{?fedora}')" ]; then
            run dnf install -y $packages
        else
            run dnf install -y dnf-plugins-core $packages
            if [ "$ID" = "rhel" ]; then
                run subscription-manager repos --enable "codeready-builder-for-rhel-$(rpm -E %rhel)-$(uname -m)-rpms"
            else
                run dnf config-manager --set-enabled crb
            fi
        fi
        run dnf builddep -y "$root/packaging/rpm/llcv.spec"
        ;;
    arch)
        run pacman -S --needed --noconfirm base-devel cmake pkgconf sdl3 libjpeg-turbo libpng wayland wayland-protocols libglvnd
        ;;
    *)
        echo "Unsupported distribution: $ID. See BUILDING.md, section \"Other distributions\"." >&2
        exit 1
        ;;
esac

echo "Build dependencies are installed. Next: ./tools/build-package.sh"
