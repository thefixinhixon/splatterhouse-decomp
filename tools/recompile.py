#!/usr/bin/env python3
"""
Codegen for Splatterhouse (Xbox 360) with the ReXGlue CLI.

Usage:
  python tools/recompile.py                                 # codegen with the manifest
  python tools/recompile.py --xex game/default.dec.xex      # checks that the XEX exists
  python tools/recompile.py --force                         # ignores validation errors
  python tools/recompile.py --cli C:\\path\\to\\rexglue.exe  # manual binary

The ReXGlue CLI is searched in this order:
  1. --cli / %REXGLUE_BIN%
  2. rexglue on PATH
  3. Local SDK build: build/rexglue-sdk/bin/rexglue(.exe), SDK installed with `--target install`

Documentation: https://github.com/rexglue/rexglue-sdk/wiki
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "splatterhouse_manifest.toml"


def find_rexglue(cli: str | None) -> Path | None:
    if cli:
        p = Path(cli)
        if not p.is_file():
            sys.exit(f"[error] CLI not found: {p}")
        return p
    env = shutil.os.environ.get("REXGLUE_BIN")
    if env and Path(env).is_file():
        return Path(env)
    for name in ("rexglue", "rexglue.exe"):
        if (which := shutil.which(name)):
            return Path(which)
    for base in (ROOT / "extern" / "rexglue-sdk" / "out", ROOT / "build",
                 ROOT / "out" / "build"):
        for p in base.rglob("rexglue.exe"):
            if p.is_file():
                return p
        for p in base.rglob("rexglue"):
            if p.is_file():
                return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Splatterhouse codegen with ReXGlue")
    ap.add_argument("--xex", type=Path, default=(ROOT / "game" / "default.dec.xex"),
                    help="Decrypted XEX (validation; the manifest points to game/default.dec.xex)")
    ap.add_argument("--cli", type=str, help="Path to the rexglue binary")
    ap.add_argument("--force", action="store_true",
                    help="Generate output even if there are validation errors (UnresolvedCall etc.)")
    ap.add_argument("--dry-run", action="store_true", help="Only validate config and XEX")
    args = ap.parse_args()

    if not MANIFEST.exists():
        sys.exit(f"[error] {MANIFEST} does not exist")
    if not args.xex.exists():
        print(f"[error] XEX not found: {args.xex}", file=sys.stderr)
        print("  Extract the ISO with exiso/xiso and decrypt:", file=sys.stderr)
        print("  xextool -r -c default.xex -> game/default.dec.xex", file=sys.stderr)
        return 1
    print(f"[recompile] Manifest: {MANIFEST}")
    print(f"[recompile] XEX:      {args.xex}")

    if args.dry_run:
        print("[recompile] --dry-run mode: codegen is not run.")
        return 0

    rexglue = find_rexglue(args.cli)
    if not rexglue:
        print("[error] ReXGlue CLI not found.", file=sys.stderr)
        print("  1. Download the release: https://github.com/rexglue/rexglue-sdk/releases", file=sys.stderr)
        print("  2. Or build the SDK and run: cmake --build out/build/win-amd64 --target install", file=sys.stderr)
        print("  3. You can also set REXGLUE_BIN or use --cli", file=sys.stderr)
        return 1
    print(f"[recompile] rexglue:   {rexglue}")

    cmd = [str(rexglue), "codegen", str(MANIFEST)]
    if args.force:
        cmd.append("--force")
    print(f"[recompile] {' '.join(cmd)} ")
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        print("[error] codegen failed (use --force to generate anyway and "
              "resolve the errors incrementally)", file=sys.stderr)
        return result.returncode
    print("[recompile] Generated in generated/. Build: cmake --build out/build/win-amd64 -j")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
