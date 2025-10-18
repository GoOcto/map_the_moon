# Quick Start Guide

## Installation
```bash
pip install numpy pyvista tqdm requests
```

## Basic Usage

### 1. Simple Viewer (easiest)
```bash
python simple_viewer.py
```
Loads 1024×1024 center region and shows interactive 3D view.

### 2. Full-Featured Viewer
```bash
# View entire tile (downsampled)
python viewer.py

# View specific window at coordinates
python viewer.py .data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG window 15000 10000

# View different tile
python viewer.py .data/SLDEM2015_512_30N_60N_000_045_FLOAT.IMG
```

### 3. Generate Images (no display needed)
```bash
python demo.py
```
Creates `lunar_surface.png` and `lunar_surface_topdown.png`

### 4. Test Data Loading
```bash
python test_loader.py [path_to_IMG_file]
```

## Viewer Controls

**Mouse:**
- Left drag: Rotate
- Right drag: Pan
- Scroll: Zoom

**Keyboard:**
- `r`: Reset view
- `q`: Quit
- `s`: Screenshot

## File Structure

```
map_the_moon/
├── viewer.py              # Main interactive viewer
├── simple_viewer.py       # Minimal example (50 lines)
├── demo.py               # Batch rendering to images
├── test_loader.py        # Data validation
├── schema.py             # Data downloader
├── requirements.txt      # Dependencies
├── README.md            # Full documentation
└── .data/               # Downloaded IMG files (1.32GB each)
    └── SLDEM2015_*.IMG
```

## Key Features

✓ Loads 1024×1024 mesh from 1.32 GB files in ~5 seconds
✓ Hardware-accelerated OpenGL rendering via PyVista/VTK
✓ Smooth camera controls (rotate, pan, zoom)
✓ Realistic terrain coloring
✓ Window function extracts any region efficiently
✓ Can downsample full tiles for overview

## Technical Details

- **Data Format**: 32-bit float, little-endian, kilometers
- **Full Resolution**: 23040 × 15360 pixels per tile
- **Output Mesh**: 1024 × 1024 vertices (1,048,576 points)
- **Coverage**: Each tile = 30° latitude × 45° longitude
- **Vertical Scale**: 0.001x for realistic proportions

## Examples

```python
# Load data programmatically
from simple_viewer import load_lunar_data
data = load_lunar_data("path/to/file.IMG", size=1024)

# data is now a 1024x1024 NumPy array with elevations in meters
print(f"Elevation range: {data.min():.0f} to {data.max():.0f} meters")
```

## Troubleshooting

**No display?** → Use `demo.py` instead
**Out of memory?** → Use window mode instead of downsample
**Slow rendering?** → Reduce mesh size or use simpler shading
