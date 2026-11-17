#!/usr/bin/env python3
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Check c/vk_decl.h against the Khronos Vulkan header.

    gpu_vulkan/c/check_decl.py <dir holding vulkan/vulkan_core.h>

vk_decl.h declares the part of Vulkan this backend uses under Vulkan's own names, so the
backend builds with no development package.  This compiles two programs, one from vk_decl.h and
one from the Khronos header, each printing every struct's size and every field's offset in
vk_decl.h and every constant's value, and compares the two.  Exit 1 and the lines that differ
when they do.  Any `vulkan_core.h` of Vulkan 1.3 serves (an Android NDK's sysroot carries one).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
decl = os.path.join(here, "vk_decl.h")


def parse(text):
    structs = []  # (name, [field names])
    for m in re.finditer(r"typedef (?:struct|union) (Vk\w+) \{(.*?)\} \1;", text, re.S):
        fields = []
        for line in m.group(2).splitlines():
            line = line.strip()
            if not line or line.startswith("/*"):
                continue
            f = re.match(r".*?(\w+)(\[[^\]]+\])?;$", line)
            if f:
                fields.append(f.group(1))
        structs.append((m.group(1), fields))
    consts = re.findall(r"^#define (VK_\w+) ", text, re.M)
    return structs, consts


def program(include, structs, consts):
    out = ["#include <stdio.h>", "#include <stddef.h>", include, "int main(void) {"]
    for name, fields in structs:
        out.append(f'    printf("{name} %zu\\n", sizeof({name}));')
        for f in fields:
            out.append(f'    printf("{name}.{f} %zu\\n", offsetof({name}, {f}));')
    for c in consts:
        out.append(f'    printf("{c} %lld\\n", (long long) ({c}));')
    out.append("    return 0;\n}")
    return "\n".join(out) + "\n"


def run(src, flags, work, tag):
    c = os.path.join(work, tag + ".c")
    exe = os.path.join(work, tag)
    with open(c, "w") as fh:
        fh.write(src)
    subprocess.run(["cc", "-std=c99", "-w", c, "-o", exe] + flags, check=True)
    return subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()


def main():
    if len(sys.argv) != 2 or not os.path.isfile(os.path.join(sys.argv[1], "vulkan", "vulkan_core.h")):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(decl) as fh:
        structs, consts = parse(fh.read())
    with tempfile.TemporaryDirectory() as work:
        # only the header's own directory: a sysroot beside it would bring another libc
        for sub in ("vulkan", "vk_video"):
            if os.path.isdir(os.path.join(sys.argv[1], sub)):
                shutil.copytree(os.path.join(sys.argv[1], sub), os.path.join(work, "inc", sub))
        ours = run(program(f'#include "{decl}"', structs, consts), [], work, "ours")
        # vulkan_xlib.h wants Xlib's Display, Window and VisualID: declared as Xlib declares them.
        theirs = run(program("#include <vulkan/vulkan_core.h>\n"
                             "typedef struct _XDisplay Display;\n"
                             "typedef unsigned long Window;\ntypedef unsigned long VisualID;\n"
                             "#include <vulkan/vulkan_xlib.h>", structs, consts),
                     ["-I", os.path.join(work, "inc")], work, "theirs")
    bad = [f"  ours {a}   khronos {b}" for a, b in zip(ours, theirs) if a != b]
    if len(ours) != len(theirs):
        bad.append(f"  {len(ours)} lines against {len(theirs)}")
    if bad:
        print("vk_decl.h differs from the Khronos header:", file=sys.stderr)
        print("\n".join(bad), file=sys.stderr)
        return 1
    print(f"vk_decl.h: {len(structs)} structs, {len(consts)} constants, the same as the Khronos header")
    return 0


sys.exit(main())
