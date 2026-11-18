# Real-world inputs (not committed)

`Scripts/Tools/gen_heightmap.py` builds the map from real data. Fetch it with `Scripts/Tools/fetch_geo.sh`.

| File | Source | Licence |
|---|---|---|
| `Copernicus_N23_E105.tif` | Copernicus DEM GLO-30 (30 m), tile N23 E105, AWS open data | © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018, provided under COPERNICUS by the EU and ESA |
| `osm.json` (`overpass.ql`) | OpenStreetMap via Overpass API: roads, rivers, reservoir, dams, POIs | © OpenStreetMap contributors, ODbL |
| `osm_landuse.json` (`overpass_landuse.ql`) | OpenStreetMap: buildings, land use | © OpenStreetMap contributors, ODbL |

Key OSM objects used: QL4C (trunk ways ref QL.4C), Tà Làng road (way 855373895), Nho Quế 1 dam (way 1320784007),
reservoir (way 555462463), viewpoint nodes for Thẩm Mã, Mã Pí Lèng Panorama and the Youth Volunteers monument.
