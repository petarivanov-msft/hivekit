#!/usr/bin/env python3
"""Resolve the project app binary filename from flasher_args.json.

Usage: resolve_app_bin.py <path-to-flasher_args.json>
Prints the bare filename (no directory), exits 0 on success, 1 on failure.
"""
import json
import sys


def main():
    if len(sys.argv) < 2:
        print("Usage: resolve_app_bin.py <flasher_args.json>", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]
    with open(path) as f:
        d = json.load(f)
    # Try canonical 'app' key first (ESP-IDF >= 4.4 style)
    if "app" in d and isinstance(d["app"], dict) and "file" in d["app"]:
        print(d["app"]["file"])
        sys.exit(0)
    # Fall back: find first offset-keyed value that ends with .bin and isn't
    # bootloader or partition table
    for v in d.values():
        if (
            isinstance(v, str)
            and v.endswith(".bin")
            and "bootloader" not in v
            and "partition" not in v
        ):
            print(v)
            sys.exit(0)
    print("ERROR: no app binary found in flasher_args.json", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
