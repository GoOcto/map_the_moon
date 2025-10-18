#!/usr/bin/env python3
"""
Demo script to load and visualize a small region of lunar data.
This version saves to an image file instead of opening an interactive viewer.
"""

import os
import struct

import numpy as np
import pyvista as pv

# Set to use off-screen rendering (no display required)
pv.OFF_SCREEN = True


def load_lunar_window(filepath, center_x=11520, center_y=7680, size=1024):
    """Load a window of lunar elevation data."""
    full_width = 23040
    full_height = 15360
    
    half_size = size // 2
    start_x = max(0, center_x - half_size)
    start_y = max(0, center_y - half_size)
    
    data = np.zeros((size, size), dtype=np.float32)
    
    print(f"Loading {size}x{size} window from ({center_x}, {center_y})...")
    
    with open(filepath, 'rb') as f:
        row_bytes = full_width * 4
        
        for i in range(size):
            if (i + 1) % 256 == 0:
                print(f"  Loading row {i+1}/{size}...")
            
            row_idx = start_y + i
            f.seek(row_idx * row_bytes + start_x * 4)
            row_data = f.read(size * 4)
            values = struct.unpack(f'<{size}f', row_data)
            data[i, :] = values
    
    # Convert from km to meters
    data = data * 1000
    
    return data


def create_mesh(elevation_data, scale_z=0.001):
    """Create a 3D mesh from elevation data."""
    height, width = elevation_data.shape
    
    x = np.arange(width)
    y = np.arange(height)
    xx, yy = np.meshgrid(x, y)
    zz = elevation_data * scale_z
    
    mesh = pv.StructuredGrid(xx, yy, zz)
    mesh["elevation"] = elevation_data.ravel(order="F")
    
    return mesh


def main():
    filepath = ".data/dem/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG"
    
    if not os.path.exists(filepath):
        print(f"Error: File not found: {filepath}")
        print("Please run 'python schema.py' first to download the data.")
        return
    
    print(f"Loading lunar data from: {filepath}")
    
    # Load a 1024x1024 window from the center
    elevation_data = load_lunar_window(filepath, size=1024)
    
    print(f"\nData Statistics:")
    print(f"  Shape: {elevation_data.shape}")
    print(f"  Min elevation: {elevation_data.min():.2f} m")
    print(f"  Max elevation: {elevation_data.max():.2f} m")
    print(f"  Range: {elevation_data.max() - elevation_data.min():.2f} m")
    
    # Create mesh
    print("\nCreating 3D mesh...")
    mesh = create_mesh(elevation_data, scale_z=0.001)
    
    # Create plotter and render
    print("Rendering...")
    plotter = pv.Plotter()
    plotter.add_mesh(mesh, 
                     scalars="elevation",
                     cmap='terrain',
                     show_edges=False,
                     smooth_shading=True)
    
    plotter.add_scalar_bar(title="Elevation (m)", 
                          vertical=True)
    
    plotter.camera_position = 'iso'
    
    # Save to file
    output_file = "lunar_surface.png"
    plotter.screenshot(output_file)
    
    print(f"\n✓ Rendered image saved to: {output_file}")
    print(f"  Resolution: 1024x768 pixels")
    
    # Also save a top-down view
    plotter.camera_position = [(512, 512, 2000), (512, 512, 0), (0, 1, 0)]
    plotter.screenshot("lunar_surface_topdown.png")
    print(f"✓ Top-down view saved to: lunar_surface_topdown.png")


if __name__ == "__main__":
    main()
