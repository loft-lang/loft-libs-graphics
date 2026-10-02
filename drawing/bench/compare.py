#!/usr/bin/env python3
"""compare.py — the performance pass, joined: does every routine pull its weight?

    python3 bench/compare.py [--n-ref 50] [--n-native 50] [--n-interp 1] [--bar 4.0]
                             [--repeat 3] [--skip-interp] [--loft loft]

Runs three lanes of the same workloads — the pure-Rust reference (bench/bench.rs, built
with `rustc -O` exactly as loft's own bench/run_bench.sh builds its), the loft native build
and the loft interpreter (bench/bench.loft) — parses their rows, joins them by routine and
prints one table. The native lane is what a CONSUMER gets: the program compiled with
`--native-release`, the libraries as the auto-built cdylibs loft keeps for them (rustc
opt-level 2), every library call crossing that boundary. The interpreter lane sets
`LOFT_NO_NATIVE_LIBS=1`, without which the libraries would still run as those cdylibs and
the lane would measure the same code twice. Two verdicts per routine, and either fails
the run:

  * the HASHES must agree across every lane that ran the routine — a routine whose lanes
    disagree is not one algorithm, and its speeds are not comparable;
  * loft-native ns/op must be within `--bar` × the reference's — the routine pulls its
    weight. A routine with no reference row is measured but not judged.

The interpreter lane is informational (its ratio to native says what compiling buys).
Nothing here knows what the routines are: any package that prints the same seven-column
rows from a bench program and a reference can use this unchanged.  @FR-Perf-Weight
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.dirname(HERE)


def run(cmd, cwd, what, env=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       env={**os.environ, **(env or {})})
    if p.returncode != 0:
        sys.stderr.write(f"{what} failed ({p.returncode}):\n{p.stderr[-4000:]}\n")
        sys.exit(2)
    rows = {}
    for line in p.stdout.splitlines():
        parts = line.rstrip("\n").split("\t")
        if len(parts) != 7 or parts[0] == "routine":
            continue
        name, iters, us, ns_op, px, ns_px, h = parts
        rows[name] = dict(iters=int(iters), ns_op=int(ns_op), px=int(px), ns_px=float(ns_px), hash=h)
    if not rows:
        sys.stderr.write(f"{what}: no rows in its output\n{p.stdout[-2000:]}\n")
        sys.exit(2)
    return rows


def best_of(times, cmd, cwd, what, env=None):
    """The lane run `times` times, each row keeping its fastest ns/op — wall time on a
    shared box is noise on top of the number, and the minimum is the least noisy
    estimate of it (loft's own bench/run_bench.sh reports best of 3 for the same reason).
    The hash must not move between runs, or the routine is not deterministic."""
    best = {}
    for _ in range(times):
        rows = run(cmd, cwd, what, env)
        for name, row in rows.items():
            if name in best:
                if row["hash"] != best[name]["hash"]:
                    sys.stderr.write(f"{what}: {name} changed its hash between runs\n")
                    sys.exit(2)
                if row["ns_op"] < best[name]["ns_op"]:
                    best[name] = row
            else:
                best[name] = row
    return best


def fmt(v):
    return "—" if v is None else f"{v:,}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-ref", type=int, default=50)
    ap.add_argument("--n-native", type=int, default=50)
    ap.add_argument("--n-interp", type=int, default=1)
    ap.add_argument("--bar", type=float, default=4.0)
    ap.add_argument("--skip-interp", action="store_true")
    ap.add_argument("--repeat", type=int, default=3, help="best-of runs for the judged lanes")
    ap.add_argument("--loft", default="loft")
    a = ap.parse_args()

    # The reference: one file, `rustc -O`, no crate — the shape loft's own bench/ uses.
    build = os.path.join(HERE, ".build")
    os.makedirs(build, exist_ok=True)
    exe = os.path.join(build, "bench_rs")
    p = subprocess.run(["rustc", "-O", "--edition=2021", os.path.join(HERE, "bench.rs"), "-o", exe],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(f"rustc failed:\n{p.stderr[-4000:]}\n")
        sys.exit(2)
    ref = best_of(a.repeat, [exe, "--n", str(a.n_ref)], PKG, "the Rust reference")
    nat = best_of(a.repeat, [a.loft, "--native-release", "bench/bench.loft", "--n", str(a.n_native)],
                  PKG, "loft --native-release")
    itp = {} if a.skip_interp else run([a.loft, "--interpret", "bench/bench.loft", "--n", str(a.n_interp)],
                                       PKG, "the loft interpreter", env={"LOFT_NO_NATIVE_LIBS": "1"})

    names = list(nat) + [n for n in ref if n not in nat]
    print(f"{'routine':14} {'rust ns/op':>12} {'native ns/op':>13} {'nat/rust':>9} "
          f"{'interp ns/op':>13} {'int/nat':>8}  {'px':>7}  hash")
    problems = []
    for name in names:
        r, n, i = ref.get(name), nat.get(name), itp.get(name)
        ratio = (n["ns_op"] / r["ns_op"]) if (r and n and r["ns_op"] > 0) else None
        iratio = (i["ns_op"] / n["ns_op"]) if (i and n and n["ns_op"] > 0) else None
        hashes = {lane: row["hash"] for lane, row in (("rust", r), ("native", n), ("interp", i)) if row}
        agree = len(set(hashes.values())) == 1
        px = (n or r or i)["px"]
        print(f"{name:14} {fmt(r and r['ns_op']):>12} {fmt(n and n['ns_op']):>13} "
              f"{('%.2f' % ratio) if ratio is not None else '—':>9} "
              f"{fmt(i and i['ns_op']):>13} {('%.1f' % iratio) if iratio is not None else '—':>8}  "
              f"{px:>7}  {'agree' if agree else 'DIFFER ' + str(hashes)}")
        if not agree:
            problems.append(f"{name}: the lanes compute different results — {hashes}")
        if ratio is not None and ratio > a.bar:
            problems.append(f"{name}: loft-native is {ratio:.2f}× the reference, over the bar of {a.bar}×")
        if r and not n:
            problems.append(f"{name}: the reference has a row the loft bench does not")
    print()
    if problems:
        print(f"{len(problems)} problem(s):")
        for p in problems:
            print("  FAIL " + p)
        sys.exit(1)
    judged = sum(1 for nm in names if ref.get(nm) and nat.get(nm))
    print(f"ok — {judged} routine(s) judged against the reference, all within {a.bar}×; "
          f"{len(names) - judged} measured without one")


if __name__ == "__main__":
    main()
