#!/usr/bin/env python3
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Write the SPIR-V of a program's GLSL into the loft file that holds it.

    gpu_vulkan/spirv.py FILE.loft ...           write every SPIR-V constant again
    gpu_vulkan/spirv.py --check FILE.loft ...   exit 1 when one is not what its GLSL gives

A SPIR-V constant is derived, never edited.  It is declared after the line

    // SPIR-V of NAME (vertex|fragment), written by gpu_vulkan/spirv.py: do not edit.

as `const SOMETHING: text = `...`;`, where NAME is a `glsl-330` source constant of the same
file.  The GLSL is given what Vulkan asks for and OpenGL does not, the rest left as written:

  - `#version 330 core` becomes `#version 450`;
  - the one `layout(std140) uniform` block is put at set 0, binding 0;
  - `uniform sampler2D tex<n>` is put at set 0, binding 1 + n;
  - the values passed from the vertex to the fragment stage are numbered in the order each
    stage declares them (glslang's --auto-map-locations), so declare them in the same order.

A plain `uniform` outside the block has no place in Vulkan and is refused.  The module is
compiled for Vulkan 1.3 with no debug names, checked by spirv-val, and written as base64 in
lines of 76.  Needs glslangValidator and spirv-val (glslang-tools, spirv-tools).
"""
import base64
import os
import re
import subprocess
import sys
import tempfile

MARK = re.compile(r"^// SPIR-V of (\w+) \((vertex|fragment)\), written by gpu_vulkan/spirv\.py: do not edit\.$")


def glsl_of(text, name):
    m = re.search(r"const " + name + r"(?:: text)? = `(.*?)`;", text, re.S)
    if not m:
        raise SystemExit(f"spirv.py: no `const {name}` text in backticks")
    # a loft text in backticks writes a brace twice
    return m.group(1).replace("{{", "{").replace("}}", "}")


def vulkan_glsl(src, name):
    out = []
    for line in src.splitlines():
        s = line.strip()
        if re.match(r"#version\s+330(\s+core)?$", s):
            line = "#version 450"
        elif re.match(r"layout\(std140\)\s+uniform\b", s):
            line = line.replace("layout(std140)", "layout(std140, set = 0, binding = 0)", 1)
        else:
            m = re.match(r"uniform\s+sampler2D\s+tex(\d+)\s*;", s)
            if m:
                line = f"layout(set = 0, binding = {1 + int(m.group(1))}) uniform sampler2D tex{m.group(1)};"
            elif re.match(r"uniform\b", s):
                raise SystemExit(f"spirv.py: {name}: `{s}` is a uniform outside the block, which Vulkan has no place for")
        out.append(line)
    return "\n".join(out) + "\n"


def compile_spirv(src, stage, name):
    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "shader." + ("vert" if stage == "vertex" else "frag"))
        spv = os.path.join(work, "shader.spv")
        with open(path, "w") as fh:
            fh.write(src)
        r = subprocess.run(["glslangValidator", "-V", "--target-env", "vulkan1.3", "-g0",
                            "--auto-map-locations", path, "-o", spv],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"spirv.py: {name} does not compile for Vulkan:\n{r.stdout}{r.stderr}")
        r = subprocess.run(["spirv-val", "--target-env", "vulkan1.3", spv], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"spirv.py: {name}: spirv-val refuses the module:\n{r.stdout}{r.stderr}")
        with open(spv, "rb") as fh:
            return fh.read()


def rewrite(text):
    lines = text.split("\n")
    out = []
    i = 0
    count = 0
    while i < len(lines):
        out.append(lines[i])
        m = MARK.match(lines[i].strip())
        if not m:
            i += 1
            continue
        name, stage = m.group(1), m.group(2)
        # the declaration after the mark, up to its closing backtick
        j = i + 1
        head = re.match(r"(\s*(?:pub )?const \w+(?:: text)? = )`", lines[j]) if j < len(lines) else None
        if not head:
            raise SystemExit(f"spirv.py: the line after the mark for {name} declares no text constant")
        while not lines[j].rstrip().endswith("`;"):
            j += 1
            if j >= len(lines):
                raise SystemExit(f"spirv.py: the SPIR-V constant for {name} has no end")
        blob = base64.b64encode(compile_spirv(vulkan_glsl(glsl_of(text, name), name), stage, name)).decode()
        body = [blob[k:k + 76] for k in range(0, len(blob), 76)]
        out.append(head.group(1) + "`" + body[0])
        out.extend(body[1:-1] if len(body) > 1 else [])
        if len(body) > 1:
            out.append(body[-1] + "`;")
        else:
            out[-1] += "`;"
        i = j + 1
        count += 1
    return "\n".join(out), count


def main(argv):
    check = "--check" in argv
    files = [a for a in argv if a != "--check"]
    if not files:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    stale = []
    for path in files:
        with open(path) as fh:
            text = fh.read()
        new, count = rewrite(text)
        if new != text:
            if check:
                stale.append(path)
            else:
                with open(path, "w") as fh:
                    fh.write(new)
        if not check:
            print(f"{path}: {count} SPIR-V constant(s)")
    if stale:
        for path in stale:
            print(f"{path}: a SPIR-V constant is not what its GLSL gives: run gpu_vulkan/spirv.py {path}",
                  file=sys.stderr)
        return 1
    return 0


sys.exit(main(sys.argv[1:]))
