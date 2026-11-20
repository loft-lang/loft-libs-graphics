#!/usr/bin/env python3
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Write README.md's table of packages from each package's own loft.toml.

    ./packages.py           write the table again
    ./packages.py --check   exit 1 when the table is not what the manifests give

The table stands between the lines `<!-- packages:begin -->` and `<!-- packages:end -->`.  A
package is a directory with a loft.toml; its row is its name, its version and its description.
The order is LAYERS below: what draws on a GPU from the bottom up, then what paints pixels,
then geometry.  A package no layer names is listed last, so a new one is never left out.
"""
import os
import re
import sys

LAYERS = [
    ("Drawing on a GPU", ["gpu", "gpu_gl", "gpu_vulkan", "gpu_metal", "gpu_record", "gpu_host"]),
    ("Built on `gpu`", ["gpu_paint", "scene3d", "graphics", "stage"]),
    ("Pixels, images and text", ["imaging", "drawing", "text2d", "tiles"]),
    ("Geometry and motion", ["shapes", "gridmesh", "tween"]),
]
# What a package is, until its own manifest says it.
FALLBACK = {"shapes": "2D collision detection", "gridmesh": "Chunk-local mesh generation primitives"}
BEGIN, END = "<!-- packages:begin -->", "<!-- packages:end -->"


def manifest(root, name):
    with open(os.path.join(root, name, "loft.toml")) as fh:
        text = fh.read()
    def field(key):
        m = re.search(r'^' + key + r'\s*=\s*"((?:[^"\\]|\\.)*)"', text, re.M)
        return m.group(1) if m else ""
    return field("version"), field("description")


def table(root):
    have = sorted(d for d in os.listdir(root) if os.path.isfile(os.path.join(root, d, "loft.toml")))
    layers = [(title, [n for n in names if n in have]) for title, names in LAYERS]
    named = {n for _, names in layers for n in names}
    rest = [n for n in have if n not in named]
    if rest:
        layers.append(("Other", rest))
    out = ["| Package | Version | What it is |", "|---|---|---|"]
    for title, names in layers:
        if not names:
            continue
        out.append(f"| **{title}** | | |")
        for n in names:
            version, description = manifest(root, n)
            out.append(f"| [`{n}`]({n}/) | {version} | {description or FALLBACK.get(n, '')} |")
    return "\n".join(out)


def main(argv):
    root = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(root, "README.md")
    with open(path) as fh:
        text = fh.read()
    if BEGIN not in text or END not in text:
        print(f"packages.py: README.md has no {BEGIN} … {END}", file=sys.stderr)
        return 2
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new = head + BEGIN + "\n" + table(root) + "\n" + END + tail
    if "--check" in argv:
        if new != text:
            print("README.md: the table of packages is not what the manifests give: run ./packages.py", file=sys.stderr)
            return 1
        return 0
    if new != text:
        with open(path, "w") as fh:
            fh.write(new)
    return 0


sys.exit(main(sys.argv[1:]))
