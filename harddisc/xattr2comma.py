#!/usr/bin/env python3
"""xattr2comma.py DIR: give every file under DIR its RISC OS filetype as a
",xxx" name suffix (IXFS reads either), from the user.RISC_OS.LoadExec
extended attribute IXFS writes, and remove the attribute. Suffixes survive
git, tar and container layers; extended attributes often don't.

Files with load/exec addresses instead of a filetype are left as they are
(and listed). Files that already have a suffix are skipped.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE)."""
import os
import re
import struct
import sys

ATTR = "user.RISC_OS.LoadExec"
SUFFIX = re.compile(r",[0-9a-fA-F]{3}$")


def main(top):
    done = untyped = 0
    for root, dirs, files in os.walk(top):
        for name in files:
            path = os.path.join(root, name)
            try:
                value = os.getxattr(path, ATTR)
            except OSError:
                continue
            load = struct.unpack("<I", value[:4])[0]
            if load >> 20 != 0xFFF:
                print(f"untyped (load &{load:08X}): {path}")
                untyped += 1
                continue
            if not SUFFIX.search(name):
                new = f"{path},{(load >> 8) & 0xFFF:03x}"
                os.rename(path, new)
                path = new
            os.removexattr(path, ATTR)
            done += 1
        for name in dirs:
            try:
                os.removexattr(os.path.join(root, name), ATTR)
            except OSError:
                pass
    print(f"xattr2comma: {done} files given a ,xxx suffix, {untyped} untyped")


if __name__ == "__main__":
    main(sys.argv[1])
