#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
. "$root/packaging/apt/repository.conf"
out="${1:-$root/dist/apt-repo}"
signing_key="${LLCV_APT_SIGNING_KEY:-}"

if ! command -v apt-ftparchive >/dev/null 2>&1; then
    echo "apt-ftparchive is required (sudo apt install apt-utils)." >&2
    exit 1
fi

suite="$LLCV_APT_SUITE"
pool="pool/$LLCV_APT_COMPONENT/l/llcv"

rm -rf "$out"
mkdir -p "$out/$pool"
found=""

for dir in "$root"/dist/*/; do
    case "$(basename "$dir")" in
        release|apt-repo) continue ;;
    esac
    for deb in "$dir"llcv_*.deb; do
        [ -e "$deb" ] || continue
        if [ -e "$out/$pool/$(basename "$deb")" ]; then
            echo "$(basename "$deb") was built more than once. Keep one build per architecture in dist/." >&2
            exit 1
        fi
        arch="$(dpkg-deb -f "$deb" Architecture)"
        mkdir -p "$out/dists/$suite/$LLCV_APT_COMPONENT/binary-$arch"
        cp "$deb" "$out/$pool/"
        found=yes
    done
done

if [ -z "$found" ]; then
    echo "No packages found under dist/*/ (run tools/build-deb.sh or tools/build-deb-docker.sh first)." >&2
    exit 1
fi

cd "$out"
architectures=""
for index in dists/"$suite"/"$LLCV_APT_COMPONENT"/binary-*; do
    arch="${index##*/binary-}"
    apt-ftparchive --arch "$arch" packages "$pool" > "$index/Packages"
    gzip -9kf "$index/Packages"
    architectures="$architectures $arch"
done
apt-ftparchive \
    -o APT::FTPArchive::Release::Origin="$LLCV_APT_ORIGIN" \
    -o APT::FTPArchive::Release::Label="$LLCV_APT_LABEL" \
    -o APT::FTPArchive::Release::Suite="$suite" \
    -o APT::FTPArchive::Release::Codename="$suite" \
    -o APT::FTPArchive::Release::Architectures="${architectures# }" \
    -o APT::FTPArchive::Release::Components="$LLCV_APT_COMPONENT" \
    -o APT::FTPArchive::Release::Description="LLCV low-latency capture viewer" \
    release "dists/$suite" > ".release-$suite"
mv ".release-$suite" "dists/$suite/Release"
if [ -n "$signing_key" ]; then
    gpg --batch --yes --local-user "$signing_key" --clearsign -o "dists/$suite/InRelease" "dists/$suite/Release"
    gpg --batch --yes --local-user "$signing_key" --armor --detach-sign -o "dists/$suite/Release.gpg" "dists/$suite/Release"
fi
echo "suite $suite:${architectures}"

if [ -n "$signing_key" ]; then
    gpg --batch --yes --armor --export "$signing_key" > "$out/llcv-archive-keyring.asc"
    echo "Signed repository written to $out"
else
    echo "Repository written to $out WITHOUT signatures."
    echo "Set LLCV_APT_SIGNING_KEY to a GPG key ID to sign it before publishing."
fi
