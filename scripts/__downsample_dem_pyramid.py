#!/usr/bin/env python3
"""Generate lower-resolution DEM tiles by averaging 2x2 pixel blocks.

The script walks the `.data/proc` directory (override with --source-dir)
looking for files named `*_CHUNKED_512.DAT`. Each input file is interpreted as
contiguous float32 samples stored in chunk-major order (row-major across
chunks, then 512x512 pixels per chunk). The data is progressively downsampled by
averaging non-overlapping 2x2 pixel blocks, and the results are written back to
files named `*_CHUNKED_<N>.DAT` with decreasing chunk sizes `N = 256, 128, ...,
2`.

Example usage:
    python scripts/downsample_dem_pyramid.py
    python scripts/downsample_dem_pyramid.py --source-dir ./data/proc --overwrite
"""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Tuple

import numpy as np

try:
    from tqdm import tqdm
except ImportError:  # pragma: no cover - optional progress bar
    tqdm = None  # type: ignore


CHUNKED_PATTERN = re.compile(r"(_CHUNKED_)(\d+)", re.IGNORECASE)
FLOAT32_BYTES = 4

def parse_lat(token: str) -> float:
    if not token:
        raise ValueError("Empty latitude token")
    sign = 1.0
    if token[-1].upper() == "S":
        sign = -1.0
    elif token[-1].upper() != "N":
        raise ValueError(f"Unexpected latitude designator: {token}")
    return float(token[:-1]) * sign


def parse_lon(token: str) -> float:
    return float(token)


def parse_lat_lon_span(name: str) -> Tuple[float, float]:
    parts = name.split("_")
    if len(parts) < 8:
        raise ValueError(f"Cannot parse lat/lon span from {name}")
    lat_a = parse_lat(parts[2])
    lat_b = parse_lat(parts[3])
    lon_a = parse_lon(parts[4])
    lon_b = parse_lon(parts[5])
    lat_span = abs(lat_a - lat_b)
    # Handle wrap-around for the final sector (e.g. 315 -> 360)
    if lon_b < lon_a:
        lon_b += 360.0
    lon_span = lon_b - lon_a
    if lat_span <= 0.0 or lon_span <= 0.0:
        raise ValueError(f"Invalid spans derived from {name}")
    return lat_span, lon_span


def extract_chunk_size(path: Path) -> int:
    match = CHUNKED_PATTERN.search(path.name)
    if not match:
        raise ValueError(f"Filename does not contain _CHUNKED_<size>: {path.name}")
    return int(match.group(2))


def update_chunk_size_in_name(path: Path, new_size: int) -> Path:
    def replacer(match: re.Match[str]) -> str:
        return f"{match.group(1)}{new_size}"
    new_name = CHUNKED_PATTERN.sub(replacer, path.name, count=1)
    return path.with_name(new_name)


def infer_chunk_grid(total_values: int, chunk_size: int, lat_span: float, lon_span: float) -> Tuple[int, int]:
    samples_per_chunk = chunk_size * chunk_size
    if total_values % samples_per_chunk != 0:
        raise ValueError("File size is not an integer multiple of chunk samples")
    total_chunks = total_values // samples_per_chunk
    ratio = lon_span / lat_span
    chunk_count_x = int(round(math.sqrt(total_chunks * ratio)))
    if chunk_count_x <= 0:
        raise ValueError("Derived chunk grid width is non-positive")
    if total_chunks % chunk_count_x != 0:
        raise ValueError("Total chunks not divisible by inferred width")
    chunk_count_y = total_chunks // chunk_count_x
    if chunk_count_y <= 0:
        raise ValueError("Derived chunk grid height is non-positive")
    if chunk_count_x * chunk_count_y != total_chunks:
        raise ValueError("Chunk grid dimensions do not multiply to total chunks")
    return chunk_count_y, chunk_count_x


def chunked_to_full(path: Path, chunk_size: int, chunks_y: int, chunks_x: int) -> np.ndarray:
    shape = (chunks_y, chunks_x, chunk_size, chunk_size)
    mm = np.memmap(path, dtype="<f4", mode="r", shape=shape)
    try:
        full = np.array(mm.transpose(0, 2, 1, 3).reshape(chunks_y * chunk_size, chunks_x * chunk_size), copy=True)
    finally:
        del mm
    return full


def full_to_chunked(grid: np.ndarray, chunk_size: int, chunks_y: int, chunks_x: int) -> np.ndarray:
    reshaped = grid.reshape(chunks_y, chunk_size, chunks_x, chunk_size).transpose(0, 2, 1, 3)
    return np.ascontiguousarray(reshaped.reshape(-1))


def downsample_by_two(grid: np.ndarray) -> np.ndarray:
    return 0.25 * (
        grid[0::2, 0::2] +
        grid[1::2, 0::2] +
        grid[0::2, 1::2] +
        grid[1::2, 1::2]
    )


def process_file(path: Path, overwrite: bool, min_chunk_size: int) -> None:
    base_chunk = extract_chunk_size(path)
    if base_chunk <= min_chunk_size:
        return

    lat_span, lon_span = parse_lat_lon_span(path.name)
    total_values = path.stat().st_size // FLOAT32_BYTES
    chunks_y, chunks_x = infer_chunk_grid(total_values, base_chunk, lat_span, lon_span)

    print(f"Processing {path.name}: chunk {base_chunk} -> down to {min_chunk_size}")
    current_grid = chunked_to_full(path, base_chunk, chunks_y, chunks_x)
    current_chunk = base_chunk

    while current_chunk > min_chunk_size:
        if current_chunk % 2 != 0:
            raise ValueError(f"Chunk size {current_chunk} is not divisible by 2")
        next_chunk = current_chunk // 2
        current_grid = downsample_by_two(current_grid)
        output_path = update_chunk_size_in_name(path, next_chunk)
        if output_path.exists() and not overwrite:
            print(f"  Skipping existing {output_path.name}")
        else:
            chunked = full_to_chunked(current_grid, next_chunk, chunks_y, chunks_x)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            chunked.astype("<f4").tofile(output_path)
            print(f"  Wrote {output_path.name} ({current_grid.shape[1]}x{current_grid.shape[0]})")
        current_chunk = next_chunk

    del current_grid


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate lower resolution DEM tiles via 2x2 averaging")
    parser.add_argument("--source-dir", type=Path, default=Path(".data/proc"), help="Directory containing *_CHUNKED_512.DAT files")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing downsampled files")
    parser.add_argument("--min-chunk", type=int, default=2, help="Smallest chunk size to emit (default: 2)")
    args = parser.parse_args()

    source_dir = args.source_dir
    if not source_dir.exists():
        raise SystemExit(f"Source directory does not exist: {source_dir}")

    files = sorted(source_dir.glob("*_CHUNKED_512.DAT"))
    if not files:
        raise SystemExit("No *_CHUNKED_512.DAT files found in source directory.")

    iterator = files
    if tqdm is not None:
        iterator = tqdm(files, desc="Tiles", unit="file")  # type: ignore

    for file_path in iterator:
        process_file(file_path, overwrite=args.overwrite, min_chunk_size=args.min_chunk)


if __name__ == "__main__":
    main()
