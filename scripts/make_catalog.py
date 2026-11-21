#!/usr/bin/env python3
"""Writes catalog/models.json, the list of models the engine works with.

Inputs, both in catalog/:
  firefox_records.json  a snapshot of Firefox's Remote Settings records
                        (collection translations-models-v2), from which the
                        translators are taken as Firefox for Android picks them:
                        records without a filter or with the Android filter,
                        the Android variant winning over the unfiltered one of
                        the same version, the highest complete version of each
                        direction;
  speech_models.json    the speech and voice detection models, written by hand.

  python3 -I scripts/make_catalog.py           # writes catalog/models.json
  python3 -I scripts/make_catalog.py --check   # exit 1 if it is not up to date
"""
import functools
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG = os.path.join(ROOT, "catalog")
ATTACHMENTS = "https://firefox-settings-attachments.cdn.mozilla.net/"
ANDROID_FILTER = "env.appinfo.OS == 'Android'"
FORMAT = 1
PIVOT_LANGUAGE = "en"
# A complete translator: one vocabulary, or separate source and target ones.
COMPLETE_KINDS = (["model", "vocab", "lex"], ["model", "srcvocab", "trgvocab", "lex"])


def kinds_of(kinds):
    for complete in COMPLETE_KINDS:
        if set(kinds) == set(complete):
            return complete
    return None


def compare_versions(a, b):
    pa = [int(x) for x in re.findall(r"\d+", a)]
    pb = [int(x) for x in re.findall(r"\d+", b)]
    for i in range(max(len(pa), len(pb))):
        x = pa[i] if i < len(pa) else 0
        y = pb[i] if i < len(pb) else 0
        if x != y:
            return -1 if x < y else 1
    # 3.0a1 (alpha) sorts before 3.0.
    return ("a" in b) - ("a" in a)


def translators(records):
    by_pair = {}  # "src|tgt" -> version -> kind -> record
    for r in records:
        flt = r.get("filter_expression") or ""
        if flt and flt != ANDROID_FILTER:
            continue
        pair = f"{r['sourceLanguage']}|{r['targetLanguage']}"
        kinds = by_pair.setdefault(pair, {}).setdefault(r["version"], {})
        kind = r["fileType"]
        if kinds.get(kind) is None or flt == ANDROID_FILTER:
            kinds[kind] = r
    out = []
    for pair in sorted(by_pair):
        versions = [(v, k) for v, k in by_pair[pair].items() if kinds_of(k.keys()) is not None]
        if not versions:
            continue
        versions.sort(key=functools.cmp_to_key(lambda a, b: compare_versions(a[0], b[0])))
        version, files = versions[-1]
        first = next(iter(files.values()))
        entry = {
            "id": first["sourceLanguage"] + first["targetLanguage"],
            "kind": "translator",
            "source_language": first["sourceLanguage"],
            "target_language": first["targetLanguage"],
            "version": version,
            "architecture": first["architecture"],
            "files": [],
        }
        for kind in kinds_of(files.keys()):
            r = files[kind]
            a = r["attachment"]
            entry["files"].append({
                "role": kind,
                "name": re.sub(r"\.zst$", "", a["filename"]),
                "url": ATTACHMENTS + a["location"],
                "compression": "zstd",
                "size": a["size"],
                "sha256": a["hash"],
                "installed_size": r["decompressedSize"],
                "installed_sha256": r["decompressedHash"],
            })
        out.append(entry)
    return out


def speech_models(entries):
    for entry in entries:
        for f in entry["files"]:
            if f["compression"] == "none":
                f.setdefault("installed_size", f["size"])
                f.setdefault("installed_sha256", f["sha256"])
    return entries


def build():
    with open(os.path.join(CATALOG, "firefox_records.json"), encoding="utf-8") as fh:
        records = json.load(fh)["data"]
    with open(os.path.join(CATALOG, "speech_models.json"), encoding="utf-8") as fh:
        speech = json.load(fh)
    models = translators(records) + speech_models(speech)
    ids = [m["id"] for m in models]
    duplicates = sorted({i for i in ids if ids.count(i) > 1})
    if duplicates:
        sys.exit(f"duplicate model ids: {', '.join(duplicates)}")
    models.sort(key=lambda m: m["id"])
    catalog = {"format": FORMAT, "pivot_language": PIVOT_LANGUAGE, "models": models}
    return json.dumps(catalog, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main():
    text = build()
    target = os.path.join(CATALOG, "models.json")
    if "--check" in sys.argv[1:]:
        try:
            with open(target, encoding="utf-8") as fh:
                current = fh.read()
        except FileNotFoundError:
            current = None
        if current != text:
            sys.exit("catalog/models.json is not up to date: run scripts/make_catalog.py")
        print("catalog/models.json is up to date")
        return
    with open(target, "w", encoding="utf-8") as fh:
        fh.write(text)
    count = json.loads(text)["models"]
    print(f"wrote {len(count)} models ({sum(m['kind'] == 'translator' for m in count)} translators)")


if __name__ == "__main__":
    main()
