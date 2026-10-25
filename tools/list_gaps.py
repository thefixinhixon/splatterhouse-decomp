"""
Lists the gaps in the project's function table (guest addresses without a
function registered in PPCFuncMappings).

Usage:
  python tools/list_gaps.py                       # all gaps in the module
  python tools/list_gaps.py 0x82F2D000 0x82F75818 # only within a range
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INIT_CPP = ROOT / "generated" / "splatterhouse_init.cpp"
MAPPING_RE = re.compile(r"\{\s*0x([0-9A-Fa-f]{8}),")

args = [a for a in sys.argv[1:]]
lo = int(args[0], 16) if len(args) > 0 else None
hi = int(args[1], 16) if len(args) > 1 else None

addrs = [int(m.group(1), 16) for m in MAPPING_RE.finditer(INIT_CPP.read_text(encoding="utf-8", errors="ignore"))]
addrs = sorted(set(a for a in addrs if 0x80000000 <= a < 0xC0000000))
print(f"{len(addrs)} registered functions")

code_entries = []
# code base/size from the pch
pch = (ROOT / "generated" / "splatterhouse_pch.h").read_text(encoding="utf-8", errors="ignore")
base = int(re.search(r"REX_CODE_BASE 0x([0-9A-Fa-f]+)", pch).group(1), 16)
cbeg, cend = base, base + int(re.search(r"REX_CODE_SIZE 0x([0-9A-Fa-f]+)", pch).group(1), 16)
print(f"code region: {cbeg:#x} - {cend:#x}\n")

prev = None
gaps = []
for a in addrs + [cend]:
    if lo is not None and (a < lo or (prev is not None and prev < lo)):
        prev = a
        continue
    if a > cend and prev is not None:
        break
    if prev is not None and a != prev:
        # gap between prev and a
        g_start, g_end = prev, a
        if lo is not None and g_end <= lo:
            prev = a
            continue
        gaps.append((max(g_start, cbeg), min(g_end, cend)))
    prev = a

total_gap = sum(e - s for s, e in gaps if e > s)
print(f"{len(gaps)} distinct gaps\n")
if lo is not None and hi is not None:
    show = [g for g in gaps if g[1] > lo and g[0] < hi]
else:
    show = [g for g in gaps if g[1] > g[0]]
for s, e in show[:80]:
    print(f"  {s:#010x} - {e:#010x}  ({e-s} bytes)")

if len(show) > 80:
    print(f"... and {len(show)-80} more")
