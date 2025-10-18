#! /usr/bin/env python3

import os

import requests
from tqdm import tqdm

#  Source for high resolution lunar elevation data:
#  http://imbrium.mit.edu/DATA/SLDEM2015/TILES/FLOAT_IMG/

TILE_SIZE = 3601
TILE_DEGREE_SIZE = 1
DATA_URL_TEMPLATE = 'http://imbrium.mit.edu/DATA/SLDEM2015/TILES/FLOAT_IMG/SLDEM2015_512_{lat_range}_{lon_range}_FLOAT.IMG'
DATA_FILE_TEMPLATE = '.data/SLDEM2015_512_{lat_range}_{lon_range}_FLOAT.IMG'

lat_ranges = ['00N_30N', '30N_60N', '30S_00S', '60S_30S']
lon_ranges = ['000_045', '045_090', '090_135', '135_180',
              '180_225', '225_270', '270_315', '315_360']





# try to open imgFile and lblFile, if not present download from imgUrl and lblUrl

def download_with_progress(url, filepath):
    """Download a file with a progress bar."""
    response = requests.get(url, stream=True)
    total_size = int(response.headers.get('content-length', 0))
    
    with open(filepath, 'wb') as f, tqdm(
        desc=os.path.basename(filepath),
        total=total_size,
        unit='B',
        unit_scale=True,
        unit_divisor=1024,
    ) as progress_bar:
        for chunk in response.iter_content(chunk_size=8192):
            if chunk:
                f.write(chunk)
                progress_bar.update(len(chunk))




def main():

    for lat_range in lat_ranges:
        for lon_range in lon_ranges:

            imgUrl = DATA_URL_TEMPLATE.replace('{lat_range}', lat_range).replace('{lon_range}', lon_range)
            lblUrl = imgUrl.replace('.IMG', '.LBL')

            imgFile = DATA_FILE_TEMPLATE.replace('{lat_range}', lat_range).replace('{lon_range}', lon_range)
            lblFile = imgFile.replace('.IMG', '.LBL')

            if not os.path.exists(lblFile):
               print(f"Downloading {lblUrl}...")
               download_with_progress(lblUrl, lblFile)
            else:
               print(f"{lblFile} already exists, skipping download.")
            if not os.path.exists(imgFile): 
               print(f"Downloading {imgUrl}...")
               download_with_progress(imgUrl, imgFile)
            else:
               print(f"{imgFile} already exists, skipping download.")



if __name__ == "__main__":
    main()





# // Example files:
# // ls -l http://imbrium.mit.edu/DATA/SLDEM2015/TILES/FLOAT_IMG/

# // every file also has a corresponding .LBL file with the corresponding metadata

# // SLDEM2015_512_00N_30N_000_045_FLOAT.IMG            14-Jul-2015 14:11          1415577600
# // SLDEM2015_512_00N_30N_045_090_FLOAT.IMG            14-Jul-2015 14:14          1415577600
# // SLDEM2015_512_00N_30N_090_135_FLOAT.IMG            14-Jul-2015 14:16          1415577600
# // SLDEM2015_512_00N_30N_135_180_FLOAT.IMG            14-Jul-2015 14:19          1415577600
# // SLDEM2015_512_00N_30N_180_225_FLOAT.IMG            14-Jul-2015 14:22          1415577600
# // SLDEM2015_512_00N_30N_225_270_FLOAT.IMG            14-Jul-2015 14:25          1415577600
# // SLDEM2015_512_00N_30N_270_315_FLOAT.IMG            14-Jul-2015 14:27          1415577600
# // SLDEM2015_512_00N_30N_315_360_FLOAT.IMG            14-Jul-2015 14:30          1415577600

# // SLDEM2015_512_30N_60N_000_045_FLOAT.IMG            14-Jul-2015 14:12          1415577600
# // SLDEM2015_512_30N_60N_045_090_FLOAT.IMG            14-Jul-2015 14:14          1415577600
# // SLDEM2015_512_30N_60N_090_135_FLOAT.IMG            14-Jul-2015 14:17          1415577600
# // SLDEM2015_512_30N_60N_135_180_FLOAT.IMG            14-Jul-2015 14:20          1415577600
# // SLDEM2015_512_30N_60N_180_225_FLOAT.IMG            14-Jul-2015 14:23          1415577600
# // SLDEM2015_512_30N_60N_225_270_FLOAT.IMG            14-Jul-2015 14:25          1415577600
# // SLDEM2015_512_30N_60N_270_315_FLOAT.IMG            14-Jul-2015 14:28          1415577600
# // SLDEM2015_512_30N_60N_315_360_FLOAT.IMG            14-Jul-2015 14:31          1415577600

# // SLDEM2015_512_30S_00S_000_045_FLOAT.IMG            14-Jul-2015 14:12          1415577600
# // SLDEM2015_512_30S_00S_045_090_FLOAT.IMG            14-Jul-2015 14:15          1415577600
# // SLDEM2015_512_30S_00S_090_135_FLOAT.IMG            14-Jul-2015 14:18          1415577600
# // SLDEM2015_512_30S_00S_135_180_FLOAT.IMG            14-Jul-2015 14:20          1415577600
# // SLDEM2015_512_30S_00S_180_225_FLOAT.IMG            14-Jul-2015 14:23          1415577600
# // SLDEM2015_512_30S_00S_225_270_FLOAT.IMG            14-Jul-2015 14:26          1415577600
# // SLDEM2015_512_30S_00S_270_315_FLOAT.IMG            14-Jul-2015 14:29          1415577600
# // SLDEM2015_512_30S_00S_315_360_FLOAT.IMG            14-Jul-2015 14:31          1415577600

# // SLDEM2015_512_60S_30S_000_045_FLOAT.IMG            14-Jul-2015 14:13          1415577600
# // SLDEM2015_512_60S_30S_045_090_FLOAT.IMG            14-Jul-2015 14:16          1415577600
# // SLDEM2015_512_60S_30S_090_135_FLOAT.IMG            14-Jul-2015 14:18          1415577600
# // SLDEM2015_512_60S_30S_135_180_FLOAT.IMG            14-Jul-2015 14:21          1415577600
# // SLDEM2015_512_60S_30S_180_225_FLOAT.IMG            14-Jul-2015 14:24          1415577600
# // SLDEM2015_512_60S_30S_225_270_FLOAT.IMG            14-Jul-2015 14:27          1415577600
# // SLDEM2015_512_60S_30S_270_315_FLOAT.IMG            14-Jul-2015 14:29          1415577600
# // SLDEM2015_512_60S_30S_315_360_FLOAT.IMG            14-Jul-2015 14:32          1415577600

# All files have   15360 lines of data with 23040 samples per line in 32-bit float format
# Total size per file: 15360 * 23040 * 4 bytes = 1415577600 bytes (approx 1.32 GB)  


