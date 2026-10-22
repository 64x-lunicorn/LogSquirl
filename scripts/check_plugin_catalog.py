#!/usr/bin/env python3
"""Checks that every download the plugin catalog offers matches its SHA-256 (#817).

    python3 scripts/check_plugin_catalog.py [plugins.json]

Downloads each entry's archive and compares its SHA-256 with the catalog's
`sha256`. An entry without a well-formed hash, an URL that is not https, or a
download whose hash differs fails the check; the exit code is the number of
failures. No file is written.
"""

import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def sha256_of(url: str) -> str:
    digest = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=120) as response:  # noqa: S310 - https only, checked below
        for chunk in iter(lambda: response.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv: list[str]) -> int:
    catalog = Path(argv[1] if len(argv) > 1 else "plugins.json")
    entries = json.loads(catalog.read_text(encoding="utf-8"))["plugins"]
    failures = 0
    for entry in entries:
        name = f"{entry.get('id')} {entry.get('version')} {'/'.join(entry.get('platforms', []))}"
        url = entry.get("download_url", "")
        expected = entry.get("sha256", "").lower()
        if not url.startswith("https://"):
            print(f"FAIL {name}: download_url is not https: {url}")
            failures += 1
            continue
        if not HEX_SHA256.match(expected):
            print(f"FAIL {name}: sha256 is not 64 hex digits: {expected!r}")
            failures += 1
            continue
        actual = sha256_of(url)
        if actual != expected:
            print(f"FAIL {name}: catalog says {expected}, download is {actual}")
            failures += 1
        else:
            print(f"ok   {name}")
    print(f"{len(entries) - failures} of {len(entries)} entries match their download")
    return failures


if __name__ == "__main__":
    sys.exit(main(sys.argv))
