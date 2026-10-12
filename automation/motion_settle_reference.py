"""motion-settle r2 D: the shared long-still reference from calibration runs.

    python motion_settle_reference.py <out.csv> <calibration run dir> [<calibration run dir> ...]

Each run dir holds reference.csv from `--calibrate` (one long still per scene, last-5 s convergence).
Per scene: only CONVERGED rows count; the shared reference is the HIGHEST of them, so a blurrier
configuration can never set the bar it is then judged against. A scene with no converged row gets no
reference (it is reported as "reference not established", not guessed). Prints the table it writes.
"""

import csv
import os
import sys


def main(out, dirs):
    per = {}
    for d in dirs:
        for r in csv.DictReader(open(os.path.join(d, "reference.csv"), encoding="utf-8")):
            per.setdefault(r["scene"], []).append(r)
    with open(out, "w", newline="", encoding="utf-8") as f:
        for scene, rows in per.items():
            conv = [r for r in rows if r["converged"] == "1" and float(r["refInk"]) > 0]
            desc = "; ".join(f"{r['label']}:{float(r['refInk']):.2f}{'' if r['converged'] == '1' else '(not converged)'}"
                             for r in rows)
            if conv:
                best = max(conv, key=lambda r: float(r["refInk"]))
                f.write(f"{scene},{float(best['refInk']):.3f}\n")
                print(f"{scene:<11} reference {float(best['refInk']):.2f} dB from {best['label']}  [{desc}]")
            else:
                print(f"{scene:<11} reference NOT ESTABLISHED  [{desc}]")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
