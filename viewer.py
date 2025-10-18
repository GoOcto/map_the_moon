#!/usr/bin/env python3
"""
3D Lunar Surface Viewer
Loads .IMG files containing lunar elevation data and displays them as a 3D mesh.
"""

import os
import struct
import sys

import numpy as np
import pyvista as pv


class LunarDataLoader:
    """Loads and processes lunar elevation data from .IMG files."""
    
    def __init__(self, filepath):
        self.filepath = filepath
        # Based on the LBL files: 15360 lines x 23040 samples, 32-bit floats
        self.full_width = 23040
        self.full_height = 15360
        
    def load_window(self, center_x, center_y, window_size=1024):
        """
        Load a window of data from the .IMG file.
        
        Args:
            center_x: X coordinate of window center (0 to full_width)
            center_y: Y coordinate of window center (0 to full_height)
            window_size: Size of the square window to extract (default 1024)
            
        Returns:
            numpy array of shape (window_size, window_size) with elevation data
        """
        # Calculate window bounds
        half_size = window_size // 2
        start_x = max(0, center_x - half_size)
        start_y = max(0, center_y - half_size)
        end_x = min(self.full_width, center_x + half_size)
        end_y = min(self.full_height, center_y + half_size)
        
        # Adjust if window extends beyond boundaries
        if end_x - start_x < window_size:
            if start_x == 0:
                end_x = min(window_size, self.full_width)
            else:
                start_x = max(0, end_x - window_size)
                
        if end_y - start_y < window_size:
            if start_y == 0:
                end_y = min(window_size, self.full_height)
            else:
                start_y = max(0, end_y - window_size)
        
        actual_width = end_x - start_x
        actual_height = end_y - start_y
        
        print(f"Loading window: x=[{start_x}:{end_x}], y=[{start_y}:{end_y}]")
        print(f"Window size: {actual_width} x {actual_height}")
        
        # Read the data
        data = np.zeros((actual_height, actual_width), dtype=np.float32)
        
        with open(self.filepath, 'rb') as f:
            # Each row is full_width floats (4 bytes each)
            row_bytes = self.full_width * 4
            
            for i in range(actual_height):
                # Seek to the start of the row we want
                row_idx = start_y + i
                f.seek(row_idx * row_bytes + start_x * 4)
                
                # Read the row data
                row_data = f.read(actual_width * 4)
                # PC_REAL format means little-endian floats
                values = struct.unpack(f'<{actual_width}f', row_data)
                data[i, :] = values
        
        # Convert from kilometers to meters
        data = data * 1000
        
        return data
    
    def load_downsampled(self, target_size=1024):
        """
        Load the entire file and downsample to target_size x target_size.
        This is memory intensive but provides a good overview.
        
        Args:
            target_size: Target resolution (default 1024)
            
        Returns:
            numpy array of shape (target_size, target_size) with elevation data
        """
        print(f"Loading full data ({self.full_width}x{self.full_height})...")
        
        # Calculate stride for downsampling
        stride_x = self.full_width // target_size
        stride_y = self.full_height // target_size
        
        data = np.zeros((target_size, target_size), dtype=np.float32)
        
        with open(self.filepath, 'rb') as f:
            row_bytes = self.full_width * 4
            
            for i in range(target_size):
                row_idx = i * stride_y
                f.seek(row_idx * row_bytes)
                
                # Read entire row
                row_data = f.read(self.full_width * 4)
                # PC_REAL format means little-endian floats
                values = np.array(struct.unpack(f'<{self.full_width}f', row_data))
                
                # Downsample by taking every stride_x-th value
                data[i, :] = values[::stride_x][:target_size]
                
                if (i + 1) % 100 == 0:
                    print(f"  Progress: {i+1}/{target_size} rows")
        
        # Convert from kilometers to meters
        data = data * 1000
        
        return data


def create_mesh(elevation_data, scale_z=0.001):
    """
    Create a 3D mesh from elevation data.
    
    Args:
        elevation_data: 2D numpy array with elevation values
        scale_z: Vertical exaggeration factor (default 0.001 to scale appropriately)
        
    Returns:
        PyVista StructuredGrid mesh
    """
    height, width = elevation_data.shape
    
    # Create coordinate grids
    x = np.arange(width)
    y = np.arange(height)
    xx, yy = np.meshgrid(x, y)
    
    # Apply the elevation data as Z coordinates with scaling
    zz = elevation_data * scale_z
    
    # Create the mesh
    mesh = pv.StructuredGrid(xx, yy, zz)
    
    # Add elevation as scalar data for coloring
    mesh["elevation"] = elevation_data.ravel(order="F")
    
    return mesh


def view_lunar_surface(filepath, mode='downsample', center_x=11520, center_y=7680, 
                       window_size=1024, scale_z=0.001):
    """
    Main viewer function.
    
    Args:
        filepath: Path to the .IMG file
        mode: 'downsample' (full file downsampled) or 'window' (extract region)
        center_x: X coordinate for window mode (default center)
        center_y: Y coordinate for window mode (default center)
        window_size: Size of window for window mode (default 1024)
        scale_z: Vertical exaggeration factor
    """
    if not os.path.exists(filepath):
        print(f"Error: File not found: {filepath}")
        return
    
    print(f"Loading lunar data from: {filepath}")
    loader = LunarDataLoader(filepath)
    
    # Load the data based on mode
    if mode == 'window':
        print(f"Loading window centered at ({center_x}, {center_y})")
        elevation_data = loader.load_window(center_x, center_y, window_size)
    else:  # downsample
        elevation_data = loader.load_downsampled(window_size)
    
    print(f"Data shape: {elevation_data.shape}")
    print(f"Elevation range: {elevation_data.min():.2f} to {elevation_data.max():.2f} meters")
    
    # Create the 3D mesh
    print("Creating 3D mesh...")
    mesh = create_mesh(elevation_data, scale_z)
    
    # Set up the plotter with enhanced controls
    print("Initializing viewer...")
    plotter = pv.Plotter()
    plotter.add_mesh(mesh, 
                     scalars="elevation",
                     cmap='terrain',  # Use terrain colormap for realistic look
                     show_edges=False,
                     smooth_shading=True)
    
    # Add a scalar bar to show elevation scale
    plotter.add_scalar_bar(title="Elevation (m)", 
                          vertical=True,
                          position_x=0.85,
                          position_y=0.1)
    
    # Set up camera and lighting
    plotter.camera_position = 'iso'
    plotter.add_light(pv.Light(position=(1, 1, 1), intensity=0.5))
    
    # Add text with info
    plotter.add_text(f"Lunar Surface Viewer\n{os.path.basename(filepath)}\n"
                    f"Resolution: {elevation_data.shape[0]}x{elevation_data.shape[1]}\n"
                    f"Mode: {mode}",
                    position='upper_left',
                    font_size=10)
    
    # Show the interactive viewer
    print("\n=== Viewer Controls ===")
    print("Mouse: Left-click and drag to rotate")
    print("       Right-click and drag to pan")
    print("       Scroll to zoom")
    print("Keyboard: 'q' to quit")
    print("          'r' to reset camera")
    print("          's' to take screenshot")
    print("======================\n")
    
    plotter.show()


def main():
    """Main entry point with command-line argument handling."""
    
    # Default file path
    default_file = ".data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG"
    
    if len(sys.argv) > 1:
        filepath = sys.argv[1]
    else:
        filepath = default_file
        print(f"Using default file: {filepath}")
        print("Usage: python viewer.py <path_to_IMG_file> [mode] [center_x] [center_y]")
        print("  mode: 'downsample' (default) or 'window'")
        print("  center_x, center_y: coordinates for window mode\n")
    
    mode = sys.argv[2] if len(sys.argv) > 2 else 'downsample'
    center_x = int(sys.argv[3]) if len(sys.argv) > 3 else 11520
    center_y = int(sys.argv[4]) if len(sys.argv) > 4 else 7680
    
    view_lunar_surface(filepath, mode=mode, center_x=center_x, center_y=center_y)


if __name__ == "__main__":
    main()
