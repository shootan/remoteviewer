"""motion-settle r2: one compact table per cell -- configuration columns side by side.

    python motion_settle_compare.py <m0runs dir> <cell> <prefix> [<prefix> ...]

Rows per scene: reached/valid against the shared reference (rest censored), glyph PSNR median at 0.5 / 1 s,
T_final median, onset first-reaction p95 and first-second gap p95 / max (ms).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from motion_settle_summary import load, stats  # noqa: E402


def main(root, cell, prefixes):
    data = {p: load(root, p).get(cell, []) for p in prefixes}
    scenes = sorted({r["scene"] for rows in data.values() for r in rows})
    w = 14
    print(f"cell {cell}: " + " | ".join(prefixes))
    for metric in ("reach", "ink500", "ink1000", "tfinal", "react95", "gap95", "gapmax"):
        print(f"\n[{metric}]".ljust(13) + "".join(p.rjust(w) for p in prefixes))
        for sc in scenes:
            line = sc.ljust(12)
            for p in prefixes:
                rows = [r for r in data[p] if r["scene"] == sc]
                if not rows:
                    line += "-".rjust(w)
                    continue
                s = stats(rows, 9000)
                v = {"reach": f"{s['reached']}/{s['valid']}", "ink500": f"{s['ink500']:.1f}", "ink1000": f"{s['ink1000']:.1f}",
                     "tfinal": f"{s['tfinal'][0]:.0f}", "react95": f"{s['react95']:.0f}", "gap95": f"{s['gap95']:.0f}",
                     "gapmax": f"{s['gapmax']:.0f}"}[metric]
                line += v.rjust(w)
            print(line)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
