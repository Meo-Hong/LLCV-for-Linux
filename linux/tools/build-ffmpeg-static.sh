#!/bin/sh
set -eu

build="$1"
host="${2:-}"
root="$(cd "$(dirname "$0")/.." && pwd)"
sources="$("$root/tools/fetch-ffmpeg.sh")"
ffmpeg_src="$(echo "$sources" | sed -n 1p)"
nvenc_src="$(echo "$sources" | sed -n 2p)"
prefix="$build/prefix"

native="$(cc -dumpmachine)"
pkg_config=pkg-config
target_cc=cc
cross=""
if [ -n "$host" ] && [ "$host" != "$native" ]; then
    pkg_config="$host-pkg-config"
    target_cc="$host-gcc"
    case "$host" in
        aarch64-*) arch=aarch64 ;;
        arm-*) arch=arm ;;
        x86_64-*) arch=x86_64 ;;
        i?86-*) arch=x86 ;;
        *) arch="${host%%-*}" ;;
    esac
    cross="--enable-cross-compile --cross-prefix=$host- --arch=$arch --target-os=linux"
fi

target="${host:-$native}"
nvenc_option=""
nvenc_encoders=""
case "$target" in
    x86_64-*|aarch64-*)
        nvenc_option="--enable-ffnvcodec --enable-nvenc"
        nvenc_encoders=",h264_nvenc,hevc_nvenc,av1_nvenc"
        ;;
esac

rm -rf "$build"
mkdir -p "$build/nvenc" "$build/ffmpeg" "$prefix/lib/pkgconfig"

cp -R "$nvenc_src/." "$build/nvenc/"
make -C "$build/nvenc" PREFIX="$prefix" install >/dev/null

x264_libdir="$($pkg_config --variable=libdir x264)"
if [ ! -f "$x264_libdir/libx264.a" ]; then
    echo "$x264_libdir/libx264.a not found; install libx264-dev for the target architecture." >&2
    exit 1
fi
x264_cflags="$($pkg_config --cflags x264)"
x264_libs="-L$x264_libdir -l:libx264.a -lpthread -lm -ldl"
printf '#include <stdint.h>\n#include <x264.h>\nlong check(void) { return (long) x264_encoder_encode; }\nint main(void) { return (int) (check() & 0xFFFF) == 1; }\n' > "$build/x264-test.c"
x264_option=""
x264_encoder=""
if $target_cc $x264_cflags -o "$build/x264-test" "$build/x264-test.c" $x264_libs >"$build/x264-test.log" 2>&1; then
    cat > "$prefix/lib/pkgconfig/x264.pc" <<EOF
Name: x264
Description: x264 static library
Version: $($pkg_config --modversion x264)
Libs: $x264_libs
Cflags: $x264_cflags
EOF
    x264_option="--enable-libx264"
    x264_encoder=",libx264"
else
    echo "The static libx264 cannot be linked into a position-independent executable here; building FFmpeg without x264." >&2
fi

cd "$build/ffmpeg"
env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS PKG_CONFIG_PATH="$prefix/lib/pkgconfig" "$ffmpeg_src/configure" \
    --prefix="$prefix" $cross --pkg-config="$pkg_config" --pkg-config-flags=--static \
    --enable-static --disable-shared --enable-pic --enable-gpl \
    --disable-programs --disable-doc --disable-debug --disable-avdevice --disable-avfilter \
    --disable-swresample --disable-postproc --disable-network --disable-autodetect --disable-everything \
    $nvenc_option --enable-vaapi $x264_option \
    --enable-encoder=aac,h264_vaapi,hevc_vaapi,av1_vaapi$nvenc_encoders$x264_encoder \
    --enable-muxer=mp4 --enable-protocol=file --enable-bsf=aac_adtstoasc >"$build/configure.log" 2>&1 || {
    tail -40 "$build/configure.log" >&2
    tail -40 "$build/ffmpeg/ffbuild/config.log" >&2 || true
    exit 1
}
grep -E "^(Enabled encoders|Enabled muxers)" -A4 "$build/configure.log" >&2 || true
env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS make -j"$(nproc)" >/dev/null
env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS make install >/dev/null
echo "$prefix"
