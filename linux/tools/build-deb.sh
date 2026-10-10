#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
parent="$(dirname "$root")"
. /etc/os-release
dist="$root/dist/${ID}-${VERSION_ID:-rolling}"
build_arch="$(dpkg --print-architecture)"
host_arch="${LLCV_HOST_ARCH:-$build_arch}"

cd "$root"
if [ -n "${LLCV_DEB_PROFILES+set}" ]; then
    profiles="$LLCV_DEB_PROFILES"
else
    profiles=""
    if ! pkg-config --exists 'sdl3 >= 3.2'; then
        echo "SDL3 3.2 or newer is not installed; building with the bundled SDL3." >&2
        profiles="pkg.llcv.bundled-sdl3"
    fi
fi
case "$profiles" in
    *pkg.llcv.bundled-sdl3*) "$root/tools/fetch-sdl3.sh" >/dev/null ;;
esac

cross=""
if [ "$host_arch" != "$build_arch" ]; then
    cross="-a$host_arch"
    triplet="$(dpkg-architecture -a"$host_arch" -qDEB_HOST_GNU_TYPE 2>/dev/null)"
    export CC="$triplet-gcc" CXX="$triplet-g++"
    export DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:+$DEB_BUILD_OPTIONS }nocheck"
    profiles="cross${profiles:+,$profiles}"
fi

dpkg-buildpackage -b -us -uc $cross ${profiles:+-P"$profiles"}

mkdir -p "$dist"
rm -f "$dist"/llcv_*_"$host_arch".deb "$dist"/llcv-dbgsym_*_"$host_arch".ddeb \
    "$dist"/llcv_*_"$host_arch".buildinfo "$dist"/llcv_*_"$host_arch".changes
echo "${VERSION_CODENAME:-}" > "$dist/suite"
for file in "$parent"/llcv_*_"$host_arch".deb "$parent"/llcv-dbgsym_*_"$host_arch".ddeb \
    "$parent"/llcv_*_"$host_arch".buildinfo "$parent"/llcv_*_"$host_arch".changes; do
    if [ -e "$file" ]; then
        mv -f "$file" "$dist/"
    fi
done
ls -1 "$dist"
