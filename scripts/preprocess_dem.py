import os
import numpy as np
import argparse
import sys

# These constants are derived from the terrain_loader.hpp file.
# They describe the dimensions of the source DEM files.
TILE_WIDTH = 23040
TILE_HEIGHT = 15360
SOURCE_DTYPE = np.float32 # The C++ code uses 'float'

# This defines the size of the square chunks we will break the data into.
# 512x512 is a good starting point as it's a common texture/tile size.
CHUNK_SIZE = 512

def preprocess_tile(source_path, dest_path):
    """
    Reads a large row-major DEM file and rewrites it into a chunked format.

    Args:
        source_path (str): The path to the input .IMG file.
        dest_path (str): The path for the output chunked .DAT file.
    """
    print(f"Processing '{os.path.basename(source_path)}'...")

    # Ensure the source file is the expected size
    expected_bytes = TILE_WIDTH * TILE_HEIGHT * SOURCE_DTYPE().itemsize
    actual_bytes = os.path.getsize(source_path)
    if actual_bytes != expected_bytes:
        print(f"  [ERROR] Unexpected file size for {source_path}.")
        print(f"  Expected {expected_bytes} bytes, but found {actual_bytes}.")
        print("  Skipping this file.")
        return

    try:
        # Use numpy.memmap to open the file without loading it all into RAM.
        # This treats the massive file on disk as if it were an in-memory array.
        source_map = np.memmap(source_path, dtype=SOURCE_DTYPE, mode='r', shape=(TILE_HEIGHT, TILE_WIDTH))

        num_chunks_y = TILE_HEIGHT // CHUNK_SIZE
        num_chunks_x = TILE_WIDTH // CHUNK_SIZE
        total_chunks = num_chunks_x * num_chunks_y

        print(f"  > Dimensions: {TILE_WIDTH}x{TILE_HEIGHT}")
        print(f"  > Chunk Grid: {num_chunks_x}x{num_chunks_y} ({total_chunks} total chunks)")
        print(f"  > Writing to '{os.path.basename(dest_path)}'")

        with open(dest_path, 'wb') as dest_file:
            # Iterate through the file in chunk-sized blocks
            for chunk_y in range(num_chunks_y):
                for chunk_x in range(num_chunks_x):
                    # Define the boundaries for the slice
                    y_start = chunk_y * CHUNK_SIZE
                    y_end = y_start + CHUNK_SIZE
                    x_start = chunk_x * CHUNK_SIZE
                    x_end = x_start + CHUNK_SIZE

                    # Extract the 2D chunk from the memory-mapped file
                    chunk_data = source_map[y_start:y_end, x_start:x_end]

                    # Write the chunk's raw bytes to the new file.
                    # The chunks are written one after another, contiguously.
                    dest_file.write(chunk_data.tobytes())

                    # Progress indicator
                    chunk_index = chunk_y * num_chunks_x + chunk_x
                    progress = (chunk_index + 1) / total_chunks
                    sys.stdout.write(f"\r  > Progress: [{int(progress * 100):3d}%]")
                    sys.stdout.flush()

        print("\n  > Done.\n")

    except Exception as e:
        print(f"\nAn error occurred while processing {source_path}: {e}")

def main():
    """
    Main function to handle command-line arguments and file discovery.
    """
    parser = argparse.ArgumentParser(
        description="Preprocesses large DEM files into a chunked format for efficient loading.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        '--input-dir',
        required=True,
        help="Directory containing the source SLDEM..._FLOAT.IMG files."
    )
    parser.add_argument(
        '--output-dir',
        required=True,
        help="Directory where the new chunked .DAT files will be saved."
    )
    args = parser.parse_args()

    # Create the output directory if it doesn't exist
    if not os.path.exists(args.output_dir):
        print(f"Creating output directory: {args.output_dir}")
        os.makedirs(args.output_dir)

    # Find and process all relevant files in the input directory
    found_files = False
    for filename in sorted(os.listdir(args.input_dir)):
        if filename.endswith("_FLOAT.IMG"):
            found_files = True
            source_file_path = os.path.join(args.input_dir, filename)

            # Create a new filename for the chunked data
            new_filename = filename.replace("_FLOAT.IMG", f"_CHUNKED_{CHUNK_SIZE}.DAT")
            dest_file_path = os.path.join(args.output_dir, new_filename)

            preprocess_tile(source_file_path, dest_file_path)

    if not found_files:
        print(f"No '_FLOAT.IMG' files found in '{args.input_dir}'. Please check the path.")

if __name__ == '__main__':
    main()
