#!/usr/bin/env python3
"""Tell IndexNow (Bing, Yandex, Seznam) that the site changed.

Reads every URL from the published sitemap and submits them in one request.
The key file must already be reachable at the document root — it is committed
in site/ and shipped by scripts/deploy_site.sh.

    python3 scripts/indexnow_submit.py
"""

from __future__ import annotations

import glob
import json
import os
import re
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = "lukelang.org"
ORIGIN = f"https://{HOST}"


def key() -> str:
    keys = [
        os.path.basename(p)[:-4]
        for p in glob.glob(os.path.join(ROOT, "site", "*.txt"))
        if re.fullmatch(r"[0-9a-f]{32}\.txt", os.path.basename(p))
    ]
    if not keys:
        sys.exit("indexnow_submit: no key file in site/ (expected <32 hex chars>.txt)")
    return keys[0]


def urls() -> list[str]:
    sitemap = urllib.request.urlopen(f"{ORIGIN}/sitemap.xml", timeout=20).read().decode()
    return re.findall(r"<loc>(.*?)</loc>", sitemap)


def main() -> int:
    k = key()
    found = urls()
    payload = {
        "host": HOST,
        "key": k,
        "keyLocation": f"{ORIGIN}/{k}.txt",
        "urlList": found,
    }
    request = urllib.request.Request(
        "https://api.indexnow.org/indexnow",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json; charset=utf-8"},
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            print(f"IndexNow accepted {len(found)} urls (HTTP {response.status})")
    except urllib.error.HTTPError as exc:
        print(f"IndexNow refused the batch: HTTP {exc.code} — {exc.read().decode()[:200]}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
