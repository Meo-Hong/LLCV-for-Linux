#!/bin/sh
set -eu

build="$1"
root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="$("$root/tools/fetch-ncnn.sh")"
prefix="$build/prefix"

rm -rf "$build"
mkdir -p "$build/ncnn"

env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS cmake -S "$source_dir" -B "$build/ncnn" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DNCNN_VULKAN=ON \
    -DNCNN_SIMPLEVK=ON \
    -DNCNN_SHARED_LIB=OFF \
    -DNCNN_OPENMP=OFF \
    -DNCNN_BUILD_TOOLS=OFF \
    -DNCNN_BUILD_EXAMPLES=OFF \
    -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_PYTHON=OFF \
    -DNCNN_C_API=OFF \
    -DNCNN_PIXEL_DRAWING=OFF >"$build/configure.log" 2>&1 || {
    tail -40 "$build/configure.log" >&2
    exit 1
}
env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS cmake --build "$build/ncnn" -j"$(nproc)" >"$build/build.log" 2>&1 || {
    tail -40 "$build/build.log" >&2
    exit 1
}
cmake --install "$build/ncnn" >/dev/null
echo "$prefix"
