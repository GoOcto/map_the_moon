# Scripts

This folder contains Python scripts for downloading and preprocessing the lunar terrain data.

## `download_dem.py`

This script downloads the high-resolution lunar Digital Elevation Model (DEM) data required by the application.

### What it does:

- Fetches data from the official source: [MIT Imbrium Data Repository](http://imbrium.mit.edu/DATA/SLDEM2015/TILES/FLOAT_IMG/).
- Downloads a series of tiled data files (`.IMG`) covering different latitude and longitude ranges of the lunar surface.
- Downloads the corresponding metadata label files (`.LBL`) for each data tile.
- Skips downloads for files that already exist.
- Displays a progress bar for each download.

### Usage:

Simply run the script from the `scripts` directory:

```bash
python download_dem.py
```

The script will download the data into a `.data` directory at the root of the project.

### TL;DR

You will need DEM data for the moon to be able to view it. Run this python script to download 32 files of about 1.3GB each for a total of 42.1GB of total storage space consumed.

## `preprocess_dem.py`

This script preprocesses the large, raw DEM files (`.IMG`) into a more efficient, chunked format (`.DAT`) that the C++ viewer can load and render quickly.

### What it does:

- Reads the very large raw `.IMG` data files (approx. 1.3 GB each).
- Uses memory-mapping (`numpy.memmap`) to handle the files without consuming excessive RAM.
- Divides each large tile into smaller, more manageable square chunks (e.g., 512x512 pixels).
- Writes these chunks sequentially into a new `.DAT` file. This layout allows the C++ application to load only the specific terrain chunks it needs for rendering, rather than the entire multi-gigabyte file.

### Usage:

The script is run via the command line and requires specifying the input and output directories.

```bash
python preprocess_dem.py --input-dir ../.data --output-dir ../.data/processed
```

- `--input-dir`: The directory containing the source `.IMG` files downloaded by `download_dem.py`.
- `--output-dir`: The directory where the new, chunked `.DAT` files will be saved.

### Chunked Data Format (`.DAT`)

The preprocessor converts the raw, row-major `.IMG` files into a custom chunked format. This is designed for efficient partial loading in the graphics application.

- **Structure**: The `.DAT` file is a flat binary file containing a sequence of data chunks.
- **Chunk Size**: Each chunk represents a square portion of the terrain, with dimensions defined by `CHUNK_SIZE` in the script (e.g., 512x512 pixels).
- **Data Type**: Each elevation sample is a 32-bit floating-point number (`float`).
- **Order**: The chunks are stored in row-major order. For a large tile divided into a grid of chunks, the chunk from the first row, first column is written first, followed by the rest of the chunks in that row, and then the chunks from the next row, and so on.

This structure allows the application to calculate the byte offset for any given chunk and read it directly from the disk without needing to process the rest of the file.

#### Data Layout Diagram

The `.DAT` file is a simple concatenation of all the chunks. The chunks themselves represent a grid that covers the original tile.

A single source tile (`23040x15360` pixels) is divided into a grid of `45x30` chunks, where each chunk is `512x512` pixels.

```
Visual Grid of Chunks:
+-------------+-------------+---...---+-------------+
| Chunk (0,0) | Chunk (0,1) |         | Chunk(0,44) |
+-------------+-------------+---...---+-------------+
| Chunk (1,0) | Chunk (1,1) |         | Chunk(1,44) |
+-------------+-------------+---...---+-------------+
|     ...     |     ...     |    ...  |     ...     |
+-------------+-------------+---...---+-------------+
| Chunk(29,0) | Chunk(29,1) |         | Chunk(29,44)|
+-------------+-------------+---...---+-------------+

File Memory Layout:
[ Chunk(0,0) Data ][ Chunk(0,1) Data ]...[ Chunk(0,44) Data ][ Chunk(1,0) Data ]...
```

#### Accessing a Specific Chunk (Pseudo-code)

Here is the logic for finding and loading a specific 512x512 chunk of elevation data based on geographic coordinates.

```
// Constants based on the data set and preprocessing script
CHUNK_SIZE = 512
PIXELS_PER_DEGREE = 512
TILE_WIDTH_DEGREES = 45
TILE_HEIGHT_DEGREES = 30
TILE_WIDTH_PIXELS = TILE_WIDTH_DEGREES * PIXELS_PER_DEGREE  // 23040
TILE_HEIGHT_PIXELS = TILE_HEIGHT_DEGREES * PIXELS_PER_DEGREE // 15360
NUM_CHUNKS_X = TILE_WIDTH_PIXELS / CHUNK_SIZE // 45

// Given a query coordinate
query_lat = ... // e.g., 15.5 (degrees North)
query_lon = ... // e.g., 25.2 (degrees East)

// 1. Determine which tile file contains the coordinate
//    (This logic matches the file naming convention)
lat_range_str = get_latitude_range_string(query_lat)   // -> "00N_30N"
lon_range_str = get_longitude_range_string(query_lon) // -> "000_045"

filename = "SLDEM2015_512_{lat_range_str}_{lon_range_str}_CHUNKED_512.DAT"

// 2. Calculate the pixel coordinate within that tile
//    (This requires knowing the lat/lon bounds of the tile)
tile_base_lat = get_tile_base_latitude(lat_range_str)     // -> 0
tile_base_lon = get_tile_base_longitude(lon_range_str)   // -> 0

// Calculate pixel offset from the top-left corner of the tile
// Note: Latitude increases upwards, but pixel Y increases downwards.
pixel_x = (query_lon - tile_base_lon) * PIXELS_PER_DEGREE
pixel_y = (tile_base_lat + TILE_HEIGHT_DEGREES - query_lat) * PIXELS_PER_DEGREE

// 3. Determine which chunk contains that pixel
chunk_x = floor(pixel_x / CHUNK_SIZE)
chunk_y = floor(pixel_y / CHUNK_SIZE)

// 4. Calculate the chunk's position in the file
chunk_index = chunk_y * NUM_CHUNKS_X + chunk_x
chunk_size_bytes = CHUNK_SIZE * CHUNK_SIZE * sizeof(float)
byte_offset = chunk_index * chunk_size_bytes

// 5. Read the chunk data
file = open(filename)
file.seek(byte_offset)
chunk_data = file.read(chunk_size_bytes)
```
