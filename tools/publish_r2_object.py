#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ci_release import CiError
from publish_r2_build import env, make_client

DEFAULT_CONTENT_TYPE = "application/octet-stream"


def main(argv: List[str]) -> int:
    def option(name: str) -> str:
        prefix = "--{}=".format(name)
        return next((a[len(prefix):] for a in argv if a.startswith(prefix)), "")

    source = option("file")
    if not source:
        raise CiError("usage: publish_r2_object.py --file=<path> "
                      "[--content-type=<mime>]")
    path = Path(source)
    if not path.is_file():
        raise CiError("{} not found".format(path))

    bucket = env("R2_BUCKET")
    prefix = env("R2_PREFIX").strip("/")
    if not prefix:
        raise CiError("R2_PREFIX must name a directory, not the bucket root")

    payload = path.read_bytes()
    key = "{}/{}".format(prefix, path.name)
    print("Uploading {} ({:.1f} MB) ...".format(key,
                                                len(payload) / 1024 / 1024))
    make_client().put_object(
        Bucket=bucket, Key=key, Body=payload,
        ContentType=option("content-type") or DEFAULT_CONTENT_TYPE,
        CacheControl="no-cache")
    print("Published {}".format(key))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CiError as exc:
        print("ERROR: {}".format(exc), file=sys.stderr)
        raise SystemExit(1)
