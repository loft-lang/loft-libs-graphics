#!/usr/bin/env python3
# Copyright (c) 2026 Jurjen Stellingwerff
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Write the SPIR-V of a program's GLSL into the loft file that holds it.

    gpu_vulkan/spirv.py FILE.loft ...           write every SPIR-V constant again
    gpu_vulkan/spirv.py --check FILE.loft ...   exit 1 when one is not what its GLSL gives

A SPIR-V constant is derived, never edited.  It is declared after the line

    // SPIR-V of NAME (vertex|fragment), written by gpu_vulkan/spirv.py: do not edit.

as `const SOMETHING: text = `...`;`, where NAME is a `glsl-330` source constant of the same
file (in backticks, or on one line in double quotes).  The GLSL is given what Vulkan asks for and OpenGL does not, the rest left as written:

  - `#version 330 core` becomes `#version 450`;
  - the one `layout(std140) uniform` block is put at set 0, binding 0;
  - `uniform sampler2D tex<n>` is put at set 0, binding 1 + n;
  - the values passed from the vertex to the fragment stage are numbered in the order each
    stage declares them (glslang's --auto-map-locations), so declare them in the same order.

A plain `uniform` outside the block has no place in Vulkan and is refused.

A shader written for `graphics`, which declares its uniforms one by one, is marked

    // SPIR-V of VERTEX with FRAGMENT (vertex|fragment), as graphics hands them on, written by gpu_vulkan/spirv.py: do not edit.

and is first made into the form `graphics` gives `gpu` (its `tools/rewritten.loft`, the
same code the library runs), so the block's layout is the one the program's uniforms are
set into.  `graphics` is looked for beside this package and then among the installed ones.  The module is
compiled for Vulkan 1.1 (SPIR-V 1.3, so `discard` is OpKill and asks for no device feature) with no debug names, checked by spirv-val, and written as base64 in
lines of 76.  Needs glslangValidator and spirv-val (glslang-tools, spirv-tools).
"""
import base64
import os
import re
import subprocess
import sys
import tempfile

THROUGH = re.compile(r"^// SPIR-V of (\w+) with (\w+) \((vertex|fragment)\), as graphics hands them on, "
                     r"written by gpu_vulkan/spirv\.py: do not edit\.$")
MARK = re.compile(r"^// SPIR-V of (\w+) \((vertex|fragment)\), written by gpu_vulkan/spirv\.py: do not edit\.$")


def glsl_of(text, name):
    m = re.search(r"(?:^|\n)(?:pub )?(?:const )?" + name + r"(?:: text)? = `(.*?)`;", text, re.S)
    if not m:
        # the same on one line, in double quotes with its newlines written as \n
        q = re.search(r"(?:^|\n)(?:pub )?(?:const )?" + name + r'(?:: text)? = "((?:[^"\\]|\\.)*)";', text)
        if not q:
            raise SystemExit(f"spirv.py: no `{name}` text constant in backticks or quotes")
        one = q.group(1).replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
        return one.replace("{{", "{").replace("}}", "}")
    # a loft text in backticks writes a brace twice
    return m.group(1).replace("{{", "{").replace("}}", "}")


def vulkan_glsl(src, name):
    out = []
    for line in src.splitlines():
        s = line.strip()
        if re.match(r"#version\s+330(\s+core)?$", s):
            line = "#version 450"
        elif re.match(r"layout\s*\(\s*std140\s*\)\s*uniform\b", s):
            line = re.sub(r"layout\s*\(\s*std140\s*\)", "layout(std140, set = 0, binding = 0)", line, count=1)
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
        r = subprocess.run(["glslangValidator", "-V", "--target-env", "vulkan1.1", "-g0",
                            "--auto-map-locations", path, "-o", spv],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"spirv.py: {name} does not compile for Vulkan:\n{r.stdout}{r.stderr}")
        r = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", spv], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"spirv.py: {name}: spirv-val refuses the module:\n{r.stdout}{r.stderr}")
        with open(spv, "rb") as fh:
            return fh.read()


def graphics_tool():
    here = os.path.dirname(os.path.abspath(__file__))
    beside = os.path.join(here, "..", "graphics", "tools", "rewritten.loft")
    if os.path.isfile(beside):
        return os.path.normpath(beside), os.path.normpath(os.path.join(here, ".."))
    import glob
    home = os.environ.get("LOFT_HOME", os.path.expanduser("~"))
    found = sorted(glob.glob(os.path.join(home, ".loft", "registry", "graphics-[0-9]*", "tools", "rewritten.loft")),
                   key=lambda p: [int(x) if x.isdigit() else 0 for x in re.split(r"[.-]", p.split("graphics-")[-1].split(os.sep)[0])])
    if not found:
        raise SystemExit("spirv.py: no graphics/tools/rewritten.loft beside this package or installed")
    return found[-1], None


REWRITTEN = {}


def through_graphics(text, vname, fname):
    """(vertex, fragment) as graphics hands the two named sources to gpu."""
    # by the sources themselves: two files may give the same names to different shaders
    key = (glsl_of(text, vname), glsl_of(text, fname))
    if key in REWRITTEN:
        return REWRITTEN[key]
    tool, lib = graphics_tool()
    with tempfile.TemporaryDirectory() as work:
        vp, fp = os.path.join(work, "v.glsl"), os.path.join(work, "f.glsl")
        # a loft text in backticks drops the newline after the opening one
        with open(vp, "w") as fh:
            fh.write(glsl_of(text, vname).lstrip("\n"))
        with open(fp, "w") as fh:
            fh.write(glsl_of(text, fname).lstrip("\n"))
        cmd = ["loft", "--interpret"] + (["--lib", lib] if lib else []) + [tool, vp, fp]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(vp + ".out"):
            raise SystemExit(f"spirv.py: graphics does not carry {vname} with {fname}:\n{r.stdout}{r.stderr}")
        with open(vp + ".out") as fh:
            v = fh.read()
        with open(fp + ".out") as fh:
            f = fh.read()
    REWRITTEN[key] = (v, f)
    return v, f


def rewrite(text):
    lines = text.split("\n")
    out = []
    i = 0
    count = 0
    while i < len(lines):
        out.append(lines[i])
        m = MARK.match(lines[i].strip())
        t = THROUGH.match(lines[i].strip())
        if not m and not t:
            i += 1
            continue
        if t:
            stage = t.group(3)
            name = t.group(1) if stage == "vertex" else t.group(2)
            pair = through_graphics(text, t.group(1), t.group(2))
            glsl = pair[0] if stage == "vertex" else pair[1]
        else:
            name, stage = m.group(1), m.group(2)
            glsl = glsl_of(text, name)
        # the declaration after the mark, up to its closing backtick
        j = i + 1
        head = re.match(r"(\s*(?:pub )?const \w+(?:: text)? = )`", lines[j]) if j < len(lines) else None
        if not head:
            raise SystemExit(f"spirv.py: the line after the mark for {name} declares no text constant")
        while not lines[j].rstrip().endswith("`;"):
            j += 1
            if j >= len(lines):
                raise SystemExit(f"spirv.py: the SPIR-V constant for {name} has no end")
        blob = base64.b64encode(compile_spirv(vulkan_glsl(glsl, name), stage, name)).decode()
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
