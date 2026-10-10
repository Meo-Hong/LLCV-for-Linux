#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
. /etc/os-release

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
        "$root/tools/build-deb.sh"
        dist="$root/dist/${ID}-${VERSION_ID:-rolling}"
        package="$(ls "$dist"/llcv_*_"$(dpkg --print-architecture)".deb)"
        install="sudo apt install $package"
        ;;
    rpm)
        "$root/tools/build-rpm.sh"
        dist="$root/dist/${ID}-${VERSION_ID:-rolling}"
        package="$(ls "$dist"/llcv-[0-9]*."$(uname -m)".rpm)"
        install="sudo dnf install $package"
        ;;
    arch)
        cd "$root/packaging/arch"
        makepkg -f --noconfirm
        mkdir -p "$root/dist/arch"
        rm -f "$root/dist/arch"/*.pkg.tar.*
        cp ./*.pkg.tar.* "$root/dist/arch/"
        package="$(ls "$root/dist/arch"/llcv-[0-9]*.pkg.tar.*)"
        install="sudo pacman -U $package"
        ;;
    *)
        echo "Unsupported distribution: $ID. See BUILDING.md, section \"Other distributions\"." >&2
        exit 1
        ;;
esac

echo
echo "Package: $package"
echo "Install: $install"
