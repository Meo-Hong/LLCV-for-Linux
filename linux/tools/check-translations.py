#!/usr/bin/env python3
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PO_DIR = ROOT / "po"

FORMAT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcspn%]")
NUMBER = re.compile(r"\d+(?:\.\d+)?")
KEY_TOKENS = ("F1", "F2", "F3", "F5", "F11", "F12", "Tab", "Esc")
TECHNICAL = re.compile(r"[A-Za-z0-9_./+-]+")
NAMED_TERMS = {"Wayland", "Plasma", "Alt", "Ctrl", "Shift", "Linux", "uvcvideo", "llcv", "quantum"}
OPTIONAL_TERMS = {"LLCV", "Linux", "UI"}
SCRIPTS = {
    "hangul": re.compile(r"[가-힣ᄀ-ᇿ㄰-㆏]"),
    "kana": re.compile(r"[぀-ヿ]"),
}
FORBIDDEN_SCRIPTS = {"ko": ["kana"], "ja": ["hangul"], "es": ["hangul", "kana"], "pt_BR": ["hangul", "kana"]}


def unquote(line):
    return line[line.index('"') + 1:line.rindex('"')]


def parse(path):
    entries = []
    entry = {}
    current = None

    def flush():
        nonlocal entry, current
        if "msgid" in entry:
            entries.append(entry)
        entry = {}
        current = None

    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            flush()
        elif line.startswith("#"):
            if current == "msgstr":
                flush()
            if line.startswith("#,") and "fuzzy" in line:
                entry["fuzzy"] = True
        elif line.startswith("msgctxt") or line.startswith("msgid_plural") or line.startswith("msgstr["):
            raise SystemExit(f"{path.name}:{number}: contexts and plurals are not supported by the loader")
        elif line.startswith("msgid "):
            if current == "msgstr":
                flush()
            current = "msgid"
            entry["msgid"] = unquote(line)
            entry["line"] = number
        elif line.startswith("msgstr "):
            current = "msgstr"
            entry["msgstr"] = unquote(line)
        elif line.startswith('"') and current:
            entry[current] += unquote(line)
    flush()
    return [e for e in entries if e["msgid"]]


def key_tokens(text):
    found = Counter()
    i = 0
    while i < len(text):
        if i == 0 or not (text[i - 1].isascii() and text[i - 1].isalnum()):
            for token in KEY_TOKENS:
                end = i + len(token)
                if text[i:end] == token and (end == len(text) or not (text[end].isascii() and text[end].isalnum())):
                    found[token] += 1
                    i = end - 1
                    break
        i += 1
    return found


def id_suffix(text):
    match = re.search(r"#{2,3}.*$", text)
    return match.group(0) if match else ""


def technical_terms(text):
    terms = Counter()
    for word in TECHNICAL.findall(FORMAT.sub(" ", text.replace("\\n", " "))):
        word = word.strip("./+-")
        uppercase = sum(c.isupper() for c in word)
        mixed = any(c.isdigit() for c in word) and uppercase > 0
        if word not in OPTIONAL_TERMS and (word in NAMED_TERMS or uppercase >= 2 or "_" in word or mixed):
            terms[word] += 1
    return terms


def problems(language, msgid, msgstr):
    found = []
    if FORMAT.findall(msgid) != FORMAT.findall(msgstr):
        found.append(f"format {FORMAT.findall(msgid)} -> {FORMAT.findall(msgstr)}")
    if id_suffix(msgid) != id_suffix(msgstr):
        found.append(f"ImGui id {id_suffix(msgid)!r} -> {id_suffix(msgstr)!r}")
    if key_tokens(msgid) != key_tokens(msgstr):
        found.append(f"key names {dict(key_tokens(msgid))} -> {dict(key_tokens(msgstr))}")
    if Counter(NUMBER.findall(FORMAT.sub(" ", msgid))) != Counter(NUMBER.findall(FORMAT.sub(" ", msgstr))):
        found.append("numbers differ")
    missing = technical_terms(msgid) - technical_terms(msgstr)
    if missing:
        found.append(f"missing terms {sorted(missing)}")
    for prefix in (" · ", "\\n\\n"):
        if msgid.startswith(prefix) != msgstr.startswith(prefix):
            found.append(f"prefix {prefix!r}")
    if msgid.endswith(" ") and language != "ja" and not msgstr.endswith(" "):
        found.append("trailing space")
    for script in FORBIDDEN_SCRIPTS.get(language, []):
        if SCRIPTS[script].search(msgstr):
            found.append(f"{script} characters")
    return found


def main():
    template = parse(PO_DIR / "llcv.pot")
    template_ids = [e["msgid"] for e in template]
    failures = 0
    for po in sorted(PO_DIR.glob("*.po")):
        language = po.stem
        entries = {e["msgid"]: e for e in parse(po)}
        for msgid in template_ids:
            entry = entries.get(msgid)
            if not entry or not entry.get("msgstr") or entry.get("fuzzy"):
                print(f"{po.name}: untranslated: {msgid}")
                failures += 1
                continue
            for problem in problems(language, msgid, entry["msgstr"]):
                print(f"{po.name}:{entry['line']}: {problem}\n    {msgid}\n    {entry['msgstr']}")
                failures += 1
        for msgid in entries.keys() - set(template_ids):
            print(f"{po.name}: obsolete: {msgid}")
            failures += 1
    print(f"{failures} problem(s) in {len(list(PO_DIR.glob('*.po')))} catalogs, {len(template_ids)} messages")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
