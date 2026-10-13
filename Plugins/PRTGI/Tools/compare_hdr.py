"""Compare aligned, linear RGB EXR captures without exposure fitting.

Requires numpy and OpenEXR. Cameras, resolution, light contributions, exposure,
and bounce limits must match; this tool cannot establish those conditions.
ROI coordinates are x0 y0 x1 y1 in pixels, with exclusive upper bounds.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import OpenEXR


def read_rgb(path):
    with OpenEXR.File(str(path)) as image:
        channels = image.channels()
        if "RGB" in channels:
            rgb = channels["RGB"].pixels
        elif "RGBA" in channels:
            rgb = channels["RGBA"].pixels[:, :, :3]
        else:
            rgb = np.stack([channels[c].pixels for c in "RGB"], axis=-1)
        result = np.array(rgb, dtype=np.float64)
    if result.ndim != 3 or result.shape[-1] != 3 or not np.isfinite(result).all():
        raise ValueError(f"Invalid/nonfinite RGB data: {path}")
    return result


def metrics(reference, candidate):
    delta = candidate - reference
    energy = np.mean(reference ** 2)
    luminance = np.array([0.2126, 0.7152, 0.0722])
    ref_luma, cand_luma = reference @ luminance, candidate @ luminance
    return {
        "pixels": int(reference.size // 3),
        "rgb_rmse": float(np.sqrt(np.mean(delta ** 2))),
        "rgb_relative_rmse": float(np.sqrt(np.mean(delta ** 2) / max(energy, 1e-20))),
        "rgb_mae": float(np.mean(np.abs(delta))),
        "reference_mean_luminance": float(np.mean(ref_luma)),
        "candidate_mean_luminance": float(np.mean(cand_luma)),
        "relative_luminance_bias": float(np.mean(cand_luma - ref_luma) / max(np.mean(ref_luma), 1e-20)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--roi", type=int, nargs=4)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    ref, cand = read_rgb(args.reference), read_rgb(args.candidate)
    if ref.shape != cand.shape:
        parser.error(f"Capture dimensions differ: {ref.shape} / {cand.shape}")
    if args.roi:
        x0, y0, x1, y1 = args.roi
        if not (0 <= x0 < x1 <= ref.shape[1] and 0 <= y0 < y1 <= ref.shape[0]):
            parser.error("ROI lies outside image or is empty")
        ref, cand = ref[y0:y1, x0:x1], cand[y0:y1, x0:x1]
    report = {"reference": str(args.reference), "candidate": str(args.candidate),
              "roi": args.roi, "exposure_fitting": False, **metrics(ref, cand)}
    payload = json.dumps(report, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n", encoding="utf-8")
    print(payload)


if __name__ == "__main__":
    main()
