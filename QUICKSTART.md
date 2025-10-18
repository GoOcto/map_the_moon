# Lunar Viewer - Quick Start

## Build & Run

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install build-essential libglew-dev libglfw3-dev libglm-dev

# Build
make

# Run
./lunar_viewer
```

## Controls

### Orbit Mode (Default - Best for exploring)

**Mouse:**
- Left-click + drag → Rotate around terrain
- Right-click + drag → Pan view
- Scroll wheel → Zoom in/out

**Keyboard:**
- WASD → Move target point
- Q/E → Move target up/down

### FPS Mode (Press Space to toggle)

**Mouse:**
- Move mouse → Look around (cursor hidden)

**Keyboard:**
- WASD → Move forward/back/left/right
- Q/E → Move up/down
- Shift → Sprint (faster movement)

### Global

- **Space** → Toggle orbit/FPS mode
- **R** → Reset camera
- **Tab** → Toggle wireframe
- **ESC** → Quit

## Get Data

If you don't have elevation data yet:

```bash
# Install Python dependencies (one-time)
pip install requests tqdm

# Download NASA lunar data (~1.32 GB per file)
python download_dem_data.py
```

Data files will be saved to `.data/` directory.

## Optional: Generate Preview Images

```bash
# Install Python dependencies (one-time)
pip install numpy pyvista

# Generate PNG preview images
python demo.py
```

Creates `lunar_surface.png` and `lunar_surface_topdown.png`.

## Project Structure

```
map_the_moon/
├── lunar_viewer           # Main C++ executable
├── src/
│   └── lunar_viewer.cpp   # Source code
├── Makefile              # Build system
├── CMakeLists.txt        # Alternative build (CMake)
├── download_dem_data.py  # Tool: Download NASA data
├── demo.py               # Tool: Generate preview images
└── .data/                # NASA elevation data files
    └── SLDEM2015_*.IMG   # Binary elevation data (1.32 GB each)
```

## Performance

- **High-end GPU** (RTX 3060+): 200-300+ FPS
- **Mid-range GPU** (GTX 1660): 120-180 FPS
- **Integrated GPU** (Intel Iris): 60-90 FPS

## Tips

1. **Start in Orbit Mode** - More intuitive for terrain viewing
2. **Left-click and drag** to rotate - natural 3D navigation
3. **Press Space** only if you want FPS fly mode
4. **Press R** anytime to reset camera if lost
5. **Use wireframe** (Tab) to see mesh structure

## Troubleshooting

**Build fails?**
→ Install dependencies: `make install-deps`

**No data files?**
→ Run `python download_dem_data.py`

**Black screen?**
→ Update graphics drivers

**Low FPS?**
→ Check if using integrated GPU instead of dedicated GPU

