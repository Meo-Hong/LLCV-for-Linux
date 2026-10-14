#!/bin/sh
set -eu

version=Sans2.004
sha256=2bbdd2c20f30670b39ca735c96d75f1fdabdb348103e43b820cf17701fd22b18
url="https://github.com/notofonts/noto-cjk/releases/download/$version/16_NotoSansJP.zip"

root="$(cd "$(dirname "$0")/.." && pwd)"
target="$root/third_party/noto-sans-jp"

for command in curl unzip python3 pyftsubset; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "$command is required (pyftsubset comes from fonttools: pip install fonttools)." >&2
        exit 1
    fi
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "Downloading $url" >&2
curl -fL --retry 3 -o "$work/NotoSansJP.zip" "$url"
if ! echo "$sha256  $work/NotoSansJP.zip" | sha256sum -c - >/dev/null 2>&1; then
    echo "SHA256 mismatch for $url" >&2
    exit 1
fi
unzip -q -o "$work/NotoSansJP.zip" NotoSansJP-Regular.otf NotoSansJP-Bold.otf LICENSE -d "$work"

python3 - "$work/kanji.txt" <<'EOF'
import sys

def jis_level1(code):
    try:
        encoded = chr(code).encode("euc_jp")
    except UnicodeEncodeError:
        return False
    return len(encoded) == 2 and 16 <= encoded[0] - 0xA0 <= 47

with open(sys.argv[1], "w", encoding="utf-8") as output:
    output.write("".join(chr(code) for code in range(0x4E00, 0xA000) if jis_level1(code)))
EOF

mkdir -p "$target"
for weight in Regular Bold; do
    pyftsubset "$work/NotoSansJP-$weight.otf" --text-file="$work/kanji.txt" --layout-features='' --name-IDs='*' \
        --output-file="$target/NotoSansJP-Kanji-$weight.otf"
done
cp "$work/LICENSE" "$target/LICENSE.txt"
sha256sum "$target"/NotoSansJP-Kanji-*.otf
