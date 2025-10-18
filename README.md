# Moon Elevation Data Viewer

This project loads and visualizes lunar elevation data from NASA's LOLA (Lunar Orbiter Laser Altimeter) instrument.

## Files

- `schema.py` - Downloads lunar elevation data tiles from NASA
- `viewer.py` - 3D visualization of the lunar surface
- `test_loader.py` - Quick test to verify data loading
- `requirements.txt` - Python dependencies

## Installation

1. Install Python dependencies:
```bash
pip install -r requirements.txt
```

Or individually:
```bash
pip install numpy pyvista tqdm requests
```

## Usage

### 1. Download Data (if not already present)

```bash
python schema.py
```

This will download lunar elevation tiles to the `.data/` directory. Each file is ~1.32 GB.

### 2. View Lunar Surface

**Quick start (view downsampled full tile):**
```bash
python viewer.py
```

**Specify a different file:**
```bash
python viewer.py .data/SLDEM2015_512_30N_60N_000_045_FLOAT.IMG
```

**View a specific window/region (faster loading):**
```bash
python viewer.py .data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG window 11520 7680
```

Parameters:
- First argument: path to .IMG file
- Second argument: mode (`downsample` or `window`)
- Third/Fourth arguments: center X and Y coordinates for window mode (default: center of image)

### 3. Viewer Controls

Once the 3D viewer opens:

**Mouse:**
- Left-click + drag: Rotate camera
- Right-click + drag: Pan camera
- Scroll wheel: Zoom in/out

**Keyboard:**
- `q`: Quit viewer
- `r`: Reset camera to default position
- `s`: Save screenshot

## Data Information

- **Source**: NASA LOLA (Lunar Orbiter Laser Altimeter)
- **Resolution**: 512 pixels per degree (~60 meters at equator)
- **Format**: 32-bit floating point, little-endian
- **Full tile size**: 23040 x 15360 pixels (~1.32 GB each)
- **Coverage**: 30° x 45° per tile
- **Values**: Elevation in kilometers (converted to meters in viewer)
- **Reference**: Relative to 1737.4 km radius sphere

## Technical Details

### Data Format
Each `.IMG` file contains:
- 15360 lines (rows)
- 23040 samples per line (columns)
- 32-bit IEEE floating point values (little-endian)
- Values in kilometers relative to reference radius
- Unit: kilometers (height above/below reference sphere)

### Viewer Modes

**Downsample Mode** (default):
- Loads entire tile and downsamples to 1024x1024
- Provides overview of full region
- Takes longer to load (~30 seconds)
- Good for exploring overall terrain

**Window Mode**:
- Extracts 1024x1024 region from specified location
- Much faster loading (~5 seconds)
- Good for detailed examination of specific areas

## Examples

View the north polar region:
```bash
python viewer.py .data/SLDEM2015_512_30N_60N_000_045_FLOAT.IMG
```

View a specific crater (example coordinates):
```bash
python viewer.py .data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG window 15000 10000
```

## Troubleshooting

**Issue**: "File not found" error
- **Solution**: Run `python schema.py` first to download the data

**Issue**: Viewer window doesn't open
- **Solution**: Make sure you have a display/X server configured. PyVista requires OpenGL support.

**Issue**: Out of memory error
- **Solution**: Use `window` mode instead of `downsample` mode for large files

**Issue**: Viewer is slow
- **Solution**: Reduce the window size or use a smaller region

## References

- LOLA Data: http://imbrium.mit.edu/DATA/SLDEM2015/
- Dataset: LRO-L-LOLA-4-GDR-V1.0
- Product: SLDEM2015 (Selenographic Digital Elevation Model 2015)
