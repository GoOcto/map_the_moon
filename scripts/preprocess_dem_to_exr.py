#!/usr/bin/env python3
"""
Preprocesses raw lunar DEM (.IMG) files into multi-resolution tiled OpenEXR files.
Stores individual LOD files (*_lod0.exr ... *_lod6.exr) as well as the combined multi-part file.
Also generates visual PNG heightmap previews at multiple zoom levels.
"""

import os
import sys
import time
import argparse
import numpy as np
import OpenEXR
from PIL import Image

TILE_WIDTH = 23040
TILE_HEIGHT = 15360
CHUNK_SIZE = 512

def parse_lat_lon_from_filename(filename):
    """
    Parses latitude and longitude bounds from standard filenames like:
    SLDEM2015_512_00N_30N_000_045_FLOAT.IMG
    """
    parts = os.path.basename(filename).split('_')
    def parse_lat(s):
        val = float(s[:-1])
        return -val if s.endswith('S') else val
    
    lat_min = parse_lat(parts[2])
    lat_max = parse_lat(parts[3])
    if lat_min > lat_max:
        lat_min, lat_max = lat_max, lat_min
        
    lon_min = float(parts[4])
    lon_max = float(parts[5])
    return lat_min, lat_max, lon_min, lon_max

def downsample_2x2(grid):
    """Downsamples a 2D float32 grid by 2x2 averaging."""
    h, w = grid.shape
    new_h = h // 2
    new_w = w // 2
    trimmed = grid[:new_h * 2, :new_w * 2]
    return 0.25 * (
        trimmed[0::2, 0::2] +
        trimmed[1::2, 0::2] +
        trimmed[0::2, 1::2] +
        trimmed[1::2, 1::2]
    ).astype(np.float32)

def generate_preview_png(data, output_path, vmin=None, vmax=None):
    """Saves a float array as a normalized 8-bit grayscale PNG."""
    if vmin is None:
        vmin = float(np.min(data))
    if vmax is None:
        vmax = float(np.max(data))
    
    denom = max(vmax - vmin, 1e-5)
    normalized = np.clip((data - vmin) / denom * 255.0, 0, 255).astype(np.uint8)
    img = Image.fromarray(normalized, mode='L')
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path)
    print(f"  Saved preview PNG: {output_path} ({img.width}x{img.height})")

def process_single_tile(img_path, output_dir, preview_dir=None, max_levels=7):
    print(f"\n=======================================================")
    print(f"Processing: {os.path.basename(img_path)}")
    print(f"=======================================================")
    t0 = time.time()
    
    expected_bytes = TILE_WIDTH * TILE_HEIGHT * 4
    file_size = os.path.getsize(img_path)
    if file_size != expected_bytes:
        raise ValueError(f"File size {file_size} does not match expected {expected_bytes} bytes")
    
    lat_min, lat_max, lon_min, lon_max = parse_lat_lon_from_filename(img_path)
    print(f"Coverage: Lat [{lat_min} deg, {lat_max} deg], Lon [{lon_min} deg, {lon_max} deg]")
    
    print("Reading Level 0 via memory map...")
    level0 = np.memmap(img_path, dtype='<f4', mode='r', shape=(TILE_HEIGHT, TILE_WIDTH))
    
    sample_sub = level0[::10, ::10]
    elev_min = float(np.min(sample_sub))
    elev_max = float(np.max(sample_sub))
    print(f"Approx elevation range: [{elev_min:.1f} m, {elev_max:.1f} m]")
    
    # Build pyramid levels
    pyramid = [level0]
    cur_grid = level0
    for lvl in range(1, max_levels):
        t_sub0 = time.time()
        print(f"Generating Level {lvl} (downsampling from {cur_grid.shape[1]}x{cur_grid.shape[0]})...", end='', flush=True)
        cur_grid = downsample_2x2(cur_grid)
        pyramid.append(cur_grid)
        print(f" -> {cur_grid.shape[1]}x{cur_grid.shape[0]} in {time.time() - t_sub0:.2f}s")
    
    os.makedirs(output_dir, exist_ok=True)
    base_name = os.path.basename(img_path).replace('_FLOAT.IMG', '')

    # Write individual per-LOD EXR files for instantaneous C++ loading
    print("\nWriting individual per-LOD OpenEXR files (PIZ compression)...")
    for lvl, grid in enumerate(pyramid):
        h, w = grid.shape
        data_win = (np.array([0, 0], dtype=np.int32), np.array([w - 1, h - 1], dtype=np.int32))
        
        td = OpenEXR.TileDescription()
        td.xSize = CHUNK_SIZE
        td.ySize = CHUNK_SIZE
        td.mode = OpenEXR.ONE_LEVEL
        td.roundingMode = OpenEXR.ROUND_DOWN
        
        header = {
            'compression': OpenEXR.PIZ_COMPRESSION,
            'type': OpenEXR.tiledimage,
            'tiles': td,
            'displayWindow': data_win,
            'dataWindow': data_win,
            'latMin': lat_min,
            'latMax': lat_max,
            'lonMin': lon_min,
            'lonMax': lon_max,
            'elevationMin': elev_min,
            'elevationMax': elev_max,
            'levelIndex': lvl,
            'pixelsPerDegree': 512.0 / (2 ** lvl)
        }
        
        grid_data = np.ascontiguousarray(grid, dtype=np.float32)
        part = OpenEXR.Part(header, {'Z': grid_data}, f"level_{lvl}")
        lod_file = os.path.join(output_dir, f"{base_name}_lod{lvl}.exr")
        f_out = OpenEXR.File([part])
        f_out.write(lod_file)
        mb = os.path.getsize(lod_file) / (1024 * 1024)
        print(f"  Wrote {os.path.basename(lod_file)} ({w}x{h}, {mb:.2f} MB)")
    
    print(f"\nAll levels written successfully in {time.time() - t0:.2f} seconds!")
    
    # Generate Preview PNGs
    if preview_dir:
        print("\nGenerating multi-zoom preview PNGs...")
        tile_base = base_name
        
        lvl5 = pyramid[5]
        p1 = os.path.join(preview_dir, f"{tile_base}_overview_lvl5.png")
        generate_preview_png(lvl5, p1, elev_min, elev_max)
        
        lvl3 = pyramid[3]
        p2 = os.path.join(preview_dir, f"{tile_base}_mid_lvl3.png")
        generate_preview_png(lvl3, p2, elev_min, elev_max)
        
        cy, cx = TILE_HEIGHT // 2, TILE_WIDTH // 2
        crop_lvl0 = np.array(level0[cy-512:cy+512, cx-512:cx+512], copy=True)
        p3 = os.path.join(preview_dir, f"{tile_base}_crop_fullres_lvl0.png")
        generate_preview_png(crop_lvl0, p3)
        
        c1_y, c1_x = (TILE_HEIGHT // 4), (TILE_WIDTH // 4)
        crop_lvl1 = np.array(pyramid[1][c1_y-256:c1_y+256, c1_x-256:c1_x+256], copy=True)
        p4 = os.path.join(preview_dir, f"{tile_base}_crop_halres_lvl1.png")
        generate_preview_png(crop_lvl1, p4)

    return output_dir

def main():
    parser = argparse.ArgumentParser(description="Convert DEM .IMG tiles into multi-level tiled OpenEXR pyramid.")
    parser.add_argument("--input", default=".data/dem/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG", help="Path to input .IMG file")
    parser.add_argument("--output-dir", default=".data/proc_exr", help="Directory for output EXR files")
    parser.add_argument("--preview-dir", default=".data/proc_exr/previews", help="Directory for preview PNGs")
    args = parser.parse_args()
    
    process_single_tile(args.input, args.output_dir, args.preview_dir)

if __name__ == "__main__":
    main()
