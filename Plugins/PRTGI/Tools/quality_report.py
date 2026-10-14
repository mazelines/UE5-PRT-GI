"""Read PRTGI v3-v7 transport quality counters without loading Unreal.

Example:
  python quality_report.py --output baseline.json Content/Precomputed/Volumes

Missing-hit fractions count secondary intersections, not lost lighting energy.
v3 caches do not contain surface-binding or per-bounce missing-hit counters.
"""

import argparse
import json
import mmap
import struct
import zlib
from pathlib import Path


def read_cache(path):
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        offset = 0

        def read(fmt):
            nonlocal offset
            fmt = "<" + fmt
            size = struct.calcsize(fmt)
            if offset + size > len(data) - 4:
                raise ValueError("Truncated cache")
            values = struct.unpack_from(fmt, data, offset)
            offset += size
            return values

        def skip(size):
            nonlocal offset
            if size < 0 or offset + size > len(data) - 4:
                raise ValueError("Invalid array size")
            offset += size

        if len(data) < 64:
            raise ValueError("Not a transport cache (possibly a Git LFS pointer)")
        magic, version = read("Ii")
        if magic != 0x34424D50 or version not in (3, 4, 5, 6, 7):
            raise ValueError(f"Unsupported transport header: {magic:x}, v{version}")
        view = memoryview(data)
        try:
            crc = zlib.crc32(view[:-4]) & 0xFFFFFFFF
        finally:
            view.release()
        if crc != struct.unpack_from("<I", data, len(data) - 4)[0]:
            raise ValueError("Payload CRC mismatch")
        signature, cell_size, probes, base_crc = read("QfiI")
        max_bounces, min_throughput = read("if") if version >= 7 else (4, 0)
        counts = []
        for _ in range(3):
            count, = read("i")
            skip(count * 16)
            counts.append(count)
        terms, = read("i")
        skip(terms * 64 + probes * 12)
        paths = read("12i" if version >= 7 else "4i")
        missing, hits = read("2i")
        seconds, quantization = read("2d")
        corrected, displacement = read("if")
        bound = unbound = missing_per_bounce = None
        if version >= 4:
            skip(counts[0] // 3 * 16)
            bound, unbound = read("2i")
            missing_per_bounce = read("11i" if version >= 7 else "3i")
        material_hits = read("i")[0] if version >= 5 else None
        terminated = read("12i") if version >= 7 else None
        if offset != len(data) - 4:
            raise ValueError("Unexpected trailing payload")
        return {
            "path": str(path), "cache_version": version,
            "max_bounces": max_bounces, "min_throughput": min_throughput,
            "terminated_low_throughput": terminated,
            "geometry_signature": str(signature), "base_crc": base_crc,
            "cell_size_cm": cell_size, "probes": probes,
            "sources": counts[0] // 3, "triangles": counts[1] // 3,
            "terms": terms, "paths": paths, "surface_hits": hits,
            "unmapped_hits": missing,
            "unmapped_hit_fraction": missing / hits if hits else 0,
            "bound_samples": bound, "unbound_samples": unbound,
            "unmapped_by_bounce": missing_per_bounce,
            "material_hits": material_hits,
            "corrected_samples": corrected,
            "max_capture_correction_cm": displacement,
            "fp16_relative_error": quantization,
            "build_seconds": seconds, "cache_mib": len(data) / 1048576,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    files = set()
    for path in args.paths:
        if path.is_dir():
            files.update(path.rglob("*.multi"))
        else:
            files.add(path)
    if not files:
        parser.error("No .multi caches found")
    report = {"metric_note": "Missing-hit fractions are not energy-loss fractions.",
              "volumes": [read_cache(path) for path in sorted(files)]}
    payload = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n", encoding="utf-8")
        print(f"Wrote {len(files)} cache reports to {args.output}")
    else:
        print(payload)


if __name__ == "__main__":
    main()
