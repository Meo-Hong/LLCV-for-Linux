#!/bin/sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

for command in xgettext msgmerge; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "$command is required (sudo apt install gettext)." >&2
        exit 1
    fi
done

find src -name '*.cpp' -o -name '*.h' | LC_ALL=C sort > po/POTFILES
xgettext --language=C++ --from-code=UTF-8 --keyword=T --keyword=N_ --add-location=file --sort-by-file \
    --package-name=LLCV --msgid-bugs-address=https://github.com/Meo-Hong/LLCV-for-Linux/issues \
    --files-from=po/POTFILES --output=po/llcv.pot
rm po/POTFILES

for po in po/*.po; do
    msgmerge --quiet --update --backup=none --no-fuzzy-matching "$po" po/llcv.pot
done
python3 tools/check-translations.py
