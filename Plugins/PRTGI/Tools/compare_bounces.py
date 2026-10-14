"""Compare aligned 4/8/12-bounce linear EXRs against one matched reference.
Requires numpy and OpenEXR (see compare_hdr.py). No exposure fitting.
Example: python compare_bounces.py ref.exr b4.exr b8.exr b12.exr --roi box 230 150 708 570 --output report.json
"""
import argparse
import json
from pathlib import Path
from compare_hdr import read_rgb, metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("four", type=Path)
    parser.add_argument("eight", type=Path)
    parser.add_argument("twelve", type=Path)
    parser.add_argument("--roi", nargs=5, action="append", metavar=("NAME", "X0", "Y0", "X1", "Y1"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reference = read_rgb(args.reference)
    regions = {"full_frame": (0, 0, reference.shape[1], reference.shape[0])}
    for name, *bounds in args.roi or []:
        try:
            x0, y0, x1, y1 = map(int, bounds)
        except ValueError:
            parser.error("ROI coordinates must be integers")
        if name in regions or not (0 <= x0 < x1 <= reference.shape[1] and 0 <= y0 < y1 <= reference.shape[0]):
            parser.error("Duplicate ROI name, empty ROI, or coordinates outside reference")
        regions[name] = (x0, y0, x1, y1)
    report = {"reference": str(args.reference), "exposure_fitting": False, "roi_pixels": regions, "bounces": {}}
    for order, path in [(4, args.four), (8, args.eight), (12, args.twelve)]:
        candidate = read_rgb(path)
        if candidate.shape != reference.shape:
            parser.error(f"Capture dimensions differ for {path}")
        report["bounces"][str(order)] = {"file": str(path), "metrics": {
            name: metrics(reference[y0:y1, x0:x1], candidate[y0:y1, x0:x1])
            for name, (x0, y0, x1, y1) in regions.items()}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote 4/8/12 comparison to {args.output}")


if __name__ == "__main__":
    main()
