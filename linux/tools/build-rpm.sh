#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
spec="$root/packaging/rpm/llcv.spec"
version="$(rpmspec -q --srpm --qf '%{version}' "$spec")"
top="$root/build/rpmbuild"

rm -rf "$top"
for directory in BUILD BUILDROOT RPMS SOURCES SPECS SRPMS; do
    mkdir -p "$top/$directory"
done

tar -C "$root" \
    --exclude=./build --exclude=./dist --exclude="./obj-*" \
    --exclude=./debian/llcv --exclude=./debian/.debhelper --exclude=./third_party/sdl3 \
    --exclude=./packaging/arch/src --exclude=./packaging/arch/pkg \
    --transform "s,^\.,llcv-$version," \
    -czf "$top/SOURCES/llcv-$version.tar.gz" .

if [ -z "$(rpm --eval '%{?fedora}')" ]; then
    sdl_dir="$("$root/tools/fetch-sdl3.sh")"
    cp "$(dirname "$sdl_dir")"/SDL3-*.tar.gz "$top/SOURCES/"
fi

rpmbuild -ba --define "_topdir $top" "$spec"

. /etc/os-release
dist="$root/dist/${ID}-${VERSION_ID:-rolling}"
mkdir -p "$dist"
rm -f "$dist"/llcv-*.rpm
find "$top/RPMS" "$top/SRPMS" -name '*.rpm' -exec cp {} "$dist/" \;
ls -1 "$dist"
