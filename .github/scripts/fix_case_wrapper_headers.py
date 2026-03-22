#!/usr/bin/env python3
"""
On Windows, case-colliding paths (for example Foo.h / foo.h) map to one file.
This repository contains many one-line wrapper headers used for Linux cross
builds. If a wrapper wins checkout on a case-insensitive filesystem, it can
self-include and strip required type definitions, causing massive compile
failures.

This script restores the "real" header content for each case-collision group
by reading blobs from HEAD and writing the non-wrapper variant to disk.
"""

from __future__ import annotations

import collections
import pathlib
import re
import subprocess
import sys


WRAPPER_RE = re.compile(
    br"^\s*#pragma\s+once\s*(?:\r?\n)+\s*#include\s+[\"<]([^\">]+)[\">]\s*$",
    re.S,
)


def git_lines(*args: str) -> list[str]:
    return subprocess.check_output(["git", *args], text=True).splitlines()


def git_blob(path: str) -> bytes:
    return subprocess.check_output(["git", "show", f"HEAD:{path}"])


def is_wrapper_blob(blob: bytes) -> bool:
    return WRAPPER_RE.match(blob) is not None


def main() -> int:
    files = git_lines("ls-files")
    by_lower: dict[str, list[str]] = collections.defaultdict(list)
    for path in files:
        by_lower[path.lower()].append(path)

    fixed_groups = 0
    skipped_groups = 0

    for variants in by_lower.values():
        if len(variants) < 2:
            continue

        blobs: list[tuple[str, bytes, bool]] = []
        for variant in variants:
            blob = git_blob(variant)
            blobs.append((variant, blob, is_wrapper_blob(blob)))

        non_wrappers = [entry for entry in blobs if not entry[2]]
        if not non_wrappers:
            skipped_groups += 1
            continue

        # Keep the richest definition if there is more than one real variant.
        _source_variant, source_blob, _ = max(non_wrappers, key=lambda entry: len(entry[1]))

        # On case-insensitive filesystems any variant path points at the same file.
        target = pathlib.Path(variants[0])
        target.parent.mkdir(parents=True, exist_ok=True)
        current = target.read_bytes() if target.exists() else b""
        if current != source_blob:
            target.write_bytes(source_blob)
            fixed_groups += 1

    print(
        f"Case-collision normalization complete: fixed={fixed_groups}, skipped={skipped_groups}",
        file=sys.stdout,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
