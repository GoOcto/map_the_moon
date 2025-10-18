#!/usr/bin/env python3
"""
Simple example showing how to load and visualize a 1024x1024 lunar surface mesh.
This is the most basic usage - just load data and show it.
"""

import struct

import numpy as np
import pyvista as pv


def load_lunar_data(filepath, size=1024):
    """
    Load a 1024x1024 region from the center of a lunar elevation IMG file.
    
    Returns:
        numpy array with elevation data in meters
    """
    # IMG file specifications
    FULL_WIDTH = 23040
    FULL_HEIGHT = 15360
    
    # Extract center region
    center_x = FULL_WIDTH // 2
    center_y = FULL_HEIGHT // 2
    half = size // 2
    start_x = center_x - half
    start_y = center_y - half
    
    # Read the binary data
    data = np.zeros((size, size), dtype=np.float32)
    
    with open(filepath, 'rb') as f:
        for i in range(size):
            # Seek to correct position and read one row
            f.seek((start_y + i) * FULL_WIDTH * 4 + start_x * 4)
            row_bytes = f.read(size * 4)
            # Unpack little-endian floats and convert km to meters
            data[i, :] = np.array(struct.unpack(f'<{size}f', row_bytes)) * 1000
    
    return data


def visualize(elevation_data):
    """Create and show a 3D mesh from elevation data."""
    h, w = elevation_data.shape
    
    # Create coordinate grids
    x, y = np.arange(w), np.arange(h)
    xx, yy = np.meshgrid(x, y)
    
    # Use elevation as Z coordinate (with scaling for better visualization)
    zz = elevation_data * 0.001
    
    # Create PyVista mesh
    mesh = pv.StructuredGrid(xx, yy, zz)
    mesh["elevation"] = elevation_data.ravel(order="F")
    
    # Visualize
    mesh.plot(scalars="elevation", cmap='terrain', show_scalar_bar=True)


# Main execution
if __name__ == "__main__":
    # Load data
    print("Loading lunar elevation data...")
    filepath = ".data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG"
    elevation = load_lunar_data(filepath, size=1024)
    
    print(f"Loaded {elevation.shape[0]}x{elevation.shape[1]} mesh")
    print(f"Elevation range: {elevation.min():.0f}m to {elevation.max():.0f}m")
    
    # Visualize
    print("Opening viewer...")
    visualize(elevation)
