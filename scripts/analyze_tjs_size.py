#!/usr/bin/env python3
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


def human(n: int) -> str:
    units = ["B", "KiB", "MiB", "GiB"]
    x = float(n)
    for u in units:
        if x < 1024 or u == units[-1]:
            return f"{x:.2f} {u}"
        x /= 1024


def strip_copy(binary: Path, stripped: Path) -> None:
    tool = shutil.which("llvm-strip") or shutil.which("strip")
    if not tool:
        raise RuntimeError("strip tool not found (llvm-strip/strip)")
    subprocess.check_call([tool, "-o", str(stripped), str(binary)])


def run_link_with_map(build_dir: Path, link_txt: Path, out_bin: Path, out_map: Path) -> None:
    cmd_text = link_txt.read_text().strip()
    argv = shlex.split(cmd_text)

    # Replace the linker output after -o.
    for i in range(len(argv) - 1):
        if argv[i] == "-o":
            argv[i + 1] = str(out_bin.name)
            break
    else:
        raise RuntimeError("cannot find -o in link command")

    # Request linker map.
    argv.append(f"-Wl,-map,{out_map.name}")

    subprocess.check_call(argv, cwd=build_dir)


def parse_map_file(map_path: Path):
    lines = map_path.read_text(errors="replace").splitlines()

    idx_to_obj = {}
    in_objects = False
    in_symbols = False
    obj_re = re.compile(r"^\[(\s*\d+)\]\s+(.+)$")
    sym_re = re.compile(r"^0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+)\s+\[(\s*\d+)\]\s+.+$")

    obj_sizes = defaultdict(int)

    for line in lines:
        if line.startswith("# Object files:"):
            in_objects = True
            in_symbols = False
            continue
        if line.startswith("# Symbols:"):
            in_symbols = True
            in_objects = False
            continue
        if line.startswith("# ") and not line.startswith("# Address"):
            if in_symbols:
                in_symbols = False
            continue

        if in_objects:
            m = obj_re.match(line)
            if m:
                idx = int(m.group(1))
                obj = m.group(2).strip()
                idx_to_obj[idx] = obj

        if in_symbols and not line.startswith("#"):
            m = sym_re.match(line)
            if m:
                size = int(m.group(1), 16)
                idx = int(m.group(2))
                obj_sizes[idx] += size

    named_obj_sizes = {}
    for idx, size in obj_sizes.items():
        named_obj_sizes[idx_to_obj.get(idx, f"[idx:{idx}]")] = size

    return named_obj_sizes


def group_name(obj_name: str) -> str:
    if obj_name == "linker synthesized":
        return "linker"

    # archive(member)
    if "(" in obj_name and obj_name.endswith(")"):
        archive = obj_name.split("(", 1)[0]
        if archive == "libtjs_core.a":
            return "tjs-core(src/*)"
        if archive.startswith("deps/"):
            parts = archive.split("/")
            if len(parts) >= 2:
                return f"deps/{parts[1]}"
        return archive

    if obj_name.startswith("CMakeFiles/tjs-cli.dir/"):
        return "tjs-cli"
    if obj_name.startswith("deps/"):
        parts = obj_name.split("/")
        if len(parts) >= 2:
            return f"deps/{parts[1]}"

    return "other"


def print_table(title: str, rows, total: int, topn: int):
    print(f"\n{title}")
    print("-" * len(title))
    print(f"{'item':50} {'bytes':>12} {'percent':>8}")
    for name, size in rows[:topn]:
        pct = (size / total * 100.0) if total else 0.0
        print(f"{name:50} {size:12d} {pct:7.2f}%")


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze build/tjs module/lib size (strip first)")
    ap.add_argument("binary", nargs="?", default="build/tjs", help="binary path (default: build/tjs)")
    ap.add_argument("--top", type=int, default=20, help="top N rows")
    args = ap.parse_args()

    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 1

    build_dir = binary.parent
    link_txt = build_dir / "CMakeFiles/tjs-cli.dir/link.txt"
    if not link_txt.exists():
        print(f"link command not found: {link_txt}", file=sys.stderr)
        return 2

    stripped = binary.with_name(binary.name + ".stripped")
    probe_bin = build_dir / (binary.name + ".sizeprobe")
    probe_map = build_dir / (binary.name + ".sizeprobe.map")

    strip_copy(binary, stripped)
    run_link_with_map(build_dir, link_txt, probe_bin, probe_map)

    obj_sizes = parse_map_file(probe_map)
    if probe_bin.exists():
        probe_bin.unlink()
    total_sym = sum(obj_sizes.values())

    by_group = defaultdict(int)
    for obj, size in obj_sizes.items():
        by_group[group_name(obj)] += size

    group_rows = sorted(by_group.items(), key=lambda kv: kv[1], reverse=True)
    obj_rows = sorted(obj_sizes.items(), key=lambda kv: kv[1], reverse=True)

    orig_size = binary.stat().st_size
    strip_size = stripped.stat().st_size

    print(f"binary: {binary}")
    print(f"original size : {orig_size} ({human(orig_size)})")
    print(f"stripped size : {strip_size} ({human(strip_size)})")
    print(f"strip reduced : {orig_size - strip_size} ({(orig_size - strip_size) / orig_size * 100:.2f}%)")
    print(f"map-attributed symbol bytes: {total_sym} ({human(total_sym)})")

    print_table("Top contributions by module/lib", group_rows, total_sym, args.top)
    print_table("Top contributions by object", obj_rows, total_sym, args.top)

    print(f"\nstripped binary: {stripped}")
    print(f"linker map: {probe_map}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
