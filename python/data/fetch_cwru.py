"""Download the Case Western Reserve University (CWRU) bearing dataset subset we use.

12 kHz drive-end accelerometer, 4 motor loads (0-3 hp), healthy baseline plus
inner-race (IR), ball (B) and outer-race @6 o'clock (OR) faults at three
fault diameters (0.007", 0.014", 0.021").

Source: https://engineering.case.edu/bearingdatacenter/download-data-file
Files are fetched into python/data/raw/ (gitignored).
"""
from __future__ import annotations

import sys
import urllib.request
from pathlib import Path

URL = "https://engineering.case.edu/sites/default/files/{n}.mat"

# file ids are listed per motor load 0,1,2,3 hp
FILES: dict[str, dict[str, list[int]]] = {
    "normal": {"-": [97, 98, 99, 100]},
    "inner": {"007": [105, 106, 107, 108], "014": [169, 170, 171, 172], "021": [209, 210, 211, 212]},
    "ball": {"007": [118, 119, 120, 121], "014": [185, 186, 187, 188], "021": [222, 223, 224, 225]},
    "outer": {"007": [130, 131, 132, 133], "014": [197, 198, 199, 200], "021": [234, 235, 236, 237]},
}


def manifest() -> list[dict]:
    rows = []
    for cls, sizes in FILES.items():
        for size, ids in sizes.items():
            for load, fid in enumerate(ids):
                rows.append({"file": fid, "cls": cls, "size": size, "load_hp": load})
    return rows


def main(dest: Path) -> int:
    dest.mkdir(parents=True, exist_ok=True)
    bad = 0
    for row in manifest():
        out = dest / f"{row['file']}.mat"
        if out.exists() and out.stat().st_size > 100_000:
            continue
        print(f"fetch {row['file']}.mat ({row['cls']} {row['size']} {row['load_hp']}hp)", flush=True)
        try:
            urllib.request.urlretrieve(URL.format(n=row["file"]), out)
        except Exception as e:  # noqa: BLE001
            print("  FAILED:", e, file=sys.stderr)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(Path(__file__).parent / "raw"))
