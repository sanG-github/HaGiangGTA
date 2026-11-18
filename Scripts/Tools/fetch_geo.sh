#!/bin/bash
# Downloads the real-world inputs of Scripts/Tools/gen_heightmap.py into Data/Geo/ (not committed).
#   Copernicus DEM GLO-30, tile N23 E105 (45 MB, public AWS open data, (c) ESA/Copernicus)
#   OpenStreetMap roads / rivers / water / dams / POIs + land use (Overpass API, (c) OSM contributors, ODbL)
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p Data/Geo
[ -f Data/Geo/Copernicus_N23_E105.tif ] || curl -sf -o Data/Geo/Copernicus_N23_E105.tif \
  https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N23_00_E105_00_DEM/Copernicus_DSM_COG_10_N23_00_E105_00_DEM.tif
fetch() {  # $1 query file, $2 output; retries while the public server is busy
  for i in 1 2 3 4 5; do
    curl -s -A "HaGiangGTA-terrain-build/1.0" --data-urlencode "data@$1" https://overpass-api.de/api/interpreter -o "$2"
    head -c 1 "$2" | grep -q "{" && return 0
    sleep $((i * 15))
  done
  echo "Overpass failed for $1" >&2; return 1
}
[ -f Data/Geo/osm.json ] || fetch Data/Geo/overpass.ql Data/Geo/osm.json
[ -f Data/Geo/osm_landuse.json ] || fetch Data/Geo/overpass_landuse.ql Data/Geo/osm_landuse.json
.venv/bin/pip install -q tifffile imagecodecs
echo "geo data ready"
