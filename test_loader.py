#!/usr/bin/env python3
"""
Quick test script to verify IMG file loading and display basic statistics.
"""

import os
import struct

import numpy as np


def test_load_img(filepath):
    """Test loading and basic statistics of an IMG file."""
    
    if not os.path.exists(filepath):
        print(f"Error: File not found: {filepath}")
        return
    
    print(f"Testing file: {filepath}")
    print(f"File size: {os.path.getsize(filepath) / (1024**3):.2f} GB")
    
    # Load a small sample (100x100 from center)
    full_width = 23040
    full_height = 15360
    
    sample_size = 100
    center_x = full_width // 2
    center_y = full_height // 2
    start_x = center_x - sample_size // 2
    start_y = center_y - sample_size // 2
    
    data = np.zeros((sample_size, sample_size), dtype=np.float32)
    
    print(f"\nLoading {sample_size}x{sample_size} sample from center...")
    
    with open(filepath, 'rb') as f:
        row_bytes = full_width * 4
        
        for i in range(sample_size):
            row_idx = start_y + i
            f.seek(row_idx * row_bytes + start_x * 4)
            row_data = f.read(sample_size * 4)
            # PC_REAL means little-endian (native byte order on PC)
            values = struct.unpack(f'<{sample_size}f', row_data)
            data[i, :] = values
    
    # Convert from kilometers to meters
    data = data * 1000
    
    print(f"\nSample Statistics:")
    print(f"  Shape: {data.shape}")
    print(f"  Min elevation: {data.min():.2f} meters")
    print(f"  Max elevation: {data.max():.2f} meters")
    print(f"  Mean elevation: {data.mean():.2f} meters")
    print(f"  Std deviation: {data.std():.2f} meters")
    
    # Check for any invalid values
    if np.any(np.isnan(data)):
        print(f"  WARNING: Found {np.sum(np.isnan(data))} NaN values")
    if np.any(np.isinf(data)):
        print(f"  WARNING: Found {np.sum(np.isinf(data))} Inf values")
    
    print("\n✓ File appears to be valid!")
    return data


if __name__ == "__main__":
    import sys
    
    filepath = sys.argv[1] if len(sys.argv) > 1 else ".data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG"
    
    test_load_img(filepath)
